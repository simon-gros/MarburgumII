#include "MarburgumWorldClock.h"

#include "MarburgumRetail.h"
#include "MarburgumWeather.h"

namespace MarburgumII::Clock
{
    namespace
    {
        const TCHAR* const DayNames[] = { TEXT("Monday"), TEXT("Tuesday"), TEXT("Wednesday"), TEXT("Thursday"), TEXT("Friday"),
            TEXT("Saturday"), TEXT("Sunday") };
        const TCHAR* const MonthNames[] = { TEXT("January"), TEXT("February"), TEXT("March"), TEXT("April"), TEXT("May"),
            TEXT("June"), TEXT("July"), TEXT("August"), TEXT("September"), TEXT("October"), TEXT("November"), TEXT("December") };

        constexpr double DegToRad = UE_DOUBLE_PI / 180.0;
        constexpr double RadToDeg = 180.0 / UE_DOUBLE_PI;

        double Sin(double Degrees) { return FMath::Sin(Degrees * DegToRad); }
        double Cos(double Degrees) { return FMath::Cos(Degrees * DegToRad); }

        double Wrap360(double Degrees)
        {
            Degrees = FMath::Fmod(Degrees, 360.0);
            return Degrees < 0.0 ? Degrees + 360.0 : Degrees;
        }

        /** Julian date of a UTC instant. */
        double JulianDate(const FDateTime& Utc)
        {
            return Utc.GetJulianDay();
        }

        /** Elevation and azimuth (clockwise from north) from declination and local hour angle, all degrees. */
        FSkyPosition Horizontal(double DeclinationDeg, double HourAngleDeg, double LatitudeDeg)
        {
            const double SinElevation = Sin(LatitudeDeg) * Sin(DeclinationDeg) + Cos(LatitudeDeg) * Cos(DeclinationDeg) * Cos(HourAngleDeg);
            FSkyPosition Out;
            Out.ElevationDeg = FMath::Asin(FMath::Clamp(SinElevation, -1.0, 1.0)) * RadToDeg;
            // Measured from south towards west, then turned to north-based clockwise.
            const double FromSouth = FMath::Atan2(Sin(HourAngleDeg), Cos(HourAngleDeg) * Sin(LatitudeDeg) - FMath::Tan(DeclinationDeg * DegToRad) * Cos(LatitudeDeg));
            Out.AzimuthDeg = Wrap360(FromSouth * RadToDeg + 180.0);
            return Out;
        }

        /** NOAA's approximation to atmospheric refraction, degrees to add to a geometric elevation. */
        double Refraction(double ElevationDeg)
        {
            if (ElevationDeg > 85.0)
            {
                return 0.0;
            }
            const double T = FMath::Tan(ElevationDeg * DegToRad);
            double ArcSeconds = 0.0;
            if (ElevationDeg > 5.0)
            {
                ArcSeconds = 58.1 / T - 0.07 / (T * T * T) + 0.000086 / FMath::Pow(T, 5.0);
            }
            else if (ElevationDeg > -0.575)
            {
                ArcSeconds = 1735.0 + ElevationDeg * (-518.2 + ElevationDeg * (103.4 + ElevationDeg * (-12.79 + ElevationDeg * 0.711)));
            }
            else
            {
                ArcSeconds = -20.774 / T;
            }
            return ArcSeconds / 3600.0;
        }
    }

    FCivil LjubljanaAt(const FDateTime& Utc)
    {
        const int64 Unix = Utc.ToUnixTimestamp();
        const Retail::FCivilTime Local = Retail::LjubljanaFromUnix(Unix);
        FCivil Out;
        Out.Year = Local.Year;
        Out.Month = Local.Month;
        Out.Day = Local.Day;
        Out.Weekday = Local.Weekday;
        Out.Hour = Local.Minute / 60;
        Out.Minute = Local.Minute % 60;
        Out.Second = static_cast<int32>(((Unix % 60) + 60) % 60);
        const int32 UtcMinute = Utc.GetHour() * 60 + Utc.GetMinute();
        Out.UtcOffsetHours = (((Local.Minute - UtcMinute) % 1440 + 1440) % 1440) / 60;
        return Out;
    }

    FCivil MarburgumAt(const FDateTime& Utc)
    {
        FCivil Out = LjubljanaAt(Utc);
        int32 Year = Out.Year;
        int32 Month = Out.Month;
        int32 Day = Out.Day;
        Weather::MirrorDate(Out.Year, Out.Month, Out.Day, Year, Month, Day);
        Out.Year = Year;
        Out.Month = Month;
        Out.Day = Day;
        Out.Weekday = static_cast<int32>(FDateTime(Year, Month, Day).GetDayOfWeek());
        return Out;
    }

