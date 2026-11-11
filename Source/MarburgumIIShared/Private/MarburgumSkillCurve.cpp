#include "MarburgumSkillCurve.h"

#include "Algo/BinarySearch.h"

namespace MarburgumII::SkillCurve
{
    namespace
    {
        int64 IntegerSqrt(int64 Value)
        {
            int64 Root = 0;
            int64 Bit = int64(1) << 62;
            while (Bit > Value)
            {
                Bit >>= 2;
            }
            while (Bit != 0)
            {
                if (Value >= Root + Bit)
                {
                    Value -= Root + Bit;
                    Root = (Root >> 1) + Bit;
                }
                else
                {
                    Root >>= 1;
                }
                Bit >>= 2;
            }
            return Root;
        }

        // round(0.06 * n^1.5) = round(sqrt(9 * n^3) / 50), with exact halves rounded to even.
        int64 LevelCost(int64 Level)
        {
            const int64 N = Level + 1;
            const int64 Radicand = 9 * N * N * N;
            const int64 Root = IntegerSqrt(Radicand);
            int64 Rounded;
            if (Root * Root == Radicand && Root % 50 == 25)
            {
                const int64 Floor = Root / 50;
                Rounded = Floor % 2 == 0 ? Floor : Floor + 1;
            }
            else
            {
                // For non-squares the square root is irrational, so it never sits exactly on a half.
                Rounded = (Root + 25) / 50;
            }
            return 20 + Rounded;
        }

        const TArray<int64>& Cumulative()
        {
            static const TArray<int64> Totals = []
            {
                TArray<int64> Out;
                Out.SetNumZeroed(MaxLevel + 1);
                for (int64 Level = 1; Level <= MaxLevel; ++Level)
                {
                    Out[Level] = Out[Level - 1] + LevelCost(Level - 1);
                }
                return Out;
            }();
            return Totals;
        }
    }

    int64 XpToNextLevel(int64 Level)
    {
        if (Level < MinLevel || Level >= MaxLevel)
        {
            return 0;
        }
        return LevelCost(Level);
    }

    int64 TotalXpForLevel(int64 Level)
    {
        return Cumulative()[FMath::Clamp(Level, MinLevel, MaxLevel)];
    }

    int64 LevelForXp(int64 Xp)
    {
        const TArray<int64>& Totals = Cumulative();
        const int64 Clamped = FMath::Max<int64>(Xp, 0);
        // The last level whose threshold is at or below Xp.
        return Algo::UpperBound(Totals, Clamped) - 1;
    }

    int64 XpIntoLevel(int64 Xp)
    {
        const int64 Clamped = FMath::Max<int64>(Xp, 0);
        return Clamped - TotalXpForLevel(LevelForXp(Clamped));
    }
}
