#pragma once

#include "CoreMinimal.h"

/**
 * The world clock (ADR-064): the original's 2026-10-09 shared-world calendar decision
 * (docs/CLIMATE_CALENDAR_WEATHER_ROADMAP.md), which the original designed but never built. One server-owned
 * Europe/Ljubljana civil clock runs at real time for every player on a shard, wherever they are; its date is mirrored
 * into the 1990s with month, day, hour and minute kept, and the weekday is the mirrored date's own (Friday
 * 9 October 2026 is Wednesday 9 October 1996).
 *
 * Also the sky over Maribor at an instant: where the sun and the moon are and how much of the moon is lit, from the
 * real UTC instant (the mirrored date is the same day of the year, and the decade between moves the sun by less than
 * the precision here). Engine-neutral and pure; the server keeps the clock (MarburgumServerClock.h), the game state
 * carries it to clients, and they draw the sky from it.
 */
namespace MarburgumII::Clock
{
    /** Where the sky is computed from: the middle of Stari most (the places file's origin, D96/TM E 549527.8, N 157564.0). */
    inline constexpr double MariborLatitudeDeg = 46.5567;
    inline constexpr double MariborLongitudeDeg = 15.6457;

    /** A Europe/Ljubljana civil instant. Weekday 0 is Monday. */
    struct FCivil
    {
        int32 Year = 1970;
        int32 Month = 1;
        int32 Day = 1;
        int32 Weekday = 3;
        int32 Hour = 0;
        int32 Minute = 0;
        int32 Second = 0;
        /** Hours ahead of UTC: 1 for CET, 2 for CEST. */
        int32 UtcOffsetHours = 1;

        /** Minutes since local midnight. */
        int32 MinuteOfDay() const { return Hour * 60 + Minute; }
        /** Hours since local midnight, with the minutes and seconds as a fraction. */
        double HourOfDay() const { return Hour + Minute / 60.0 + Second / 3600.0; }
    };

    /** Europe/Ljubljana at a UTC instant, with the EU summer-time rule of MarburgumRetail.h (one authority for both). */
    MARBURGUMIISHARED_API FCivil LjubljanaAt(const FDateTime& Utc);

    /**
     * The Marburgum date and time at a UTC instant: Ljubljana's civil time with the year mirrored into the 1990s
     * (MarburgumWeather.h MirrorDate, 29 February reading as 28 in a common year) and the weekday recomputed for the
     * mirrored date. Hour, minute and second are Ljubljana's.
     */
    MARBURGUMIISHARED_API FCivil MarburgumAt(const FDateTime& Utc);

    /** "Wednesday 13 November 1996". */
    MARBURGUMIISHARED_API FString DateLabel(const FCivil& Civil);
    /** "20:15". */
    MARBURGUMIISHARED_API FString TimeLabel(const FCivil& Civil);
    /** "Wed 13 Nov 1996 · 20:15", for the banner. */
    MARBURGUMIISHARED_API FString ShortLabel(const FCivil& Civil);

    /** A body in the sky as seen from a place: degrees above the horizon, and clockwise from north. */
    struct FSkyPosition
    {
        double ElevationDeg = 0.0;
        double AzimuthDeg = 0.0;
    };

    /**
     * The sun's apparent position: NOAA's solar position equations (Meeus), refraction included, so the sun is
     * at -0.833° at sunrise. Good to about 0.05° in elevation over this century.
     */
    MARBURGUMIISHARED_API FSkyPosition SunAt(const FDateTime& Utc, double LatitudeDeg = MariborLatitudeDeg, double LongitudeDeg = MariborLongitudeDeg);

    struct FMoon
    {
        /** Topocentric (parallax corrected), without refraction. */
        FSkyPosition Position;
        /** The lit fraction of the disc, 0 at new moon and 1 at full. */
        double Illumination = 0.0;
    };

    /**
     * The moon: Paul Schlyter's low-precision lunar theory ("How to compute planetary positions"), with the main
     * perturbations and the topocentric parallax. Good to about half a degree; the phase to a few hundredths.
     */
    MARBURGUMIISHARED_API FMoon MoonAt(const FDateTime& Utc, double LatitudeDeg = MariborLatitudeDeg, double LongitudeDeg = MariborLongitudeDeg);

    /** The replicated clock: a UTC instant at a server world time, and how fast it runs from there. */
    struct FAnchor
    {
        FDateTime Utc;
        /** The server's world time (seconds) at which the clock read Utc. */
        double ServerSeconds = 0.0;
        /** Clock seconds per world second: 1 in play, more in reviews and tests. */
        double Speed = 1.0;

        /** The clock at a server world time. A non-finite or non-positive speed runs at real time. */
        MARBURGUMIISHARED_API FDateTime At(double ServerSecondsNow) const;
    };
}