    FString DateLabel(const FCivil& Civil)
    {
        return FString::Printf(TEXT("%s %d %s %d"), DayNames[FMath::Clamp(Civil.Weekday, 0, 6)], Civil.Day,
            MonthNames[FMath::Clamp(Civil.Month, 1, 12) - 1], Civil.Year);
    }

    FString TimeLabel(const FCivil& Civil)
    {
        return FString::Printf(TEXT("%02d:%02d"), Civil.Hour, Civil.Minute);
    }

    FString ShortLabel(const FCivil& Civil)
    {
        const FString Day = FString(DayNames[FMath::Clamp(Civil.Weekday, 0, 6)]).Left(3);
        const FString Month = FString(MonthNames[FMath::Clamp(Civil.Month, 1, 12) - 1]).Left(3);
        return FString::Printf(TEXT("%s %d %s %d  ·  %s"), *Day, Civil.Day, *Month, Civil.Year, *TimeLabel(Civil));
    }

    FSkyPosition SunAt(const FDateTime& Utc, double LatitudeDeg, double LongitudeDeg)
    {
        // NOAA Solar Calculator (after Meeus, Astronomical Algorithms), the spreadsheet's equations in order.
        const double T = (JulianDate(Utc) - 2451545.0) / 36525.0;
        const double L0 = Wrap360(280.46646 + T * (36000.76983 + T * 0.0003032));
        const double M = 357.52911 + T * (35999.05029 - 0.0001537 * T);
        const double E = 0.016708634 - T * (0.000042037 + 0.0000001267 * T);
        const double C = Sin(M) * (1.914602 - T * (0.004817 + 0.000014 * T)) + Sin(2.0 * M) * (0.019993 - 0.000101 * T)
            + Sin(3.0 * M) * 0.000289;
        const double Omega = 125.04 - 1934.136 * T;
        const double Lambda = L0 + C - 0.00569 - 0.00478 * Sin(Omega);
        const double Epsilon0 = 23.0 + (26.0 + (21.448 - T * (46.815 + T * (0.00059 - T * 0.001813))) / 60.0) / 60.0;
        const double Epsilon = Epsilon0 + 0.00256 * Cos(Omega);
        const double Declination = FMath::Asin(Sin(Epsilon) * Sin(Lambda)) * RadToDeg;

        const double Y = FMath::Square(FMath::Tan(Epsilon / 2.0 * DegToRad));
        const double EquationOfTimeMinutes = 4.0 * RadToDeg
            * (Y * Sin(2.0 * L0) - 2.0 * E * Sin(M) + 4.0 * E * Y * Sin(M) * Cos(2.0 * L0) - 0.5 * Y * Y * Sin(4.0 * L0)
                - 1.25 * E * E * Sin(2.0 * M));
        const double UtcMinutes = Utc.GetTimeOfDay().GetTotalMinutes();
        const double TrueSolarMinutes = FMath::Fmod(UtcMinutes + EquationOfTimeMinutes + 4.0 * LongitudeDeg + 1440.0 * 2.0, 1440.0);
        const double HourAngle = TrueSolarMinutes / 4.0 - 180.0;

        FSkyPosition Out = Horizontal(Declination, HourAngle, LatitudeDeg);
        Out.ElevationDeg += Refraction(Out.ElevationDeg);
        return Out;
    }

