#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MarburgumDaylightLook.h"
#include "MarburgumWeatherLook.h"
#include <limits>

#include "MarburgumWeatherEffects.generated.h"

class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UMarburgumWeatherSynth;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class UPostProcessComponent;
class USkyLightComponent;
class UStaticMeshComponent;
class UVolumetricCloudComponent;

/**
 * The server's weather, drawn and heard (ADR-062): rain and snow round the camera, cloud overhead, a duller and hazier
 * Lent under it, wet and snowy ground, lightning with thunder after it, and the sound of all of it. A port of the
 * original's AMarburgumWeatherEffects (feat/dynamic-weather), without its trees' wind and season.
 *
 * **The level's daylight stays the authority for a clear day.** This actor finds the sun, sky light and height fog the
 * level was built with, remembers what they were, and scales them from there; destroying it puts them back. The grade
 * is changed through a post-process component of its own at a higher priority rather than by editing the level's.
 *
 * The look follows WeatherLook::Compute and eases towards each new server weather over a few seconds, so a server step
 * never shows. Lightning is decided here, not by the server: the server says how stormy it is and each client sees its
 * own flashes, which is all thunder needs to be. UMarburgumWeatherPresentation spawns one per client world.
 *
 * **Day and night (ADR-064).** Under the weather, the world clock on the game state turns the sun to where it is over
 * Maribor and brings in a night light (the moon, or the city's skyglow), dims the haze's glow and holds the eye back
 * at night, by MarburgumDaylightLook.h. The weather then scales that, as it scaled the level's clear day before.
 * -MB2NoDaylight leaves the sun where the level put it, for captures that need the level's own day.
 */
UCLASS(NotPlaceable, Transient)
class MARBURGUMIICLIENT_API AMarburgumWeatherEffects : public AActor
{
    GENERATED_BODY()

public:
    AMarburgumWeatherEffects();

    /** The weather to present now. The first one is shown at once; later ones are eased into. */
    void SetWeather(const MarburgumII::WeatherLook::FInput& Weather);

    /** The look being drawn this frame, and the one it is easing towards, for smokes and evidence. */
    const MarburgumII::WeatherLook::FLook& GetCurrentLook() const { return Current; }
    const MarburgumII::WeatherLook::FLook& GetTargetLook() const { return Target; }

    /** A lightning flash now, for captures and review; a storm makes its own. */
    void FlashNow(float Strength = 1.0f);
    /** Holds a flash at its peak until released, so a capture taken some frames later still shows it. */
    void HoldFlash(bool bHold) { bFlashHeld = bHold; }

    /** False when the curtain assets are missing (scripts/create-weather-content.sh look has not been run). */
    bool CanDrawPrecipitation() const { return NearMaterial != nullptr; }
    /** False when the level brought its own clouds or the cloud material is missing. */
    bool CanDrawClouds() const { return CloudMaterial != nullptr; }
    /** False when MPC_Weather is missing, so the ground cannot look wet. */
    bool CanWetGround() const { return Collection != nullptr; }

    /** For smokes: what is drawn and heard this frame. */
    bool IsRainDrawn() const;
    bool IsNearRainDrawn() const;
    bool AreCloudsDrawn() const;
    bool IsSoundPlaying() const;
    float GetShelter() const { return Shelter; }
    int32 GetFlashes() const { return Flashes; }
    bool HasSun() const { return Baseline.Sun.IsValid(); }
    bool HasFog() const { return Baseline.Fog.IsValid(); }
    /** The sun's intensity now over the level's own, and the fog's density likewise; 1 where there is none. */
    float SunRatio() const;
    float FogRatio() const;
    /** The daylight being drawn, and whether the world clock drives it (false before a clock, or with -MB2NoDaylight). */
    const MarburgumII::DaylightLook::FDaylight& GetDaylight() const { return Day; }
    bool HasDaylight() const { return bHasDaylight; }
    /** The night light's intensity now, lux. */
    float NightLightLux() const;
    /** The level's sun's rotation as drawn now; zero where there is no sun. */
    FRotator SunRotationDrawn() const;
    /** The level's sky light captures the sky live (so it darkens with it by itself). */
    bool IsSkyLive() const { return Baseline.bSkyRealTime; }
    /** What MPC_Weather holds now, read back from the world's collection instance; -1 where there is none. */
    float WrittenWetness() const;

    virtual void Tick(float DeltaSeconds) override;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    /** Also restores, because a world that never began play never ends it either. */
    virtual void Destroyed() override;

private:
    /** What the level's own environment actors were set to before the weather touched them. */
    struct FBaseline
    {
        TWeakObjectPtr<UDirectionalLightComponent> Sun;
        float SunIntensity = 0.0f;
        float SunAngle = 0.5f;
        bool bSunCastsCloudShadows = false;
        float SunCloudShade = 1.0f;
        FRotator SunRotation = FRotator::ZeroRotator;
        TWeakObjectPtr<USkyLightComponent> Sky;
        float SkyIntensity = 0.0f;
        bool bSkyRealTime = false;
        TWeakObjectPtr<UExponentialHeightFogComponent> Fog;
        float FogDensity = 0.0f;
        FLinearColor FogColour = FLinearColor::White;
        FVector4 Saturation = FVector4(1.0, 1.0, 1.0, 1.0);
        FVector4 Contrast = FVector4(1.0, 1.0, 1.0, 1.0);
        /** The level's own exposure compensation, which the weather's is added to rather than replacing. */
        float ExposureBias = 0.0f;
        /** The level's adaptation floor, which night lowers. */
        float MinBrightness = 0.03f;
    };

    void CaptureBaseline();
    void RestoreBaseline();
    void Apply(float Flash);
    void WriteGround(float Wetness, float SnowCover);
    void FollowCamera();
    bool CurrentEye(FVector& OutEye) const;
    void UpdateShelter(float DeltaSeconds);
    void UpdateLightning(float DeltaSeconds);
    void UpdateDaylight();

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> Root;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> NearCurtain;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> FarCurtain;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UPostProcessComponent> Grade;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UDirectionalLightComponent> Lightning;

    /** The moon or the city's skyglow (ADR-064); dark by day. */
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UDirectionalLightComponent> NightLight;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UVolumetricCloudComponent> Clouds;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UMarburgumWeatherSynth> Sound;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> NearMaterial;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> FarMaterial;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> CloudMaterial;

    /** MPC_Weather, whose wetness and snow cover Lent's paving and landscape read. */
    UPROPERTY(Transient)
    TObjectPtr<UMaterialParameterCollection> Collection;
    float WetnessWritten = -1.0f;
    float SnowWritten = -1.0f;

    FBaseline Baseline;
    bool bBaselineCaptured = false;
    bool bHasWeather = false;
    /** The level brought its own clouds, so ours stay hidden and theirs are left alone. */
    bool bForeignClouds = false;
    MarburgumII::WeatherLook::FLook Target;
    MarburgumII::WeatherLook::FLook Current;

    MarburgumII::DaylightLook::FDaylight Day;
    bool bHasDaylight = false;
    bool bDaylightOff = false;
    float NightMinBrightnessOverride = std::numeric_limits<float>::quiet_NaN();

    /** 0 in the open, 1 when a roof is within the near curtain's reach overhead. Eased, so a doorway does not pop. */
    float Shelter = 0.0f;
    bool bHasShelter = false;
    FRandomStream Random;
    float FlashAge = MarburgumII::WeatherLook::FlashSeconds;
    uint32 FlashSeed = 0;
    float FlashStrength = 0.0f;
    int32 Flashes = 0;
    bool bFlashHeld = false;
};
