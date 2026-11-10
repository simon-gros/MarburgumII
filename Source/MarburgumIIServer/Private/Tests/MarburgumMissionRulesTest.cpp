#include "MarburgumMissionRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumMissionRulesTest,
    "Marburgum.Server.MissionRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarburgumMissionRulesTest::RunTest(const FString& Parameters)
{
    using MarburgumII::Missions::DecideNpcInteraction;
    const FString Angler(TEXT("lent_angler"));
    const FString Mission(TEXT("lent_first_catch"));
    const FString Simon(TEXT("simon"));

    // Talking to the angler for the first time starts the mission and grants its start item.
    FMarburgumWorldRecord Empty;
    Empty.Revision = 7;
    FMarburgumWorldRecord Started;
    FMarburgumNpcDecision Decision = DecideNpcInteraction(Empty, Simon, Angler, Started);
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
    Decision = DecideNpcInteraction(Started, Simon, Angler, Unused);
    TestFalse(TEXT("no fish refused"), Decision.bApplied);
    TestTrue(TEXT("requirement reason"), Decision.Reason == EMarburgumInteractionReason::RequirementNotMet);
    TestEqual(TEXT("refusal names the stage"), Decision.Stage, FString(TEXT("catch_first_fish")));
    TestEqual(TEXT("refusal builds no record"), Unused.Revision, int64(-1));

    // Another character is unaffected by Simon's progress and gets the offer.
    Decision = DecideNpcInteraction(Started, TEXT("vera"), Angler, Unused);
    TestTrue(TEXT("other character offered"), Decision.Effect == EMarburgumInteractionEffect::MissionStarted);

    // With a fish (not obtainable in this build) the stage completes, consuming exactly one.
    FMarburgumWorldRecord WithFish = Started;
    WithFish.Inventories.FindOrAdd(Simon).Add(TEXT("roach"), 2);
    FMarburgumWorldRecord Advanced;
    Decision = DecideNpcInteraction(WithFish, Simon, Angler, Advanced);
    TestTrue(TEXT("catch shown"), Decision.bApplied);
    TestTrue(TEXT("stage effect"), Decision.Effect == EMarburgumInteractionEffect::StageCompleted);
    TestEqual(TEXT("next stage"), Advanced.Missions.FindRef(Simon).FindRef(Mission).Stage, FString(TEXT("recover_cashbox")));
    TestEqual(TEXT("one fish consumed"), Advanced.Inventories.FindRef(Simon).FindRef(TEXT("roach")), 1);
    TestEqual(TEXT("rod kept"), Advanced.Inventories.FindRef(Simon).FindRef(TEXT("fishing_rod")), 1);

    // The next stage targets the debris, so the angler has nothing more; the last registered stage
    // never invents an ending.
    Decision = DecideNpcInteraction(Advanced, Simon, Angler, Unused);
    TestTrue(TEXT("angler done"), Decision.Reason == EMarburgumInteractionReason::NothingToDo);
    Decision = DecideNpcInteraction(Advanced, Simon, TEXT("drava_cashbox_debris"), Unused);
    TestFalse(TEXT("unregistered ending refused"), Decision.bApplied);
    TestTrue(TEXT("unregistered ending reason"), Decision.Reason == EMarburgumInteractionReason::NothingToDo);

    // An NPC that offers nothing.
    Decision = DecideNpcInteraction(Empty, Simon, TEXT("lent_passerby"), Unused);
    TestTrue(TEXT("unknown npc"), Decision.Reason == EMarburgumInteractionReason::NothingToDo);
    return true;
}

#endif
