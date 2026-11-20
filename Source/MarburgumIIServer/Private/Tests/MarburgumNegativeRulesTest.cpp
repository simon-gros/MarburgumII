#include "MarburgumMissionRules.h"

#include "HAL/FileManager.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumMissionLogic.h"
#include "MarburgumWorldStore.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

// Chapter VI, The Negative, and the Frame 37 side story (ADR-084).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumNegativePrerequisitesTest,
    "Marburgum.Server.Story.Negative.Prerequisites",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumNegativeBranchesTest,
    "Marburgum.Server.Story.Negative.AlbinBranches",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumNegativeFrameFirstTest,
    "Marburgum.Server.Story.Negative.FrameThirtySevenFirst",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumNegativeRefusalsTest,
    "Marburgum.Server.Story.Negative.Refusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumNegativePersistenceTest,
    "Marburgum.Server.Story.Negative.Persistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumConditionalStagesTest,
    "Marburgum.Shared.Mission.ConditionalStages",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    using MarburgumII::Missions::DecideChoice;
    using MarburgumII::Missions::DecideInteraction;
    using MarburgumII::Missions::DecidePhotograph;

    constexpr const TCHAR* Negative = TEXT("raeda_the_negative");
    constexpr const TCHAR* Frame37 = TEXT("nika_frame_37");
    constexpr const TCHAR* Kiosk67 = TEXT("kiosk_67_customer_stopped_coming");

    void Complete(FMarburgumWorldRecord& Record, const FString& Character, const TCHAR* Mission, const TCHAR* Outcome = TEXT(""))
    {
        FMarburgumStoredMission& State = Record.Missions.FindOrAdd(Character).FindOrAdd(Mission);
        const MarburgumII::Gameplay::FMissionDefinition* Definition = MarburgumII::Gameplay::FindMission(Mission);
        State.Stage = Definition ? Definition->Stages[Definition->StageCount - 1].Id : TEXT("");
        State.bCompleted = true;
        State.bRewardPaid = true;
        State.Outcome = Outcome;
    }

    /**
     * A character who has played the Prologue to Chapter III, Nika's Then and Now (the camera) and, unless KioskOutcome is
     * empty, Kiosk 67 to that outcome; with the papers those missions leave in custody. No Chapter IV or V.
     */
    void Prepare(FMarburgumWorldRecord& Record, const FString& Character, const TCHAR* KioskOutcome, int64 Money = 200)
    {
        for (const TCHAR* Done : { TEXT("lent_first_catch"), TEXT("raeda_names_no_longer_match"), TEXT("raeda_last_shift"), TEXT("raeda_city_for_sale") })
        {
            Complete(Record, Character, Done);
        }
        Complete(Record, Character, TEXT("nika_then_and_now"), TEXT("darkroom_lesson"));
        if (*KioskOutcome)
        {
            Complete(Record, Character, Kiosk67, KioskOutcome);
        }
        TMap<FString, int32>& Items = Record.Inventories.FindOrAdd(Character);
        for (const TCHAR* Kept : { TEXT("torn_gate_photograph"), TEXT("faded_transport_ticket"), TEXT("carbon_copy_receipt"), TEXT("kainach_serial_photos"),
                 TEXT("camera"), TEXT("photo_vodni_stolp"), TEXT("photo_sodni_stolp"), TEXT("photo_stari_most") })
        {
            Items.Add(Kept, 1);
        }
        if (Money > 0)
        {
            Record.Money.Add(Character, Money);
        }
        Record.Revision = FMath::Max<int64>(Record.Revision, 100);
    }

    struct FStep
    {
        FAutomationTestBase& Test;
        FMarburgumWorldRecord& Record;
        FString Character;

        FMarburgumMissionDecision Talk(const TCHAR* Target, EMarburgumInteractionEffect Expected, const FString& What)
        {
            FMarburgumWorldRecord Next;
            const FMarburgumMissionDecision Decision = DecideInteraction(Record, Character, Target, Next);
            Test.TestTrue(FString::Printf(TEXT("%s: %s applied"), *Character, *What), Decision.bApplied);
            Test.TestTrue(FString::Printf(TEXT("%s: %s effect"), *Character, *What), Decision.Effect == Expected);
            if (Decision.bApplied)
            {
                Test.TestEqual(FString::Printf(TEXT("%s: %s is one revision"), *Character, *What), Next.Revision, Record.Revision + 1);
                Record = Next;
            }
            return Decision;
        }

        /** An interaction that must change nothing, refused for Reason. */
        void Refused(const TCHAR* Target, EMarburgumInteractionReason Reason, const FString& What)
        {
            FMarburgumWorldRecord Next;
            const FMarburgumMissionDecision Decision = DecideInteraction(Record, Character, Target, Next);
            Test.TestFalse(FString::Printf(TEXT("%s: %s changes nothing"), *Character, *What), Decision.bApplied);
            Test.TestTrue(FString::Printf(TEXT("%s: %s reason"), *Character, *What), Decision.Reason == Reason);
        }

        FMarburgumMissionDecision Shoot(const TCHAR* Subject, EMarburgumInteractionEffect Expected, const FString& What)
        {
            FMarburgumWorldRecord Next;
            const FMarburgumMissionDecision Decision = DecidePhotograph(Record, Character, Subject, Next);
            Test.TestTrue(FString::Printf(TEXT("%s: %s applied"), *Character, *What), Decision.bApplied);
            Test.TestTrue(FString::Printf(TEXT("%s: %s effect"), *Character, *What), Decision.Effect == Expected);
            if (Decision.bApplied)
            {
                Record = Next;
            }
            return Decision;
        }

        FMarburgumMissionDecision Choose(const TCHAR* Target, const TCHAR* Mission, const TCHAR* Choice, const FString& What)
        {
            FMarburgumWorldRecord Next;
            const FMarburgumMissionDecision Decision = DecideChoice(Record, Character, Target, Mission, Choice, Next);
            Test.TestTrue(FString::Printf(TEXT("%s: %s applied"), *Character, *What), Decision.bApplied);
            if (Decision.bApplied)
            {
                Record = Next;
            }
            return Decision;
        }

        int32 Held(const TCHAR* Item) const { return Record.Inventories.FindRef(Character).FindRef(Item); }
        int64 Xp(const TCHAR* Skill) const { return Record.Skills.FindRef(Character).FindRef(Skill); }
        int64 Money() const { return Record.Money.FindRef(Character); }

        FString Stage(const TCHAR* Mission) const
        {
            const TMap<FString, FMarburgumStoredMission>* Owned = Record.Missions.Find(Character);
            const FMarburgumStoredMission* State = Owned ? Owned->Find(Mission) : nullptr;
            return State ? State->Stage : FString();
        }

        bool Completed(const TCHAR* Mission) const
        {
            const TMap<FString, FMarburgumStoredMission>* Owned = Record.Missions.Find(Character);
            const FMarburgumStoredMission* State = Owned ? Owned->Find(Mission) : nullptr;
            return State && State->bCompleted;
        }
    };

    /** The side story from Nika's offer to its decision; Nika must be free to offer it (Chapter VI waiting or elsewhere). */
    void PlayFrameThirtySeven(FStep& Step)
    {
        FAutomationTestBase& Test = Step.Test;
        const FMarburgumMissionDecision Offer = Step.Talk(TEXT("nika_vidmar"), EMarburgumInteractionEffect::MissionStarted, TEXT("Nika offers Frame 37"));
        Test.TestEqual(TEXT("the side story is what starts"), Offer.Mission, FString(Frame37));
        Step.Talk(TEXT("foto_vidmar_light_table"), EMarburgumInteractionEffect::StageCompleted, TEXT("the loupe"));

        // The deduction: a wrong answer is refused and records nothing; the right one goes on.
        const FMarburgumWorldRecord Before = Step.Record;
        for (const TCHAR* Wrong : { TEXT("the_man"), TEXT("the_light_leak") })
        {
            FMarburgumWorldRecord Next;
            const FMarburgumMissionDecision Decision = DecideChoice(Step.Record, Step.Character, TEXT("foto_vidmar_light_table"), Frame37, Wrong, Next);
            Test.TestFalse(FString::Printf(TEXT("wrong answer %s is not applied"), Wrong), Decision.bApplied);
            Test.TestTrue(FString::Printf(TEXT("wrong answer %s reason"), Wrong), Decision.Reason == EMarburgumInteractionReason::WrongConclusion);
        }
        Test.TestEqual(TEXT("wrong answers change no revision"), Step.Record.Revision, Before.Revision);
        Test.TestEqual(TEXT("still at the deduction"), Step.Stage(Frame37), FString(TEXT("what_matters")));
        const int64 PhotographyBefore = Step.Xp(TEXT("photography"));
        Step.Choose(TEXT("foto_vidmar_light_table"), Frame37, TEXT("the_place"), TEXT("the place, not the person"));
        Test.TestEqual(TEXT("the deduction's Photography"), Step.Xp(TEXT("photography")), PhotographyBefore + 10);

        Step.Talk(TEXT("nika_vidmar"), EMarburgumInteractionEffect::StageCompleted, TEXT("Nika's print"));
        Test.TestEqual(TEXT("one Frame 37 print"), Step.Held(TEXT("frame_37_print")), 1);
    }

    /** From the date-board photograph to the side story's decision. */
    void FinishFrameThirtySeven(FStep& Step, bool bDateBoardFirstTime)
    {
        FAutomationTestBase& Test = Step.Test;
        const int64 PhotographyBefore = Step.Xp(TEXT("photography"));
        Step.Shoot(TEXT("melje_date_board_view"), EMarburgumInteractionEffect::StageCompleted, TEXT("the date board"));
        Test.TestEqual(TEXT("the date board's first-photograph XP, once"), Step.Xp(TEXT("photography")), PhotographyBefore + (bDateBoardFirstTime ? 25 : 0));
        Test.TestEqual(TEXT("one date-board print"), Step.Held(TEXT("photo_melje_date_board")), 1);
    }

    void CloseFrameThirtySeven(FStep& Step)
    {
        Step.Talk(TEXT("nika_vidmar"), EMarburgumInteractionEffect::StageCompleted, TEXT("Nika sees the yard"));
        Step.Refused(TEXT("nika_vidmar"), EMarburgumInteractionReason::ChoiceRequired, TEXT("the scrap decision is due"));
        const FMarburgumMissionDecision Done = Step.Choose(TEXT("nika_vidmar"), Frame37, TEXT("lab_safe"), TEXT("the scrap in the lab safe"));
        Step.Test.TestTrue(TEXT("the side story completes"), Done.Effect == EMarburgumInteractionEffect::MissionCompleted && Step.Completed(Frame37));
    }

    /** Chapter VI from Nika's offer to the Glas strip (Frame 37 played inside it when bSideStoryInside). */
    void PlayToTheStrip(FStep& Step, bool bSideStoryInside)
    {
        FAutomationTestBase& Test = Step.Test;
        const FMarburgumMissionDecision Offer = Step.Talk(TEXT("nika_vidmar"), EMarburgumInteractionEffect::MissionStarted, TEXT("Nika offers The Negative"));
        Test.TestEqual(TEXT("Chapter VI starts"), Offer.Mission, FString(Negative));
        Step.Talk(TEXT("nika_vidmar"), EMarburgumInteractionEffect::StageCompleted, TEXT("Nika dates the print"));
        Step.Talk(TEXT("foto_vidmar_order_book"), EMarburgumInteractionEffect::StageCompleted, TEXT("the order book"));
        Step.Shoot(TEXT("foto_vidmar_order_book_page"), EMarburgumInteractionEffect::StageCompleted, TEXT("a copy of the page"));
        Test.TestEqual(TEXT("the page copy"), Step.Held(TEXT("photo_foto_vidmar_order_book")), 1);
        Step.Talk(TEXT("drago_potocnik"), EMarburgumInteractionEffect::StageCompleted, TEXT("Drago's wallet print"));
        Test.TestEqual(TEXT("Drago's account"), Step.Held(TEXT("drago_wallet_print_account")), 1);
        Step.Talk(TEXT("ivana_zupan"), EMarburgumInteractionEffect::StageCompleted, TEXT("Ivana Zupan"));
        // The contact sheet is not on the light table until it is in hand.
        {
            FMarburgumWorldRecord Next;
            const FMarburgumMissionDecision Empty = DecidePhotograph(Step.Record, Step.Character, TEXT("zupan_contact_sheet_copy"), Next);
            Test.TestFalse(TEXT("no sheet, nothing to photograph"), Empty.bApplied);
            Test.TestTrue(TEXT("the sheet is what is missing"), Empty.Reason == EMarburgumInteractionReason::RequirementNotMet && Empty.Item == TEXT("zupan_contact_sheet"));
        }
        Step.Talk(TEXT("block_13_storage_cage"), EMarburgumInteractionEffect::StageCompleted, TEXT("the storage cage"));
        Test.TestEqual(TEXT("the original contact sheet"), Step.Held(TEXT("zupan_contact_sheet")), 1);
        Step.Shoot(TEXT("zupan_contact_sheet_copy"), EMarburgumInteractionEffect::StageCompleted, TEXT("the sheet on the light table"));
        Test.TestEqual(TEXT("the sheet's copy"), Step.Held(TEXT("photo_zupan_contact_sheet")), 1);
        Test.TestEqual(TEXT("at Frame 37"), Step.Stage(Negative), FString(TEXT("frame_37_print")));
        if (bSideStoryInside)
        {
            // Nika has no print to compare yet: she offers the scrap tin, the side story.
            PlayFrameThirtySeven(Step);
            // With the print in hand, Chapter VI's step at Nika comes first (registry order).
            Step.Talk(TEXT("nika_vidmar"), EMarburgumInteractionEffect::StageCompleted, TEXT("the print matches the sheet"));
            FinishFrameThirtySeven(Step, true);
            CloseFrameThirtySeven(Step);
        }
        else
        {
            Step.Talk(TEXT("nika_vidmar"), EMarburgumInteractionEffect::StageCompleted, TEXT("the print already in hand matches the sheet"));
        }
        Test.TestEqual(TEXT("one print, however it came"), Step.Held(TEXT("frame_37_print")), 1);
        Step.Talk(TEXT("vera_horvat"), EMarburgumInteractionEffect::StageCompleted, TEXT("Vera lays out the chronology"));
        Test.TestEqual(TEXT("the inference, as an inference"), Step.Held(TEXT("chronology_frame_37")), 1);
        Step.Talk(TEXT("mariborski_glas_archive"), EMarburgumInteractionEffect::StageCompleted, TEXT("the Glas photo desk"));
        Test.TestEqual(TEXT("the missing strip"), Step.Held(TEXT("torn_strip_print")), 1);
    }

    /** Albin's route for the Kiosk 67 outcome, to his testimony. */
    void PlayAlbin(FStep& Step, const TCHAR* KioskOutcome)
    {
        FAutomationTestBase& Test = Step.Test;
        const FString Outcome(KioskOutcome);
        // People of the other routes have nothing for this character.
        if (Outcome != TEXT("object_sold"))
        {
            Step.Refused(TEXT("stamp_collector"), EMarburgumInteractionReason::NothingToDo, TEXT("the collector, another route"));
        }
        if (Outcome != TEXT("left_alone"))
        {
            Step.Refused(TEXT("viole_terrace_regular"), EMarburgumInteractionReason::NothingToDo, TEXT("the terrace regular, another route"));
        }
        if (Outcome == TEXT("temporary_work"))
        {
            Test.TestEqual(TEXT("temporary work: straight to Albin"), Step.Stage(Negative), FString(TEXT("albin_willing")));
            Step.Talk(TEXT("kiosk_67_customer"), EMarburgumInteractionEffect::StageCompleted, TEXT("Albin testifies willingly"));
        }
        else if (Outcome == TEXT("tab_paid") || Outcome == TEXT("debt_erased"))
        {
            Test.TestEqual(TEXT("settled tab: Albin hesitates"), Step.Stage(Negative), FString(TEXT("albin_hesitates")));
            Step.Talk(TEXT("kiosk_67_customer"), EMarburgumInteractionEffect::StageCompleted, TEXT("Albin hesitates"));
            Test.TestEqual(TEXT("no testimony yet"), Step.Held(TEXT("albin_testimony")), 0);
            Step.Refused(TEXT("kiosk_67_customer"), EMarburgumInteractionReason::NothingToDo, TEXT("asking again at once"));
            Step.Talk(TEXT("petra_kralj"), EMarburgumInteractionEffect::StageCompleted, TEXT("Petra's word"));
            Step.Talk(TEXT("kiosk_67_customer"), EMarburgumInteractionEffect::StageCompleted, TEXT("one more conversation"));
        }
        else if (Outcome == TEXT("object_sold"))
        {
            Step.Talk(TEXT("kiosk_67_customer"), EMarburgumInteractionEffect::StageCompleted, TEXT("the stamp he sold"));
            Step.Refused(TEXT("stamp_collector"), EMarburgumInteractionReason::ChoiceRequired, TEXT("the collector's terms"));
            const int64 Money = Step.Money();
            if (Money >= 40)
            {
                const int64 Trading = Step.Xp(TEXT("trading"));
                Step.Choose(TEXT("stamp_collector"), Negative, TEXT("buy_stamp"), TEXT("buy the stamp back"));
                Test.TestEqual(TEXT("40 paid"), Step.Money(), Money - 40);
                Test.TestEqual(TEXT("Trading for the deal"), Step.Xp(TEXT("trading")), Trading + 15);
            }
            else
            {
                FMarburgumWorldRecord Next;
                const FMarburgumMissionDecision Poor = DecideChoice(Step.Record, Step.Character, TEXT("stamp_collector"), Negative, TEXT("buy_stamp"), Next);
                Test.TestTrue(TEXT("cannot afford: refused"), !Poor.bApplied && Poor.Reason == EMarburgumInteractionReason::InsufficientMoney);
                const int64 Photography = Step.Xp(TEXT("photography"));
                Step.Choose(TEXT("stamp_collector"), Negative, TEXT("catalogue_work"), TEXT("work it off"));
                Test.TestEqual(TEXT("Photography for the catalogue"), Step.Xp(TEXT("photography")), Photography + 20);
                Test.TestEqual(TEXT("no money spent"), Step.Money(), Money);
            }
            Test.TestEqual(TEXT("the stamp in hand"), Step.Held(TEXT("machine_hall_council_stamp")), 1);
            Step.Talk(TEXT("kiosk_67_customer"), EMarburgumInteractionEffect::StageCompleted, TEXT("Albin with his stamp"));
            Test.TestEqual(TEXT("the stamp went back to Albin"), Step.Held(TEXT("machine_hall_council_stamp")), 0);
        }
        else
        {
            Step.Talk(TEXT("kiosk_67_customer"), EMarburgumInteractionEffect::StageCompleted, TEXT("Albin refuses"));
            Test.TestEqual(TEXT("a refusal is not testimony"), Step.Held(TEXT("albin_testimony")), 0);
            Step.Refused(TEXT("kiosk_67_customer"), EMarburgumInteractionReason::NothingToDo, TEXT("pressing him again"));
            Step.Talk(TEXT("viole_terrace_regular"), EMarburgumInteractionEffect::StageCompleted, TEXT("the match-day circle vouches"));
            Step.Talk(TEXT("kiosk_67_customer"), EMarburgumInteractionEffect::StageCompleted, TEXT("Albin, vouched for"));
        }
        Test.TestEqual(TEXT("Albin's testimony"), Step.Held(TEXT("albin_testimony")), 1);
        Test.TestEqual(TEXT("at the ticket"), Step.Stage(Negative), FString(TEXT("the_ticket")));
    }

    void PlayTheTicket(FStep& Step)
    {
        FAutomationTestBase& Test = Step.Test;
        const int64 Money = Step.Money();
        const FMarburgumMissionDecision Done = Step.Talk(TEXT("nika_vidmar"), EMarburgumInteractionEffect::MissionCompleted, TEXT("the ticket"));
        Test.TestEqual(TEXT("60 paid once"), Step.Money(), Money + 60);
        Test.TestEqual(TEXT("the original went to the lab safe"), Step.Held(TEXT("zupan_contact_sheet")), 0);
        Test.TestEqual(TEXT("a custody receipt instead"), Step.Held(TEXT("contact_sheet_custody_receipt")), 1);
        Test.TestEqual(TEXT("the ticket stays"), Step.Held(TEXT("faded_transport_ticket")), 1);
        Test.TestTrue(TEXT("Chapter VI closed"), Step.Completed(Negative));
        // Nothing twice: Nika has nothing more, and the money does not move.
        FMarburgumWorldRecord Next;
        const FMarburgumMissionDecision Again = DecideInteraction(Step.Record, Step.Character, TEXT("nika_vidmar"), Next);
        Test.TestFalse(TEXT("the final step does not repeat"), Again.bApplied && Again.Mission == Negative);
    }
}

