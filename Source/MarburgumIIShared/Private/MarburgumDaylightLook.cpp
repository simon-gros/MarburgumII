#include "MarburgumDaylightLook.h"

namespace MarburgumII::DaylightLook
{
    namespace
    {
        float Smooth(float Edge0, float Edge1, float X)
        {
            const float T = FMath::Clamp((X - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
            return T * T * (3.0f - 2.0f * T);
        }

        /** Unit vector towards a sky position: X north, Y east, Z up. */
        FVector Towards(double ElevationDeg, double AzimuthDeg)
        {
            const double E = FMath::DegreesToRadians(ElevationDeg);
            const double A = FMath::DegreesToRadians(AzimuthDeg);
            return FVector(FMath::Cos(E) * FMath::Cos(A), FMath::Cos(E) * FMath::Sin(A), FMath::Sin(E));
        }

        void RotationTowards(const FVector& Direction, float& OutPitch, float& OutYaw)
        {
            const FRotator Rotation = (-Direction).Rotation();
            OutPitch = static_cast<float>(Rotation.Pitch);
            OutYaw = static_cast<float>(FRotator::NormalizeAxis(Rotation.Yaw));
        }

        bool Finite(const Clock::FSkyPosition& Position)
        {
            return FMath::IsFinite(Position.ElevationDeg) && FMath::IsFinite(Position.AzimuthDeg);
        }
    }

    void LightRotation(const Clock::FSkyPosition& From, float& OutPitch, float& OutYaw)
    {
        RotationTowards(Towards(From.ElevationDeg, From.AzimuthDeg), OutPitch, OutYaw);
    }

    FDaylight Compute(const Clock::FSkyPosition& InSun, const Clock::FMoon& InMoon)
    {
        Clock::FSkyPosition Sun = InSun;
        if (!Finite(Sun))
        {
            Sun.ElevationDeg = 45.0;
            Sun.AzimuthDeg = 180.0;
        }
        Clock::FMoon Moon = InMoon;
        if (!Finite(Moon.Position) || !FMath::IsFinite(Moon.Illumination))
        {
            Moon = Clock::FMoon();
            Moon.Position.ElevationDeg = -90.0;
        }
        Moon.Illumination = FMath::Clamp(Moon.Illumination, 0.0, 1.0);

        FDaylight Out;
        Out.Sun = Sun;
        Out.Moon = Moon;
        const float Elevation = static_cast<float>(Sun.ElevationDeg);
        LightRotation(Sun, Out.SunPitch, Out.SunYaw);
        // Gone a degree and a half after the disc's centre crosses the horizon; full again two degrees up, where the
        // atmosphere has already reddened and weakened it.
        Out.SunScale = Smooth(-1.5f, 2.0f, Elevation);

        // Twilight: the sky still lights the street down to the sun at -6° (civil dusk); full night from -12°.
        Out.Night = 1.0f - Smooth(-12.0f, 0.0f, Elevation);

        const float MoonPart = MoonOverSun * FMath::Pow(static_cast<float>(Moon.Illumination), 3.0f)
            * Smooth(0.0f, MoonFullAboveDeg, static_cast<float>(Moon.Position.ElevationDeg));
        const float GlowPart = SkyglowOverSun;
        // In from the moment the sun sets to the end of civil dusk.
        Out.NightLight = (MoonPart + GlowPart) * Smooth(1.0f, -6.0f, Elevation);
        Out.bMoonLight = MoonPart > GlowPart;
        const FVector Direction = (Towards(Moon.Position.ElevationDeg, Moon.Position.AzimuthDeg) * MoonPart
            + Towards(SkyglowElevationDeg, SkyglowAzimuthDeg) * GlowPart).GetSafeNormal();
        RotationTowards(Direction.IsNearlyZero() ? Towards(SkyglowElevationDeg, SkyglowAzimuthDeg) : Direction, Out.NightPitch, Out.NightYaw);

        Out.MinBrightnessScale = FMath::Lerp(1.0f, NightMinBrightnessScale, Out.Night);
        Out.FogGlow = FMath::Lerp(1.0f, NightFogGlow, Out.Night);
        Out.StaticSkyScale = FMath::Lerp(1.0f, 0.1f, Out.Night);
        return Out;
    }

    FDaylight At(const FDateTime& Utc)
    {
        return Compute(Clock::SunAt(Utc), Clock::MoonAt(Utc));
    }
}
