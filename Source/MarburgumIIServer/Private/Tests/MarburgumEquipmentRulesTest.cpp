#include "MarburgumEquipmentRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumEquipmentRulesTest,
    "Marburgum.Server.EquipmentRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    using MarburgumII::Equipment::DecideEquip;

    const TCHAR* Simon = TEXT("simon");
    const TCHAR* Rod = TEXT("fishing_rod");
    const TCHAR* Camera = TEXT("camera");

    FMarburgumWorldRecord Holding(std::initializer_list<TPair<const TCHAR*, int32>> Items)
    {
        FMarburgumWorldRecord Record;
        Record.Revision = 5;
        for (const TPair<const TCHAR*, int32>& Item : Items)
        {
            Record.Inventories.FindOrAdd(Simon).Add(Item.Key, Item.Value);
        }
        return Record;
    }

    /** Apply one request to Record in place; the decision is returned for its fields. */
    FMarburgumEquipDecision Equip(FMarburgumWorldRecord& Record, const FString& Slot, const FString& Item)
    {
        FMarburgumWorldRecord Next;
        const FMarburgumEquipDecision Decision = DecideEquip(Record, Simon, Slot, Item, Next);
        if (Decision.bApplied)
        {
            Record = Next;
        }
        return Decision;
    }

    FString Name(EMarburgumInteractionReason Reason)
    {
        return MarburgumII::Gameplay::ToString(Reason);
    }

    FString Worn(const FMarburgumWorldRecord& Record, const FString& Slot)
    {
        return Record.Equipment.FindRef(Simon).FindRef(Slot);
    }
}

