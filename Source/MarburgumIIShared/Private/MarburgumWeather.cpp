#include "MarburgumWeather.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace MarburgumII::Weather
{
    namespace
    {
        /** States every climate needs; the planner places them by name. */
        const TCHAR* const RequiredStates[] = {
            TEXT("clear"), TEXT("partly_cloudy"), TEXT("overcast"), TEXT("light_rain"),
            TEXT("steady_rain"), TEXT("heavy_rain"), TEXT("thunderstorm"), TEXT("fog"), TEXT("mist"),
        };

        bool LoadRoot(const FString& Path, const FString& Text, TSharedPtr<FJsonObject>& Out, FString& Error)
        {
            const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Text);
            if (!FJsonSerializer::Deserialize(Reader, Out) || !Out.IsValid())
            {
                Error = FString::Printf(TEXT("climate file is not valid JSON: %s"), *Path);
                return false;
            }
            double Version = 0.0;
            if (!Out->TryGetNumberField(TEXT("version"), Version) || FMath::RoundToInt(Version) != 1)
            {
                Error = FString::Printf(TEXT("unsupported climate file version: %s"), *Path);
                return false;
            }
            return true;
        }

        bool Number(const FJsonObject& Object, const TCHAR* Field, double& Out)
        {
            return Object.TryGetNumberField(Field, Out) && FMath::IsFinite(Out);
        }

        /** Optional tunable: keeps the default when absent, fails when present but not a number. */
        bool Tunable(const FJsonObject& Object, const TCHAR* Field, double& InOut)
        {
            if (!Object.HasField(Field))
            {
                return true;
            }
            return Number(Object, Field, InOut);
        }

        bool TunableInt(const FJsonObject& Object, const TCHAR* Field, int32& InOut)
        {
            double Value = InOut;
            if (!Tunable(Object, Field, Value))
            {
                return false;
            }
            InOut = FMath::RoundToInt(Value);
            return true;
        }

        bool TunableRange(const FJsonObject& Object, const TCHAR* Field, double& InOutMin, double& InOutMax)
        {
            if (!Object.HasField(Field))
            {
                return true;
            }
            const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
            if (!Object.TryGetArrayField(Field, Pair) || Pair == nullptr || Pair->Num() != 2
                || !(*Pair)[0].IsValid() || !(*Pair)[1].IsValid())
            {
                return false;
            }
            double Min = 0.0;
            double Max = 0.0;
            if (!(*Pair)[0]->TryGetNumber(Min) || !(*Pair)[1]->TryGetNumber(Max)
                || !FMath::IsFinite(Min) || !FMath::IsFinite(Max) || Min > Max)
            {
                return false;
            }
            InOutMin = Min;
            InOutMax = Max;
            return true;
        }

        bool TunableIntRange(const FJsonObject& Object, const TCHAR* Field, int32& InOutMin, int32& InOutMax)
        {
            double Min = InOutMin;
            double Max = InOutMax;
            if (!TunableRange(Object, Field, Min, Max))
            {
                return false;
            }
            InOutMin = FMath::RoundToInt(Min);
            InOutMax = FMath::RoundToInt(Max);
            return true;
        }

        bool LoadNormals(const FString& Path, const FString& Text, FClimate& Climate, FString& Error)
        {
            TSharedPtr<FJsonObject> Root;
            if (!LoadRoot(Path, Text, Root, Error))
            {
                return false;
            }
            Number(*Root, TEXT("latitude_deg"), Climate.LatitudeDeg);
            Number(*Root, TEXT("longitude_deg"), Climate.LongitudeDeg);
            const TArray<TSharedPtr<FJsonValue>>* Months = nullptr;
            if (!Root->TryGetArrayField(TEXT("months"), Months) || Months == nullptr || Months->Num() != 12)
            {
                Error = FString::Printf(TEXT("climate normals need 12 months: %s"), *Path);
                return false;
            }
            for (int32 Index = 0; Index < 12; ++Index)
            {
                const TSharedPtr<FJsonObject> Row = (*Months)[Index].IsValid() ? (*Months)[Index]->AsObject() : nullptr;
                double Month = 0.0;
                FMonthNormals& Out = Climate.Months[Index];
                if (!Row.IsValid()
                    || !Number(*Row, TEXT("month"), Month) || FMath::RoundToInt(Month) != Index + 1
                    || !Number(*Row, TEXT("mean_c"), Out.MeanC)
                    || !Number(*Row, TEXT("high_c"), Out.HighC)
                    || !Number(*Row, TEXT("low_c"), Out.LowC)
                    || !Number(*Row, TEXT("rain_mm"), Out.RainMm)
                    || !Number(*Row, TEXT("sunshine_h"), Out.SunshineHours)
                    || !Number(*Row, TEXT("precipitation_days"), Out.PrecipitationDays)
                    || !Number(*Row, TEXT("thunder_days"), Out.ThunderDays)
                    || !Number(*Row, TEXT("fog_days"), Out.FogDays)
                    || Out.LowC > Out.HighC
                    || Out.PrecipitationDays < 0.0 || Out.ThunderDays < 0.0 || Out.FogDays < 0.0)
                {
                    Error = FString::Printf(TEXT("climate month %d is malformed in %s"), Index + 1, *Path);
                    return false;
                }
            }
            return true;
        }

        bool LoadStates(const FString& Path, const FString& Text, FClimate& Climate, FString& Error)
        {
            TSharedPtr<FJsonObject> Root;
            if (!LoadRoot(Path, Text, Root, Error))
            {
                return false;
            }
            const TSharedPtr<FJsonObject>* States = nullptr;
            if (!Root->TryGetObjectField(TEXT("states"), States) || States == nullptr)
            {
                Error = FString::Printf(TEXT("weather states missing in %s"), *Path);
                return false;
            }
            for (const auto& Entry : (*States)->Values)
            {
                const TSharedPtr<FJsonObject> Row = Entry.Value.IsValid() ? Entry.Value->AsObject() : nullptr;
                FStateTargets State;
                State.Id = FString(Entry.Key.ToView());
                if (!Row.IsValid()
                    || !Number(*Row, TEXT("cloud_cover"), State.CloudCover)
                    || !Number(*Row, TEXT("precipitation"), State.Precipitation)
                    || !Number(*Row, TEXT("fog"), State.Fog)
                    || !Number(*Row, TEXT("thunder"), State.Thunder)
                    || !Number(*Row, TEXT("wind_ms"), State.WindMs)
                    || !Number(*Row, TEXT("gust"), State.Gust)
                    || State.CloudCover < 0.0 || State.CloudCover > 1.0
                    || State.Precipitation < 0.0 || State.Precipitation > 1.0
                    || State.Fog < 0.0 || State.Fog > 1.0
                    || State.Thunder < 0.0 || State.Thunder > 1.0
                    || State.WindMs < 0.0 || State.Gust < 0.0 || State.Gust > 1.0)
                {
                    Error = FString::Printf(TEXT("weather state %s is malformed in %s"), *State.Id, *Path);
                    return false;
                }
                Climate.States.Add(MoveTemp(State));
            }
            if (Climate.States.Num() > 255)
            {
                Error = FString::Printf(TEXT("too many weather states in %s"), *Path);
                return false;
            }
            for (const TCHAR* Required : RequiredStates)
            {
                if (Climate.FindState(Required) == INDEX_NONE)
                {
                    Error = FString::Printf(TEXT("weather state %s is missing in %s"), Required, *Path);
                    return false;
                }
            }

            FDayRules& Days = Climate.Days;
            const TSharedPtr<FJsonObject>* DaysObject = nullptr;
            if (Root->TryGetObjectField(TEXT("days"), DaysObject) && DaysObject != nullptr)
            {
                const FJsonObject& D = **DaysObject;
                if (!Tunable(D, TEXT("wet_persistence"), Days.WetPersistence)
                    || !TunableIntRange(D, TEXT("rain_spells"), Days.RainSpellsMin, Days.RainSpellsMax)
                    || !TunableIntRange(D, TEXT("rain_spell_hours"), Days.RainSpellHoursMin, Days.RainSpellHoursMax)
                    || !Tunable(D, TEXT("light_below_mm_per_day"), Days.LightBelowMmPerDay)
                    || !Tunable(D, TEXT("heavy_above_mm_per_day"), Days.HeavyAboveMmPerDay)
                    || !TunableIntRange(D, TEXT("thunder_start_hour"), Days.ThunderStartHourMin, Days.ThunderStartHourMax)
                    || !TunableIntRange(D, TEXT("thunder_hours"), Days.ThunderHoursMin, Days.ThunderHoursMax)
                    || !TunableInt(D, TEXT("thunder_build_up_hours"), Days.ThunderBuildUpHours)
                    || !TunableInt(D, TEXT("fog_from_hour"), Days.FogFromHour)
                    || !TunableRange(D, TEXT("fog_burn_off_after_sunrise_hours"),
                        Days.FogBurnOffAfterSunriseMin, Days.FogBurnOffAfterSunriseMax)
                    || !Tunable(D, TEXT("convective_cumulus_above_mean_c"), Days.ConvectiveCumulusAboveMeanC)
                    || !Tunable(D, TEXT("temperature_anomaly_c"), Days.TemperatureAnomalyC)
                    || !Tunable(D, TEXT("rain_day_high_drop_c"), Days.RainDayHighDropC)
                    || !Tunable(D, TEXT("cloud_damps_diurnal_range"), Days.CloudDampsDiurnalRange)
                    || Days.WetPersistence < 0.0 || Days.WetPersistence >= 1.0
                    || Days.RainSpellsMin < 1 || Days.RainSpellHoursMin < 1
                    || Days.ThunderStartHourMin < 0 || Days.ThunderStartHourMax > 23
                    || Days.ThunderHoursMin < 1 || Days.FogFromHour < 0 || Days.FogFromHour > 22
                    || Days.ThunderBuildUpHours < 0
                    || Days.LightBelowMmPerDay < 0.0 || Days.HeavyAboveMmPerDay < Days.LightBelowMmPerDay
                    || Days.FogBurnOffAfterSunriseMin < 0.0
                    || Days.TemperatureAnomalyC < 0.0 || Days.RainDayHighDropC < 0.0
                    || Days.CloudDampsDiurnalRange < 0.0 || Days.CloudDampsDiurnalRange > 1.0)
                {
                    Error = FString::Printf(TEXT("weather day rules are malformed in %s"), *Path);
                    return false;
                }
            }

            FDynamicsRules& Dynamics = Climate.Dynamics;
            const TSharedPtr<FJsonObject>* DynamicsObject = nullptr;
            if (Root->TryGetObjectField(TEXT("dynamics"), DynamicsObject) && DynamicsObject != nullptr)
            {
                const FJsonObject& D = **DynamicsObject;
                if (!Tunable(D, TEXT("precipitation_ramp_minutes"), Dynamics.PrecipitationRampMinutes)
                    || !Tunable(D, TEXT("thunder_ramp_minutes"), Dynamics.ThunderRampMinutes)
                    || !Tunable(D, TEXT("wetness_soak_minutes"), Dynamics.WetnessSoakMinutes)
                    || !Tunable(D, TEXT("wetness_dry_minutes"), Dynamics.WetnessDryMinutes)
                    || !Tunable(D, TEXT("snow_below_c"), Dynamics.SnowBelowC)
                    || !Tunable(D, TEXT("sleet_below_c"), Dynamics.SleetBelowC)
                    || !Tunable(D, TEXT("snow_cm_per_hour_at_full"), Dynamics.SnowCmPerHourAtFull)
                    || !Tunable(D, TEXT("melt_cm_per_degree_hour"), Dynamics.MeltCmPerDegreeHour)
                    || !Tunable(D, TEXT("reconstruct_hours"), Dynamics.ReconstructHours)
                    || !Tunable(D, TEXT("resync_after_minutes"), Dynamics.ResyncAfterMinutes)
                    || Dynamics.PrecipitationRampMinutes <= 0.0 || Dynamics.ThunderRampMinutes <= 0.0
                    || Dynamics.WetnessSoakMinutes <= 0.0 || Dynamics.WetnessDryMinutes <= 0.0
                    || Dynamics.SnowBelowC > Dynamics.SleetBelowC
                    || Dynamics.SnowCmPerHourAtFull < 0.0 || Dynamics.MeltCmPerDegreeHour < 0.0
                    || Dynamics.ReconstructHours < 0.0 || Dynamics.ResyncAfterMinutes <= 0.0)
                {
                    Error = FString::Printf(TEXT("weather dynamics are malformed in %s"), *Path);
                    return false;
                }
            }
            return true;
        }

        /** splitmix64: a stable hash, the same on every platform and build. */
        uint64 Mix(uint64 X)
        {
            X += 0x9E3779B97F4A7C15ull;
            X = (X ^ (X >> 30)) * 0xBF58476D1CE4E5B9ull;
            X = (X ^ (X >> 27)) * 0x94D049BB133111EBull;
            return X ^ (X >> 31);
        }

        /** A uniform number in [0, 1) belonging to one date and one decision (Salt). */
        double Roll(const uint64 Seed, const int32 Year, const int32 Month, const int32 Day, const uint32 Salt)
        {
            const uint64 Date = static_cast<uint64>(Year) * 10000ull + static_cast<uint64>(Month) * 100ull
                + static_cast<uint64>(Day);
            const uint64 Hash = Mix(Mix(Mix(Seed) ^ Date) ^ Salt);
            return static_cast<double>(Hash >> 11) * (1.0 / 9007199254740992.0);
        }

        int32 RollInt(const uint64 Seed, const int32 Year, const int32 Month, const int32 Day, const uint32 Salt,
            const int32 Min, const int32 Max)
        {
            const int32 Value = Min + FMath::FloorToInt(Roll(Seed, Year, Month, Day, Salt) * (Max - Min + 1));
            return FMath::Clamp(Value, Min, Max);
        }

        /** Decision salts; the numbers are part of the deterministic contract, never reuse one. */
        enum ESalt : uint32
        {
            BaseWet = 1,
            Persistence = 2,
            ThunderDay = 3,
            FogDay = 4,
            OvercastDay = 5,
            Cumulus = 6,
            SpellCount = 7,
            SpellStart = 10,
            SpellLength = 20,
            Intensity = 30,
            ThunderStart = 40,
            ThunderLength = 41,
            FogBurnOff = 42,
            Anomaly = 50,
            WindDirection = 60,
        };

        FDateTime CivilDate(const int32 Year, const int32 Month, const int32 Day)
        {
            return FDateTime(Year, Month, FMath::Clamp(Day, 1, FDateTime::DaysInMonth(Year, Month)));
        }

        double WetChance(const FClimate& Climate, const int32 Year, const int32 Month)
        {
            const FMonthNormals& Normals = Climate.Months[Month - 1];
            return FMath::Clamp(Normals.PrecipitationDays / FDateTime::DaysInMonth(Year, Month), 0.0, 0.95);
        }

        bool BaseWetDay(const FClimate& Climate, const uint64 Seed, const FDateTime& Date)
        {
            return Roll(Seed, Date.GetYear(), Date.GetMonth(), Date.GetDay(), BaseWet)
                < WetChance(Climate, Date.GetYear(), Date.GetMonth());
        }

        /** The day's raw temperature anomaly, -Anomaly..+Anomaly, peaked at 0. */
        double RawAnomaly(const FClimate& Climate, const uint64 Seed, const FDateTime& Date)
        {
            const int32 Y = Date.GetYear();
            const int32 M = Date.GetMonth();
            const int32 D = Date.GetDay();
            const double Sum = Roll(Seed, Y, M, D, Anomaly) + Roll(Seed, Y, M, D, Anomaly + 1)
                + Roll(Seed, Y, M, D, Anomaly + 2);
            return Climate.Days.TemperatureAnomalyC * (Sum / 3.0 * 2.0 - 1.0);
        }

        /** Monthly lows/highs interpolated between mid-months, so the seasons change smoothly. */
        void NormalLowHigh(const FClimate& Climate, const int32 DayOfYear, double& OutLow, double& OutHigh)
        {
            const double MonthLength = 365.25 / 12.0;
            const double Position = (DayOfYear - 15.5) / MonthLength;
            const int32 Index = FMath::FloorToInt(Position);
            const double Alpha = Position - Index;
            const FMonthNormals& A = Climate.Months[(Index % 12 + 12) % 12];
            const FMonthNormals& B = Climate.Months[((Index + 1) % 12 + 12) % 12];
            OutLow = FMath::Lerp(A.LowC, B.LowC, Alpha);
            OutHigh = FMath::Lerp(A.HighC, B.HighC, Alpha);
        }

        uint8 StateOf(const FClimate& Climate, const TCHAR* Id)
        {
            return static_cast<uint8>(Climate.FindState(Id));
        }

        /** Rain states by weight, so overlapping spells keep the heavier one. */
        int32 RainRank(const FClimate& Climate, const uint8 State)
        {
            const FString& Id = Climate.States[State].Id;
            if (Id == TEXT("heavy_rain") || Id == TEXT("thunderstorm"))
            {
                return 3;
            }
            if (Id == TEXT("steady_rain"))
            {
                return 2;
            }
            return Id == TEXT("light_rain") ? 1 : 0;
        }

        double LerpAngle(const double FromDegrees, const double ToDegrees, const double Alpha)
        {
            const double Delta = FMath::Fmod(ToDegrees - FromDegrees + 540.0, 360.0) - 180.0;
            return FMath::Fmod(FromDegrees + Delta * Alpha + 360.0, 360.0);
        }

        /** Smooth 0..1 between two instants: the shape of the diurnal temperature curve. */
        double Ease(const double T)
        {
            return 0.5 - 0.5 * FMath::Cos(UE_PI * FMath::Clamp(T, 0.0, 1.0));
        }

        FDayPlan PlanCivilDay(const FClimate& Climate, const FDateTime& CivilDay, const uint64 Seed)
        {
            int32 Year = 0;
            int32 Month = 0;
            int32 Day = 0;
            MirrorDate(CivilDay.GetYear(), CivilDay.GetMonth(), CivilDay.GetDay(), Year, Month, Day);
            return PlanDay(Climate, Year, Month, Day, Seed);
        }

        double MoveTowards(const double Current, const double Target, const double MaxDelta)
        {
            if (FMath::Abs(Target - Current) <= MaxDelta)
            {
                return Target;
            }
            return Current + FMath::Sign(Target - Current) * MaxDelta;
        }
    }

    bool ParseClimate(const FString& NormalsJson, const FString& StatesJson, FClimate& OutClimate, FString& OutError)
    {
        FClimate Climate;
        OutError.Reset();
        if (!LoadNormals(NormalsFile, NormalsJson, Climate, OutError) || !LoadStates(StatesFile, StatesJson, Climate, OutError))
        {
            return false;
        }
        OutClimate = MoveTemp(Climate);
        return true;
    }

    bool LoadClimate(const FString& Directory, FClimate& OutClimate, FString& OutError)
    {
        FString Normals;
        FString States;
        for (const auto& [File, Text] : { TPair<const TCHAR*, FString*>(NormalsFile, &Normals), TPair<const TCHAR*, FString*>(StatesFile, &States) })
        {
            const FString Path = FPaths::Combine(Directory, File);
            if (!FFileHelper::LoadFileToString(*Text, *Path))
            {
                OutError = FString::Printf(TEXT("could not read climate file: %s"), *Path);
                return false;
            }
        }
        return ParseClimate(Normals, States, OutClimate, OutError);
    }

    int32 MirroredYear(const int32 Year)
    {
        return 1990 + ((Year - 1990) % 10 + 10) % 10;
    }

    void MirrorDate(const int32 Year, const int32 Month, const int32 Day, int32& OutYear, int32& OutMonth, int32& OutDay)
    {
        OutYear = MirroredYear(Year);
        OutMonth = Month;
        OutDay = FMath::Min(Day, FDateTime::DaysInMonth(OutYear, Month));
    }

    double DaylightHours(const double LatitudeDeg, const int32 DayOfYear)
    {
        const double Declination = FMath::DegreesToRadians(23.44) * FMath::Sin(2.0 * UE_PI * (284 + DayOfYear) / 365.0);
        const double X = -FMath::Tan(FMath::DegreesToRadians(LatitudeDeg)) * FMath::Tan(Declination);
        return 2.0 * FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(X, -1.0, 1.0))) / 15.0;
    }

    FString PrecipitationTypeFor(const FDynamicsRules& Rules, const double Precipitation, const double TemperatureC)
    {
        if (Precipitation < 0.02)
        {
            return TEXT("none");
        }
        if (TemperatureC <= Rules.SnowBelowC)
        {
            return TEXT("snow");
        }
        return TemperatureC <= Rules.SleetBelowC ? TEXT("sleet") : TEXT("rain");
    }

    FDayPlan PlanDay(
        const FClimate& Climate,
        const int32 MirroredYear,
        const int32 Month,
        const int32 Day,
        const uint64 WorldSeed)
    {
        check(Climate.FindState(TEXT("clear")) != INDEX_NONE);
        const FDayRules& Rules = Climate.Days;
        const FMonthNormals& Normals = Climate.Months[Month - 1];
        const FDateTime Date = CivilDate(MirroredYear, Month, Day);
        const FDateTime Yesterday = Date - FTimespan::FromDays(1.0);
        const int32 Y = Date.GetYear();
        const int32 M = Date.GetMonth();
        const int32 D = Date.GetDay();
        auto R = [&](const uint32 Salt) { return Roll(WorldSeed, Y, M, D, Salt); };

        FDayPlan Plan;
        Plan.Year = Y;
        Plan.Month = M;
        Plan.Day = D;

        // Wet or dry: the month's precipitation-day frequency, sometimes carrying yesterday's draw
        // over so that wet and dry spells last several days without changing the monthly mean.
        Plan.bWet = R(Persistence) < Rules.WetPersistence
            ? BaseWetDay(Climate, WorldSeed, Yesterday)
            : BaseWetDay(Climate, WorldSeed, Date);
        Plan.bThunder = Plan.bWet
            && R(ThunderDay) < FMath::Clamp(Normals.ThunderDays / FMath::Max(Normals.PrecipitationDays, 0.1), 0.0, 1.0);
        const int32 DaysInMonth = FDateTime::DaysInMonth(Y, M);
        const double DryDays = FMath::Max(DaysInMonth - Normals.PrecipitationDays, 1.0);
        Plan.bFog = !Plan.bWet && R(FogDay) < FMath::Clamp(Normals.FogDays / DryDays, 0.0, 0.95);

        const int32 DayOfYear = Date.GetDayOfYear();
        const double Daylight = DaylightHours(Climate.LatitudeDeg, DayOfYear);
        const double SolarNoon = 12.0 + (15.0 - Climate.LongitudeDeg) / 15.0;
        Plan.SunriseHour = SolarNoon - Daylight / 2.0;
        Plan.WarmestHour = SolarNoon + 3.0;

        const uint8 Clear = StateOf(Climate, TEXT("clear"));
        const uint8 Partly = StateOf(Climate, TEXT("partly_cloudy"));
        const uint8 Overcast = StateOf(Climate, TEXT("overcast"));
        const uint8 Light = StateOf(Climate, TEXT("light_rain"));
        const uint8 Steady = StateOf(Climate, TEXT("steady_rain"));
        const uint8 Heavy = StateOf(Climate, TEXT("heavy_rain"));
        const uint8 Storm = StateOf(Climate, TEXT("thunderstorm"));
        const uint8 Fog = StateOf(Climate, TEXT("fog"));
        const uint8 Mist = StateOf(Climate, TEXT("mist"));

        if (Plan.bThunder)
        {
            // A convective day: a fine morning, cloud building, a storm in the afternoon, clearing.
            Plan.Regime = TEXT("thunder");
            const int32 Start = RollInt(WorldSeed, Y, M, D, ThunderStart, Rules.ThunderStartHourMin, Rules.ThunderStartHourMax);
            const int32 Length = RollInt(WorldSeed, Y, M, D, ThunderLength, Rules.ThunderHoursMin, Rules.ThunderHoursMax);
            for (int32 Hour = 0; Hour < 24; ++Hour)
            {
                uint8 State = Clear;
                if (Hour < Start - Rules.ThunderBuildUpHours)
                {
                    State = Hour >= 10 ? Partly : Clear;
                }
                else if (Hour < Start)
                {
                    State = Hour == Start - 1 ? Overcast : Partly;
                }
                else if (Hour < Start + Length)
                {
                    State = Storm;
                }
                else if (Hour == Start + Length)
                {
                    State = Steady;
                }
                else if (Hour < Start + Length + 3)
                {
                    State = Partly;
                }
                Plan.Hours[Hour] = State;
            }
        }
        else if (Plan.bWet)
        {
            // A frontal day: overcast, with one to three spells of rain whose weight follows the
            // month's millimetres per precipitation day.
            Plan.Regime = TEXT("rain");
            for (uint8& Hour : Plan.Hours)
            {
                Hour = Overcast;
            }
            const double MmPerDay = Normals.RainMm / FMath::Max(Normals.PrecipitationDays, 0.1);
            const double Amount = -MmPerDay * FMath::Loge(1.0 - 0.999 * R(Intensity));
            const uint8 SpellState = Amount < Rules.LightBelowMmPerDay
                ? Light
                : (Amount > Rules.HeavyAboveMmPerDay ? Heavy : Steady);
            const int32 Spells = RollInt(WorldSeed, Y, M, D, SpellCount, Rules.RainSpellsMin, Rules.RainSpellsMax);
            for (int32 Spell = 0; Spell < Spells; ++Spell)
            {
                const int32 Start = RollInt(WorldSeed, Y, M, D, SpellStart + Spell, 0, 23);
                const int32 Length = RollInt(WorldSeed, Y, M, D, SpellLength + Spell,
                    Rules.RainSpellHoursMin, Rules.RainSpellHoursMax);
                const int32 End = FMath::Min(23, Start + Length - 1);
                for (int32 Hour = Start; Hour <= End; ++Hour)
                {
                    // Steady and heavy rain set in and ease off through an hour of light rain.
                    const bool bEdge = SpellState != Light && Length >= 3 && (Hour == Start || Hour == End);
                    const uint8 State = bEdge ? Light : SpellState;
                    if (RainRank(Climate, State) > RainRank(Climate, Plan.Hours[Hour]))
                    {
                        Plan.Hours[Hour] = State;
                    }
                }
            }
        }
        else
        {
            // A dry day: grey or fine, in proportion to the month's sunshine.
            const double SunshineFraction = Normals.SunshineHours
                / (DaysInMonth * DaylightHours(Climate.LatitudeDeg, CivilDate(Y, M, 15).GetDayOfYear()));
            const bool bOvercast = R(OvercastDay) < FMath::Clamp(0.9 - 1.2 * SunshineFraction, 0.1, 0.8);
            Plan.Regime = bOvercast ? TEXT("overcast") : TEXT("clear");
            const bool bCumulus = !bOvercast && Normals.MeanC >= Rules.ConvectiveCumulusAboveMeanC && R(Cumulus) < 0.5;
            for (int32 Hour = 0; Hour < 24; ++Hour)
            {
                Plan.Hours[Hour] = bOvercast ? Overcast : (bCumulus && Hour >= 12 && Hour <= 17 ? Partly : Clear);
            }
            if (Plan.bFog)
            {
                // Fog from the small hours until it burns off after sunrise, then an hour of mist.
                const double BurnOff = FMath::Lerp(Rules.FogBurnOffAfterSunriseMin, Rules.FogBurnOffAfterSunriseMax, R(FogBurnOff));
                const int32 Clears = FMath::Clamp(FMath::FloorToInt(Plan.SunriseHour + BurnOff), Rules.FogFromHour + 1, 23);
                for (int32 Hour = Rules.FogFromHour; Hour < Clears; ++Hour)
                {
                    Plan.Hours[Hour] = Fog;
                }
                Plan.Hours[Clears] = Mist;
            }
        }

        // Temperature: the normals for the date, a day-to-day anomaly that carries over partly from
        // yesterday, a narrower range under cloud and a cooler wet day.
        double Low = 0.0;
        double High = 0.0;
        NormalLowHigh(Climate, DayOfYear, Low, High);
        const double DayAnomaly = 0.6 * RawAnomaly(Climate, WorldSeed, Date) + 0.4 * RawAnomaly(Climate, WorldSeed, Yesterday);
        double CloudMean = 0.0;
        for (const uint8 Hour : Plan.Hours)
        {
            CloudMean += Climate.States[Hour].CloudCover / 24.0;
        }
        const double Mid = (Low + High) / 2.0 + DayAnomaly - (Plan.bWet ? Rules.RainDayHighDropC / 2.0 : 0.0);
        const double Half = (High - Low) / 2.0 * (1.0 - Rules.CloudDampsDiurnalRange * CloudMean);
        Plan.LowC = Mid - Half;
        Plan.HighC = Mid + Half;

        // Mostly westerly to southerly in Maribor's basin; the day's direction, blended over the day.
        Plan.WindDirectionDegrees = 240.0 + 70.0 * (R(WindDirection) * 2.0 - 1.0);
        return Plan;
    }

    FTargets PlanTargets(const FClimate& Climate, const FDateTime& Utc, const uint64 WorldSeed)
    {
        // The plan runs on Central European *standard* time (UTC+1) all year: weather follows the
        // sun, so the summer-time change of the civil clock never shifts or repeats an hour of it.
        const FDateTime Standard = Utc + FTimespan::FromHours(1.0);
        const FDateTime Today = CivilDate(Standard.GetYear(), Standard.GetMonth(), Standard.GetDay());
        const FDateTime Tomorrow = Today + FTimespan::FromDays(1.0);
        const FDateTime Yesterday = Today - FTimespan::FromDays(1.0);
        const FDayPlan Plan = PlanCivilDay(Climate, Today, WorldSeed);
        const FDayPlan Next = PlanCivilDay(Climate, Tomorrow, WorldSeed);
        const FDayPlan Previous = PlanCivilDay(Climate, Yesterday, WorldSeed);

        // Each hour's state is reached on the hour and blends into the next across the hour.
        const int32 Hour = Standard.GetHour();
        const double Alpha = (Standard.GetMinute() * 60.0 + Standard.GetSecond() + Standard.GetMillisecond() / 1000.0) / 3600.0;
        const uint8 From = Plan.Hours[Hour];
        const uint8 To = Hour < 23 ? Plan.Hours[Hour + 1] : Next.Hours[0];
        const FStateTargets& A = Climate.States[From];
        const FStateTargets& B = Climate.States[To];

        FTargets Targets;
        Targets.CloudCover = FMath::Lerp(A.CloudCover, B.CloudCover, Alpha);
        Targets.Precipitation = FMath::Lerp(A.Precipitation, B.Precipitation, Alpha);
        Targets.Fog = FMath::Lerp(A.Fog, B.Fog, Alpha);
        Targets.Thunder = FMath::Lerp(A.Thunder, B.Thunder, Alpha);
        Targets.WindMs = FMath::Lerp(A.WindMs, B.WindMs, Alpha);
        Targets.Gust = FMath::Lerp(A.Gust, B.Gust, Alpha);
        Targets.StateIndex = Alpha < 0.5 ? From : To;
        const double HourOfDay = Hour + Alpha;
        Targets.WindDirectionDegrees = LerpAngle(Plan.WindDirectionDegrees, Next.WindDirectionDegrees, HourOfDay / 24.0);

        // Coolest at sunrise, warmest mid-afternoon; nights run from one day's high to the next
        // day's low, so the curve is continuous across midnight.
        if (HourOfDay < Plan.SunriseHour)
        {
            const double Span = Plan.SunriseHour + 24.0 - Previous.WarmestHour;
            Targets.TemperatureC = FMath::Lerp(Previous.HighC, Plan.LowC,
                Ease((HourOfDay + 24.0 - Previous.WarmestHour) / Span));
        }
        else if (HourOfDay < Plan.WarmestHour)
        {
            Targets.TemperatureC = FMath::Lerp(Plan.LowC, Plan.HighC,
                Ease((HourOfDay - Plan.SunriseHour) / (Plan.WarmestHour - Plan.SunriseHour)));
        }
        else
        {
            const double Span = Next.SunriseHour + 24.0 - Plan.WarmestHour;
            Targets.TemperatureC = FMath::Lerp(Plan.HighC, Next.LowC, Ease((HourOfDay - Plan.WarmestHour) / Span));
        }
        return Targets;
    }

    void FSimulation::Configure(const FClimate* InClimate, const uint64 InWorldSeed)
    {
        Climate = InClimate;
        WorldSeed = InWorldSeed;
        bHasTime = false;
    }

    void FSimulation::Reconstruct(const FDateTime& Utc)
    {
        if (Climate == nullptr)
        {
            return;
        }
        Precipitation = 0.0;
        Thunder = 0.0;
        Wetness = 0.0;
        SnowDepthM = 0.0;
        const double StepSeconds = 120.0;
        Time = Utc - FTimespan::FromHours(Climate->Dynamics.ReconstructHours);
        while (Time < Utc)
        {
            const double Seconds = FMath::Min(StepSeconds, (Utc - Time).GetTotalSeconds());
            Time += FTimespan::FromSeconds(Seconds);
            Last = PlanTargets(*Climate, Time, WorldSeed);
            Step(Seconds, Last);
        }
        Time = Utc;
        Last = PlanTargets(*Climate, Time, WorldSeed);
        bHasTime = true;
    }

    void FSimulation::AdvanceTo(const FDateTime& Utc)
    {
        if (Climate == nullptr)
        {
            return;
        }
        if (!bHasTime || Utc < Time || (Utc - Time).GetTotalMinutes() > Climate->Dynamics.ResyncAfterMinutes)
        {
            // A clock jump or a long stall: rebuild a plausible present rather than racing
            // through hours of weather in a few frames (roadmap section 22).
            Reconstruct(Utc);
            return;
        }
        while (Time < Utc)
        {
            const double Seconds = FMath::Min(60.0, (Utc - Time).GetTotalSeconds());
            Time += FTimespan::FromSeconds(Seconds);
            Last = PlanTargets(*Climate, Time, WorldSeed);
            Step(Seconds, Last);
        }
    }

    void FSimulation::Step(const double Seconds, const FTargets& Targets)
    {
        const FDynamicsRules& Rules = Climate->Dynamics;
        Precipitation = MoveTowards(Precipitation, Targets.Precipitation, Seconds / (Rules.PrecipitationRampMinutes * 60.0));
        Thunder = MoveTowards(Thunder, Targets.Thunder, Seconds / (Rules.ThunderRampMinutes * 60.0));

        const FString Type = PrecipitationTypeFor(Rules, Precipitation, Targets.TemperatureC);
        const bool bMelting = Targets.TemperatureC > 0.0 && SnowDepthM > 0.0;
        if (Type == TEXT("rain") || Type == TEXT("sleet"))
        {
            // Soaks in over WetnessSoakMinutes at moderate rain; faster in a downpour.
            Wetness += Seconds / (Rules.WetnessSoakMinutes * 60.0) * FMath::Clamp(Precipitation / 0.5, 0.25, 1.5);
        }
        else if (bMelting)
        {
            // Meltwater keeps the streets wet while snow lies, soaking in like light rain.
            Wetness += Seconds / (Rules.WetnessSoakMinutes * 60.0) * 0.5;
        }
        else
        {
            // Dries over WetnessDryMinutes on a mild, breezy, part-sunny day; faster warm, windy and
            // sunny, slower under cloud, barely at all in frost.
            double Drying = FMath::Clamp(
                1.0 + 0.05 * (Targets.TemperatureC - 15.0) + 0.08 * (Targets.WindMs - 2.5)
                    + 0.6 * (0.5 - Targets.CloudCover),
                0.15, 3.0);
            if (Targets.TemperatureC <= 0.0)
            {
                Drying *= 0.2;
            }
            Wetness -= Seconds / (Rules.WetnessDryMinutes * 60.0) * Drying;
        }

        if (Type == TEXT("snow"))
        {
            SnowDepthM += Precipitation * Rules.SnowCmPerHourAtFull / 100.0 * Seconds / 3600.0;
        }
        if (bMelting)
        {
            SnowDepthM -= Rules.MeltCmPerDegreeHour * Targets.TemperatureC / 100.0 * Seconds / 3600.0;
        }
        Wetness = FMath::Clamp(Wetness, 0.0, 1.0);
        SnowDepthM = FMath::Max(SnowDepthM, 0.0);
    }

    FState FSimulation::CurrentState() const
    {
        FState State;
        State.Source = TEXT("auto");
        if (Climate == nullptr)
        {
            return State;
        }
        State.State = Climate->States.IsValidIndex(Last.StateIndex) ? Climate->States[Last.StateIndex].Id : FString();
        State.CloudCover = Last.CloudCover;
        State.Precipitation = Precipitation;
        State.PrecipitationType = PrecipitationTypeFor(Climate->Dynamics, Precipitation, Last.TemperatureC);
        State.Wetness = Wetness;
        State.TemperatureC = Last.TemperatureC;
        State.SnowDepthM = SnowDepthM;
        State.Fog = Last.Fog;
        State.Thunder = Thunder;
        State.WindSpeed = Last.WindMs;
        State.WindDirectionDegrees = Last.WindDirectionDegrees;
        State.Gust = Last.Gust;
        return State;
    }

    bool TryProfile(const FString& Id, FState& OutState)
    {
        const FString Profile = Id.TrimStartAndEnd().ToLower();
        FState Next;
        Next.Source = Profile;
        Next.State = Profile;
        // Wind follows weather_states.json's clear/overcast/steady-rain values, the nearest states.
        Next.WindDirectionDegrees = 240.0;
        if (Profile == TEXT("clear_dry"))
        {
            Next.CloudCover = 0.15;
            Next.TemperatureC = 20.0;
            Next.WindSpeed = 1.5;
            Next.Gust = 0.10;
        }
        else if (Profile == TEXT("overcast"))
        {
            Next.CloudCover = 0.95;
            Next.TemperatureC = 13.0;
            Next.WindSpeed = 3.5;
            Next.Gust = 0.25;
        }
        else if (Profile == TEXT("rain_wet"))
        {
            Next.CloudCover = 1.0;
            Next.Precipitation = 0.7;
            Next.PrecipitationType = TEXT("rain");
            Next.Wetness = 1.0;
            Next.TemperatureC = 11.0;
            Next.WindSpeed = 5.0;
            Next.Gust = 0.35;
        }
        else if (Profile == TEXT("winter_cold"))
        {
            Next.CloudCover = 0.80;
            Next.Wetness = 0.15;
            Next.TemperatureC = -2.0;
            Next.SnowDepthM = 0.05;
            Next.WindSpeed = 2.5;
            Next.Gust = 0.20;
        }
        else
        {
            return false;
        }
        OutState = MoveTemp(Next);
        return true;
    }

    FState DefaultProfile()
    {
        FState State;
        const bool bFound = TryProfile(TEXT("clear_dry"), State);
        check(bFound);
        return State;
    }

    FString StateLabel(const FString& State)
    {
        static const TPair<const TCHAR*, const TCHAR*> Labels[] = {
            { TEXT("clear"), TEXT("Clear") },
            { TEXT("partly_cloudy"), TEXT("Partly cloudy") },
            { TEXT("overcast"), TEXT("Overcast") },
            { TEXT("light_rain"), TEXT("Light rain") },
            { TEXT("steady_rain"), TEXT("Rain") },
            { TEXT("heavy_rain"), TEXT("Heavy rain") },
            { TEXT("thunderstorm"), TEXT("Thunderstorm") },
            { TEXT("fog"), TEXT("Fog") },
            { TEXT("mist"), TEXT("Mist") },
            { TEXT("clear_dry"), TEXT("Clear") },
            { TEXT("rain_wet"), TEXT("Rain") },
            { TEXT("winter_cold"), TEXT("Cold") },
        };
        for (const TPair<const TCHAR*, const TCHAR*>& Label : Labels)
        {
            if (State == Label.Key)
            {
                return Label.Value;
            }
        }
        return State;
    }

    FString Summary(const FString& State, const double TemperatureC, const double Wetness, const double SnowDepthM)
    {
        FString Text = FString::Printf(TEXT("%s  \u00B7  %d \u00B0C"), *StateLabel(State), FMath::RoundToInt(TemperatureC));
        if (SnowDepthM >= 0.01)
        {
            Text += TEXT("  \u00B7  snow lying");
        }
        else if (Wetness >= 0.5)
        {
            Text += TEXT("  \u00B7  wet streets");
        }
        else if (Wetness >= 0.15)
        {
            Text += TEXT("  \u00B7  damp streets");
        }
        return Text;
    }
}
