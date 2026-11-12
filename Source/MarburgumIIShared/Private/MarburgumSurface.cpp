#include "MarburgumSurface.h"

namespace MarburgumII::Surface
{
    namespace
    {
        constexpr int32 HeaderBytes = 16;
        constexpr int32 KindCount = 5;

        uint32 ReadU32(TConstArrayView<uint8> Bytes, int32 At)
        {
            return uint32(Bytes[At]) | uint32(Bytes[At + 1]) << 8 | uint32(Bytes[At + 2]) << 16 | uint32(Bytes[At + 3]) << 24;
        }
    }

    bool FLandscapeMap::Parse(TConstArrayView<uint8> Bytes)
    {
        Cells = 0;
        HalfExtentM = 0.0;
        Kinds.Reset();
        if (Bytes.Num() < HeaderBytes || FMemory::Memcmp(Bytes.GetData(), "MBSK", 4) != 0 || ReadU32(Bytes, 4) != 1)
        {
            return false;
        }
        const uint32 Count = ReadU32(Bytes, 8);
        const uint32 HalfBits = ReadU32(Bytes, 12);
        float Half = 0.f;
        FMemory::Memcpy(&Half, &HalfBits, sizeof(Half));
        if (Count == 0 || Count > 16384 || !FMath::IsFinite(Half) || Half <= 0.f
            || int64(Bytes.Num()) != HeaderBytes + int64(Count) * int64(Count))
        {
            return false;
        }
        for (int32 Index = HeaderBytes; Index < Bytes.Num(); ++Index)
        {
            if (Bytes[Index] >= KindCount)
            {
                return false;
            }
        }
        Kinds = TArray<uint8>(Bytes.GetData() + HeaderBytes, Bytes.Num() - HeaderBytes);
        Cells = static_cast<int32>(Count);
        HalfExtentM = Half;
        return true;
    }

    FString FLandscapeMap::SurfaceAt(double UnrealX, double UnrealY) const
    {
        if (!IsLoaded())
        {
            return Unknown;
        }
        const double East = UnrealY / 100.0;
        const double South = -UnrealX / 100.0;
        const double Cell = 2.0 * HalfExtentM / Cells;
        const double Row = FMath::FloorToDouble((South + HalfExtentM) / Cell);
        const double Column = FMath::FloorToDouble((East + HalfExtentM) / Cell);
        if (Row < 0.0 || Column < 0.0 || Row >= Cells || Column >= Cells)
        {
            return Unknown;
        }
        return LandscapeKindSurface(KindAt(static_cast<int32>(Row), static_cast<int32>(Column)));
    }

    const TCHAR* LandscapeKindSurface(uint8 Kind)
    {
        switch (Kind)
        {
        case 0: return TEXT("concrete");
        case 1: return TEXT("grass");
        case 2: return TEXT("soil");
        case 3:
        case 4: return TEXT("gravel");
        default: return Unknown;
        }
    }

    double WetStepShare(double Wetness)
    {
        return FMath::SmoothStep(0.15, 0.6, FMath::IsFinite(Wetness) ? Wetness : 0.0);
    }

    FName FootstepSetFor(const FString& Surface, double Wetness, double SnowDepthM, double Roll)
    {
        if (FMath::IsFinite(SnowDepthM) && SnowDepthM >= SnowCrunchDepthM)
        {
            return FootstepCrunch;
        }
        if (Surface == TEXT("gravel") || Surface == TEXT("rail_ballast"))
        {
            return FootstepCrunch;
        }
        if (Surface == TEXT("grass") || Surface == TEXT("meadow") || Surface == TEXT("forest_floor") || Surface == TEXT("soil"))
        {
            return FootstepSoft;
        }
        return Roll < WetStepShare(Wetness) ? FootstepHardWet : FootstepHard;
    }

    float StepVolume(double SpeedCmPerSecond)
    {
        // The original's gaits: walk 0.6, run 0.85, sprint 1.0, over Marburgum II's 2.2 m/s walk and 4.4 m/s run.
        return static_cast<float>(FMath::GetMappedRangeValueClamped(FVector2D(220.0, 440.0), FVector2D(0.6, 0.85), SpeedCmPerSecond)
            + (SpeedCmPerSecond > 470.0 ? 0.15 : 0.0));
    }

    bool FFootfallDetector::Update(double HeightCm, double DeltaSeconds)
    {
        if (!FMath::IsFinite(HeightCm))
        {
            return false;
        }
        if (!bHasLow)
        {
            Low = HeightCm;
            bHasLow = true;
            return false;
        }
        Low = FMath::Min(Low + LowRiseCmPerSecond * FMath::Max(DeltaSeconds, 0.0), HeightCm);
        if (!bLifted)
        {
            bLifted = HeightCm > Low + LiftCm;
            return false;
        }
        if (HeightCm < Low + ContactCm)
        {
            bLifted = false;
            return true;
        }
        return false;
    }
}
