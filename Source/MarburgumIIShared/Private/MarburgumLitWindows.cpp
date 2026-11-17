#include "MarburgumLitWindows.h"

namespace MarburgumII::LitWindows
{
    namespace
    {
        /** The murmur3 finaliser, as the street lamps' photocells use. */
        uint32 Mix(uint32 H)
        {
            H ^= H >> 16;
            H *= 0x85ebca6bu;
            H ^= H >> 13;
            H *= 0xc2b2ae35u;
            H ^= H >> 16;
            return H;
        }

        /** A building's own seed: FNV-1a over its id's characters, fixed across platforms and engine versions. */
        uint32 Seed(const FString& BuildingId)
        {
            uint32 H = 2166136261u;
            for (const TCHAR C : BuildingId)
            {
                H ^= static_cast<uint32>(C);
                H *= 16777619u;
            }
            return Mix(H);
        }

        double Smooth(double X)
        {
            const double T = FMath::Clamp(X, 0.0, 1.0);
            return T * T * (3.0 - 2.0 * T);
        }

        struct FPoint
        {
            double Hour;
            float Share;
        };

        /** Piecewise linear through the points; flat beyond the ends. */
        template <int32 N>
        float Through(const FPoint (&Points)[N], double Hour)
        {
            if (Hour <= Points[0].Hour)
            {
                return Points[0].Share;
            }
            for (int32 I = 1; I < N; ++I)
            {
                if (Hour <= Points[I].Hour)
                {
                    const double T = (Hour - Points[I - 1].Hour) / (Points[I].Hour - Points[I - 1].Hour);
                    return static_cast<float>(FMath::Lerp(static_cast<double>(Points[I - 1].Share), static_cast<double>(Points[I].Share), T));
                }
            }
            return Points[N - 1].Share;
        }

        // The evening: hours from the midnight before the evening began, so 26 is two in the morning after it. Shares of
        // the upper floors' households up with a light on, were it dark: home from work from four, most in by seven,
        // the late news at ten, a few all night.
        const FPoint WorkdayEvening[] = {
            { 12.0, 0.06f }, { 14.0, 0.08f }, { 15.0, 0.12f }, { 16.0, 0.24f }, { 17.0, 0.42f }, { 18.0, 0.58f },
            { 19.0, 0.68f }, { 20.0, 0.72f }, { 21.0, 0.66f }, { 22.0, 0.46f }, { 23.0, 0.24f }, { 24.0, 0.11f },
            { 25.0, 0.06f }, { 26.0, 0.04f }, { 27.0, 0.03f }, { 29.0, 0.02f }, { 36.0, 0.02f },
        };
        // Friday and Saturday evenings go on an hour longer.
        const FPoint WeekendEvening[] = {
            { 12.0, 0.06f }, { 14.0, 0.08f }, { 15.0, 0.12f }, { 16.0, 0.24f }, { 17.0, 0.42f }, { 18.0, 0.58f },
            { 19.0, 0.68f }, { 20.0, 0.72f }, { 21.0, 0.70f }, { 22.0, 0.60f }, { 23.0, 0.42f }, { 24.0, 0.24f },
            { 25.0, 0.13f }, { 26.0, 0.07f }, { 27.0, 0.04f }, { 28.0, 0.03f }, { 30.0, 0.02f }, { 36.0, 0.02f },
        };
        // The early wave: work and school started at six or seven.
        const FPoint WorkdayMorning[] = {
            { 4.5, 0.0f }, { 5.0, 0.08f }, { 5.5, 0.22f }, { 6.0, 0.38f }, { 6.5, 0.42f }, { 7.0, 0.34f },
            { 7.5, 0.20f }, { 8.0, 0.10f }, { 9.0, 0.05f }, { 10.0, 0.0f },
        };
        const FPoint SaturdayMorning[] = {
            { 5.5, 0.0f }, { 6.0, 0.06f }, { 7.0, 0.16f }, { 8.0, 0.24f }, { 9.0, 0.20f }, { 10.0, 0.10f }, { 11.0, 0.0f },
        };
        const FPoint SundayMorning[] = {
            { 6.5, 0.0f }, { 7.0, 0.06f }, { 8.0, 0.16f }, { 9.0, 0.20f }, { 10.0, 0.12f }, { 11.0, 0.0f },
        };

        /** A shop's night light: a display, a till left on. */
        constexpr float ShopsShutShare = 0.06f;
        constexpr float ShopsOpenShare = 0.85f;
    }

    int32 PanesPerFlat(const FString& BuildingId)
    {
        return Mix(Seed(BuildingId) ^ 0x9e3779b9u) % 3u == 0u ? 3 : 2;
    }

