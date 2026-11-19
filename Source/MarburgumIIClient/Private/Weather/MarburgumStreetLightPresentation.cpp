#include "Weather/MarburgumStreetLightPresentation.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "MarburgumGameState.h"
#include "MarburgumStreetLamps.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumStreetLights, Log, All);

namespace MarburgumStreetLightPresentation
{
    namespace Lights = MarburgumII::StreetLights;

    /** Lamps are worked out and the lights moved this often, seconds; a lamp warms over minutes. */
    constexpr double UpdateSeconds = 0.25;
    /** A lamp's glass is written again once its output or colour has moved this much. */
    constexpr float OutputStep = 0.004f;
    constexpr float KelvinStep = 8.0f;

    template <typename T>
    T* MakeLight(AActor* Owner, int32 Index, const TCHAR* Stem)
    {
        T* Light = NewObject<T>(Owner, *FString::Printf(TEXT("%s%d"), Stem, Index), RF_Transient);
        Light->SetMobility(EComponentMobility::Movable);
        Light->SetupAttachment(Owner->GetRootComponent());
        Light->SetCastShadows(false);
        // The compressed falloff (MarburgumStreetLights.h): no inverse square, so the intensity is lux at the lamp.
        Light->bUseInverseSquaredFalloff = false;
        Light->SetLightFalloffExponent(Lights::FalloffExponent);
        Light->SetIntensityUnits(ELightUnits::Unitless);
        Light->SetUseTemperature(true);
        Light->SetLightColor(FLinearColor::White);
        Light->SetSourceRadius(8.0f);
        Light->SetVisibility(false);
        Light->RegisterComponent();
        return Light;
    }
}

bool UMarburgumStreetLightPresentation::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return World && World->IsGameWorld() && !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

bool UMarburgumStreetLightPresentation::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UMarburgumStreetLightPresentation::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UMarburgumStreetLightPresentation, STATGROUP_Tickables);
}

void UMarburgumStreetLightPresentation::Tick(float DeltaTime)
{
    using namespace MarburgumStreetLightPresentation;
    UWorld* World = GetWorld();
    if (!World || World->GetNetMode() == NM_DedicatedServer)
    {
        return;
    }
    SinceUpdate += DeltaTime;
    if (SinceUpdate < UpdateSeconds)
    {
        return;
    }
    SinceUpdate = 0.0;
    const UMarburgumStreetLampsSubsystem* Subsystem = World->GetSubsystem<UMarburgumStreetLampsSubsystem>();
    AMarburgumStreetLamps* Lamps = Subsystem ? Subsystem->GetLamps() : nullptr;
    if (!Lamps || Lamps->GetLamps().IsEmpty())
    {
        return;
    }
    if (Outputs.Num() != Lamps->GetLamps().Num())
    {
        Outputs.SetNum(Lamps->GetLamps().Num());
        Written.Init(-1.0f, Outputs.Num() * 2);
        bDark = FParse::Param(FCommandLine::Get(), TEXT("MB2NoStreetLights"));
    }
    if (!PoolActor)
    {
        FActorSpawnParameters Spawn;
        Spawn.Name = TEXT("MarburgumStreetLightPool");
        Spawn.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
        Spawn.ObjectFlags |= RF_Transient;
        PoolActor = World->SpawnActor<AActor>(Spawn);
        if (!PoolActor)
        {
            return;
        }
        USceneComponent* Root = NewObject<USceneComponent>(PoolActor, TEXT("Root"), RF_Transient);
        Root->SetMobility(EComponentMobility::Movable);
        PoolActor->SetRootComponent(Root);
        Root->RegisterComponent();
        for (int32 Index = 0; Index < Lights::LitPoolSize; ++Index)
        {
            USpotLightComponent* Spot = MakeLight<USpotLightComponent>(PoolActor, Index, TEXT("Mast"));
            Spot->SetInnerConeAngle(Lights::MastInnerConeDeg);
            Spot->SetOuterConeAngle(Lights::MastOuterConeDeg);
            Spot->SetAttenuationRadius(Lights::MastReachLightCm);
            Spots.Add(Spot);
            UPointLightComponent* Point = MakeLight<UPointLightComponent>(PoolActor, Index, TEXT("Lantern"));
            Point->SetAttenuationRadius(Lights::LanternReachLightCm);
            Points.Add(Point);
        }
    }
    UpdateLamps(*Lamps);
    UpdatePool(*Lamps);
}

