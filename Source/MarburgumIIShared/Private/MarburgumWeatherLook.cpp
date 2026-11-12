#include "MarburgumWeatherLook.h"

namespace MarburgumII::WeatherLook
{
    namespace
    {
        float Unit(double Value)
        {
            return FMath::IsFinite(Value) ? FMath::Clamp(static_cast<float>(Value), 0.0f, 1.0f) : 0.0f;
        }

        float NonNegative(double Value, float Max)
        {
            return FMath::IsFinite(Value) ? FMath::Clamp(static_cast<float>(Value), 0.0f, Max) : 0.0f;
        }

        /** How much of what falls is snow: sleet is taken as half and half. */
        float SnowShareOf(const FString& Type)
        {
            if (Type == TEXT("snow"))
            {
                return 1.0f;
            }
            return Type == TEXT("sleet") ? 0.5f : 0.0f;
        }

        bool Falls(const FString& Type)
        {
            return Type == TEXT("rain") || Type == TEXT("sleet") || Type == TEXT("snow");
        }

        /** The grey an overcast sky turns the haze, and the paler grey of a fog bank. */
        const FLinearColor OvercastGrey(0.56f, 0.59f, 0.63f);
        const FLinearColor FogGrey(0.76f, 0.78f, 0.80f);

        float Hash(uint32 Seed, uint32 Salt)
        {
            uint32 X = Seed * 0x9E3779B9u + Salt * 0x85EBCA6Bu;
            X ^= X >> 16;
            X *= 0x7FEB352Du;
            X ^= X >> 15;
            X *= 0x846CA68Bu;
            X ^= X >> 16;
            return static_cast<float>(X & 0xFFFFFF) / static_cast<float>(0x1000000);
        }
    }

    float FallSpeed(float SnowShare)
    {
        // About 8.5 m/s for a raindrop and 1.2 m/s for a snowflake; sleet between.
        return FMath::Lerp(8.5f, 1.2f, FMath::Clamp(SnowShare, 0.0f, 1.0f));
    }

    float SnowCoverFor(double SnowDepthM)
    {
        // Not the original's (its Unreal client drew snow only on the trees): a dusting of a centimetre shows as about
        // a third, five centimetres hides the ground.
        const float Depth = NonNegative(SnowDepthM, 10.0f);
        return FMath::Clamp(FMath::Sqrt(Depth / 0.05f), 0.0f, 1.0f);
    }

    FLook Compute(const FInput& Weather)
    {
        FLook Look;
        const float Cover = Unit(Weather.CloudCover);
        const float Fog = Unit(Weather.Fog);
        const float Thunder = Unit(Weather.Thunder);
        const float Wind = NonNegative(Weather.WindSpeed, 40.0f);
        const float Gust = Unit(Weather.Gust);
        const float Falling = Falls(Weather.PrecipitationType) ? Unit(Weather.Precipitation) : 0.0f;
        const float SnowShare = SnowShareOf(Weather.PrecipitationType);

        // --- Light. Under cover the light comes from the sky rather than the sun: the clouds stop most of the sun
        // (CloudShade, or SunScaleUnclouded where no cloud of ours can), the sky fill rises, shadows soften, and the eye
        // opens up, so the street is darker and flatter but not black. Rain darkens it a little further. The original
        // calibrated these on its city to a clear street's mean level 64 of 255 coming out about 50 overcast and 40 in
        // steady rain, about 0.8 and 1.5 EV under clear. Lent's histogram auto exposure opens the eye for a dimmer sun
        // by itself, more than enough: the original's exposure (+1.4 cover^1.5 - 0.2 falling) left Lent's promenade
        // brighter under cloud than in sun. Here the weather closes the eye a little instead, calibrated on Lent's
        // captures to about 0.7 EV under clear for a dry closed sky and 1.3 EV in steady rain (ADR-062;
        // scripts/weather-smoke-linux.sh writes brightness.txt).
        Look.SunScale = 1.0f - 0.1f * Cover - 0.1f * Falling;
        Look.SunScaleUnclouded = 1.0f - 0.88f * FMath::Pow(Cover, 1.6f);
        Look.SunSoftness = 1.0f + 39.0f * Cover * Cover;
        Look.CloudShade = 0.8f + 0.1f * Falling;
        Look.SkyScale = 1.0f + 1.6f * Cover - 0.3f * Falling;
        Look.ExposureBias = -0.7f * FMath::Pow(Cover, 1.5f) - 1.0f * Falling;
        Look.SaturationScale = 1.0f - 0.25f * Cover - 0.1f * Falling;
        Look.ContrastScale = 1.0f - 0.1f * Cover;

        // --- Air. Fog at full strength is fifty times the clear day's haze (the original's fifteen, for a city with
        // thicker haze; Lent's clear day is nearly clear, and fifteen times it left the far bank sharp at 150 m); rain
        // shortens the view too. The haze turns grey under cloud and paler in fog.
        Look.FogDensityScale = 1.0f + 3.0f * Falling + 49.0f * Fog;
        const float Overcast = 0.6f * Cover;
        Look.FogColourAmount = FMath::Clamp(Overcast + Fog, 0.0f, 1.0f);
        Look.FogColour = Overcast + Fog > KINDA_SMALL_NUMBER ? FMath::Lerp(OvercastGrey, FogGrey, Fog / (Overcast + Fog)) : OvercastGrey;

        // --- Sky. The engine cloud material's coverage runs from none at -1 to closed near +1; rain clouds are denser
        // and darker, and the storm look comes with thunder.
        Look.CloudCover = Cover;
        Look.CloudCoverage = FMath::Lerp(-0.9f, 0.8f, Cover);
        Look.CloudDensity = 0.008f * (1.0f + 1.5f * FMath::Max(Falling, 0.5f * Fog));
        Look.Storm = FMath::Clamp(Thunder + 0.6f * Falling, 0.0f, 1.0f);

        // --- What falls, and how it leans: the drop's path is its fall speed against the wind's, towards wherever the
        // wind blows to.
        Look.Rain = Falling * (1.0f - SnowShare);
        Look.Snow = Falling * SnowShare;
        Look.LeanDegrees = FMath::Min(FMath::RadiansToDegrees(FMath::Atan2(Wind, FallSpeed(SnowShare))), 65.0f);
        const float From = FMath::IsFinite(Weather.WindDirectionDegrees) ? static_cast<float>(Weather.WindDirectionDegrees) : 0.0f;
        Look.LeanBearingDegrees = FMath::Fmod(FMath::Fmod(From + 180.0f, 360.0f) + 360.0f, 360.0f);

        // --- The ground: the server's own wetness, the one value grip and footsteps follow too (ADR-061).
        Look.Wetness = Unit(Weather.Wetness);
        Look.SnowCover = SnowCoverFor(Weather.SnowDepthM);

        Look.FlashesPerMinute = 3.0f * Thunder;

        // --- Sound. Snow falls silently; sleet patters at about half.
        Look.RainSound = Falling * (1.0f - 0.85f * SnowShare);
        Look.WindSound = FMath::Clamp((Wind - 1.0f) / 12.0f, 0.0f, 1.0f);
        Look.Gust = Gust;
        return Look;
    }