    FTag TagOf(const FString& BuildingId, int32 Facade, int32 Storey, int32 Bay)
    {
        const int32 Flat = FMath::Max(Bay, 0) / PanesPerFlat(BuildingId);
        uint32 H = Seed(BuildingId);
        H = Mix(H ^ (static_cast<uint32>(Facade) * 0x27d4eb2du));
        H = Mix(H ^ (static_cast<uint32>(Storey) * 0x165667b1u));
        H = Mix(H ^ (static_cast<uint32>(Flat) * 0xd3a2646cu));
        FTag Tag;
        Tag.Evening = Rank(H);
        Tag.Morning = Rank(H >> 8);
        Tag.Kind = Rank(H >> 16);
        Tag.bGround = Storey == 0;
        return Tag;
    }

    ERoom RoomOf(const FTag& Tag)
    {
        if (Tag.bGround)
        {
            return ERoom::Shop;
        }
        return Tag.Kind < TungstenShare ? ERoom::Tungsten : Tag.Kind < TungstenShare + FluorescentShare ? ERoom::Fluorescent : ERoom::Television;
    }

    float CurtainOf(const FTag& Tag)
    {
        const float X = Tag.Evening * 7.0f + Tag.Morning * 3.0f;
        return 0.55f + 0.45f * (X - FMath::FloorToFloat(X));
    }

    bool ShopsOpen(int32 Weekday, double Hour)
    {
        if (Weekday < 0 || Weekday > 5)
        {
            return false;
        }
        return Hour >= 7.0 && Hour < (Weekday == 5 ? 13.0 : 19.0);
    }

    float EveningShareAt(const Clock::FCivil& Local)
    {
        // Before noon the evening is yesterday's, still going on.
        double Hour = Local.HourOfDay();
        int32 Weekday = Local.Weekday;
        if (Hour < 12.0)
        {
            Hour += 24.0;
            Weekday = (Weekday + 6) % 7;
        }
        const bool bWeekend = Weekday == 4 || Weekday == 5;
        return bWeekend ? Through(WeekendEvening, Hour) : Through(WorkdayEvening, Hour);
    }

    float MorningShareAt(const Clock::FCivil& Local)
    {
        const double Hour = Local.HourOfDay();
        switch (Local.Weekday)
        {
        case 5:
            return Through(SaturdayMorning, Hour);
        case 6:
            return Through(SundayMorning, Hour);
        default:
            return Through(WorkdayMorning, Hour);
        }
    }

    float DuskOf(const StreetLights::FSky& Sky)
    {
        if (!FMath::IsFinite(Sky.SunElevationDeg))
        {
            return 0.0f;
        }
        const double Threshold = StreetLights::SwitchOnDeg(Sky);
        const double Start = Threshold + DuskStartAboveDeg;
        const double Full = Threshold - DuskFullBelowDeg;
        return static_cast<float>(Smooth((Start - Sky.SunElevationDeg) / (Start - Full)));
    }

    FState StateAt(const Clock::FCivil& Local, const StreetLights::FSky& Sky)
    {
        FState State;
        State.Evening = EveningShareAt(Local);
        State.Morning = MorningShareAt(Local);
        State.Ground = ShopsOpen(Local.Weekday, Local.HourOfDay()) ? ShopsOpenShare : ShopsShutShare;
        State.Dusk = DuskOf(Sky);
        return State;
    }

    bool IsLit(const FTag& Tag, const FState& State)
    {
        if (State.Dusk <= 0.0f)
        {
            return false;
        }
        if (Tag.bGround)
        {
            return Tag.Evening < State.Ground;
        }
        return Tag.Evening < State.Evening || Tag.Morning < State.Morning;
    }

    FLinearColor GlowOf(const FTag& Tag)
    {
        float Kelvin = TungstenKelvin;
        float Strength = CurtainOf(Tag);
        switch (RoomOf(Tag))
        {
        case ERoom::Fluorescent:
            Kelvin = FluorescentKelvin;
            break;
        case ERoom::Television:
            Kelvin = TelevisionKelvin;
            Strength *= TelevisionGlow;
            break;
        case ERoom::Shop:
            Kelvin = ShopKelvin;
            Strength = ShopGlow;
            break;
        default:
            break;
        }
        const FLinearColor Colour = StreetLights::KelvinColour(Kelvin);
        return FLinearColor(Colour.R * Strength * GlowOnScreen, Colour.G * Strength * GlowOnScreen, Colour.B * Strength * GlowOnScreen, 1.0f);
    }
}
