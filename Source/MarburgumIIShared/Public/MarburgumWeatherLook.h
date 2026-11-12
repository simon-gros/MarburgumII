#pragma once

#include "CoreMinimal.h"

/**
 * What the server's weather looks and sounds like, as numbers the client applies (ADR-062): ported from the original's
 * MarburgumWeatherLook.h/.cpp and MarburgumWeatherShelter.h on its unmerged feat/dynamic-weather branch
 * (docs/CLIMATE_CALENDAR_WEATHER_ROADMAP.md section 12), numbers unchanged.
 *
 * Kept apart from the actor that applies them (MarburgumWeatherEffects.h) so the judgement calls are in one place and
 * testable without a world: how far an overcast sky dims the sun, how thick fog gets, how far rain leans in a wind.
 * Every value is relative to the level's authored daylight rather than absolute, so retuning the clear day retunes
 * every weather with it. The original calibrated them against its own city's daylight with GPU captures; Lent's
 * daylight is a different level's, so the captures here are what should move them next.
 */
namespace MarburgumII::WeatherLook
{
    /** The replicated weather this needs (AMarburgumGameState::Weather), as plain values. Nothing in it is trusted. */
    struct FInput
    {
        double CloudCover = 0.0;
        double Precipitation = 0.0;
        /** rain, sleet or snow; anything else (none, empty) means nothing falls. */
        FString PrecipitationType;
        double Fog = 0.0;
        double Thunder = 0.0;
        /** Mean wind, m/s, blowing from WindDirectionDegrees (clockwise from north). */
        double WindSpeed = 0.0;
        double WindDirectionDegrees = 0.0;
        double Gust = 0.0;
        double Wetness = 0.0;
        double SnowDepthM = 0.0;
    };

    struct FLook
    {
        // --- Light, as multipliers on the authored sun and sky fill, and an exposure bias in EV.
        /**
         * The sun when the clouds drawn overhead cast their own shadow on the city, which is what dims it under cover;
         * this is only the haze. The clouds stay sunlit from above, as real ones are, which a dimmed sun would make black.
         */
        float SunScale = 1.0f;
        /** The sun when no cloud of ours can shade the city (the level brought its own sky). */
        float SunScaleUnclouded = 1.0f;
        /** The sun's apparent size as a multiple of the real one's: shadows go soft under cloud. */
        float SunSoftness = 1.0f;
        /**
         * How much of the sun a cloud overhead stops, 0..1, where our clouds shade the city. Not all of it: what gets
         * through, from a sun widened to tens of degrees, is the soft and nearly shadowless light an overcast day has.
         */
        float CloudShade = 0.0f;
        float SkyScale = 1.0f;
        float ExposureBias = 0.0f;
        float SaturationScale = 1.0f;
        float ContrastScale = 1.0f;

        // --- Air: the height fog's density as a multiple of the authored one, and the colour it leans towards, by how much.
        float FogDensityScale = 1.0f;
        FLinearColor FogColour = FLinearColor::White;
        float FogColourAmount = 0.0f;

        // --- Sky: the cloud cover itself, and the engine cloud material's parameters for it.
        float CloudCover = 0.0f;
        float CloudCoverage = -1.0f;
        float CloudDensity = 0.008f;
        float Storm = 0.0f;

        // --- What falls, 0..1 each, and how it leans: degrees off vertical, towards a compass bearing.
        float Rain = 0.0f;
        float Snow = 0.0f;
        float LeanDegrees = 0.0f;
        float LeanBearingDegrees = 0.0f;

        // --- The ground (MPC_Weather, read by Lent's paving and landscape): how wet it looks, and how much snow lies.
        float Wetness = 0.0f;
        float SnowCover = 0.0f;

        /** Lightning seen per minute; each flash brings its thunder after the sound's travel time. */
        float FlashesPerMinute = 0.0f;

        // --- The ambience bed, 0..1 each.
        float RainSound = 0.0f;
        float WindSound = 0.0f;
        float Gust = 0.0f;
    };

    /** The look of one weather. Non-finite or out-of-range server values are clamped, never trusted. */
    MARBURGUMIISHARED_API FLook Compute(const FInput& Weather);

    /** Part of the way from one look to another, for easing between server steps. */
    MARBURGUMIISHARED_API FLook Blend(const FLook& From, const FLook& To, float Alpha);

    /** How fast the drops fall in still air, in metres per second, for a share of snow 0..1. */
    MARBURGUMIISHARED_API float FallSpeed(float SnowShare);

    /** How white the ground is for so much lying snow: a dusting shows, five centimetres covers it. */
    MARBURGUMIISHARED_API float SnowCoverFor(double SnowDepthM);