bool FMarburgumNegativePrerequisitesTest::RunTest(const FString& Parameters)
{
    // Without Kiosk 67 Chapter VI waits, and Nika offers the side story instead: nothing else from Chapter VI happens.
    {
        FMarburgumWorldRecord Record;
        Prepare(Record, TEXT("noor"), TEXT(""));
        FMarburgumWorldRecord Next;
        const FMarburgumMissionDecision Offer = DecideInteraction(Record, TEXT("noor"), TEXT("nika_vidmar"), Next);
        TestTrue(TEXT("Nika offers the side story"), Offer.bApplied && Offer.Mission == Frame37);
        TestFalse(TEXT("Chapter VI not started"), Next.Missions.FindRef(TEXT("noor")).Contains(Negative));
    }
    // Without Chapter III Nika has only the side story; without Then and Now (no camera yet) she offers that first.
    // Chapter V is never required or rewarded.
    const TPair<const TCHAR*, const TCHAR*> Missing[] = {
        { TEXT("raeda_city_for_sale"), Frame37 }, { TEXT("nika_then_and_now"), TEXT("nika_then_and_now") } };
    for (const TPair<const TCHAR*, const TCHAR*>& Case : Missing)
    {
        FMarburgumWorldRecord Record;
        Prepare(Record, TEXT("mira"), TEXT("tab_paid"));
        Record.Missions.FindOrAdd(TEXT("mira")).Remove(Case.Key);
        FMarburgumWorldRecord Next;
        const FMarburgumMissionDecision Decision = DecideInteraction(Record, TEXT("mira"), TEXT("nika_vidmar"), Next);
        TestTrue(FString::Printf(TEXT("without %s Nika offers %s instead"), Case.Key, Case.Value), Decision.bApplied && Decision.Mission == Case.Value);
        TestFalse(FString::Printf(TEXT("without %s Chapter VI does not start"), Case.Key), Next.Missions.FindRef(TEXT("mira")).Contains(Negative));
    }
    const MarburgumII::Gameplay::FMissionDefinition* Chapter = MarburgumII::Gameplay::FindMission(Negative);
    TestNotNull(TEXT("Chapter VI registered"), Chapter);
    if (Chapter)
    {
        bool bNeedsChapterFive = false;
        for (int32 Index = 0; Index < Chapter->RequiresCompletedCount; ++Index)
        {
            bNeedsChapterFive |= FString(Chapter->RequiresCompleted[Index]) == TEXT("raeda_busy_signal");
        }
        TestFalse(TEXT("Chapter V is not pretended"), bNeedsChapterFive);
        TestNull(TEXT("Chapter V does not exist yet"), MarburgumII::Gameplay::FindMission(TEXT("raeda_busy_signal")));
    }
    return true;
}