    FMoon MoonAt(const FDateTime& Utc, double LatitudeDeg, double LongitudeDeg)
    {
        // Schlyter's elements are counted from 2000 January 0.0 UT.
        const double D = JulianDate(Utc) - 2451543.5;

        const double N = Wrap360(125.1228 - 0.0529538083 * D);
        const double I = 5.1454;
        const double W = Wrap360(318.0634 + 0.1643573223 * D);
        const double A = 60.2666;
        const double Ecc = 0.054900;
        const double M = Wrap360(115.3654 + 13.0649929509 * D);

        const double SunW = 282.9404 + 4.70935e-5 * D;
        const double SunM = Wrap360(356.0470 + 0.9856002585 * D);
        const double SunL = Wrap360(SunW + SunM);

        // Kepler's equation.
        double EccAnomaly = M + Ecc * RadToDeg * Sin(M) * (1.0 + Ecc * Cos(M));
        for (int32 Iteration = 0; Iteration < 8; ++Iteration)
        {
            const double Next = EccAnomaly - (EccAnomaly - Ecc * RadToDeg * Sin(EccAnomaly) - M) / (1.0 - Ecc * Cos(EccAnomaly));
            if (FMath::Abs(Next - EccAnomaly) < 1.0e-6)
            {
                EccAnomaly = Next;
                break;
            }
            EccAnomaly = Next;
        }
        const double Xv = A * (Cos(EccAnomaly) - Ecc);
        const double Yv = A * FMath::Sqrt(1.0 - Ecc * Ecc) * Sin(EccAnomaly);
        const double V = FMath::Atan2(Yv, Xv) * RadToDeg;
        double R = FMath::Sqrt(Xv * Xv + Yv * Yv);

        const double Xh = R * (Cos(N) * Cos(V + W) - Sin(N) * Sin(V + W) * Cos(I));
        const double Yh = R * (Sin(N) * Cos(V + W) + Cos(N) * Sin(V + W) * Cos(I));
        const double Zh = R * (Sin(V + W) * Sin(I));
        double Lon = FMath::Atan2(Yh, Xh) * RadToDeg;
        double Lat = FMath::Atan2(Zh, FMath::Sqrt(Xh * Xh + Yh * Yh)) * RadToDeg;

        // The largest perturbations: evection, variation, the yearly equation and the rest of Schlyter's list.
        const double Lm = N + W + M;
        const double Elong = Lm - SunL;
        const double F = Lm - N;
        Lon += -1.274 * Sin(M - 2.0 * Elong) + 0.658 * Sin(2.0 * Elong) - 0.186 * Sin(SunM) - 0.059 * Sin(2.0 * M - 2.0 * Elong)
            - 0.057 * Sin(M - 2.0 * Elong + SunM) + 0.053 * Sin(M + 2.0 * Elong) + 0.046 * Sin(2.0 * Elong - SunM)
            + 0.041 * Sin(M - SunM) - 0.035 * Sin(Elong) - 0.031 * Sin(M + SunM) - 0.015 * Sin(2.0 * F - 2.0 * Elong)
            + 0.011 * Sin(M - 4.0 * Elong);
        Lat += -0.173 * Sin(F - 2.0 * Elong) - 0.055 * Sin(M - F - 2.0 * Elong) - 0.046 * Sin(M + F - 2.0 * Elong)
            + 0.033 * Sin(F + 2.0 * Elong) + 0.017 * Sin(2.0 * M + F);
        R += -0.58 * Cos(M - 2.0 * Elong) - 0.46 * Cos(2.0 * Elong);

        // Ecliptic to equatorial.
        const double Obliquity = 23.4393 - 3.563e-7 * D;
        const double Xe = Cos(Lat) * Cos(Lon);
        const double Ye = Cos(Lat) * Sin(Lon) * Cos(Obliquity) - Sin(Lat) * Sin(Obliquity);
        const double Ze = Cos(Lat) * Sin(Lon) * Sin(Obliquity) + Sin(Lat) * Cos(Obliquity);
        const double RightAscension = FMath::Atan2(Ye, Xe) * RadToDeg;
        const double Declination = FMath::Atan2(Ze, FMath::Sqrt(Xe * Xe + Ye * Ye)) * RadToDeg;

        // Local sidereal time from the sun's mean longitude, as Schlyter does.
        const double UtcHours = Utc.GetTimeOfDay().GetTotalHours();
        const double SiderealDeg = Wrap360(SunL + 180.0 + UtcHours * 15.0 + LongitudeDeg);
        const double HourAngle = SiderealDeg - RightAscension;

        FMoon Out;
        Out.Position = Horizontal(Declination, HourAngle, LatitudeDeg);
        // Seen from the surface rather than the earth's centre, the moon sits lower by up to its parallax (about 1°).
        const double Parallax = FMath::Asin(1.0 / R) * RadToDeg;
        Out.Position.ElevationDeg -= Parallax * Cos(Out.Position.ElevationDeg);

        // The phase from the angle between the sun and the moon seen from the earth, with the sun's true longitude (its
        // ecliptic latitude is zero to this precision).
        const double SunEcc = 0.016709 - 1.151e-9 * D;
        const double SunE = SunM + SunEcc * RadToDeg * Sin(SunM) * (1.0 + SunEcc * Cos(SunM));
        const double SunLon = FMath::Atan2(FMath::Sqrt(1.0 - SunEcc * SunEcc) * Sin(SunE), Cos(SunE) - SunEcc) * RadToDeg + SunW;
        const double Elongation = FMath::Acos(FMath::Clamp(Cos(SunLon - Lon) * Cos(Lat), -1.0, 1.0)) * RadToDeg;
        Out.Illumination = (1.0 - Cos(Elongation)) / 2.0;
        return Out;
    }

    FDateTime FAnchor::At(double ServerSecondsNow) const
    {
        const double Rate = FMath::IsFinite(Speed) && Speed > 0.0 ? Speed : 1.0;
        const double Elapsed = FMath::IsFinite(ServerSecondsNow) ? ServerSecondsNow - ServerSeconds : 0.0;
        return Utc + FTimespan::FromSeconds(Elapsed * Rate);
    }
}
