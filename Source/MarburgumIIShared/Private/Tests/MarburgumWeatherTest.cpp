#include "MarburgumWeather.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// A port of the original's MarburgumAutoWeatherTest.cpp (feat/dynamic-weather) against the same climate files.

namespace
{
    using namespace MarburgumII::Weather;

    constexpr EAutomationTestFlags WeatherTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
        | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter;

    FString ClimateDirectory()
    {
        return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Marburgum"), TEXT("Weather"));
    }

    bool LoadRepoClimate(FAutomationTestBase& Test, FClimate& Out)
    {
        FString Error;
        const bool bLoaded = LoadClimate(ClimateDirectory(), Out, Error);
        Test.TestTrue(*FString::Printf(TEXT("climate loads (%s)"), *Error), bLoaded);
        return bLoaded;
    }

    double HourPrecipitation(const FClimate& Climate, const FDayPlan& Plan, int32 Hour)
    {
        return Climate.States[Plan.Hours[Hour]].Precipitation;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumWeatherClimateTest, "Marburgum.Shared.Weather.Climate", WeatherTestFlags)

bool FMarburgumWeatherClimateTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("2026 mirrors 1996"), MirroredYear(2026), 1996);
    TestEqual(TEXT("2030 mirrors 1990"), MirroredYear(2030), 1990);
    TestEqual(TEXT("1989 mirrors 1999"), MirroredYear(1989), 1999);
    int32 Y = 0, M = 0, D = 0;
    MirrorDate(2028, 2, 29, Y, M, D); // 1998 is not a leap year
    TestTrue(TEXT("29 February in a common mirrored year reads as the 28th"), Y == 1998 && M == 2 && D == 28);
    MirrorDate(2032, 2, 29, Y, M, D); // 1992 is
    TestTrue(TEXT("29 February in a leap mirrored year stays"), Y == 1992 && M == 2 && D == 29);

    FClimate Climate;
    if (!LoadRepoClimate(*this, Climate))
    {
        return false;
    }
    TestEqual(TEXT("July precipitation days"), Climate.Months[6].PrecipitationDays, 13.1);
    TestEqual(TEXT("January fog days"), Climate.Months[0].FogDays, 5.7);
    TestTrue(TEXT("thunderstorm state"), Climate.FindState(TEXT("thunderstorm")) != INDEX_NONE
        && Climate.States[Climate.FindState(TEXT("thunderstorm"))].Thunder > 0.5);
    TestTrue(TEXT("streets soak faster than they dry"), Climate.Dynamics.WetnessSoakMinutes < Climate.Dynamics.WetnessDryMinutes);

    FClimate Missing;
    FString Error;
    TestFalse(TEXT("a missing directory fails"), LoadClimate(TEXT("/nonexistent/climate"), Missing, Error));
    TestFalse(TEXT("and says why"), Error.IsEmpty());

    TestTrue(TEXT("8.6 h of daylight at the winter solstice"), FMath::IsNearlyEqual(DaylightHours(46.56, 355), 8.6, 0.25));
    TestTrue(TEXT("15.7 h at the summer solstice"), FMath::IsNearlyEqual(DaylightHours(46.56, 172), 15.7, 0.25));

    // Malformed tunables are refused rather than indexing past the 24 hours, inverting ranges or growing negative snow.
    FString Normals;
    FString States;
    TestTrue(TEXT("normals readable"), FFileHelper::LoadFileToString(Normals, *FPaths::Combine(ClimateDirectory(), NormalsFile)));
    TestTrue(TEXT("states readable"), FFileHelper::LoadFileToString(States, *FPaths::Combine(ClimateDirectory(), StatesFile)));
    FClimate Parsed;
    TestTrue(TEXT("the files parse from text"), ParseClimate(Normals, States, Parsed, Error));
    const TPair<const TCHAR*, const TCHAR*> Corruptions[] = {
        { TEXT("\"fog_from_hour\": 3"), TEXT("\"fog_from_hour\": 23") },
        { TEXT("\"cloud_damps_diurnal_range\": 0.5"), TEXT("\"cloud_damps_diurnal_range\": 1.2") },
        { TEXT("\"heavy_above_mm_per_day\": 12.0"), TEXT("\"heavy_above_mm_per_day\": 4.0") },
        { TEXT("\"snow_cm_per_hour_at_full\": 2.0"), TEXT("\"snow_cm_per_hour_at_full\": -1.0") },
        { TEXT("\"fog_burn_off_after_sunrise_hours\": [1, 3]"), TEXT("\"fog_burn_off_after_sunrise_hours\": [1, 1e309]") },
        { TEXT("\"version\": 1"), TEXT("\"version\": 2") },
    };
    for (const TPair<const TCHAR*, const TCHAR*>& Corruption : Corruptions)
    {
        const FString Invalid = States.Replace(Corruption.Key, Corruption.Value, ESearchCase::CaseSensitive);
        TestNotEqual(*FString::Printf(TEXT("corruption applies: %s"), Corruption.Value), Invalid, States);
        FClimate Rejected;
        Error.Reset();
        TestFalse(*FString::Printf(TEXT("rejected: %s"), Corruption.Value), ParseClimate(Normals, Invalid, Rejected, Error));
        TestFalse(*FString::Printf(TEXT("with a reason: %s"), Corruption.Value), Error.IsEmpty());
    }

    // Plans are deterministic per date and seed; another seed is another history.
    const FDayPlan A = PlanDay(Climate, 1996, 10, 9, 1996);
    const FDayPlan B = PlanDay(Climate, 1996, 10, 9, 1996);
    TestEqual(TEXT("same regime"), A.Regime, B.Regime);
    TestTrue(TEXT("same hours"), FMemory::Memcmp(A.Hours, B.Hours, sizeof(A.Hours)) == 0);
    TestEqual(TEXT("same low"), A.LowC, B.LowC);
    TestTrue(TEXT("low below high"), A.LowC < A.HighC);
    int32 Differences = 0;
    for (int32 Day = 1; Day <= 30; ++Day)
    {
        const FDayPlan One = PlanDay(Climate, 1996, 6, Day, 1);
        const FDayPlan Two = PlanDay(Climate, 1996, 6, Day, 2);
        Differences += FMemory::Memcmp(One.Hours, Two.Hours, sizeof(One.Hours)) != 0 ? 1 : 0;
    }
    TestTrue(TEXT("another seed is another June"), Differences > 5);
    const FDateTime Instant(2026, 10, 9, 14, 37, 0);
    const FTargets T1 = PlanTargets(Climate, Instant, 1996);
    const FTargets T2 = PlanTargets(Climate, Instant, 1996);
    TestTrue(TEXT("the same instant asks for the same sky"), T1.Precipitation == T2.Precipitation && T1.TemperatureC == T2.TemperatureC
        && T1.StateIndex == T2.StateIndex);

    // Forty simulated years per month converge on the ARSO 1971-2000 normals.
    const int32 Years = 40;
    for (int32 Month = 1; Month <= 12; ++Month)
    {
        double WetDays = 0.0, ThunderDays = 0.0, FogDays = 0.0, MeanC = 0.0;
        int32 Days = 0;
        for (int32 Year = 1960; Year < 1960 + Years; ++Year)
        {
            for (int32 Day = 1; Day <= FDateTime::DaysInMonth(Year, Month); ++Day)
            {
                const FDayPlan Plan = PlanDay(Climate, Year, Month, Day, 1996);
                WetDays += Plan.bWet ? 1.0 : 0.0;
                ThunderDays += Plan.bThunder ? 1.0 : 0.0;
                FogDays += Plan.bFog ? 1.0 : 0.0;
                MeanC += (Plan.LowC + Plan.HighC) / 2.0;
                ++Days;
            }
        }
        const FMonthNormals& N = Climate.Months[Month - 1];
        TestTrue(*FString::Printf(TEXT("month %d wet days %.1f vs %.1f"), Month, WetDays / Years, N.PrecipitationDays),
            FMath::Abs(WetDays / Years - N.PrecipitationDays) <= FMath::Max(1.5, 0.15 * N.PrecipitationDays));
        TestTrue(*FString::Printf(TEXT("month %d thunder days %.1f vs %.1f"), Month, ThunderDays / Years, N.ThunderDays),
            FMath::Abs(ThunderDays / Years - N.ThunderDays) <= FMath::Max(1.0, 0.25 * N.ThunderDays));
        TestTrue(*FString::Printf(TEXT("month %d fog days %.1f vs %.1f"), Month, FogDays / Years, N.FogDays),
            FMath::Abs(FogDays / Years - N.FogDays) <= FMath::Max(1.0, 0.30 * N.FogDays));
        TestTrue(*FString::Printf(TEXT("month %d mean %.1f vs %.1f C"), Month, MeanC / Days, N.MeanC), FMath::Abs(MeanC / Days - N.MeanC) <= 1.5);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumWeatherSimulationTest, "Marburgum.Shared.Weather.Simulation", WeatherTestFlags)