    FLook Blend(const FLook& From, const FLook& To, float Alpha)
    {
        Alpha = FMath::IsFinite(Alpha) ? FMath::Clamp(Alpha, 0.0f, 1.0f) : 1.0f;
        auto Mix = [Alpha](float A, float B) { return FMath::Lerp(A, B, Alpha); };
        FLook Out;
        Out.SunScale = Mix(From.SunScale, To.SunScale);
        Out.SunScaleUnclouded = Mix(From.SunScaleUnclouded, To.SunScaleUnclouded);
        Out.SunSoftness = Mix(From.SunSoftness, To.SunSoftness);
        Out.CloudShade = Mix(From.CloudShade, To.CloudShade);
        Out.SkyScale = Mix(From.SkyScale, To.SkyScale);
        Out.ExposureBias = Mix(From.ExposureBias, To.ExposureBias);
        Out.SaturationScale = Mix(From.SaturationScale, To.SaturationScale);
        Out.ContrastScale = Mix(From.ContrastScale, To.ContrastScale);
        Out.FogDensityScale = Mix(From.FogDensityScale, To.FogDensityScale);
        Out.FogColour = FMath::Lerp(From.FogColour, To.FogColour, Alpha);
        Out.FogColourAmount = Mix(From.FogColourAmount, To.FogColourAmount);
        Out.CloudCover = Mix(From.CloudCover, To.CloudCover);
        Out.CloudCoverage = Mix(From.CloudCoverage, To.CloudCoverage);
        Out.CloudDensity = Mix(From.CloudDensity, To.CloudDensity);
        Out.Storm = Mix(From.Storm, To.Storm);
        Out.Rain = Mix(From.Rain, To.Rain);
        Out.Snow = Mix(From.Snow, To.Snow);
        Out.LeanDegrees = Mix(From.LeanDegrees, To.LeanDegrees);
        // The short way round, so a wind backing through north does not swing the rain the long way.
        const float Turn = FMath::FindDeltaAngleDegrees(From.LeanBearingDegrees, To.LeanBearingDegrees);
        Out.LeanBearingDegrees = FMath::Fmod(From.LeanBearingDegrees + Turn * Alpha + 360.0f, 360.0f);
        Out.Wetness = Mix(From.Wetness, To.Wetness);
        Out.SnowCover = Mix(From.SnowCover, To.SnowCover);
        Out.FlashesPerMinute = Mix(From.FlashesPerMinute, To.FlashesPerMinute);
        Out.RainSound = Mix(From.RainSound, To.RainSound);
        Out.WindSound = Mix(From.WindSound, To.WindSound);
        Out.Gust = Mix(From.Gust, To.Gust);
        return Out;
    }

    float FlashEnvelope(float Age, uint32 Seed)
    {
        if (!FMath::IsFinite(Age) || Age < 0.0f || Age >= FlashSeconds)
        {
            return 0.0f;
        }
        // The return stroke, then restrikes down the same channel some tens of milliseconds apart, each decaying in a
        // few hundredths of a second, over a faint afterglow.
        const float Starts[] = { 0.0f, 0.06f + 0.06f * Hash(Seed, 1), 0.17f + 0.12f * Hash(Seed, 2) };
        const float Peaks[] = { 1.0f, 0.35f + 0.5f * Hash(Seed, 3), Hash(Seed, 4) < 0.5f ? 0.0f : 0.6f };
        float Light = 0.0f;
        for (int32 Index = 0; Index < 3; ++Index)
        {
            if (Age >= Starts[Index])
            {
                Light = FMath::Max(Light, Peaks[Index] * FMath::Exp(-(Age - Starts[Index]) / 0.045f));
            }
        }
        const float Afterglow = 0.15f * (1.0f - Age / FlashSeconds);
        return FMath::Clamp(FMath::Max(Light, Afterglow), 0.0f, 1.0f);
    }

    float ThunderDelay(float DistanceMetres)
    {
        return FMath::IsFinite(DistanceMetres) ? FMath::Max(DistanceMetres, 0.0f) / 343.0f : 0.0f;
    }
}
