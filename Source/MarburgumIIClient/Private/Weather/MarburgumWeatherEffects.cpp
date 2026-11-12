#include "Weather/MarburgumWeatherEffects.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Weather/MarburgumWeatherSynth.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumWeatherEffects, Log, All);

namespace MarburgumWeatherEffects
{
    namespace Look = MarburgumII::WeatherLook;

    /** Seconds for the look to cover about two thirds of the way to a new server weather. */
    constexpr float EaseSeconds = 3.0f;

    /**
     * A flash at full strength lights the city this many times as brightly as the clear-day sun. The original found
     * 2.5 turned a street white under a close strike (mean level 155 of 255 from 43).
     */
    constexpr float FlashOverSun = 1.6f;

    UStaticMeshComponent* MakeCurtain(AActor& Owner, USceneComponent* Parent, const TCHAR* Name)
    {
        UStaticMeshComponent* Curtain = Owner.CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Curtain->SetupAttachment(Parent);
        // Placed in the world each frame, about the camera rather than the actor.
        Curtain->SetUsingAbsoluteLocation(true);
        Curtain->SetUsingAbsoluteRotation(true);
        Curtain->SetUsingAbsoluteScale(true);
        Curtain->SetMobility(EComponentMobility::Movable);
        Curtain->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
        Curtain->SetGenerateOverlapEvents(false);
        Curtain->CanCharacterStepUpOn = ECB_No;
        // Falling water and snow on a sheet round the eye: nothing should shadow, reflect, bounce or decal it, and no
        // capture should see it, or the sky would be raining into its own fill.
        Curtain->CastShadow = false;
        Curtain->bReceivesDecals = false;
        Curtain->bAffectDynamicIndirectLighting = false;
        Curtain->bAffectDistanceFieldLighting = false;
        Curtain->bVisibleInReflectionCaptures = false;
        Curtain->bVisibleInRealTimeSkyCaptures = false;
        Curtain->bVisibleInRayTracing = false;
        Curtain->SetHiddenInGame(true);
        return Curtain;
    }

    bool Differs(float A, float B)
    {
        return !FMath::IsNearlyEqual(A, B, 1.0e-4f * FMath::Max(1.0f, FMath::Abs(B)));
    }
}

using namespace MarburgumWeatherEffects;

AMarburgumWeatherEffects::AMarburgumWeatherEffects()
{
    PrimaryActorTick.bCanEverTick = true;
    // After the camera has moved for this frame, so the curtains are never a frame behind it.
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    SetCanBeDamaged(false);
    SetReplicates(false);

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Root->SetMobility(EComponentMobility::Movable);
    RootComponent = Root;

    NearCurtain = MakeCurtain(*this, Root, TEXT("NearCurtain"));
    FarCurtain = MakeCurtain(*this, Root, TEXT("FarCurtain"));

    Grade = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Grade"));
    Grade->SetupAttachment(Root);
    // Above the level's own grade (an unbound volume at the default priority), and overriding only what the weather
    // changes; everything else is still the level's.
    Grade->bUnbound = true;
    Grade->Priority = 10.0f;
    Grade->BlendWeight = 1.0f;

    Lightning = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Lightning"));
    Lightning->SetupAttachment(Root);
    Lightning->SetUsingAbsoluteRotation(true);
    Lightning->SetMobility(EComponentMobility::Movable);
    // A second directional light that is dark except during a flash. It casts no shadow: a flash is over before a
    // shadow could be read, and shadows are what would make it cost anything.
    Lightning->CastShadows = false;
    Lightning->bAtmosphereSunLight = false;
    Lightning->Intensity = 0.0f;
    Lightning->LightColor = FColor(206, 219, 255);
    // The level's sun stays the light forward shading, translucency and volumetric fog use; a flash never takes over.
    Lightning->ForwardShadingPriority = -1;
    Lightning->SetVisibility(false);

    Clouds = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("Clouds"));
    Clouds->SetupAttachment(Root);
    Clouds->SetUsingAbsoluteLocation(true);
    Clouds->SetVisibility(false);

    Sound = CreateDefaultSubobject<UMarburgumWeatherSynth>(TEXT("Sound"));
    Sound->SetupAttachment(Root);
}

