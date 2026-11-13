#include "MarburgumDaylightLook.h"
#include "MarburgumWorldClock.h"
#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// The world clock and the sky over Maribor (ADR-064). The reference positions are PyEphem 4.2.1's for the same place
// (46.5567 N, 15.6457 E, 270 m), the sun with standard refraction and the moon topocentric without it.

namespace
{
    using namespace MarburgumII;

    constexpr EAutomationTestFlags ClockTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
        | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter;

    FDateTime Utc(int32 Year, int32 Month, int32 Day, int32 Hour, int32 Minute = 0, int32 Second = 0)
    {
        return FDateTime(Year, Month, Day, Hour, Minute, Second);
    }

    double AngleBetween(double A, double B)
    {
        const double D = FMath::Fmod(FMath::Abs(A - B), 360.0);
        return D > 180.0 ? 360.0 - D : D;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumWorldClockCalendarTest, "Marburgum.Shared.WorldClock.Calendar", ClockTestFlags)
bool FMarburgumWorldClockCalendarTest::RunTest(const FString& Parameters)
{
    using namespace Clock;

    // The decision's own example: 9 October 2026 at 02:30 in Maribor is Wednesday 9 October 1996 at 02:30, though
    // 9 October 2026 is a Friday. CEST then, so 00:30 UTC.
    const FCivil Example = MarburgumAt(Utc(2026, 10, 9, 0, 30));
    TestEqual(TEXT("mirrored year"), Example.Year, 1996);
    TestEqual(TEXT("month kept"), Example.Month, 10);
    TestEqual(TEXT("day kept"), Example.Day, 9);
    TestEqual(TEXT("Ljubljana hour"), Example.Hour, 2);
    TestEqual(TEXT("minute"), Example.Minute, 30);
    TestEqual(TEXT("the mirrored date's own weekday: Wednesday"), Example.Weekday, 2);
    TestEqual(TEXT("summer time"), Example.UtcOffsetHours, 2);
    TestEqual(TEXT("the real date is a Friday"), LjubljanaAt(Utc(2026, 10, 9, 0, 30)).Weekday, 4);
    TestEqual(TEXT("date label"), DateLabel(Example), FString(TEXT("Wednesday 9 October 1996")));
    TestEqual(TEXT("time label"), TimeLabel(Example), FString(TEXT("02:30")));
    TestEqual(TEXT("short label"), ShortLabel(Example), FString(TEXT("Wed 9 Oct 1996  ·  02:30")));

    // Winter time; the evening of 13 November 2026 UTC is the same evening in 1996.
    const FCivil Winter = MarburgumAt(Utc(2026, 11, 13, 19, 15, 42));
    TestEqual(TEXT("CET"), Winter.UtcOffsetHours, 1);
    TestEqual(TEXT("20:15 in Maribor"), TimeLabel(Winter), FString(TEXT("20:15")));
    TestEqual(TEXT("seconds kept"), Winter.Second, 42);
    TestEqual(TEXT("13 November 1996 was a Wednesday"), DateLabel(Winter), FString(TEXT("Wednesday 13 November 1996")));

    // Local midnight crosses the date before UTC does.
    const FCivil PastMidnight = MarburgumAt(Utc(2026, 12, 31, 23, 30));
    TestEqual(TEXT("New Year in Maribor while it is still 31 December in UTC"), DateLabel(PastMidnight),
        FString(TEXT("Wednesday 1 January 1997")));
    TestEqual(TEXT("00:30"), TimeLabel(PastMidnight), FString(TEXT("00:30")));

    // The EU changeovers: last Sunday of March and October at 01:00 UTC.
    TestEqual(TEXT("before spring forward"), LjubljanaAt(Utc(2026, 3, 29, 0, 59)).UtcOffsetHours, 1);
    TestEqual(TEXT("after spring forward"), LjubljanaAt(Utc(2026, 3, 29, 1, 0)).UtcOffsetHours, 2);
    TestEqual(TEXT("before fall back"), LjubljanaAt(Utc(2026, 10, 25, 0, 59)).UtcOffsetHours, 2);
    TestEqual(TEXT("after fall back"), LjubljanaAt(Utc(2026, 10, 25, 1, 0)).UtcOffsetHours, 1);

    // 29 February in a year whose mirror is not a leap year reads as the 28th, which keeps its own weekday.
    const FCivil Leap = MarburgumAt(Utc(2028, 2, 29, 12, 0));
    TestEqual(TEXT("1998 has no 29 February"), DateLabel(Leap), FString(TEXT("Saturday 28 February 1998")));

    // The anchor: the clock at another world time, at real time and sped up.
    FAnchor Anchor;
    Anchor.Utc = Utc(2026, 6, 18, 8, 0);
    Anchor.ServerSeconds = 100.0;
    Anchor.Speed = 1.0;
    TestEqual(TEXT("ten seconds on"), Anchor.At(110.0), Utc(2026, 6, 18, 8, 0, 10));
    Anchor.Speed = 1440.0;
    TestEqual(TEXT("a day a minute"), Anchor.At(160.0), Utc(2026, 6, 19, 8, 0));
    Anchor.Speed = std::numeric_limits<double>::quiet_NaN();
    TestEqual(TEXT("a nonsense speed runs at real time"), Anchor.At(110.0), Utc(2026, 6, 18, 8, 0, 10));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumWorldClockSkyTest, "Marburgum.Shared.WorldClock.Sky", ClockTestFlags)
bool FMarburgumWorldClockSkyTest::RunTest(const FString& Parameters)
{
    using namespace Clock;

    struct FCase
    {
        FDateTime At;
        double SunElevation;
        double SunAzimuth;
        double MoonElevation;
        double MoonAzimuth;
        double MoonLit;
        const TCHAR* What;
    };
    const FCase Cases[] = {
        { Utc(2026, 6, 21, 10, 57), 66.883, 178.700, 2.889, 93.593, 0.454, TEXT("summer solstice noon: 90 - 46.56 + 23.44") },
        { Utc(2026, 12, 21, 10, 57), 20.047, 180.376, -11.856, 37.635, 0.900, TEXT("winter solstice noon") },
        { Utc(2026, 3, 20, 11, 10), 43.386, 181.766, 51.067, 156.573, 0.027, TEXT("equinox noon") },
        { Utc(2026, 11, 13, 7, 0), 8.556, 127.655, -28.634, 101.165, 0.144, TEXT("a November morning") },
        { Utc(2026, 11, 13, 15, 0), 3.393, 239.305, 14.876, 192.191, 0.166, TEXT("a November afternoon") },
        { Utc(2026, 11, 13, 21, 0), -54.700, 315.060, -26.294, 257.835, 0.184, TEXT("a November night") },
        { Utc(2026, 3, 3, 11, 38), 36.364, 188.858, -37.418, 9.072, 1.000, TEXT("the total lunar eclipse of 3 March 2026: full") },
        { Utc(2026, 8, 12, 17, 46), 3.635, 287.990, 3.285, 288.278, 0.000, TEXT("the solar eclipse of 12 August 2026: new, and on the sun") },
        { Utc(1996, 11, 13, 20, 0), -46.371, 296.710, -22.965, 266.232, 0.089, TEXT("a 1990s night") },
    };
    for (const FCase& Case : Cases)
    {
        const FSkyPosition Sun = SunAt(Case.At);
        const FMoon Moon = MoonAt(Case.At);
        TestTrue(FString::Printf(TEXT("%s: sun elevation %.3f near %.3f"), Case.What, Sun.ElevationDeg, Case.SunElevation),
            FMath::Abs(Sun.ElevationDeg - Case.SunElevation) < 0.05);
        TestTrue(FString::Printf(TEXT("%s: sun azimuth %.3f near %.3f"), Case.What, Sun.AzimuthDeg, Case.SunAzimuth),
            AngleBetween(Sun.AzimuthDeg, Case.SunAzimuth) < 0.1);
        TestTrue(FString::Printf(TEXT("%s: moon elevation %.3f near %.3f"), Case.What, Moon.Position.ElevationDeg, Case.MoonElevation),
            FMath::Abs(Moon.Position.ElevationDeg - Case.MoonElevation) < 0.3);
        TestTrue(FString::Printf(TEXT("%s: moon azimuth %.3f near %.3f"), Case.What, Moon.Position.AzimuthDeg, Case.MoonAzimuth),
            AngleBetween(Moon.Position.AzimuthDeg, Case.MoonAzimuth) < 0.3);
        TestTrue(FString::Printf(TEXT("%s: moon lit %.3f near %.3f"), Case.What, Moon.Illumination, Case.MoonLit),
            FMath::Abs(Moon.Illumination - Case.MoonLit) < 0.01);
    }

    // The disc's centre appears on the horizon on 13 November 2026 at 05:58:06 and 15:24:46 UTC (PyEphem, 1010 hPa,
    // 10 °C, refraction included as here).
    TestTrue(TEXT("the centre is still under at 05:57"), SunAt(Utc(2026, 11, 13, 5, 57)).ElevationDeg < 0.0);
    TestTrue(TEXT("and up at 05:59"), SunAt(Utc(2026, 11, 13, 5, 59)).ElevationDeg > 0.0);
    TestTrue(TEXT("still up at 15:24"), SunAt(Utc(2026, 11, 13, 15, 24)).ElevationDeg > 0.0);
    TestTrue(TEXT("down by 15:26"), SunAt(Utc(2026, 11, 13, 15, 26)).ElevationDeg < 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumDaylightLookTest, "Marburgum.Shared.WorldClock.Daylight", ClockTestFlags)
bool FMarburgumDaylightLookTest::RunTest(const FString& Parameters)
{
    using namespace DaylightLook;

    // The light points away from where the sun is: a sun in the south-east at 30° shines north-west and down.
    Clock::FSkyPosition SouthEast;
    SouthEast.ElevationDeg = 30.0;
    SouthEast.AzimuthDeg = 135.0;
    float Pitch = 0.0f;
    float Yaw = 0.0f;
    LightRotation(SouthEast, Pitch, Yaw);
    TestTrue(TEXT("pitched 30° down"), FMath::IsNearlyEqual(Pitch, -30.0f, 0.01f));
    TestTrue(TEXT("heading north-west (-45° from north towards west)"), FMath::IsNearlyEqual(FRotator::NormalizeAxis(Yaw), -45.0f, 0.01f));
    const FVector Forward = FRotator(Pitch, Yaw, 0.0f).Vector();
    TestTrue(TEXT("travelling north (+X) and west (-Y) and down"), Forward.X > 0.5 && Forward.Y < -0.5 && Forward.Z < -0.4);

    // A summer morning: full sun, no night light, the level's own eye and haze.
    const FDaylight Morning = At(FDateTime(2026, 6, 18, 8, 0));
    TestTrue(TEXT("morning sun at full strength"), FMath::IsNearlyEqual(Morning.SunScale, 1.0f));
    TestTrue(TEXT("no night light by day"), Morning.NightLight == 0.0f);
    TestTrue(TEXT("no night"), Morning.Night == 0.0f);
    TestTrue(TEXT("the level's exposure by day"), FMath::IsNearlyEqual(Morning.MinBrightnessScale, 1.0f));
    TestTrue(TEXT("the level's haze by day"), FMath::IsNearlyEqual(Morning.FogGlow, 1.0f));

    // Sunset on 13 November: the sun fading, the night light coming in, twilight under way.
    const FDaylight Dusk = At(FDateTime(2026, 11, 13, 15, 40));
    TestTrue(FString::Printf(TEXT("the sun nearly gone at %.2f°"), Dusk.Sun.ElevationDeg), Dusk.SunScale < 0.2f);
    TestTrue(TEXT("twilight, not night"), Dusk.Night > 0.0f && Dusk.Night < 0.6f);

    // A November night with a thin moon below the horizon: the skyglow floor, dark sun, the eye open.
    const FDaylight Night = At(FDateTime(2026, 11, 13, 21, 0));
    TestTrue(TEXT("no sun at night"), Night.SunScale == 0.0f);
    TestTrue(TEXT("full night"), FMath::IsNearlyEqual(Night.Night, 1.0f));
    TestTrue(TEXT("the skyglow and nothing more"), FMath::IsNearlyEqual(Night.NightLight, SkyglowOverSun, 1.0e-4f) && !Night.bMoonLight);
    TestTrue(TEXT("the eye held back"), FMath::IsNearlyEqual(Night.MinBrightnessScale, NightMinBrightnessScale));
    TestTrue(TEXT("the haze no longer glowing"), Night.FogGlow < 0.1f);
    TestTrue(TEXT("the skyglow from high in the south"), FMath::IsNearlyEqual(Night.NightPitch, -SkyglowElevationDeg, 0.01f));

    // A full moon high on a winter night (25 December 2026, close to full): the moon lights the street, from where it is.
    const FDaylight Moonlit = At(FDateTime(2026, 12, 24, 23, 0));
    TestTrue(FString::Printf(TEXT("a full moon (%.3f lit) high (%.1f°)"), Moonlit.Moon.Illumination, Moonlit.Moon.Position.ElevationDeg),
        Moonlit.Moon.Illumination > 0.95 && Moonlit.Moon.Position.ElevationDeg > 40.0);
    TestTrue(TEXT("outshines the skyglow"), Moonlit.bMoonLight && Moonlit.NightLight > 3.0f * SkyglowOverSun);
    TestTrue(TEXT("but stays well under the sun"), Moonlit.NightLight <= MoonOverSun + SkyglowOverSun);
    float MoonPitch = 0.0f;
    float MoonYaw = 0.0f;
    LightRotation(Moonlit.Moon.Position, MoonPitch, MoonYaw);
    const double OffMoon = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
        FVector::DotProduct(FRotator(Moonlit.NightPitch, Moonlit.NightYaw, 0.0f).Vector(), FRotator(MoonPitch, MoonYaw, 0.0f).Vector()), -1.0, 1.0)));
    TestTrue(FString::Printf(TEXT("coming from close to the moon (%.1f degrees off it)"), OffMoon), OffMoon < 12.0);

    // Untrusted input reads as a plain day.
    Clock::FSkyPosition Broken;
    Broken.ElevationDeg = std::numeric_limits<double>::quiet_NaN();
    Clock::FMoon NoMoon;
    NoMoon.Illumination = std::numeric_limits<double>::infinity();
    const FDaylight Safe = Compute(Broken, NoMoon);
    TestTrue(TEXT("nonsense reads as noon"), FMath::IsFinite(Safe.SunPitch) && Safe.SunScale == 1.0f && Safe.NightLight == 0.0f);
    return true;
}

#endif
