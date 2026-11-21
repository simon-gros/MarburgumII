#include "MarburgumCharacterSheet.h"

namespace MarburgumII::CharacterSheet
{
    using namespace MarburgumII::Gameplay;

    int32 IndexOf(const FString& Id)
    {
        for (int32 Index = 0; Index < AttributeCount; ++Index)
        {
            if (Id == Attributes[Index].Id)
            {
                return Index;
            }
        }
        return INDEX_NONE;
    }

    int32 ClampScore(int32 Score)
    {
        return FMath::Clamp(Score, AttributeMinScore, AttributeMaxScore);
    }

    int32 Modifier(int32 Score)
    {
        // Integer floor division, as the original: (delta - 1) / 2 rounds a negative odd delta down, not towards zero.
        const int32 Delta = ClampScore(Score) - AttributeDefaultScore;
        return Delta >= 0 ? Delta / 2 : (Delta - 1) / 2;
    }

    namespace
    {
        bool Usable(double Value)
        {
            // Anything a 32-bit score could hold once clamped; NaN and infinities are not numbers to trust.
            return FMath::IsFinite(Value) && Value > -1.0e9 && Value < 1.0e9;
        }
    }

    FScores Normalize(const FRawScores& Raw, FNormalizeReport& OutReport)
    {
        OutReport = FNormalizeReport();
        FScores Scores;
        for (int32 Index = 0; Index < AttributeCount; ++Index)
        {
            const FAttributeDefinition& Attribute = Attributes[Index];
            const double* Own = Raw.Numbers.Find(Attribute.Id);
            const double* Alias = *Attribute.LegacyAlias ? Raw.Numbers.Find(Attribute.LegacyAlias) : nullptr;
            const bool bOwn = Own && Usable(*Own);
            const bool bAlias = Alias && Usable(*Alias);
            if (*Attribute.LegacyAlias && (Alias || Raw.Invalid.Contains(Attribute.LegacyAlias)))
            {
                OutReport.bLegacyAlias = true;
            }
            double Chosen = AttributeDefaultScore;
            if (bOwn)
            {
                Chosen = *Own;
                if (bAlias && static_cast<int32>(*Alias) != static_cast<int32>(*Own))
                {
                    OutReport.bConflict = true;
                    OutReport.Notes.Add(FString::Printf(TEXT("%s=%d kept over %s=%d"), Attribute.Id, static_cast<int32>(*Own),
                        Attribute.LegacyAlias, static_cast<int32>(*Alias)));
                }
            }
            else if (bAlias)
            {
                Chosen = *Alias;
                OutReport.Notes.Add(FString::Printf(TEXT("%s from %s"), Attribute.Id, Attribute.LegacyAlias));
                if (Own || Raw.Invalid.Contains(Attribute.Id))
                {
                    OutReport.bRepaired = true;
                    OutReport.Notes.Add(FString::Printf(TEXT("%s unusable"), Attribute.Id));
                }
            }
            else
            {
                OutReport.bRepaired = true;
                OutReport.Notes.Add(FString::Printf(TEXT("%s %s, default"), Attribute.Id,
                    Own || Raw.Invalid.Contains(Attribute.Id) ? TEXT("unusable") : TEXT("missing")));
            }
            const int32 Whole = static_cast<int32>(Chosen);
            const int32 Clamped = ClampScore(Whole);
            if (Whole != Chosen || Clamped != Whole)
            {
                OutReport.bRepaired = true;
                OutReport.Notes.Add(FString::Printf(TEXT("%s %g -> %d"), Attribute.Id, Chosen, Clamped));
            }
            Scores[Index] = Clamped;
        }
        auto Known = [](const FString& Key)
        {
            for (const FAttributeDefinition& Attribute : Attributes)
            {
                if (Key == Attribute.Id || (*Attribute.LegacyAlias && Key == Attribute.LegacyAlias))
                {
                    return true;
                }
            }
            return false;
        };
        TArray<FString> Keys;
        Raw.Numbers.GetKeys(Keys);
        Keys.Append(Raw.Invalid);
        Keys.Sort();
        for (const FString& Key : Keys)
        {
            if (!Known(Key))
            {
                OutReport.bRepaired = true;
                OutReport.Notes.Add(FString::Printf(TEXT("unknown %s dropped"), *Key));
            }
        }
        return Scores;
    }
}
