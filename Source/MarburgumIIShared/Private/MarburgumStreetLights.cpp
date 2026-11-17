#include "MarburgumStreetLights.h"

#include "Dom/JsonObject.h"
#include "MarburgumWorldClock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace MarburgumII::StreetLights
{
    namespace
    {
        /** The sun's rate is read over this much clock time. */
        constexpr double RateSeconds = 60.0;
        /** A threshold crossed further back than this reads as long ago (a lamp lit all night is warm). */
        constexpr double LongAgoSeconds = 86400.0;
        /** However dark the weather, a lamp does not switch on with the sun higher than this. */
        constexpr double LatestSwitchOnDeg = 6.0;

        double Smooth(double X)
        {
            const double T = FMath::Clamp(X, 0.0, 1.0);
            return T * T * (3.0 - 2.0 * T);
        }

        /** A fixed hash of the lamp's index: the murmur3 finaliser. */
        uint32 Mix(uint32 H)
        {
            H ^= H >> 16;
            H *= 0x85ebca6bu;
            H ^= H >> 13;
            H *= 0xc2b2ae35u;
            H ^= H >> 16;
            return H;
        }
    }

    bool Parse(const FString& Json, TArray<FLamp>& OutLamps, FString& OutError)
    {
        OutLamps.Reset();
        TSharedPtr<FJsonObject> Root;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
        {
            OutError = TEXT("not JSON");
            return false;
        }
        FString Format;
        if (!Root->TryGetStringField(TEXT("format"), Format) || Format != TEXT("marburgumii.street_lights"))
        {
            OutError = TEXT("not a street lights file (format marburgumii.street_lights)");
            return false;
        }
        int32 Version = 0;
        if (!Root->TryGetNumberField(TEXT("version"), Version) || Version != 1)
        {
            OutError = FString::Printf(TEXT("street lights version %d, this build reads 1"), Version);
            return false;
        }
        const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
        if (!Root->TryGetArrayField(TEXT("lamps"), Rows) || Rows == nullptr)
        {
            OutError = TEXT("no lamps");
            return false;
        }
        OutLamps.Reserve(Rows->Num());
        for (int32 Index = 0; Index < Rows->Num(); ++Index)
        {
            const TArray<TSharedPtr<FJsonValue>>* Row = nullptr;
            if (!(*Rows)[Index].IsValid() || !(*Rows)[Index]->TryGetArray(Row) || Row == nullptr || Row->Num() != 6)
            {
                OutError = FString::Printf(TEXT("lamp %d is not [east, south, top, kind, yaw, source]"), Index);
                return false;
            }
            double Values[6];
            for (int32 Field = 0; Field < 6; ++Field)
            {
                if ((*Row)[Field]->Type != EJson::Number || !FMath::IsFinite((*Row)[Field]->AsNumber()))
                {
                    OutError = FString::Printf(TEXT("lamp %d field %d is not a number"), Index, Field);
                    return false;
                }
                Values[Field] = (*Row)[Field]->AsNumber();
            }
            const int32 Kind = static_cast<int32>(Values[3]);
            const int32 Source = static_cast<int32>(Values[5]);
            if (Kind < 0 || Kind >= static_cast<int32>(EKind::Count) || Source < 0 || Source >= static_cast<int32>(ESource::Count))
            {
                OutError = FString::Printf(TEXT("lamp %d has kind %d, source %d"), Index, Kind, Source);
                return false;
            }
            FLamp& Lamp = OutLamps.AddDefaulted_GetRef();
            Lamp.Location = FromPlan(Values[0], Values[1], Values[2]);
            Lamp.Kind = static_cast<EKind>(Kind);
            Lamp.Yaw = YawFromPlan(Values[4]);
            Lamp.Source = static_cast<ESource>(Source);
        }
        return true;
    }

    FVector LightPoint(const FLamp& Lamp)
    {
        if (Lamp.Kind == EKind::Mast)
        {
            return Lamp.Location + FRotator(0.0f, Lamp.Yaw, 0.0f).RotateVector(FVector(MastReachCm, 0.0, MastLightHeightCm));
        }
        return Lamp.Location + FVector(0.0, 0.0, LanternLightHeightCm);
    }

    FLinearColor KelvinColour(float Kelvin)
    {
        // The engine's own blackbody, normalised to its brightest channel so the output alone sets the brightness.
        FLinearColor Colour = FLinearColor::MakeFromColorTemperature(Kelvin);
        const float Peak = FMath::Max3(Colour.R, Colour.G, Colour.B);
        return Peak > 0.0f ? FLinearColor(Colour.R / Peak, Colour.G / Peak, Colour.B / Peak, 1.0f) : FLinearColor::White;
    }

    float LuxAt(EKind Kind, float DistanceCm, float Output)
    {
        const bool bMast = Kind == EKind::Mast;
        const float Reach = bMast ? MastReachLightCm : LanternReachLightCm;
        const float Ratio = FMath::Clamp(DistanceCm / Reach, 0.0f, 1.0f);
        return (bMast ? MastPeakLux : LanternPeakLux) * FMath::Max(Output, 0.0f) * FMath::Pow(1.0f - Ratio * Ratio, FalloffExponent);
    }

    FSky SkyAt(const FDateTime& Utc, double CloudCover, double Fog, double Thunder)
    {
        FSky Sky;
        const Clock::FSkyPosition Now = Clock::SunAt(Utc);
        const Clock::FSkyPosition Before = Clock::SunAt(Utc - FTimespan::FromSeconds(RateSeconds));
        Sky.SunElevationDeg = Now.ElevationDeg;
        Sky.SunRateDegPerSecond = (Now.ElevationDeg - Before.ElevationDeg) / RateSeconds;
        Sky.CloudCover = CloudCover;
        Sky.Fog = Fog;
        Sky.Thunder = Thunder;
        return Sky;
    }

    double SwitchOnDeg(const FSky& Sky)
    {
        // Broken cloud hardly darkens the street; a closed sky does, and fog and a storm under it more.
        const double Cover = FMath::Clamp(Sky.CloudCover, 0.0, 1.0);
        const double Earlier = OvercastEarlierDeg * Cover * Cover + FogEarlierDeg * FMath::Clamp(Sky.Fog, 0.0, 1.0)
            + StormEarlierDeg * FMath::Clamp(Sky.Thunder, 0.0, 1.0);
        return FMath::Min(ClearSwitchOnDeg + Earlier, LatestSwitchOnDeg);
    }

    double PhotocellOffsetDeg(int32 LampIndex)
    {
        const double Unit = static_cast<double>(Mix(static_cast<uint32>(LampIndex) * 2654435761u + 0x5eed) % 10001u) / 10000.0;
        return (Unit * 2.0 - 1.0) * PhotocellSpreadDeg;
    }

    FOutput OutputOf(const FSky& Sky, int32 LampIndex)
    {
        FOutput Out;
        if (!FMath::IsFinite(Sky.SunElevationDeg))
        {
            return Out;
        }
        const double Threshold = SwitchOnDeg(Sky) + PhotocellOffsetDeg(LampIndex);
        const bool bRising = Sky.SunRateDegPerSecond > 0.0;
        if (Sky.SunElevationDeg >= Threshold + (bRising ? MorningLaterDeg : 0.0))
        {
            return Out;
        }
        // On since the sun crossed its threshold this evening; in the morning, since last night.
        Out.OnSeconds = !bRising && Sky.SunRateDegPerSecond < -1e-9
            ? FMath::Min((Threshold - Sky.SunElevationDeg) / -Sky.SunRateDegPerSecond, LongAgoSeconds)
            : LongAgoSeconds;
        const double Warm = Smooth(Out.OnSeconds / WarmUpSeconds);
        Out.Output = static_cast<float>(FMath::Lerp(static_cast<double>(StrikeOutput), 1.0, Warm));
        Out.Kelvin = static_cast<float>(FMath::Lerp(static_cast<double>(StrikeKelvin), static_cast<double>(WarmKelvin), FMath::Sqrt(Warm)));
        return Out;
    }
}
