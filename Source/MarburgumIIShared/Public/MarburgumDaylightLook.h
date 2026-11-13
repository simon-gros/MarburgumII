#pragma once

#include "CoreMinimal.h"
#include "MarburgumWorldClock.h"

/**
 * Day and night over Lent (ADR-064): what the world clock's sky (MarburgumWorldClock.h) does to the level's light, as
 * numbers relative to the level's own clear day, under whatever the weather (MarburgumWeatherLook.h) then does.
 *
 * - **The sun** is turned to where it is over Maribor at that instant; Lent's sky atmosphere colours it low and takes
 *   it below the horizon. Its strength is the level's, faded out over the last degrees as it sets, so nothing is lit
 *   from under the ground.
 * - **The night light** is the moon, where it is up, plus a faint glow from high in the south, the city's own
 *   skyglow, which is the floor that keeps a moonless night playable. One light carries both, coming from between the
 *   two weighted by their strengths, so it never jumps as the moon rises or sets. Neither is physical: a real full
 *   moon is 1/400,000 of the sun, which no screen shows. A full moon high up gives MoonOverSun of the sun, a thinner
 *   moon much less (the opposition surge: roughly the lit fraction cubed). It comes in through twilight.
 * - **The eye** cannot open all the way at night. Lent's histogram auto exposure would otherwise adapt a moonless
 *   street to the brightness of noon. Its adaptation floor is raised in the dark, so night reads as night while the
 *   street, people and the far bank stay legible: the original design asks that night keep interactions and
 *   objectives visible. Calibrated on the promenade view of the weather smoke (ADR-064).
 * - **The haze** glows with the day: its inscattering is luminance the level authored for daylight, so it dims at night.
 *
 * Pure and engine-neutral; AMarburgumWeatherEffects draws it.
 */
namespace MarburgumII::DaylightLook
{
    /** The full moon's light over the level's sun. */
    inline constexpr float MoonOverSun = 0.04f;
    /** The skyglow floor over the level's sun: a moonless night. */
    inline constexpr float SkyglowOverSun = 0.015f;
    /** Where the skyglow comes from: high in the south, the way the city's glow sits over the old town from Lent. */
    inline constexpr float SkyglowElevationDeg = 60.0f;
    inline constexpr float SkyglowAzimuthDeg = 200.0f;
    /** The moon's light reaches its full share once it is this high; it rises from nothing at the horizon. */
    inline constexpr float MoonFullAboveDeg = 10.0f;
    /**
     * The eye's adaptation floor in full night, over the level's (0.03). Measured on the promenade (mean grey of 255, the
     * level's noon 131): with the level's own floor a moonless night adapted to 94 and a full moon to 108, as bright as
     * day; at six times it a moonless night is 26, the street and the far bank in silhouette.
     */
    inline constexpr float NightMinBrightnessScale = 6.0f;
    /** The haze's glow in full night, as a fraction of the level's. */
    inline constexpr float NightFogGlow = 0.04f;

    struct FDaylight
    {
        /** The sun light's rotation, degrees (light travels along its forward vector: X north, Y east, Z up). */
        float SunPitch = -45.0f;
        float SunYaw = 180.0f;
        /** The sun light's strength over the level's. */
        float SunScale = 1.0f;

        /** The night light's strength over the level's sun, its rotation, and whether the moon outshines the skyglow. */
        float NightLight = 0.0f;
        float NightPitch = -SkyglowElevationDeg;
        float NightYaw = SkyglowAzimuthDeg + 180.0f;
        bool bMoonLight = false;

        /** 0 in daylight, 1 once the sun is 12° under (nautical dusk); twilight in between. */
        float Night = 0.0f;
        /** The factor on the level's adaptation floor. */
        float MinBrightnessScale = 1.0f;
        /** The haze's inscattering over the level's. */
        float FogGlow = 1.0f;
        /** For a sky light that does not capture the sky live: its strength over the level's. */
        float StaticSkyScale = 1.0f;

        /** Copies of the sky for logs and evidence. */
        Clock::FSkyPosition Sun;
        Clock::FMoon Moon;
    };

    /** Where a light at this sky position points: pitch -elevation, yaw towards the azimuth's opposite. */
    MARBURGUMIISHARED_API void LightRotation(const Clock::FSkyPosition& From, float& OutPitch, float& OutYaw);

    /** The daylight under this sky. Non-finite positions read as the sun at noon and no moon. */
    MARBURGUMIISHARED_API FDaylight Compute(const Clock::FSkyPosition& Sun, const Clock::FMoon& Moon);

    /** The daylight at a UTC instant over Maribor. */
    MARBURGUMIISHARED_API FDaylight At(const FDateTime& Utc);
}