bool FMarburgumEquipmentRulesTest::RunTest(const FString& Parameters)
{
    // CHAR: equip -> slot filled, custody untouched, revision advanced.
    {
        FMarburgumWorldRecord Record = Holding({ { Rod, 1 } });
        const FMarburgumEquipDecision D = Equip(Record, TEXT("right_hand"), Rod);
        TestTrue(TEXT("rod into the right hand"), D.bApplied && D.Effect == EMarburgumInteractionEffect::ItemEquipped);
        TestEqual(TEXT("slot holds the rod"), Worn(Record, TEXT("right_hand")), FString(Rod));
        TestEqual(TEXT("custody unchanged"), Record.Inventories.FindRef(Simon).FindRef(Rod), 1);
        TestEqual(TEXT("revision advanced once"), Record.Revision, int64(6));

        // One rod cannot be in two hands: no duplication through slots.
        const FMarburgumEquipDecision Twice = Equip(Record, TEXT("left_hand"), Rod);
        TestTrue(TEXT("second hand refused"), !Twice.bApplied && Twice.Reason == EMarburgumInteractionReason::InsufficientItems);
        TestTrue(TEXT("left hand still empty"), Worn(Record, TEXT("left_hand")).IsEmpty());

        // The same item in the same slot again changes nothing.
        const FMarburgumEquipDecision Again = Equip(Record, TEXT("right_hand"), Rod);
        TestTrue(TEXT("already there"), !Again.bApplied && Again.Reason == EMarburgumInteractionReason::NothingToDo);
        TestEqual(TEXT("no revision for a refusal"), Record.Revision, int64(6));

        // Unequip empties the slot and keeps the rod.
        const FMarburgumEquipDecision Off = Equip(Record, TEXT("right_hand"), FString());
        TestTrue(TEXT("rod out of the hand"), Off.bApplied && Off.Effect == EMarburgumInteractionEffect::ItemUnequipped && Off.Item == Rod);
        TestFalse(TEXT("no empty slot map left behind"), Record.Equipment.Contains(Simon));
        TestEqual(TEXT("rod still carried"), Record.Inventories.FindRef(Simon).FindRef(Rod), 1);
        const FMarburgumEquipDecision OffAgain = Equip(Record, TEXT("right_hand"), FString());
        TestTrue(TEXT("empty slot cannot be emptied"), !OffAgain.bApplied && OffAgain.Reason == EMarburgumInteractionReason::NothingToDo);
    }
    // Two copies fill two hands; a swap keeps the replaced item in the bag.
    {
        FMarburgumWorldRecord Record = Holding({ { Rod, 2 }, { Camera, 1 } });
        TestTrue(TEXT("first rod"), Equip(Record, TEXT("right_hand"), Rod).bApplied);
        TestTrue(TEXT("second rod"), Equip(Record, TEXT("left_hand"), Rod).bApplied);
        const FMarburgumEquipDecision Swap = Equip(Record, TEXT("left_hand"), Camera);
        TestTrue(TEXT("camera replaces a rod"), Swap.bApplied && Swap.Replaced == Rod);
        TestEqual(TEXT("left hand holds the camera"), Worn(Record, TEXT("left_hand")), FString(Camera));
        TestEqual(TEXT("both rods still carried"), Record.Inventories.FindRef(Simon).FindRef(Rod), 2);
        TestEqual(TEXT("one rod free again"), MarburgumII::Equipment::FreeCopies(Record, Simon, Rod), 1);
    }
    // Ids arrive as FNames spelt the way the engine first met them ("Camera", "Head"): stored and reported in catalogue case.
    {
        FMarburgumWorldRecord Record = Holding({ { Camera, 1 } });
        const FMarburgumEquipDecision D = Equip(Record, TEXT("Left_Hand"), TEXT("Camera"));
        TestTrue(TEXT("a capitalised camera is still a camera"), D.bApplied);
        TestTrue(TEXT("decision in catalogue case"), D.Slot.Equals(TEXT("left_hand"), ESearchCase::CaseSensitive)
            && D.Item.Equals(Camera, ESearchCase::CaseSensitive));
        for (const TPair<FString, FString>& Entry : Record.Equipment.FindRef(Simon))
        {
            TestTrue(TEXT("stored in catalogue case"), Entry.Key.Equals(TEXT("left_hand"), ESearchCase::CaseSensitive)
                && Entry.Value.Equals(Camera, ESearchCase::CaseSensitive));
        }
        const FMarburgumEquipDecision Misfit = Equip(Record, TEXT("Head"), TEXT("Camera"));
        TestTrue(TEXT("a refusal reports catalogue case too"), !Misfit.bApplied
            && Misfit.Slot.Equals(TEXT("head"), ESearchCase::CaseSensitive) && Misfit.Item.Equals(Camera, ESearchCase::CaseSensitive));
        TestTrue(TEXT("an unknown slot still reports the item in catalogue case"),
            Equip(Record, TEXT("pocket"), TEXT("Camera")).Item.Equals(Camera, ESearchCase::CaseSensitive));
        TestTrue(TEXT("the same camera again, spelt otherwise, is nothing to do"),
            Equip(Record, TEXT("LEFT_HAND"), TEXT("camera")).Reason == EMarburgumInteractionReason::NothingToDo);
    }
    // Refusals: unknown slot, misfit item, unknown item, nothing held, another character's things.
    {
        FMarburgumWorldRecord Record = Holding({ { Rod, 1 }, { TEXT("chub"), 3 } });
        TestEqual(TEXT("unknown slot"), Name(Equip(Record, TEXT("pocket"), Rod).Reason), Name(EMarburgumInteractionReason::UnknownSlot));
        TestEqual(TEXT("unknown slot to empty"), Name(Equip(Record, TEXT("pocket"), FString()).Reason), Name(EMarburgumInteractionReason::UnknownSlot));
        TestEqual(TEXT("a rod is not a hat"), Name(Equip(Record, TEXT("head"), Rod).Reason), Name(EMarburgumInteractionReason::NotEquippable));
        TestEqual(TEXT("a fish is not held"), Name(Equip(Record, TEXT("right_hand"), TEXT("chub")).Reason), Name(EMarburgumInteractionReason::NotEquippable));
        TestEqual(TEXT("unknown item"), Name(Equip(Record, TEXT("right_hand"), TEXT("unknown_item")).Reason), Name(EMarburgumInteractionReason::NotEquippable));
        TestEqual(TEXT("a camera you do not hold"), Name(Equip(Record, TEXT("right_hand"), Camera).Reason), Name(EMarburgumInteractionReason::InsufficientItems));
        FMarburgumWorldRecord Next;
        TestEqual(TEXT("a character with nothing"), Name(DecideEquip(Record, TEXT("stranger"), TEXT("right_hand"), Rod, Next).Reason), Name(EMarburgumInteractionReason::InsufficientItems));
        TestFalse(TEXT("refusals changed nothing"), Record.Equipment.Contains(Simon));
        TestEqual(TEXT("refusals kept the revision"), Record.Revision, int64(5));
    }
    // Reconcile: a slot naming a copy that left custody (sold, handed over) is emptied, the rest kept.
    {
        FMarburgumWorldRecord Record = Holding({ { Rod, 2 }, { Camera, 1 } });
        Equip(Record, TEXT("right_hand"), Rod);
        Equip(Record, TEXT("left_hand"), Rod);
        Record.Inventories.FindOrAdd(Simon).Add(Rod, 1);
        const TArray<FString> Emptied = MarburgumII::Equipment::Reconcile(Record, Simon);
        TestTrue(TEXT("the off hand gives way first"), Emptied.Num() == 1 && Emptied[0] == TEXT("left_hand"));
        TestEqual(TEXT("the right hand keeps its rod"), Worn(Record, TEXT("right_hand")), FString(Rod));
        Record.Inventories.FindOrAdd(Simon).Remove(Rod);
        TestEqual(TEXT("no rod at all empties the right hand"), MarburgumII::Equipment::Reconcile(Record, Simon).Num(), 1);
        TestFalse(TEXT("and leaves no slot map"), Record.Equipment.Contains(Simon));
        TestTrue(TEXT("a consistent record needs nothing"), MarburgumII::Equipment::Reconcile(Record, Simon).IsEmpty());
    }
    return true;
}

#endif