void AMarburgumWeatherEffects::BeginPlay()
{
    Super::BeginPlay();
    Random.Initialize(static_cast<int32>(FPlatformTime::Cycles()));

    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Look::CurtainMesh, nullptr, LOAD_NoWarn);
    UMaterialInterface* Curtain = LoadObject<UMaterialInterface>(nullptr, Look::CurtainMaterial, nullptr, LOAD_NoWarn);
    if (Mesh && Curtain)
    {
        NearMaterial = UMaterialInstanceDynamic::Create(Curtain, this);
        FarMaterial = UMaterialInstanceDynamic::Create(Curtain, this);
        NearMaterial->SetScalarParameterValue(FName(Look::LayerParameter), 0.0f);
        FarMaterial->SetScalarParameterValue(FName(Look::LayerParameter), 1.0f);
        NearCurtain->SetStaticMesh(Mesh);
        FarCurtain->SetStaticMesh(Mesh);
        NearCurtain->SetMaterial(0, NearMaterial);
        FarCurtain->SetMaterial(0, FarMaterial);
        FarCurtain->SetWorldScale3D(FVector(Look::CurtainFarScale));
        // The far sheet first, the near one over it.
        FarCurtain->SetTranslucentSortPriority(0);
        NearCurtain->SetTranslucentSortPriority(1);
    }
    else
    {
        UE_LOG(LogMarburgumWeatherEffects, Warning, TEXT("%s or %s is missing, so no rain or snow will be drawn; run scripts/create-weather-content.sh look"),
            Look::CurtainMesh, Look::CurtainMaterial);
    }

    for (TActorIterator<AVolumetricCloud> It(GetWorld()); It; ++It)
    {
        bForeignClouds = true;
    }
    if (!bForeignClouds)
    {
        if (UMaterialInterface* CloudParent = LoadObject<UMaterialInterface>(nullptr, Look::CloudMaterial, nullptr, LOAD_NoWarn))
        {
            CloudMaterial = UMaterialInstanceDynamic::Create(CloudParent, this);
            Clouds->SetMaterial(CloudMaterial);
        }
        else
        {
            UE_LOG(LogMarburgumWeatherEffects, Warning, TEXT("%s is missing, so no clouds will be drawn; run scripts/create-weather-content.sh look"), Look::CloudMaterial);
        }
    }

    Collection = LoadObject<UMaterialParameterCollection>(nullptr, Look::Collection, nullptr, LOAD_NoWarn);
    if (!Collection)
    {
        UE_LOG(LogMarburgumWeatherEffects, Warning, TEXT("%s is missing, so the ground cannot look wet; run scripts/create-weather-content.sh look"), Look::Collection);
    }

    if (!bBaselineCaptured)
    {
        CaptureBaseline();
    }
    if (bHasWeather && !Sound->IsPlaying())
    {
        Sound->Start();
    }
}

void AMarburgumWeatherEffects::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (Sound && Sound->IsPlaying())
    {
        Sound->Stop();
    }
    RestoreBaseline();
    Super::EndPlay(EndPlayReason);
}

void AMarburgumWeatherEffects::Destroyed()
{
    RestoreBaseline();
    Super::Destroyed();
}

