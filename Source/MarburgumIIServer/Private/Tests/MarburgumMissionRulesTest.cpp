#include "MarburgumFishingRules.h"
#include "MarburgumMissionRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumMissionRulesTest,
    "Marburgum.Server.MissionRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumMissionChoiceTest,
    "Marburgum.Server.MissionChoices",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumFishingRulesTest,
    "Marburgum.Server.FishingRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    using MarburgumII::Missions::DecideChoice;
    using MarburgumII::Missions::DecideInteraction;

    /** Apply one decision to Record in place, failing the test when it is refused. */
    bool Step(FAutomationTestBase& Test, FMarburgumWorldRecord& Record, const FString& Character, const FString& Target,
        EMarburgumInteractionEffect Expected, const TCHAR* What)
    {
        FMarburgumWorldRecord Next;
        const FMarburgumMissionDecision Decision = DecideInteraction(Record, Character, Target, Next);
        Test.TestTrue(FString::Printf(TEXT("%s applied"), What), Decision.bApplied);
        Test.TestTrue(FString::Printf(TEXT("%s effect"), What), Decision.Effect == Expected);
        if (Decision.bApplied)
        {
            Record = Next;
        }
        return Decision.bApplied;
    }
}

bool FMarburgumMissionRulesTest::RunTest(const FString& Parameters)
{
    const FString Angler(TEXT("lent_angler"));
    const FString Mission(TEXT("lent_first_catch"));
    const FString Simon(TEXT("simon"));

    // Talking to the angler for the first time starts the mission and grants its start item.
    FMarburgumWorldRecord Empty;
    Empty.Revision = 7;
    FMarburgumWorldRecord Started;
    FMarburgumMissionDecision Decision = DecideInteraction(Empty, Simon, Angler, Started);
    TestTrue(TEXT("offer accepted"), Decision.bApplied);
    TestTrue(TEXT("effect"), Decision.Effect == EMarburgumInteractionEffect::MissionStarted);
    TestEqual(TEXT("revision advances once"), Started.Revision, int64(8));
    TestEqual(TEXT("first stage"), Started.Missions.FindRef(Simon).FindRef(Mission).Stage, FString(TEXT("catch_first_fish")));
    TestEqual(TEXT("started revision"), Started.Missions.FindRef(Simon).FindRef(Mission).StartedRevision, int64(8));
    TestEqual(TEXT("start item granted"), Started.Inventories.FindRef(Simon).FindRef(TEXT("fishing_rod")), 1);
    TestEqual(TEXT("decision names the item"), Decision.Item, FString(TEXT("fishing_rod")));
    TestTrue(TEXT("input record untouched"), Empty.Missions.IsEmpty() && Empty.Inventories.IsEmpty());

    // Talking again without a fish: refused, nothing built.
    FMarburgumWorldRecord Unused;
    Unused.Revision = -1;
    Decision = DecideInteraction(Started, Simon, Angler, Unused);
    TestFalse(TEXT("no fish refused"), Decision.bApplied);
    TestTrue(TEXT("requirement reason"), Decision.Reason == EMarburgumInteractionReason::RequirementNotMet);
    TestEqual(TEXT("refusal names the stage"), Decision.Stage, FString(TEXT("catch_first_fish")));
    TestEqual(TEXT("refusal builds no record"), Unused.Revision, int64(-1));

    // Another character is unaffected by Simon's progress and gets the offer.
    Decision = DecideInteraction(Started, TEXT("vera"), Angler, Unused);
    TestTrue(TEXT("other character offered"), Decision.Effect == EMarburgumInteractionEffect::MissionStarted);

    // Debris and the service room before their stage: nothing to do yet.
    Decision = DecideInteraction(Started, Simon, TEXT("drava_cashbox_debris"), Unused);
    TestTrue(TEXT("debris early"), Decision.Reason == EMarburgumInteractionReason::NothingToDo);

    // With a fish the stage completes, consuming exactly one.
    FMarburgumWorldRecord Record = Started;
    Record.Inventories.FindOrAdd(Simon).Add(TEXT("roach"), 2);
    Step(*this, Record, Simon, Angler, EMarburgumInteractionEffect::StageCompleted, TEXT("catch shown"));
    TestEqual(TEXT("next stage"), Record.Missions.FindRef(Simon).FindRef(Mission).Stage, FString(TEXT("recover_cashbox")));
    TestEqual(TEXT("one fish consumed"), Record.Inventories.FindRef(Simon).FindRef(TEXT("roach")), 1);
    TestEqual(TEXT("rod kept"), Record.Inventories.FindRef(Simon).FindRef(TEXT("fishing_rod")), 1);

    // The debris hands over the six clues of the original stage 2.
    Decision = DecideInteraction(Record, Simon, TEXT("drava_cashbox_debris"), Unused);
    TestEqual(TEXT("six grants"), Decision.Granted.Num(), 6);
    Step(*this, Record, Simon, TEXT("drava_cashbox_debris"), EMarburgumInteractionEffect::StageCompleted, TEXT("cashbox"));
    for (const TCHAR* Clue : { TEXT("drava_cashbox"), TEXT("brass_key"), TEXT("water_damaged_accounting_page"), TEXT("torn_gate_photograph"),
             TEXT("faded_transport_ticket"), TEXT("carbon_copy_receipt") })
    {
        TestEqual(FString::Printf(TEXT("clue %s"), Clue), Record.Inventories.FindRef(Simon).FindRef(Clue), 1);
    }

    // Petra: the cashbox step comes before her own Kiosk 67 offer.
    Step(*this, Record, Simon, TEXT("petra_kralj"), EMarburgumInteractionEffect::StageCompleted, TEXT("address"));
    TestFalse(TEXT("kiosk not started by the step"), Record.Missions.FindRef(Simon).Contains(TEXT("kiosk_67_customer_stopped_coming")));

    // The service room needs the brass key held, and leaves it in the pocket.
    FMarburgumWorldRecord NoKey = Record;
    NoKey.Inventories.FindOrAdd(Simon).Remove(TEXT("brass_key"));
    Decision = DecideInteraction(NoKey, Simon, TEXT("raeda_service_room"), Unused);
    TestTrue(TEXT("key required"), Decision.Reason == EMarburgumInteractionReason::RequirementNotMet);
    TestEqual(TEXT("missing item named"), Decision.Item, FString(TEXT("brass_key")));
    Step(*this, Record, Simon, TEXT("raeda_service_room"), EMarburgumInteractionEffect::StageCompleted, TEXT("service room"));
    TestEqual(TEXT("key kept"), Record.Inventories.FindRef(Simon).FindRef(TEXT("brass_key")), 1);

    // Vera completes the mission: reward once, journal entry recorded.
    Decision = DecideInteraction(Record, Simon, TEXT("vera_horvat"), Unused);
    TestTrue(TEXT("final effect"), Decision.Effect == EMarburgumInteractionEffect::MissionCompleted);
    TestEqual(TEXT("reward paid"), Decision.MoneyDelta, int64(50));
    Record = Unused;
    const FMarburgumStoredMission& Done = Record.Missions.FindRef(Simon).FindRef(Mission);
    TestTrue(TEXT("completed"), Done.bCompleted);
    TestTrue(TEXT("reward flagged"), Done.bRewardPaid);
    TestEqual(TEXT("journal entry"), Done.JournalEntry, FString(TEXT("blue_ledger_r90")));
    TestEqual(TEXT("money"), Record.Money.FindRef(Simon), int64(50));

    // A completed mission is not offered again, and Vera now offers the next chapter.
    Decision = DecideInteraction(Record, Simon, Angler, Unused);
    TestTrue(TEXT("no second offer"), Decision.Reason == EMarburgumInteractionReason::NothingToDo);
    Decision = DecideInteraction(Record, Simon, TEXT("vera_horvat"), Unused);
    TestTrue(TEXT("next chapter offered"), Decision.Effect == EMarburgumInteractionEffect::MissionStarted
        && Decision.Mission == TEXT("raeda_names_no_longer_match"));

    // Before the first chapter is complete, Vera's chapter waits on it.
    Decision = DecideInteraction(Started, Simon, TEXT("vera_horvat"), Unused);
    TestTrue(TEXT("prerequisite"), Decision.Reason == EMarburgumInteractionReason::PrerequisiteIncomplete);

    // Someone who offers nothing.
    Decision = DecideInteraction(Empty, Simon, TEXT("drago_potocnik"), Unused);
    TestTrue(TEXT("nothing to do"), Decision.Reason == EMarburgumInteractionReason::NothingToDo);
    return true;
}

