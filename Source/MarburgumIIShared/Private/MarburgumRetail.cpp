#include "MarburgumRetail.h"

namespace MarburgumII::Retail
{
    namespace
    {
        constexpr int64 SecondsPerDay = 86400;

        bool IsLeap(int32 Year)
        {
            return (Year % 4 == 0 && Year % 100 != 0) || Year % 400 == 0;
        }

        int32 DaysInMonth(int32 Year, int32 Month)
        {
            static constexpr int32 Days[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
            return Month == 2 && IsLeap(Year) ? 29 : Days[Month];
        }

        // Howard Hinnant's public-domain days_from_civil and civil_from_days, as the original used them.
        int64 DaysFromCivil(int32 Year, int32 Month, int32 Day)
        {
            Year -= Month <= 2;
            const int64 Era = (Year >= 0 ? Year : Year - 399) / 400;
            const int64 YearOfEra = Year - Era * 400;
            const int64 DayOfYear = (153 * (Month + (Month > 2 ? -3 : 9)) + 2) / 5 + Day - 1;
            const int64 DayOfEra = YearOfEra * 365 + YearOfEra / 4 - YearOfEra / 100 + DayOfYear;
            return Era * 146097 + DayOfEra - 719468;
        }

        FCivilTime CivilFromDays(int64 Days)
        {
            const int64 Z = Days + 719468;
            const int64 Era = (Z >= 0 ? Z : Z - 146096) / 146097;
            const int64 DayOfEra = Z - Era * 146097;
            const int64 YearOfEra = (DayOfEra - DayOfEra / 1460 + DayOfEra / 36524 - DayOfEra / 146096) / 365;
            const int64 DayOfYear = DayOfEra - (365 * YearOfEra + YearOfEra / 4 - YearOfEra / 100);
            const int64 Mp = (5 * DayOfYear + 2) / 153;
            FCivilTime Civil;
            Civil.Day = static_cast<int32>(DayOfYear - (153 * Mp + 2) / 5 + 1);
            Civil.Month = static_cast<int32>(Mp < 10 ? Mp + 3 : Mp - 9);
            Civil.Year = static_cast<int32>(YearOfEra + Era * 400 + (Civil.Month <= 2 ? 1 : 0));
            // 1970-01-01 was a Thursday, which is 3 counting from Monday.
            Civil.Weekday = static_cast<int32>(((Days + 3) % 7 + 7) % 7);
            return Civil;
        }

        /** Days since the epoch of the last Sunday of a month. */
        int64 LastSunday(int32 Year, int32 Month)
        {
            const int64 Last = DaysFromCivil(Year, Month, DaysInMonth(Year, Month));
            const int32 Weekday = CivilFromDays(Last).Weekday;
            return Last - (Weekday == 6 ? 0 : Weekday + 1);
        }

        bool IsSummer(int64 UnixSeconds)
        {
            const int32 Year = CivilFromDays(UnixSeconds / SecondsPerDay).Year;
            const int64 Start = LastSunday(Year, 3) * SecondsPerDay + 3600;
            const int64 End = LastSunday(Year, 10) * SecondsPerDay + 3600;
            return UnixSeconds >= Start && UnixSeconds < End;
        }

        FString TwoDigits(int32 Value)
        {
            return FString::Printf(TEXT("%02d"), Value);
        }

        FString FormatMinute(int32 Minute)
        {
            Minute = FMath::Clamp(Minute, 0, 24 * 60);
            return TwoDigits(Minute / 60) + TEXT(":") + TwoDigits(Minute % 60);
        }
    }

    FCivilTime LjubljanaFromUnix(int64 UnixSeconds)
    {
        UnixSeconds = FMath::Max<int64>(UnixSeconds, 0);
        const int64 Local = UnixSeconds + (IsSummer(UnixSeconds) ? 2 : 1) * 3600;
        FCivilTime Civil = CivilFromDays(Local / SecondsPerDay);
        Civil.Minute = static_cast<int32>((Local % SecondsPerDay) / 60);
        return Civil;
    }

    bool IsOpen(const Gameplay::FShopDefinition& Shop, int32 Weekday, int32 Minute)
    {
        if (Weekday < 0 || Weekday > 6 || Minute < 0 || Minute > 24 * 60 - 1 || !(Shop.DayMask & (1 << Weekday)))
        {
            return false;
        }
        if (Shop.OpenMinute == Shop.CloseMinute)
        {
            return false;
        }
        if (Shop.OpenMinute < Shop.CloseMinute)
        {
            return Minute >= Shop.OpenMinute && Minute < Shop.CloseMinute;
        }
        return Minute >= Shop.OpenMinute || Minute < Shop.CloseMinute;
    }

    FString HoursLabel(const Gameplay::FShopDefinition& Shop)
    {
        static const TCHAR* Names[] = { TEXT("Mon"), TEXT("Tue"), TEXT("Wed"), TEXT("Thu"), TEXT("Fri"), TEXT("Sat"), TEXT("Sun") };
        TArray<FString> Runs;
        int32 RunStart = -1;
        for (int32 Day = 0; Day <= 7; ++Day)
        {
            const bool bOn = Day < 7 && (Shop.DayMask & (1 << Day));
            if (bOn && RunStart < 0)
            {
                RunStart = Day;
            }
            if (!bOn && RunStart >= 0)
            {
                Runs.Add(Day - 1 == RunStart ? FString(Names[RunStart]) : FString::Printf(TEXT("%s–%s"), Names[RunStart], Names[Day - 1]));
                RunStart = -1;
            }
        }
        const FString Days = Runs.IsEmpty() ? FString(TEXT("closed")) : FString::Join(Runs, TEXT(", "));
        return FString::Printf(TEXT("%s %s–%s"), *Days, *FormatMinute(Shop.OpenMinute), *FormatMinute(Shop.CloseMinute));
    }
}