void AMarburgumWeatherEffects::CaptureBaseline()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }
    bBaselineCaptured = true;
    // The sun is the light that lights the atmosphere; any directional light will do if none says so.
    for (TActorIterator<ADirectionalLight> It(World); It; ++It)
    {
        UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(It->GetLightComponent());
        if (Light && (!Baseline.Sun.IsValid() || Light->bAtmosphereSunLight))
        {
            Baseline.Sun = Light;
            Baseline.SunIntensity = Light->Intensity;
            Baseline.SunAngle = Light->LightSourceAngle;
            Baseline.bSunCastsCloudShadows = Light->bCastCloudShadows != 0;
            Baseline.SunCloudShade = Light->CloudShadowOnSurfaceStrength;
        }
    }
    for (TActorIterator<ASkyLight> It(World); It; ++It)
    {
        if (USkyLightComponent* Light = It->GetLightComponent())
        {
            Baseline.Sky = Light;
            Baseline.SkyIntensity = Light->Intensity;
            break;
        }
    }
    for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
    {
        if (UExponentialHeightFogComponent* Haze = It->GetComponent())
        {
            Baseline.Fog = Haze;
            Baseline.FogDensity = Haze->FogDensity;
            Baseline.FogColour = Haze->FogInscatteringLuminance;
            break;
        }
    }
    for (TActorIterator<APostProcessVolume> It(World); It; ++It)
    {
        if (It->bUnbound && It->bEnabled && It->BlendWeight > 0.0f)
        {
            if (It->Settings.bOverride_ColorSaturation)
            {
                Baseline.Saturation = It->Settings.ColorSaturation;
            }
            if (It->Settings.bOverride_ColorContrast)
            {
                Baseline.Contrast = It->Settings.ColorContrast;
            }
            if (It->Settings.bOverride_AutoExposureBias)
            {
                Baseline.ExposureBias = It->Settings.AutoExposureBias;
            }
            UE_LOG(LogMarburgumWeatherEffects, Log, TEXT("weather baseline grade %s method=%d%s bias=%.2f%s min=%.2f%s max=%.2f%s"), *It->GetName(),
                int32(It->Settings.AutoExposureMethod), It->Settings.bOverride_AutoExposureMethod ? TEXT("*") : TEXT(""),
                It->Settings.AutoExposureBias, It->Settings.bOverride_AutoExposureBias ? TEXT("*") : TEXT(""),
                It->Settings.AutoExposureMinBrightness, It->Settings.bOverride_AutoExposureMinBrightness ? TEXT("*") : TEXT(""),
                It->Settings.AutoExposureMaxBrightness, It->Settings.bOverride_AutoExposureMaxBrightness ? TEXT("*") : TEXT(""));
        }
    }
    UE_LOG(LogMarburgumWeatherEffects, Log, TEXT("weather baseline sun=%s %.2f lux angle=%.2f sky=%s %.2f fog=%s %.4f"),
        Baseline.Sun.IsValid() ? TEXT("yes") : TEXT("no"), Baseline.SunIntensity, Baseline.SunAngle,
        Baseline.Sky.IsValid() ? TEXT("yes") : TEXT("no"), Baseline.SkyIntensity,
        Baseline.Fog.IsValid() ? TEXT("yes") : TEXT("no"), Baseline.FogDensity);
}

void AMarburgumWeatherEffects::RestoreBaseline()
{
    if (UDirectionalLightComponent* Sun = Baseline.Sun.Get())
    {
        Sun->SetIntensity(Baseline.SunIntensity);
        Sun->SetLightSourceAngle(Baseline.SunAngle);
        if ((Sun->bCastCloudShadows != 0) != Baseline.bSunCastsCloudShadows || Sun->CloudShadowOnSurfaceStrength != Baseline.SunCloudShade)
        {
            Sun->bCastCloudShadows = Baseline.bSunCastsCloudShadows;
            Sun->CloudShadowOnSurfaceStrength = Baseline.SunCloudShade;
            Sun->MarkRenderStateDirty();
        }
    }
    if (USkyLightComponent* Sky = Baseline.Sky.Get())
    {
        Sky->SetIntensity(Baseline.SkyIntensity);
    }
    if (UExponentialHeightFogComponent* Haze = Baseline.Fog.Get())
    {
        Haze->SetFogDensity(Baseline.FogDensity);
        Haze->SetFogInscatteringColor(Baseline.FogColour);
    }
    // The collection outlives this actor in an editor session; the next one starts from a dry street.
    if (WetnessWritten > 0.0f || SnowWritten > 0.0f)
    {
        WriteGround(0.0f, 0.0f);
    }
}

void AMarburgumWeatherEffects::WriteGround(float Wetness, float SnowCover)
{
    UWorld* World = GetWorld();
    if (!Collection || !World || (!Differs(Wetness, WetnessWritten) && !Differs(SnowCover, SnowWritten)))
    {
        return;
    }
    if (UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(Collection))
    {
        Instance->SetScalarParameterValue(FName(Look::WetnessParameter), Wetness);
        Instance->SetScalarParameterValue(FName(Look::SnowCoverParameter), SnowCover);
        WetnessWritten = Wetness;
        SnowWritten = SnowCover;
    }
}