bool FMarburgumMissionChoiceTest::RunTest(const FString& Parameters)
{
    const FString Simon(TEXT("simon"));
    const FString Kiosk(TEXT("kiosk_67_customer_stopped_coming"));

    // Kiosk 67 through its four questions to the decision.
    FMarburgumWorldRecord Record;
    Step(*this, Record, Simon, TEXT("petra_kralj"), EMarburgumInteractionEffect::MissionStarted, TEXT("kiosk start"));
    for (const TCHAR* Witness : { TEXT("kiosk_67_bus_regular"), TEXT("kiosk_67_cafe_regular"), TEXT("kiosk_67_neighbour"), TEXT("kiosk_67_customer") })
    {
        Step(*this, Record, Simon, Witness, EMarburgumInteractionEffect::StageCompleted, Witness);
    }
    TestEqual(TEXT("decision due"), Record.Missions.FindRef(Simon).FindRef(Kiosk).Stage, FString(TEXT("resolution")));

    // A plain interaction at a decision is refused: the choice panel sends the choice.
    FMarburgumWorldRecord Unused;
    FMarburgumMissionDecision Decision = DecideInteraction(Record, Simon, TEXT("kiosk_67_customer"), Unused);
    TestTrue(TEXT("choice required"), Decision.Reason == EMarburgumInteractionReason::ChoiceRequired);

    // Unknown choice, wrong place, and paying the tab without the money are refused.
    Decision = DecideChoice(Record, Simon, TEXT("kiosk_67_customer"), Kiosk, TEXT("burn_the_ledger"), Unused);
    TestTrue(TEXT("unknown choice"), Decision.Reason == EMarburgumInteractionReason::UnknownChoice);
    Decision = DecideChoice(Record, Simon, TEXT("petra_kralj"), Kiosk, TEXT("find_work"), Unused);
    TestTrue(TEXT("wrong place"), Decision.Reason == EMarburgumInteractionReason::UnknownChoice);
    Decision = DecideChoice(Record, Simon, TEXT("kiosk_67_customer"), Kiosk, TEXT("pay_tab"), Unused);
    TestTrue(TEXT("cannot pay"), Decision.Reason == EMarburgumInteractionReason::InsufficientMoney);

    // With money, paying the tab: -20, Reputation +15, outcome and flags recorded, mission completed.
    FMarburgumWorldRecord Rich = Record;
    Rich.Money.Add(Simon, 50);
    FMarburgumWorldRecord Paid;
    Decision = DecideChoice(Rich, Simon, TEXT("kiosk_67_customer"), Kiosk, TEXT("pay_tab"), Paid);
    TestTrue(TEXT("paid"), Decision.bApplied && Decision.Effect == EMarburgumInteractionEffect::MissionCompleted);
    TestEqual(TEXT("money spent"), Paid.Money.FindRef(Simon), int64(30));
    TestEqual(TEXT("reputation"), Paid.Skills.FindRef(Simon).FindRef(TEXT("reputation")), int64(15));
    const FMarburgumStoredMission& Outcome = Paid.Missions.FindRef(Simon).FindRef(Kiosk);
    TestTrue(TEXT("completed"), Outcome.bCompleted);
    TestEqual(TEXT("choice kept"), Outcome.Choice, FString(TEXT("pay_tab")));
    TestEqual(TEXT("outcome kept"), Outcome.Outcome, FString(TEXT("tab_paid")));
    TestTrue(TEXT("stage flag"), Outcome.Flags.FindRef(TEXT("ledger_initials_recognized")));

    // Selling the object pays in Trading and Reputation XP without money; a second decision is refused.
    FMarburgumWorldRecord Sold;
    Decision = DecideChoice(Record, Simon, TEXT("kiosk_67_customer"), Kiosk, TEXT("sell_object"), Sold);
    TestTrue(TEXT("sold"), Decision.bApplied);
    TestEqual(TEXT("trading xp"), Sold.Skills.FindRef(Simon).FindRef(TEXT("trading")), int64(20));
    TestFalse(TEXT("no zero money entry"), Sold.Money.Contains(Simon));
    Decision = DecideChoice(Sold, Simon, TEXT("kiosk_67_customer"), Kiosk, TEXT("leave_alone"), Unused);
    TestTrue(TEXT("decided once"), Decision.Reason == EMarburgumInteractionReason::UnknownChoice);
    return true;
}