void UMarburgumStreetLightPresentation::UpdateLamps(AMarburgumStreetLamps& Lamps)
{
    using namespace MarburgumStreetLightPresentation;
    FDateTime Utc;
    if (!AMarburgumGameState::WorldUtc(GetWorld(), Utc))
    {
        return;
    }
    const FMarburgumWeather* Weather = AMarburgumGameState::WeatherOf(GetWorld());
    const bool bWeather = Weather && Weather->bKnown;
    Sky = Lights::SkyAt(Utc, bWeather ? Weather->CloudCover : 0.0, bWeather ? Weather->Fog : 0.0, bWeather ? Weather->Thunder : 0.0);
    if (!bHasSky)
    {
        bHasSky = true;
        UE_LOG(LogMarburgumStreetLights, Log, TEXT("street lights from the world clock utc=%s sun_elevation=%.2f switch_on=%.2f lamps=%d dark=%d"),
            *Utc.ToIso8601(), Sky.SunElevationDeg, Lights::SwitchOnDeg(Sky), Outputs.Num(), bDark ? 1 : 0);
    }
    Burning = 0;
    for (int32 Lamp = 0; Lamp < Outputs.Num(); ++Lamp)
    {
        Outputs[Lamp] = bDark ? Lights::FOutput() : Lights::OutputOf(Sky, Lamp);
        const Lights::FOutput& Out = Outputs[Lamp];
        Burning += Out.IsOn() ? 1 : 0;
        float& WrittenOutput = Written[Lamp * 2];
        float& WrittenKelvin = Written[Lamp * 2 + 1];
        if (FMath::Abs(Out.Output - WrittenOutput) >= OutputStep || FMath::Abs(Out.Kelvin - WrittenKelvin) >= KelvinStep
            || (Out.Output == 0.0f) != (WrittenOutput == 0.0f))
        {
            Lamps.SetState(Lamp, Out.Output, Lights::KelvinColour(Out.Kelvin));
            WrittenOutput = Out.Output;
            WrittenKelvin = Out.Kelvin;
        }
    }
}

bool UMarburgumStreetLightPresentation::Eye(FVector& OutEye) const
{
    const APlayerController* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
    if (Controller && Controller->PlayerCameraManager)
    {
        OutEye = Controller->PlayerCameraManager->GetCameraLocation();
        return true;
    }
    if (Controller && Controller->GetPawn())
    {
        OutEye = Controller->GetPawn()->GetActorLocation();
        return true;
    }
    return false;
}

void UMarburgumStreetLightPresentation::UpdatePool(AMarburgumStreetLamps& Lamps)
{
    namespace Lights = MarburgumII::StreetLights;
    FVector At;
    if (!Eye(At))
    {
        return;
    }
    const TArray<Lights::FLamp>& All = Lamps.GetLamps();
    TArray<TPair<float, int32>> Near;
    Nearest = INDEX_NONE;
    NearestCm = 0.0f;
    for (int32 Lamp = 0; Lamp < All.Num(); ++Lamp)
    {
        const float Distance = static_cast<float>(FVector::Dist(All[Lamp].Location, At));
        if (Nearest == INDEX_NONE || Distance < NearestCm)
        {
            Nearest = Lamp;
            NearestCm = Distance;
        }
        if (Outputs[Lamp].IsOn() && All[Lamp].bStanding && Distance < Lights::LitReachCm)
        {
            Near.Emplace(Distance, Lamp);
        }
    }
    Near.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key || (A.Key == B.Key && A.Value < B.Value); });
    int32 SpotsUsed = 0;
    int32 PointsUsed = 0;
    Casting = 0;
    Brightest = 0.0f;
    for (const TPair<float, int32>& Candidate : Near)
    {
        if (Casting >= Lights::LitPoolSize)
        {
            break;
        }
        const Lights::FLamp& Lamp = All[Candidate.Value];
        const Lights::FOutput& Out = Outputs[Candidate.Value];
        const bool bMast = Lamp.Kind == Lights::EKind::Mast;
        ULocalLightComponent* Light = bMast ? static_cast<ULocalLightComponent*>(Spots[SpotsUsed++]) : static_cast<ULocalLightComponent*>(Points[PointsUsed++]);
        const float Lux = (bMast ? Lights::MastPeakLux : Lights::LanternPeakLux) * Out.Output;
        // Pointing straight down: a spot's cone opens along its forward axis.
        Light->SetWorldLocationAndRotation(Lights::LightPoint(Lamp), FRotator(-90.0f, Lamp.Yaw, 0.0f));
        Light->SetIntensity(Lux);
        Light->SetTemperature(Out.Kelvin);
        Light->SetVisibility(true);
        Brightest = FMath::Max(Brightest, Lux);
        ++Casting;
    }
    for (int32 Index = SpotsUsed; Index < Spots.Num(); ++Index)
    {
        Spots[Index]->SetVisibility(false);
    }
    for (int32 Index = PointsUsed; Index < Points.Num(); ++Index)
    {
        Points[Index]->SetVisibility(false);
    }
}

void UMarburgumStreetLightPresentation::Deinitialize()
{
    if (PoolActor)
    {
        PoolActor->Destroy();
        PoolActor = nullptr;
    }
    Spots.Reset();
    Points.Reset();
    Super::Deinitialize();
}
