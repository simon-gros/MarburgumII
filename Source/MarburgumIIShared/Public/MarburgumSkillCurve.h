#pragma once

#include "CoreMinimal.h"

/**
 * The original Marburgum's skill curve (src/shared/skill_curve.cpp, ADR-047): levels 0 to 1990, and the step
 * from level n to n + 1 costs 20 + round(0.06 * (n + 1)^1.5) XP, exact halves rounded to even. Integer-only,
 * so the server and every client agree to the point. The server owns XP; clients derive level and progress.
 */
namespace MarburgumII::SkillCurve
{
    inline constexpr int64 MinLevel = 0;
    inline constexpr int64 MaxLevel = 1990;

    /** XP from Level to Level + 1; 0 at and beyond the top level. */
    MARBURGUMIISHARED_API int64 XpToNextLevel(int64 Level);

    /** Total XP at which Level is reached (clamped to the curve). */
    MARBURGUMIISHARED_API int64 TotalXpForLevel(int64 Level);

    /** The level a total of Xp has reached; negative XP is level 0. */
    MARBURGUMIISHARED_API int64 LevelForXp(int64 Xp);

    /** XP earned past the start of the current level. */
    MARBURGUMIISHARED_API int64 XpIntoLevel(int64 Xp);
}