bool FMarburgumFishingRulesTest::RunTest(const FString& Parameters)
{
    using namespace MarburgumII;
    const Gameplay::FFishingSpotDefinition* Lent = Gameplay::FindFishingSpot(TEXT("drava_fishing_spot"));
    TestNotNull(TEXT("spot"), Lent);
    if (!Lent)
    {
        return false;
    }
    // Weights 5, 4, 3 in order: rolls 0-4 chub, 5-8 roach, 9-11 perch, then around again; negative rolls wrap.
    TestEqual(TEXT("roll 0"), FString(Fishing::DrawSpecies(*Lent, 0)->Item), FString(TEXT("chub")));
    TestEqual(TEXT("roll 4"), FString(Fishing::DrawSpecies(*Lent, 4)->Item), FString(TEXT("chub")));
    TestEqual(TEXT("roll 5"), FString(Fishing::DrawSpecies(*Lent, 5)->Item), FString(TEXT("roach")));
    TestEqual(TEXT("roll 11"), FString(Fishing::DrawSpecies(*Lent, 11)->Item), FString(TEXT("perch")));
    TestEqual(TEXT("roll 12"), FString(Fishing::DrawSpecies(*Lent, 12)->Item), FString(TEXT("chub")));
    TestEqual(TEXT("roll -1"), FString(Fishing::DrawSpecies(*Lent, -1)->Item), FString(TEXT("perch")));

    // The wait is the original's 1.5 s plus at most 2.5 s; the window 1.8 s.
    TestEqual(TEXT("shortest wait"), Fishing::BiteDelaySeconds(0), 1.5);
    TestEqual(TEXT("longest wait"), Fishing::BiteDelaySeconds(2500), 4.0);
    TestTrue(TEXT("wait bounded"), Fishing::BiteDelaySeconds(MAX_uint32) >= 1.5 && Fishing::BiteDelaySeconds(MAX_uint32) <= 4.0);
    TestEqual(TEXT("window"), Fishing::BiteWindowSeconds(), 1.8);

    // A catch adds one fish and its XP in one record.
    FMarburgumWorldRecord Empty;
    FMarburgumWorldRecord Next;
    const FMarburgumCatchDecision Catch = Fishing::DecideCatch(Empty, TEXT("simon"), TEXT("drava_fishing_spot"), 9, Next);
    TestTrue(TEXT("caught"), Catch.bApplied);
    TestEqual(TEXT("perch"), Catch.Item, FString(TEXT("perch")));
    TestEqual(TEXT("xp"), Catch.Xp, int64(24));
    TestEqual(TEXT("fish held"), Next.Inventories.FindRef(TEXT("simon")).FindRef(TEXT("perch")), 1);
    TestEqual(TEXT("fishing xp"), Next.Skills.FindRef(TEXT("simon")).FindRef(TEXT("fishing")), int64(24));
    TestEqual(TEXT("one revision"), Next.Revision, int64(1));
    FMarburgumWorldRecord Unchanged;
    TestFalse(TEXT("unknown spot"), Fishing::DecideCatch(Empty, TEXT("simon"), TEXT("nowhere"), 0, Unchanged).bApplied);
    return true;
}

#endif