    /**
     * One flash's brightness, 0..1, Age seconds after it began: a return stroke and up to two restrikes, which is what
     * makes lightning flicker rather than blink. Seed picks the restrikes.
     */
    MARBURGUMIISHARED_API float FlashEnvelope(float Age, uint32 Seed);

    /** How long a flash lasts; FlashEnvelope is zero after it. */
    inline constexpr float FlashSeconds = 0.6f;

    /** Seconds from a flash to its thunder, Distance metres away. */
    MARBURGUMIISHARED_API float ThunderDelay(float DistanceMetres);

    // --- Content (scripts/create-weather-content.sh look; MarburgumWeatherContentCommandlet.cpp). The client draws no
    // rain, snow or cloud of its own when these are missing, and everything else still follows the weather.

    /** An open cylinder the client keeps on the camera, drawn with a procedural rain and snow material. */
    inline constexpr const TCHAR* CurtainMesh = TEXT("/Game/Marburgum/Weather/SM_WeatherCurtain.SM_WeatherCurtain");
    inline constexpr const TCHAR* CurtainMeshPackage = TEXT("/Game/Marburgum/Weather/SM_WeatherCurtain");
    inline constexpr const TCHAR* CurtainMaterial = TEXT("/Game/Marburgum/Weather/M_WeatherCurtain.M_WeatherCurtain");
    inline constexpr const TCHAR* CurtainMaterialPackage = TEXT("/Game/Marburgum/Weather/M_WeatherCurtain");
    /** The curtain's radius and height in metres; the material's streak and flake sizes assume them. */
    inline constexpr double CurtainRadiusMetres = 6.0;
    inline constexpr double CurtainHeightMetres = 24.0;
    /** How much of the curtain hangs below the camera. */
    inline constexpr double CurtainBelowMetres = 6.0;
    /** The far curtain is the same mesh scaled by this; the material draws it finer to match. */
    inline constexpr double CurtainFarScale = 2.5;
    /** Scalar parameters: how hard it rains and snows, 0..1, set per curtain. */
    inline constexpr const TCHAR* RainParameter = TEXT("Rain");
    inline constexpr const TCHAR* SnowParameter = TEXT("Snow");
    /** 0 for the near curtain, 1 for the far one: another seed, finer streaks, fainter. */
    inline constexpr const TCHAR* LayerParameter = TEXT("Layer");
    /** How bright the drops look on screen, 1 by day; raised for a lightning flash. */
    inline constexpr const TCHAR* BrightnessParameter = TEXT("Brightness");

    /** The sky's clouds: an instance of the engine's simple volumetric cloud, so a cooked build carries the material. */
    inline constexpr const TCHAR* CloudMaterial = TEXT("/Game/Marburgum/Weather/MI_WeatherClouds.MI_WeatherClouds");
    inline constexpr const TCHAR* CloudMaterialPackage = TEXT("/Game/Marburgum/Weather/MI_WeatherClouds");
    inline constexpr const TCHAR* CloudParent = TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst");
    inline constexpr const TCHAR* CloudCoverageParameter = TEXT("Cloud_GlobalCoverage");
    inline constexpr const TCHAR* CloudDensityParameter = TEXT("Cloud_GlobalDensity");
    inline constexpr const TCHAR* CloudStormParameter = TEXT("StormClouds");

    /** The ground's weather for materials: wetness and snow cover, 0..1 each. Lent's paving and landscape read them. */
    inline constexpr const TCHAR* Collection = TEXT("/Game/Marburgum/Weather/MPC_Weather.MPC_Weather");
    inline constexpr const TCHAR* CollectionPackage = TEXT("/Game/Marburgum/Weather/MPC_Weather");
    inline constexpr const TCHAR* WetnessParameter = TEXT("wetness");
    inline constexpr const TCHAR* SnowCoverParameter = TEXT("snow_cover");

    // --- Shelter. A roof within the near curtain's reach above the camera stops the near rain (the rain within 6 m);
    // the far curtain keeps raining, which is the street beyond the arcade.

    inline constexpr double RoofReachMetres()
    {
        return CurtainHeightMetres - CurtainBelowMetres;
    }

    /** 1 under a roof, 0 in the open. Anything outside that is a bad sample and counts as open. */
    inline float Shelter01(float Shelter)
    {
        return FMath::IsFinite(Shelter) ? FMath::Clamp(Shelter, 0.0f, 1.0f) : 0.0f;
    }

    /** What the near curtain should draw. The far curtain keeps Amount. */
    inline float ShelteredAmount(float Amount, float Shelter)
    {
        return Amount * (1.0f - Shelter01(Shelter));
    }
}