bool FMarburgumNegativeBranchesTest::RunTest(const FString& Parameters)
{
    // Every Kiosk 67 outcome, each a different character in one shared world, one after the other: each route is its own,
    // and nobody's progress moves anybody else's.
    FMarburgumWorldRecord Record;
    const TPair<const TCHAR*, const TCHAR*> Players[] = {
        { TEXT("ana"), TEXT("temporary_work") }, { TEXT("bor"), TEXT("tab_paid") }, { TEXT("cene"), TEXT("debt_erased") },
        { TEXT("dora"), TEXT("object_sold") }, { TEXT("ema"), TEXT("left_alone") }, { TEXT("fran"), TEXT("object_sold") },
    };
    for (const TPair<const TCHAR*, const TCHAR*>& Player : Players)
    {
        // Fran cannot pay the collector and has to work the stamp off.
        Prepare(Record, Player.Key, Player.Value, FCString::Strcmp(Player.Key, TEXT("fran")) == 0 ? 0 : 200);
    }
    for (const TPair<const TCHAR*, const TCHAR*>& Player : Players)
    {
        const FMarburgumWorldRecord Before = Record;
        FStep Step{ *this, Record, Player.Key };
        PlayToTheStrip(Step, true);
        PlayAlbin(Step, Player.Value);
        PlayTheTicket(Step);
        // Photography XP: the order book 10, the sheet 20, the date board 25, the deduction 10 (+20 for working off the stamp).
        const int64 Expected = 55 + 10 + (FCString::Strcmp(Player.Key, TEXT("fran")) == 0 ? 20 : 0);
        TestEqual(FString::Printf(TEXT("%s: Photography XP"), Player.Key), Step.Xp(TEXT("photography")), Expected);
        TestEqual(FString::Printf(TEXT("%s: three subjects documented"), Player.Key),
            Record.Discoveries.FindRef(Player.Key).Num(), 3);
        for (const TPair<const TCHAR*, const TCHAR*>& Other : Players)
        {
            if (FCString::Strcmp(Other.Key, Player.Key) != 0)
            {
                TestTrue(FString::Printf(TEXT("%s's play left %s's inventory alone"), Player.Key, Other.Key),
                    Record.Inventories.FindRef(Other.Key).OrderIndependentCompareEqual(Before.Inventories.FindRef(Other.Key)));
                TestEqual(FString::Printf(TEXT("%s's play left %s's money alone"), Player.Key, Other.Key),
                    Record.Money.FindRef(Other.Key), Before.Money.FindRef(Other.Key));
            }
        }
    }
    return true;
}