bool FMarburgumWeatherSimulationTest::RunTest(const FString& Parameters)
{
    FClimate Climate;
    if (!LoadRepoClimate(*this, Climate))
    {
        return false;
    }
    const FDynamicsRules& Rules = Climate.Dynamics;

    // Two weeks in spring, summer and winter at 30 s steps: nothing jumps faster than its ramp.
    for (const FDateTime& Start : { FDateTime(2026, 3, 20), FDateTime(2026, 7, 10), FDateTime(2026, 12, 28) })
    {
        FSimulation Simulation;
        Simulation.Configure(&Climate, 1996);
        Simulation.Reconstruct(Start);
        FState Previous = Simulation.CurrentState();
        const double StepSeconds = 30.0;
        bool bSmooth = true;
        FString FirstJump;
        for (int32 Step = 1; Step <= 14 * 24 * 120 && bSmooth; ++Step)
        {
            const FDateTime Now = Start + FTimespan::FromSeconds(Step * StepSeconds);
            Simulation.AdvanceTo(Now);
            const FState State = Simulation.CurrentState();
            bSmooth = FMath::Abs(State.Precipitation - Previous.Precipitation) <= StepSeconds / (Rules.PrecipitationRampMinutes * 60.0) + 1.0e-9
                && FMath::Abs(State.Thunder - Previous.Thunder) <= StepSeconds / (Rules.ThunderRampMinutes * 60.0) + 1.0e-9
                && State.Wetness - Previous.Wetness <= StepSeconds / (Rules.WetnessSoakMinutes * 60.0) * 1.5 + 1.0e-9
                && FMath::Abs(State.TemperatureC - Previous.TemperatureC) < 0.25
                && FMath::Abs(State.CloudCover - Previous.CloudCover) < 0.02
                && State.Wetness >= 0.0 && State.Wetness <= 1.0
                && Climate.FindState(State.State) != INDEX_NONE;
            if (!bSmooth)
            {
                FirstJump = Now.ToIso8601();
            }
            Previous = State;
        }
        TestTrue(*FString::Printf(TEXT("two smooth weeks from %s (first jump: %s)"), *Start.ToIso8601(), *FirstJump), bSmooth);
    }

    // A summer storm day followed by a fine one: the streets soak, and are dry again by noon.
    bool bFound = false;
    for (int32 Day = 1; Day < 31 && !bFound; ++Day)
    {
        const FDayPlan Storm = PlanDay(Climate, 1996, 7, Day, 1996);
        const FDayPlan After = PlanDay(Climate, 1996, 7, Day + 1, 1996);
        if (!Storm.bThunder || After.bWet)
        {
            continue;
        }
        bFound = true;
        // 1996 mirrors 2026; plan days start at 00:00 CET, 23:00 UTC the evening before.
        const FDateTime DayStartUtc = FDateTime(2026, 7, Day) - FTimespan::FromHours(1.0);
        FSimulation Simulation;
        Simulation.Configure(&Climate, 1996);
        Simulation.Reconstruct(DayStartUtc);
        double Peak = 0.0;
        for (int32 Minute = 1; Minute <= 36 * 60; ++Minute)
        {
            Simulation.AdvanceTo(DayStartUtc + FTimespan::FromMinutes(Minute));
            Peak = FMath::Max(Peak, Simulation.CurrentState().Wetness);
        }
        TestTrue(*FString::Printf(TEXT("the storm soaks the streets (peak %.2f)"), Peak), Peak > 0.9);
        TestTrue(TEXT("dry again by noon of the fine day"), Simulation.CurrentState().Wetness < 0.1);
    }
    TestTrue(TEXT("a July storm day before a fine one exists"), bFound);

    // A wet winter day lays snow when it is cold enough, and snow falls only at or below SnowBelowC.
    double SnowPeak = 0.0;
    bool bSnowWarm = false;
    for (int32 Index = 0; Index < 3 * 31 && SnowPeak <= 0.0; ++Index)
    {
        const int32 Month = Index < 31 ? 1 : (Index < 62 ? 2 : 12);
        const int32 Day = Index % 31 + 1;
        if (Day > FDateTime::DaysInMonth(1996, Month))
        {
            continue;
        }
        const FDayPlan Plan = PlanDay(Climate, 1996, Month, Day, 1996);
        if (!Plan.bWet || Plan.LowC > 0.0)
        {
            continue;
        }
        FSimulation Simulation;
        Simulation.Configure(&Climate, 1996);
        const FDateTime DayStartUtc = FDateTime(2026, Month, Day) - FTimespan::FromHours(1.0);
        Simulation.Reconstruct(DayStartUtc);
        for (int32 Minute = 1; Minute <= 24 * 60; ++Minute)
        {
            Simulation.AdvanceTo(DayStartUtc + FTimespan::FromMinutes(Minute));
            const FState State = Simulation.CurrentState();
            bSnowWarm |= State.PrecipitationType == TEXT("snow") && State.TemperatureC > Rules.SnowBelowC;
            SnowPeak = FMath::Max(SnowPeak, State.SnowDepthM);
        }
    }
    TestTrue(TEXT("a cold wet day lays snow"), SnowPeak > 0.0);
    TestFalse(TEXT("snow never falls above SnowBelowC"), bSnowWarm);
    TestEqual(TEXT("no precipitation"), PrecipitationTypeFor(Rules, 0.0, 10.0), FString(TEXT("none")));
    TestEqual(TEXT("rain"), PrecipitationTypeFor(Rules, 0.5, 10.0), FString(TEXT("rain")));
    TestEqual(TEXT("sleet"), PrecipitationTypeFor(Rules, 0.5, 1.0), FString(TEXT("sleet")));
    TestEqual(TEXT("snow"), PrecipitationTypeFor(Rules, 0.5, -3.0), FString(TEXT("snow")));

    // A clock jump rebuilds a plausible present instead of racing through it; backwards too.
    const FDateTime Start(2026, 4, 2, 6, 0, 0);
    const FDateTime Later = Start + FTimespan::FromDays(3.0);
    FSimulation Jumped;
    Jumped.Configure(&Climate, 1996);
    Jumped.Reconstruct(Start);
    Jumped.AdvanceTo(Later);
    FSimulation Fresh;
    Fresh.Configure(&Climate, 1996);
    Fresh.Reconstruct(Later);
    TestEqual(TEXT("a jump lands where a fresh start does (wetness)"), Jumped.CurrentState().Wetness, Fresh.CurrentState().Wetness);
    TestEqual(TEXT("a jump lands where a fresh start does (rain)"), Jumped.CurrentState().Precipitation, Fresh.CurrentState().Precipitation);
    TestEqual(TEXT("and stands at the new time"), Jumped.GetTime(), Later);
    Jumped.AdvanceTo(Start);
    TestEqual(TEXT("a restored clock never replays weather"), Jumped.GetTime(), Start);

    // A known wet hour: rain sets in and the streets get wet within a quarter of an hour.
    int32 WetDay = 0;
    for (int32 Day = 1; Day <= 30 && WetDay == 0; ++Day)
    {
        const FDayPlan Plan = PlanDay(Climate, 1996, 6, Day, 1996);
        for (int32 Hour = 0; Hour < 20 && WetDay == 0; ++Hour)
        {
            if (HourPrecipitation(Climate, Plan, Hour) >= 0.6 && HourPrecipitation(Climate, Plan, Hour + 1) >= 0.6)
            {
                WetDay = Day;
                const FDateTime Utc = FDateTime(2026, 6, Day, Hour + 1, 0, 0) - FTimespan::FromHours(1.0);
                FSimulation Simulation;
                Simulation.Configure(&Climate, 1996);
                Simulation.Reconstruct(Utc);
                Simulation.AdvanceTo(Utc + FTimespan::FromMinutes(14));
                const FState State = Simulation.CurrentState();
                TestTrue(TEXT("it is raining"), State.Precipitation > 0.5 && State.PrecipitationType == TEXT("rain"));
                TestTrue(*FString::Printf(TEXT("the streets are wet (%.2f)"), State.Wetness), State.Wetness > 0.5);
                TestEqual(TEXT("the source is the climate"), State.Source, FString(TEXT("auto")));
            }
        }
    }
    TestTrue(TEXT("a June day with two hours of steady rain exists"), WetDay > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumWeatherProfilesTest, "Marburgum.Shared.Weather.Profiles", WeatherTestFlags)

bool FMarburgumWeatherProfilesTest::RunTest(const FString& Parameters)
{
    FState State;
    TestTrue(TEXT("rain_wet"), TryProfile(TEXT(" Rain_Wet "), State));
    TestEqual(TEXT("rain_wet is soaked"), State.Wetness, 1.0);
    TestEqual(TEXT("rain_wet rains"), State.PrecipitationType, FString(TEXT("rain")));
    TestTrue(TEXT("winter_cold"), TryProfile(TEXT("winter_cold"), State));
    TestEqual(TEXT("winter_cold has 5 cm of snow"), State.SnowDepthM, 0.05);
    TestFalse(TEXT("unknown profile"), TryProfile(TEXT("monsoon"), State));
    const FState Default = DefaultProfile();
    TestEqual(TEXT("default is clear_dry"), Default.Source, FString(TEXT("clear_dry")));
    TestEqual(TEXT("and dry"), Default.Wetness, 0.0);

    TestEqual(TEXT("summary"), Summary(TEXT("light_rain"), 11.4, 0.7, 0.0), FString(TEXT("Light rain  ·  11 °C  ·  wet streets")));
    TestEqual(TEXT("damp"), Summary(TEXT("overcast"), -0.4, 0.2, 0.0), FString(TEXT("Overcast  ·  0 °C  ·  damp streets")));
    TestEqual(TEXT("snow wins"), Summary(TEXT("clear"), -3.0, 0.9, 0.04), FString(TEXT("Clear  ·  -3 °C  ·  snow lying")));
    TestEqual(TEXT("unknown state shows its id"), StateLabel(TEXT("hail")), FString(TEXT("hail")));
    return true;
}

// Not a check: prints a month's plan for picking a review clock (-MB2WeatherClock), as the original's
// "[weather-calendar]" case did. Does nothing unless MB2_WEATHER_MONTH=YYYY-MM is set (MB2_WORLD_SEED picks the seed).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumWeatherCalendarTest, "Marburgum.Shared.Weather.Calendar", WeatherTestFlags)

bool FMarburgumWeatherCalendarTest::RunTest(const FString& Parameters)
{
    const FString Month = FPlatformMisc::GetEnvironmentVariable(TEXT("MB2_WEATHER_MONTH"));
    FString YearText;
    FString MonthText;
    if (!Month.Split(TEXT("-"), &YearText, &MonthText))
    {
        return true;
    }
    FClimate Climate;
    if (!LoadRepoClimate(*this, Climate))
    {
        return false;
    }
    const int32 Year = FCString::Atoi(*YearText);
    const int32 MonthNumber = FMath::Clamp(FCString::Atoi(*MonthText), 1, 12);
    const FString SeedText = FPlatformMisc::GetEnvironmentVariable(TEXT("MB2_WORLD_SEED"));
    const uint64 Seed = SeedText.IsEmpty() ? 1996 : FCString::Strtoui64(*SeedText, nullptr, 10);
    const TCHAR* Letters = TEXT("cpo-lrhTfm");
    for (int32 Day = 1; Day <= FDateTime::DaysInMonth(Year, MonthNumber); ++Day)
    {
        int32 MY = 0, MM = 0, MD = 0;
        MirrorDate(Year, MonthNumber, Day, MY, MM, MD);
        const FDayPlan Plan = PlanDay(Climate, MY, MM, MD, Seed);
        FString Hours;
        for (const uint8 Hour : Plan.Hours)
        {
            const FString& Id = Climate.States[Hour].Id;
            const int32 Index = Id == TEXT("clear") ? 0 : Id == TEXT("partly_cloudy") ? 1 : Id == TEXT("overcast") ? 2
                : Id == TEXT("light_rain") ? 4 : Id == TEXT("steady_rain") ? 5 : Id == TEXT("heavy_rain") ? 6
                : Id == TEXT("thunderstorm") ? 7 : Id == TEXT("fog") ? 8 : Id == TEXT("mist") ? 9 : 3;
            Hours.AppendChar(Letters[Index]);
        }
        AddInfo(FString::Printf(TEXT("MB2CALENDAR %04d-%02d-%02d %-8s %s %5.1f..%4.1f C"), Year, MonthNumber, Day, *Plan.Regime, *Hours, Plan.LowC, Plan.HighC));
    }
    AddInfo(TEXT("MB2CALENDAR hours are CET 00..23: c clear, p partly cloudy, o overcast, l/r/h light/steady/heavy rain, T thunderstorm, f fog, m mist"));
    return true;
}

#endif
