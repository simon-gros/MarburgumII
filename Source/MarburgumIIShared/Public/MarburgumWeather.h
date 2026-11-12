#pragma once

#include "CoreMinimal.h"

/**
 * Weather, ported from the original's automatic weather (UnrealMarburgum MarburgumWeather.h and
 * MarburgumWeatherClimate.h on its unmerged feat/dynamic-weather branch, docs/CLIMATE_CALENDAR_WEATHER_ROADMAP.md
 * sections 5, 7, 8, 12 and 16; ADR-061).
 *
 * Each mirrored 1990s day gets a deterministic plan of one state per hour, drawn from the month's ARSO Maribor
 * 1971-2000 normals and a world seed, so a relog or a restart never rerolls the sky. Each hour blends into the next
 * across the hour, and the world's actual precipitation, thunder, wetness and snow ease toward what the plan asks.
 * Pure functions of trusted UTC; nothing here reads a clock. The server runs it; clients only show its result.
 */
namespace MarburgumII::Weather
{
    /** The world's weather as the server sends it. */
    struct FState
    {
        /** What the server runs: "auto" (the climate) or a fixed profile id. */
        FString Source;
        /** The hourly state now (weather_states.json), or the profile id for a fixed profile. */
        FString State;
        double CloudCover = 0.0;
        /** 0..1. */
        double Precipitation = 0.0;
        /** "none", "rain", "sleet" or "snow". */
        FString PrecipitationType = TEXT("none");
        /** 0..1, the one value for road grip and footsteps. */
        double Wetness = 0.0;
        double TemperatureC = 0.0;
        /** Metres of lying snow. */
        double SnowDepthM = 0.0;
        double Fog = 0.0;
        /** 0..1; belongs to an active storm only. */
        double Thunder = 0.0;
        /** Mean wind, m/s, and the direction it blows from, degrees clockwise from north. */
        double WindSpeed = 0.0;
        double WindDirectionDegrees = 0.0;
        /** 0..1: how gusty. */
        double Gust = 0.0;
    };

    /** The original's four fixed developer profiles (clear_dry, overcast, rain_wet, winter_cold), exactly. */
    MARBURGUMIISHARED_API bool TryProfile(const FString& Id, FState& OutState);

    /** clear_dry: what a server without the climate data runs. */
    MARBURGUMIISHARED_API FState DefaultProfile();

    /** One hourly state's targets (weather_states.json "states"). */
    struct FStateTargets
    {
        FString Id;
        double CloudCover = 0.0;
        double Precipitation = 0.0;
        double Fog = 0.0;
        double Thunder = 0.0;
        double WindMs = 0.0;
        double Gust = 0.0;
    };

    /** One month of maribor_1971_2000.json. */
    struct FMonthNormals
    {
        double MeanC = 0.0;
        double HighC = 0.0;
        double LowC = 0.0;
        double RainMm = 0.0;
        double SunshineHours = 0.0;
        double PrecipitationDays = 0.0;
        double ThunderDays = 0.0;
        double FogDays = 0.0;
    };

    /** weather_states.json "days": how a day's plan is drawn. */
    struct FDayRules
    {
        double WetPersistence = 0.4;
        int32 RainSpellsMin = 1;
        int32 RainSpellsMax = 3;
        int32 RainSpellHoursMin = 2;
        int32 RainSpellHoursMax = 6;
        double LightBelowMmPerDay = 5.0;
        double HeavyAboveMmPerDay = 12.0;
        int32 ThunderStartHourMin = 13;
        int32 ThunderStartHourMax = 18;
        int32 ThunderHoursMin = 1;
        int32 ThunderHoursMax = 3;
        int32 ThunderBuildUpHours = 2;
        int32 FogFromHour = 3;
        double FogBurnOffAfterSunriseMin = 1.0;
        double FogBurnOffAfterSunriseMax = 3.0;
        double ConvectiveCumulusAboveMeanC = 10.0;
        double TemperatureAnomalyC = 3.0;
        double RainDayHighDropC = 2.0;
        double CloudDampsDiurnalRange = 0.5;
    };

    /** weather_states.json "dynamics": how the world eases toward the plan (real-time minutes). */
    struct FDynamicsRules
    {
        double PrecipitationRampMinutes = 4.0;
        double ThunderRampMinutes = 4.0;
        double WetnessSoakMinutes = 15.0;
        double WetnessDryMinutes = 60.0;
        double SnowBelowC = 0.5;
        double SleetBelowC = 2.0;
        double SnowCmPerHourAtFull = 2.0;
        double MeltCmPerDegreeHour = 0.4;
        double ReconstructHours = 36.0;
        double ResyncAfterMinutes = 15.0;
    };

    struct FClimate
    {
        double LatitudeDeg = 46.56;
        double LongitudeDeg = 15.65;
        FMonthNormals Months[12];
        TArray<FStateTargets> States;
        FDayRules Days;
        FDynamicsRules Dynamics;