bool FMarburgumNegativeFrameFirstTest::RunTest(const FString& Parameters)
{
    // The side story before Chapter VI is open (no Kiosk 67 yet): the print and the date board come first, unattributed.
    FMarburgumWorldRecord Record;
    Prepare(Record, TEXT("gal"), TEXT(""));
    FStep Step{ *this, Record, TEXT("gal") };
    PlayFrameThirtySeven(Step);
    FinishFrameThirtySeven(Step, true);
    CloseFrameThirtySeven(Step);
    const int64 PhotographyAfterSide = Step.Xp(TEXT("photography"));

    // Then Kiosk 67 is resolved, and Chapter VI uses the same print and photograph: nothing is handed out twice.
    Complete(Record, TEXT("gal"), Kiosk67, TEXT("debt_erased"));
    PlayToTheStrip(Step, false);
    PlayAlbin(Step, TEXT("debt_erased"));
    PlayTheTicket(Step);
    TestEqual(TEXT("one Frame 37 print"), Step.Held(TEXT("frame_37_print")), 1);
    TestEqual(TEXT("one date-board print"), Step.Held(TEXT("photo_melje_date_board")), 1);
    TestEqual(TEXT("Chapter VI's Photography is only its own two documents"), Step.Xp(TEXT("photography")), PhotographyAfterSide + 30);

    // A unique item is never doubled even if two missions hand it over: grant it again through a stage that names it.
    {
        FMarburgumWorldRecord Again = Record;
        FMarburgumStoredMission& Side = Again.Missions.FindOrAdd(TEXT("gal")).FindOrAdd(Frame37);
        Side.bCompleted = false;
        Side.Stage = TEXT("nika_prints");
        FMarburgumWorldRecord Next;
        const FMarburgumMissionDecision Repeat = DecideInteraction(Again, TEXT("gal"), TEXT("nika_vidmar"), Next);
        TestTrue(TEXT("the stage itself completes"), Repeat.bApplied);
        TestEqual(TEXT("but the print is not doubled"), Next.Inventories.FindRef(TEXT("gal")).FindRef(TEXT("frame_37_print")), 1);
        TestFalse(TEXT("and the receipt does not claim one"), Repeat.Granted.Contains(TEXT("frame_37_print")));
    }
    return true;
}