void AMarburgumWeatherEffects::SetWeather(const MarburgumII::WeatherLook::FInput& Weather)
{
    if (!bBaselineCaptured)
    {
        CaptureBaseline();
    }
    Target = Look::Compute(Weather);
    if (!bHasWeather)
    {
        // The first weather is how the world already is, not a change to ease into.
        Current = Target;
        bHasWeather = true;
        UpdateShelter(0.0f);
        Apply(0.0f);
    }
    if (Sound && !Sound->IsPlaying() && HasActorBegunPlay())
    {
        Sound->Start();
    }
}

void AMarburgumWeatherEffects::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bHasWeather)
    {
        return;
    }
    Current = Look::Blend(Current, Target, 1.0f - FMath::Exp(-DeltaSeconds / EaseSeconds));
    FollowCamera();
    UpdateShelter(DeltaSeconds);
    UpdateLightning(DeltaSeconds);
    Apply(Look::FlashEnvelope(FlashAge, FlashSeed) * FlashStrength);
}

void AMarburgumWeatherEffects::FollowCamera()
{
    FVector Eye = FVector::ZeroVector;
    if (!CurrentEye(Eye))
    {
        return;
    }
    // The curtain's axis runs back up the drops' path: down, and towards wherever the wind blows (+X north, +Y east).
    // Leaning the sheet is right on every side of the camera at once; a lean drawn in the material would be right only
    // across the wind.
    const double Lean = FMath::DegreesToRadians(static_cast<double>(Current.LeanDegrees));
    const double Bearing = FMath::DegreesToRadians(static_cast<double>(Current.LeanBearingDegrees));
    const FVector Travel(FMath::Cos(Bearing) * FMath::Sin(Lean), FMath::Sin(Bearing) * FMath::Sin(Lean), -FMath::Cos(Lean));
    const FRotator Upright = FRotationMatrix::MakeFromZ(-Travel).Rotator();
    NearCurtain->SetWorldLocationAndRotation(Eye, Upright);
    FarCurtain->SetWorldLocationAndRotation(Eye, Upright);
}

