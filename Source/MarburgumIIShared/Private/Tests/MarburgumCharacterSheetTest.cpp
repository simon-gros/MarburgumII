#include "MarburgumCharacterSheet.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumCharacterSheetTest, "Marburgum.Shared.CharacterSheet",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext
        | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

bool FMarburgumCharacterSheetTest::RunTest(const FString& Parameters)
{
    using namespace MarburgumII::CharacterSheet;
    using namespace MarburgumII::Gameplay;

    // ATTR-01: the canonical four, in sheet order, and no Wind among them.
    TestEqual(TEXT("four attributes"), AttributeCount, 4);
    const TCHAR* Canonical[] = { TEXT("might"), TEXT("endurance"), TEXT("eye"), TEXT("poise") };
    for (int32 Index = 0; Index < AttributeCount; ++Index)
    {
        TestEqual(*FString::Printf(TEXT("attribute %d id"), Index), FString(Attributes[Index].Id), FString(Canonical[Index]));
        TestEqual(*FString::Printf(TEXT("index of %s"), Canonical[Index]), IndexOf(Canonical[Index]), Index);
        TestNotEqual(*FString::Printf(TEXT("attribute %d is not Wind"), Index), FString(Attributes[Index].DisplayName), FString(TEXT("Wind")));
    }
    TestEqual(TEXT("wind is not an attribute id"), IndexOf(TEXT("wind")), int32(INDEX_NONE));
    TestEqual(TEXT("Endurance carries the legacy alias"), FString(Attributes[1].LegacyAlias), FString(TEXT("wind")));
    TestEqual(TEXT("Endurance is displayed as Endurance"), FString(Attributes[1].DisplayName), FString(TEXT("Endurance")));

    // The original's range and modifier: every score from below the range to above it.
    TestEqual(TEXT("range"), AttributeMinScore, 1);
    TestEqual(TEXT("range"), AttributeMaxScore, 20);
    TestEqual(TEXT("default"), AttributeDefaultScore, 10);
    const int32 Expected[] = { -5, -5, -4, -4, -3, -3, -2, -2, -1, -1, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 5 };
    for (int32 Score = 0; Score <= 22; ++Score)
    {
        TestEqual(*FString::Printf(TEXT("modifier of %d"), Score), Modifier(Score), Expected[Score]);
    }
    TestEqual(TEXT("far below clamps"), Modifier(-1000), -5);
    TestEqual(TEXT("far above clamps"), Modifier(1000), 5);

    // ATTR-02/03: normalization of new, migrated, mixed and damaged records.
    {
        FNormalizeReport Report;
        FRawScores Raw;
        Raw.Numbers = { { TEXT("might"), 10 }, { TEXT("endurance"), 10 }, { TEXT("eye"), 10 }, { TEXT("poise"), 10 } };
        const FScores Scores = Normalize(Raw, Report);
        TestTrue(TEXT("canonical defaults stay default"), Scores.IsDefault());
        TestFalse(TEXT("a canonical record needs nothing"), Report.Changed());
    }
    {
        FNormalizeReport Report;
        FRawScores Raw;
        Raw.Numbers = { { TEXT("might"), 12 }, { TEXT("wind"), 14 }, { TEXT("eye"), 9 }, { TEXT("poise"), 11 } };
        const FScores Scores = Normalize(Raw, Report);
        TestEqual(TEXT("wind becomes endurance"), Scores[1], 14);
        TestEqual(TEXT("might kept"), Scores[0], 12);
        TestEqual(TEXT("eye kept"), Scores[2], 9);
        TestEqual(TEXT("poise kept"), Scores[3], 11);
        TestTrue(TEXT("legacy alias reported"), Report.bLegacyAlias);
        TestFalse(TEXT("no conflict"), Report.bConflict);
        TestFalse(TEXT("nothing repaired"), Report.bRepaired);

        // Idempotent: writing back the canonical scores and reading them again changes and reports nothing.
        FRawScores Again;
        for (int32 Index = 0; Index < AttributeCount; ++Index)
        {
            Again.Numbers.Add(Attributes[Index].Id, Scores[Index]);
        }
        FNormalizeReport Second;
        TestTrue(TEXT("second migration is a no-op"), Normalize(Again, Second) == Scores);
        TestFalse(TEXT("second migration reports nothing"), Second.Changed());
    }
    {
        FNormalizeReport Report;
        FRawScores Raw;
        Raw.Numbers = { { TEXT("might"), 10 }, { TEXT("endurance"), 15 }, { TEXT("wind"), 7 }, { TEXT("eye"), 10 }, { TEXT("poise"), 10 } };
        const FScores Scores = Normalize(Raw, Report);
        TestEqual(TEXT("canonical endurance wins a conflict"), Scores[1], 15);
        TestTrue(TEXT("conflict audited"), Report.bConflict);
        TestTrue(TEXT("legacy alias reported"), Report.bLegacyAlias);
    }
    {
        FNormalizeReport Report;
        FRawScores Raw;
        Raw.Numbers = { { TEXT("might"), 10 }, { TEXT("endurance"), 13 }, { TEXT("wind"), 13 }, { TEXT("eye"), 10 }, { TEXT("poise"), 10 } };
        Normalize(Raw, Report);
        TestFalse(TEXT("equal values are no conflict"), Report.bConflict);
    }
    {
        FNormalizeReport Report;
        FRawScores Raw;
        Raw.Numbers = { { TEXT("wind"), 6 }, { TEXT("eye"), 10 }, { TEXT("poise"), 10 }, { TEXT("might"), 10 } };
        Raw.Invalid = { TEXT("endurance") };
        const FScores Scores = Normalize(Raw, Report);
        TestEqual(TEXT("unusable endurance falls back to wind"), Scores[1], 6);
        TestTrue(TEXT("and is reported as repaired"), Report.bRepaired);
    }
    {
        FNormalizeReport Report;
        FRawScores Raw;
        Raw.Numbers = { { TEXT("might"), 99 }, { TEXT("endurance"), -4 }, { TEXT("eye"), 12.7 }, { TEXT("strength"), 18 } };
        Raw.Invalid = { TEXT("poise") };
        const FScores Scores = Normalize(Raw, Report);
        TestEqual(TEXT("too high clamps to 20"), Scores[0], 20);
        TestEqual(TEXT("too low clamps to 1"), Scores[1], 1);
        TestEqual(TEXT("fraction truncates"), Scores[2], 12);
        TestEqual(TEXT("unusable falls back to default"), Scores[3], 10);
        TestTrue(TEXT("repairs reported"), Report.bRepaired);
        TestFalse(TEXT("no alias"), Report.bLegacyAlias);
        TestTrue(TEXT("unknown key named"), Report.Notes.ContainsByPredicate([](const FString& Note) { return Note.Contains(TEXT("strength")); }));
    }
    {
        FNormalizeReport Report;
        const FScores Scores = Normalize(FRawScores(), Report);
        TestTrue(TEXT("an empty entry is the default character"), Scores.IsDefault());
        TestTrue(TEXT("and its missing scores are reported"), Report.bRepaired);
    }

    // Equipment slots: the original eight in sheet order, and the authored fits.
    const TCHAR* Slots[] = { TEXT("head"), TEXT("coat"), TEXT("trousers"), TEXT("shoes"), TEXT("hands"), TEXT("right_hand"),
        TEXT("left_hand"), TEXT("bag") };
    TestEqual(TEXT("eight slots"), int32(UE_ARRAY_COUNT(EquipmentSlots)), 8);
    for (int32 Index = 0; Index < 8; ++Index)
    {
        TestEqual(*FString::Printf(TEXT("slot %d"), Index), FString(EquipmentSlots[Index].Id), FString(Slots[Index]));
        TestEqual(*FString::Printf(TEXT("slot %d column"), Index), EquipmentSlots[Index].bRightColumn, Index >= 4);
    }
    TestTrue(TEXT("a rod is held in the right hand"), FitsSlot(TEXT("fishing_rod"), TEXT("right_hand")));
    TestTrue(TEXT("or the left"), FitsSlot(TEXT("fishing_rod"), TEXT("left_hand")));
    TestFalse(TEXT("a rod is not a hat"), FitsSlot(TEXT("fishing_rod"), TEXT("head")));
    TestFalse(TEXT("a fish fits nowhere"), FitsSlot(TEXT("chub"), TEXT("right_hand")));
    TestFalse(TEXT("an unknown slot fits nothing"), FitsSlot(TEXT("camera"), TEXT("pocket")));
    TestTrue(TEXT("something fits a hand"), AnyItemFitsSlot(TEXT("right_hand")));
    TestFalse(TEXT("nothing is worn on the head yet"), AnyItemFitsSlot(TEXT("head")));
    return true;
}

#endif