bool FMarburgumNegativeRefusalsTest::RunTest(const FString& Parameters)
{
    FMarburgumWorldRecord Record;
    Prepare(Record, TEXT("hana"), TEXT("left_alone"));
    // Chapter IV is done too, so Vera has nothing new to offer when the chronology is refused below.
    Complete(Record, TEXT("hana"), TEXT("raeda_voices_on_tape"), TEXT("tone_spared"));
    FStep Step{ *this, Record, TEXT("hana") };
    Step.Talk(TEXT("nika_vidmar"), EMarburgumInteractionEffect::MissionStarted, TEXT("the offer"));

    // The torn photograph is what Nika dates: without it the step is never skipped. Nika offers what else she has (the
    // scrap tin), and once that is under way she says what is missing.
    {
        FMarburgumWorldRecord Without = Record;
        Without.Inventories.FindOrAdd(TEXT("hana")).Remove(TEXT("torn_gate_photograph"));
        FStep Lost{ *this, Without, TEXT("hana") };
        const FMarburgumMissionDecision Offer = Lost.Talk(TEXT("nika_vidmar"), EMarburgumInteractionEffect::MissionStarted, TEXT("without the photograph"));
        TestEqual(TEXT("the side story, not Chapter VI"), Offer.Mission, FString(Frame37));
        TestEqual(TEXT("Chapter VI did not move"), Lost.Stage(Negative), FString(TEXT("date_the_print")));
        FMarburgumWorldRecord Next;
        const FMarburgumMissionDecision Refused = DecideInteraction(Without, TEXT("hana"), TEXT("nika_vidmar"), Next);
        TestTrue(TEXT("no photograph: refused"), !Refused.bApplied && Refused.Reason == EMarburgumInteractionReason::RequirementNotMet
            && Refused.Item == TEXT("torn_gate_photograph"));
    }
    // Out of order: the cage before Ivana, Vera before the evidence, the Glas before the chronology.
    Step.Talk(TEXT("nika_vidmar"), EMarburgumInteractionEffect::StageCompleted, TEXT("dated"));
    Step.Refused(TEXT("block_13_storage_cage"), EMarburgumInteractionReason::NothingToDo, TEXT("the cage too early"));
    Step.Refused(TEXT("kiosk_67_customer"), EMarburgumInteractionReason::NothingToDo, TEXT("Albin too early"));

    // A photograph of the wrong subject does not complete the page step; the right one does, and a repeat is refused.
    Step.Talk(TEXT("foto_vidmar_order_book"), EMarburgumInteractionEffect::StageCompleted, TEXT("the order book"));
    Step.Shoot(TEXT("vodni_stolp"), EMarburgumInteractionEffect::Photographed, TEXT("a tower instead of the page"));
    TestEqual(TEXT("still at the page"), Step.Stage(Negative), FString(TEXT("copy_order_book")));
    Step.Shoot(TEXT("foto_vidmar_order_book_page"), EMarburgumInteractionEffect::StageCompleted, TEXT("the page"));
    {
        FMarburgumWorldRecord Next;
        const FMarburgumMissionDecision Again = DecidePhotograph(Record, TEXT("hana"), TEXT("foto_vidmar_order_book_page"), Next);
        TestTrue(TEXT("the same page again is refused"), !Again.bApplied && Again.Reason == EMarburgumInteractionReason::AlreadyPhotographed);
    }
    // No camera, no photograph; an unknown subject is refused.
    {
        FMarburgumWorldRecord NoCamera = Record;
        NoCamera.Inventories.FindOrAdd(TEXT("hana")).Remove(TEXT("camera"));
        FMarburgumWorldRecord Next;
        TestTrue(TEXT("no camera"), DecidePhotograph(NoCamera, TEXT("hana"), TEXT("melje_date_board_view"), Next).Reason == EMarburgumInteractionReason::NoCamera);
        TestTrue(TEXT("unknown subject"), DecidePhotograph(Record, TEXT("hana"), TEXT("frame_37"), Next).Reason == EMarburgumInteractionReason::UnknownTarget);
    }
    // A choice that does not exist here, or another character's decision, is refused.
    {
        FMarburgumWorldRecord Next;
        TestTrue(TEXT("no decision is due at the collector"),
            DecideChoice(Record, TEXT("hana"), TEXT("stamp_collector"), Negative, TEXT("buy_stamp"), Next).Reason == EMarburgumInteractionReason::UnknownChoice);
        TestTrue(TEXT("a stranger cannot answer the deduction"),
            DecideChoice(Record, TEXT("nobody"), TEXT("foto_vidmar_light_table"), Frame37, TEXT("the_place"), Next).Reason == EMarburgumInteractionReason::UnknownChoice);
    }
    // Vera without the yard's photograph: the chronology is refused and names what is missing.
    Step.Talk(TEXT("drago_potocnik"), EMarburgumInteractionEffect::StageCompleted, TEXT("Drago"));
    Step.Talk(TEXT("ivana_zupan"), EMarburgumInteractionEffect::StageCompleted, TEXT("Ivana"));
    Step.Talk(TEXT("block_13_storage_cage"), EMarburgumInteractionEffect::StageCompleted, TEXT("the cage"));
    Step.Shoot(TEXT("zupan_contact_sheet_copy"), EMarburgumInteractionEffect::StageCompleted, TEXT("the sheet"));
    PlayFrameThirtySeven(Step);
    Step.Talk(TEXT("nika_vidmar"), EMarburgumInteractionEffect::StageCompleted, TEXT("the print matches"));
    {
        FMarburgumWorldRecord Next;
        const FMarburgumMissionDecision Refused = DecideInteraction(Record, TEXT("hana"), TEXT("vera_horvat"), Next);
        TestTrue(TEXT("Vera wants the yard's photograph"), !Refused.bApplied && Refused.Reason == EMarburgumInteractionReason::RequirementNotMet
            && Refused.Item == TEXT("photo_melje_date_board"));
    }
    return true;
}