bool AMarburgumWeatherEffects::CurrentEye(FVector& OutEye) const
{
    if (const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
    {
        OutEye = Camera->GetCameraLocation();
        return true;
    }
    return false;
}

void AMarburgumWeatherEffects::UpdateShelter(float DeltaSeconds)
{
    FVector Eye = FVector::ZeroVector;
    if (!CurrentEye(Eye))
    {
        return;
    }
    float Wanted = 0.0f;
    if (UWorld* World = GetWorld())
    {
        // Only the level's static geometry is a roof: a car or a body passing over the camera is not.
        const double ReachCm = Look::RoofReachMetres() * 100.0;
        FHitResult Hit;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumWeatherRoof), false, this);
        if (World->LineTraceSingleByObjectType(Hit, Eye, Eye + FVector(0.0, 0.0, ReachCm), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
        {
            Wanted = 1.0f;
        }
    }
    if (!bHasShelter)
    {
        Shelter = Wanted;
        bHasShelter = true;
        return;
    }
    Shelter = FMath::FInterpTo(Shelter, Wanted, FMath::Max(0.0f, DeltaSeconds), 8.0f);
}

void AMarburgumWeatherEffects::UpdateLightning(float DeltaSeconds)
{
    if (bFlashHeld)
    {
        FlashAge = 0.0f;
        return;
    }
    FlashAge += DeltaSeconds;
    if (Current.FlashesPerMinute <= 0.01f || FlashAge < Look::FlashSeconds)
    {
        return;
    }
    if (Random.FRand() >= Current.FlashesPerMinute / 60.0f * DeltaSeconds)
    {
        return;
    }
    FlashAge = 0.0f;
    FlashSeed = Random.GetUnsignedInt();
    ++Flashes;
    // Most flashes are kilometres off; now and then one is close. Close ones are brighter, crack, and arrive almost at
    // once; distant ones are a glow and a long low roll a quarter of a minute on.
    const float Distance = FMath::Lerp(400.0f, 8000.0f, FMath::Pow(Random.FRand(), 0.8f));
    FlashStrength = FMath::Clamp(2000.0f / Distance, 0.25f, 1.0f);
    Lightning->SetWorldRotation(FRotator(-Random.FRandRange(35.0f, 75.0f), Random.FRandRange(0.0f, 360.0f), 0.0f));
    Sound->Thunder(Look::ThunderDelay(Distance), FMath::Clamp(1.0f / FMath::Sqrt(Distance / 1000.0f), 0.2f, 1.0f));
}

void AMarburgumWeatherEffects::FlashNow(float Strength)
{
    FlashAge = 0.0f;
    FlashSeed = Random.GetUnsignedInt();
    FlashStrength = FMath::Clamp(Strength, 0.0f, 1.0f);
    ++Flashes;
    Lightning->SetWorldRotation(FRotator(-55.0f, Random.FRandRange(0.0f, 360.0f), 0.0f));
}

void AMarburgumWeatherEffects::Apply(float Flash)
{
    const Look::FLook& Now = Current;

    // Our clouds shade the city themselves when they are drawn; otherwise the sun is dimmed for them.
    const bool bCloudsShade = !bForeignClouds && CloudMaterial && Now.CloudCover > 0.02f;
    if (UDirectionalLightComponent* Sun = Baseline.Sun.Get())
    {
        const float Intensity = Baseline.SunIntensity * (bCloudsShade ? Now.SunScale : Now.SunScaleUnclouded);
        if (Differs(Sun->Intensity, Intensity))
        {
            Sun->SetIntensity(Intensity);
        }
        const float Angle = FMath::Min(Baseline.SunAngle * Now.SunSoftness, 30.0f);
        if (Differs(Sun->LightSourceAngle, Angle))
        {
            Sun->SetLightSourceAngle(Angle);
        }
        const bool bCloudShadows = bCloudsShade || Baseline.bSunCastsCloudShadows;
        const float Shade = bCloudsShade ? Now.CloudShade : Baseline.SunCloudShade;
        if ((Sun->bCastCloudShadows != 0) != bCloudShadows || Differs(Sun->CloudShadowOnSurfaceStrength, Shade))
        {
            Sun->bCastCloudShadows = bCloudShadows;
            Sun->CloudShadowOnSurfaceStrength = Shade;
            Sun->MarkRenderStateDirty();
        }
    }
    if (USkyLightComponent* Sky = Baseline.Sky.Get())
    {
        const float Intensity = Baseline.SkyIntensity * Now.SkyScale;
        if (Differs(Sky->Intensity, Intensity))
        {
            Sky->SetIntensity(Intensity);
        }
    }
    if (UExponentialHeightFogComponent* Haze = Baseline.Fog.Get())
    {
        const float Density = Baseline.FogDensity * Now.FogDensityScale;
        if (Differs(Haze->FogDensity, Density))
        {
            Haze->SetFogDensity(Density);
        }
        // The haze lights up with a flash; that is most of what makes a flash read in rain.
        const FLinearColor Colour = FMath::Lerp(Baseline.FogColour, Now.FogColour, Now.FogColourAmount) * (1.0f + 2.0f * Flash);
        if (!Haze->FogInscatteringLuminance.Equals(Colour, 1.0e-4f))
        {
            Haze->SetFogInscatteringColor(Colour);
        }
    }

    FPostProcessSettings& Settings = Grade->Settings;
    Settings.bOverride_AutoExposureBias = true;
    Settings.AutoExposureBias = Baseline.ExposureBias + Now.ExposureBias;
    Settings.bOverride_ColorSaturation = true;
    Settings.ColorSaturation = FVector4(Baseline.Saturation.X * Now.SaturationScale, Baseline.Saturation.Y * Now.SaturationScale,
        Baseline.Saturation.Z * Now.SaturationScale, Baseline.Saturation.W);
    Settings.bOverride_ColorContrast = true;
    Settings.ColorContrast = FVector4(Baseline.Contrast.X * Now.ContrastScale, Baseline.Contrast.Y * Now.ContrastScale,
        Baseline.Contrast.Z * Now.ContrastScale, Baseline.Contrast.W);

    const bool bFalling = CanDrawPrecipitation() && Now.Rain + Now.Snow > 0.005f;
    NearCurtain->SetHiddenInGame(!bFalling || Shelter > 0.98f);
    FarCurtain->SetHiddenInGame(!bFalling);
    if (bFalling)
    {
        NearMaterial->SetScalarParameterValue(FName(Look::RainParameter), Look::ShelteredAmount(Now.Rain, Shelter));
        NearMaterial->SetScalarParameterValue(FName(Look::SnowParameter), Look::ShelteredAmount(Now.Snow, Shelter));
        NearMaterial->SetScalarParameterValue(FName(Look::BrightnessParameter), 1.0f + 3.0f * Flash);
        FarMaterial->SetScalarParameterValue(FName(Look::RainParameter), Now.Rain);
        FarMaterial->SetScalarParameterValue(FName(Look::SnowParameter), Now.Snow);
        FarMaterial->SetScalarParameterValue(FName(Look::BrightnessParameter), 1.0f + 3.0f * Flash);
    }

    if (!bForeignClouds && CloudMaterial)
    {
        const bool bCloudy = Now.CloudCover > 0.02f;
        if (Clouds->IsVisible() != bCloudy)
        {
            Clouds->SetVisibility(bCloudy);
        }
        CloudMaterial->SetScalarParameterValue(FName(Look::CloudCoverageParameter), Now.CloudCoverage);
        CloudMaterial->SetScalarParameterValue(FName(Look::CloudDensityParameter), Now.CloudDensity);
        CloudMaterial->SetScalarParameterValue(FName(Look::CloudStormParameter), Now.Storm);
    }

    const bool bFlashing = Flash > 0.001f;
    if (Lightning->IsVisible() != bFlashing)
    {
        Lightning->SetVisibility(bFlashing);
    }
    if (bFlashing)
    {
        const float SunLux = Baseline.Sun.IsValid() && Baseline.SunIntensity > 0.0f ? Baseline.SunIntensity : 10.0f;
        Lightning->SetIntensity(SunLux * FlashOverSun * Flash);
    }

    WriteGround(Now.Wetness, Now.SnowCover);
    if (Sound)
    {
        Sound->SetWeather(Now.RainSound, Now.WindSound, Now.Gust);
    }
}

bool AMarburgumWeatherEffects::IsRainDrawn() const
{
    return FarCurtain && FarCurtain->GetStaticMesh() && !FarCurtain->bHiddenInGame;
}

bool AMarburgumWeatherEffects::IsNearRainDrawn() const
{
    return NearCurtain && NearCurtain->GetStaticMesh() && !NearCurtain->bHiddenInGame;
}

bool AMarburgumWeatherEffects::AreCloudsDrawn() const
{
    return Clouds && CloudMaterial && Clouds->IsVisible();
}

bool AMarburgumWeatherEffects::IsSoundPlaying() const
{
    return Sound && Sound->IsPlaying();
}

float AMarburgumWeatherEffects::SunRatio() const
{
    const UDirectionalLightComponent* Sun = Baseline.Sun.Get();
    return Sun && Baseline.SunIntensity > 0.0f ? Sun->Intensity / Baseline.SunIntensity : 1.0f;
}

float AMarburgumWeatherEffects::FogRatio() const
{
    const UExponentialHeightFogComponent* Haze = Baseline.Fog.Get();
    return Haze && Baseline.FogDensity > 0.0f ? Haze->FogDensity / Baseline.FogDensity : 1.0f;
}

float AMarburgumWeatherEffects::WrittenWetness() const
{
    UWorld* World = GetWorld();
    UMaterialParameterCollectionInstance* Instance = World && Collection ? World->GetParameterCollectionInstance(Collection) : nullptr;
    float Value = -1.0f;
    if (!Instance || !Instance->GetScalarParameterValue(FName(Look::WetnessParameter), Value))
    {
        return -1.0f;
    }
    return Value;
}