        int32 FindState(const FString& Id) const
        {
            return States.IndexOfByPredicate([&Id](const FStateTargets& State) { return State.Id == Id; });
        }
    };

    /** The climate files under Content/Marburgum/Weather, staged with the game. */
    inline constexpr const TCHAR* NormalsFile = TEXT("maribor_1971_2000.json");
    inline constexpr const TCHAR* StatesFile = TEXT("weather_states.json");

    /** Parses the two climate files; false with OutError naming what is wrong. Rejects every malformed tunable. */
    MARBURGUMIISHARED_API bool ParseClimate(const FString& NormalsJson, const FString& StatesJson, FClimate& OutClimate, FString& OutError);

    /** Reads NormalsFile and StatesFile from a directory. */
    MARBURGUMIISHARED_API bool LoadClimate(const FString& Directory, FClimate& OutClimate, FString& OutError);

    /** Hours of daylight at a latitude on a day of the year (1..366). */
    MARBURGUMIISHARED_API double DaylightHours(double LatitudeDeg, int32 DayOfYear);

    /** The mirrored 1990s year: 1990 + positive_mod(Year - 1990, 10). */
    MARBURGUMIISHARED_API int32 MirroredYear(int32 Year);

    /** The mirrored date: month and day kept; 29 February in a non-leap mirrored year reads as 28 February. */
    MARBURGUMIISHARED_API void MirrorDate(int32 Year, int32 Month, int32 Day, int32& OutYear, int32& OutMonth, int32& OutDay);

    /** One mirrored day's plan. */
    struct FDayPlan
    {
        int32 Year = 1990;
        int32 Month = 1;
        int32 Day = 1;
        /** "clear", "overcast", "rain" or "thunder"; fog days also set bFog. */
        FString Regime;
        bool bWet = false;
        bool bThunder = false;
        bool bFog = false;
        /** State index into FClimate::States for each hour 0..23 of Central European standard time. */
        uint8 Hours[24] = {};
        double LowC = 0.0;
        double HighC = 0.0;
        double SunriseHour = 6.0;
        double WarmestHour = 15.0;
        /** The day's prevailing wind, degrees it blows from. */
        double WindDirectionDegrees = 240.0;
    };

    /** The deterministic plan of a mirrored date; hours are CET (UTC+1) all year. */
    MARBURGUMIISHARED_API FDayPlan PlanDay(const FClimate& Climate, int32 MirroredYear, int32 Month, int32 Day, uint64 WorldSeed);

    /** What the plan asks for at an instant: the hour blended into the next, before easing. */
    struct FTargets
    {
        double CloudCover = 0.0;
        double Precipitation = 0.0;
        double Fog = 0.0;
        double Thunder = 0.0;
        double WindMs = 0.0;
        double Gust = 0.0;
        double WindDirectionDegrees = 0.0;
        double TemperatureC = 0.0;
        /** The dominant hourly state (this hour's until half past, then the next one's). */
        int32 StateIndex = 0;
    };

    MARBURGUMIISHARED_API FTargets PlanTargets(const FClimate& Climate, const FDateTime& Utc, uint64 WorldSeed);

    /** "none", "rain", "sleet" or "snow" for a precipitation amount at a temperature. */
    MARBURGUMIISHARED_API FString PrecipitationTypeFor(const FDynamicsRules& Rules, double Precipitation, double TemperatureC);

    /**
     * The world's actual weather. Precipitation and thunder ease toward the plan; wetness soaks in while it rains and
     * dries afterwards (faster warm, windy and sunny, barely below 0 C); snow settles below SnowBelowC and melts above
     * 0 C. Advancing backwards, or further than ResyncAfterMinutes, rebuilds from the plan's recent history instead of
     * racing through hours of weather.
     */
    class MARBURGUMIISHARED_API FSimulation
    {
    public:
        /** Climate must outlive the simulation. */
        void Configure(const FClimate* InClimate, uint64 InWorldSeed);
        bool IsConfigured() const { return Climate != nullptr; }

        /** Simulates the last ReconstructHours from a dry, snowless start, then stands at Utc. */
        void Reconstruct(const FDateTime& Utc);
        void AdvanceTo(const FDateTime& Utc);

        /** Source is "auto". */
        FState CurrentState() const;
        const FDateTime& GetTime() const { return Time; }

    private:
        void Step(double Seconds, const FTargets& Targets);

        const FClimate* Climate = nullptr;
        uint64 WorldSeed = 0;
        bool bHasTime = false;
        FDateTime Time;
        FTargets Last;
        double Precipitation = 0.0;
        double Thunder = 0.0;
        double Wetness = 0.0;
        double SnowDepthM = 0.0;
    };

    /** "Light rain", "Thunderstorm", …; the id itself for a state this client does not know. */
    MARBURGUMIISHARED_API FString StateLabel(const FString& State);

    /** The weather in a few words for the HUD: "Light rain  ·  11 °C  ·  wet streets". */
    MARBURGUMIISHARED_API FString Summary(const FString& State, double TemperatureC, double Wetness, double SnowDepthM);
}