bool FMarburgumNegativePersistenceTest::RunTest(const FString& Parameters)
{
    // Mid-chapter on the left_alone route, with a unique original in hand, the deduction answered and a discovery made:
    // all of it survives a save and a reload (a server restart), and play goes on from exactly there.
    FMarburgumWorldRecord Record;
    Prepare(Record, TEXT("iza"), TEXT("left_alone"));
    FStep Step{ *this, Record, TEXT("iza") };
    PlayToTheStrip(Step, true);
    Step.Talk(TEXT("kiosk_67_customer"), EMarburgumInteractionEffect::StageCompleted, TEXT("Albin refuses"));

    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), FGuid::NewGuid().ToString());
    const FString Path = FPaths::Combine(Directory, TEXT("store.json"));
    FString Error;
    {
        FMarburgumWorldStore Store(Path);
        TestTrue(TEXT("empty world"), Store.Load(Error));
        TestTrue(TEXT("commit"), Store.Commit(Record, Error));
    }
    FMarburgumWorldRecord Loaded;
    {
        FMarburgumWorldStore Store(Path);
        TestTrue(TEXT("reload"), Store.Load(Error));
        TestEqual(TEXT("schema unchanged"), Store.GetLoadedSchema(), MarburgumII::Gameplay::WorldStoreSchemaVersion);
        Loaded = Store.Get();
    }
    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    FStep After{ *this, Loaded, TEXT("iza") };
    TestEqual(TEXT("stage kept"), After.Stage(Negative), FString(TEXT("match_day_circle")));
    TestTrue(TEXT("side story kept"), After.Completed(Frame37));
    TestEqual(TEXT("the original kept"), After.Held(TEXT("zupan_contact_sheet")), 1);
    TestEqual(TEXT("the strip kept"), After.Held(TEXT("torn_strip_print")), 1);
    TestEqual(TEXT("discoveries kept"), Loaded.Discoveries.FindRef(TEXT("iza")).Num(), 3);
    TestEqual(TEXT("XP kept"), After.Xp(TEXT("photography")), Step.Xp(TEXT("photography")));
    const TMap<FString, FMarburgumStoredMission> Kept = Loaded.Missions.FindRef(TEXT("iza"));
    const FMarburgumStoredMission* Chapter = Kept.Find(Negative);
    TestTrue(TEXT("the chronology flag kept"), Chapter && Chapter->Flags.FindRef(TEXT("kainach_after_december")));
    const FMarburgumStoredMission* Side = Kept.Find(Frame37);
    TestTrue(TEXT("the deduction's outcome kept"), Side && Side->Outcome == TEXT("scrap_in_lab") && Side->Choice == TEXT("lab_safe"));
    // And the route continues from the reloaded world.
    After.Talk(TEXT("viole_terrace_regular"), EMarburgumInteractionEffect::StageCompleted, TEXT("after the restart: the circle"));
    After.Talk(TEXT("kiosk_67_customer"), EMarburgumInteractionEffect::StageCompleted, TEXT("after the restart: Albin"));
    PlayTheTicket(After);
    return true;
}

