#pragma once

#include "CoreMinimal.h"
#include "MarburgumGameplayContract.h"

/**
 * The four attributes as one engine-neutral contract (ADR-085, original src/shared/character_stats): Might, Endurance,
 * Eye and Poise, each a whole score from AttributeMinScore to AttributeMaxScore, AttributeDefaultScore for a new
 * character, with the modifier floor((score - default) / 2). Endurance is the original Wind; `wind` is read once as a
 * legacy alias of `endurance` and never written back, so there is never a fifth score or a second copy of one.
 *
 * Endurance (a lasting attribute) is not Stamina (the breath a run spends and rest brings back) and not Athletics (a
 * skill): nothing here reads or changes the condition. No score changes a blow, a cast, a price, a run or a swim yet.
 */
namespace MarburgumII::CharacterSheet
{
    inline constexpr int32 AttributeCount = UE_ARRAY_COUNT(Gameplay::Attributes);
    static_assert(AttributeCount == 4, "Might, Endurance, Eye and Poise");

    /** Scores in catalogue order (Might, Endurance, Eye, Poise). */
    struct FScores
    {
        int32 Values[AttributeCount] = { Gameplay::AttributeDefaultScore, Gameplay::AttributeDefaultScore,
            Gameplay::AttributeDefaultScore, Gameplay::AttributeDefaultScore };

        int32 operator[](int32 Index) const { return Values[Index]; }
        int32& operator[](int32 Index) { return Values[Index]; }
        bool IsDefault() const { return *this == FScores(); }
        bool operator==(const FScores& Other) const
        {
            for (int32 Index = 0; Index < AttributeCount; ++Index)
            {
                if (Values[Index] != Other.Values[Index])
                {
                    return false;
                }
            }
            return true;
        }
        bool operator!=(const FScores& Other) const { return !(*this == Other); }
    };

    /** Index of an attribute id in catalogue order, or INDEX_NONE (a legacy alias is not an id). */
    MARBURGUMIISHARED_API int32 IndexOf(const FString& Id);

    MARBURGUMIISHARED_API int32 ClampScore(int32 Score);

    /** floor((score - default) / 2) of the score clamped to the range: -5 at 1, 0 at 10 and 11, +5 at 20. */
    MARBURGUMIISHARED_API int32 Modifier(int32 Score);

    /** One stored attribute entry as read: numeric fields, and fields present but not a usable number. */
    struct FRawScores
    {
        TMap<FString, double> Numbers;
        TArray<FString> Invalid;
    };

    /** What normalizing one entry had to do, for the server's migration evidence. */
    struct FNormalizeReport
    {
        /** A legacy alias (`wind`) supplied or shadowed a score. */
        bool bLegacyAlias = false;
        /** Both the id and its legacy alias held different usable values; the id won. */
        bool bConflict = false;
        /** A value was clamped, fractional, unusable or missing, or an unknown key was dropped. */
        bool bRepaired = false;
        TArray<FString> Notes;

        bool Changed() const { return bLegacyAlias || bConflict || bRepaired; }
    };

    /**
     * The four scores an entry stands for. For each attribute: its own id when that holds a usable number, else its
     * legacy alias, else the default. When both hold usable numbers that differ, the canonical id wins and the conflict
     * is reported. Values are truncated to whole numbers and clamped to the range; unknown keys are dropped. Normalizing
     * a normalized entry changes nothing and reports nothing.
     */
    MARBURGUMIISHARED_API FScores Normalize(const FRawScores& Raw, FNormalizeReport& OutReport);
}