bool FMarburgumConditionalStagesTest::RunTest(const FString& Parameters)
{
    using namespace MarburgumII::Missions;
    const MarburgumII::Gameplay::FMissionDefinition* Chapter = MarburgumII::Gameplay::FindMission(Negative);
    if (!TestNotNull(TEXT("Chapter VI registered"), Chapter))
    {
        return false;
    }
    const MarburgumII::Gameplay::FMissionStageDefinition* Strip = MarburgumII::Gameplay::FindStage(*Chapter, TEXT("glas_full_frame"));
    if (!TestNotNull(TEXT("the strip stage"), Strip))
    {
        return false;
    }
    const TPair<const TCHAR*, const TCHAR*> Routes[] = {
        { TEXT("temporary_work"), TEXT("albin_willing") }, { TEXT("tab_paid"), TEXT("albin_hesitates") }, { TEXT("debt_erased"), TEXT("albin_hesitates") },
        { TEXT("object_sold"), TEXT("albin_stamp") }, { TEXT("left_alone"), TEXT("albin_refuses") },
    };
    for (const TPair<const TCHAR*, const TCHAR*>& Route : Routes)
    {
        TMap<FString, FProgressView> Views;
        Views.Add(Kiosk67, { TEXT("resolution"), true, Route.Key });
        const auto Find = [&Views](const FString& Id) { return Views.Find(Id); };
        const MarburgumII::Gameplay::FMissionStageDefinition* Next = NextStage(*Chapter, *Strip, Find);
        TestTrue(FString::Printf(TEXT("%s leads to %s"), Route.Key, Route.Value), Next && FString(Next->Id) == Route.Value);
        // Walk the route to the end: it reaches the ticket, through only stages that apply.
        int32 Steps = 0;
        while (Next && FString(Next->Id) != TEXT("the_ticket") && Steps++ < 10)
        {
            TestTrue(FString::Printf(TEXT("%s: %s applies"), Route.Key, Next->Id), StageApplies(*Next, Find));
            Next = NextStage(*Chapter, *Next, Find);
        }
        TestTrue(FString::Printf(TEXT("%s reaches the ticket"), Route.Key), Next && FString(Next->Id) == TEXT("the_ticket"));
    }
    // Unfinished, or an outcome nobody lists: no conditional stage applies, and the first stage never is one.
    {
        TMap<FString, FProgressView> Views;
        Views.Add(Kiosk67, { TEXT("hospital"), false, TEXT("") });
        const auto Find = [&Views](const FString& Id) { return Views.Find(Id); };
        const MarburgumII::Gameplay::FMissionStageDefinition* Next = NextStage(*Chapter, *Strip, Find);
        TestTrue(TEXT("an unfinished gate applies no branch"), Next && FString(Next->Id) == TEXT("the_ticket"));
        TestTrue(TEXT("the first stage is unconditional"), FirstStage(*Chapter, Find) == &Chapter->Stages[0]);
    }
    return true;
}

#endif
