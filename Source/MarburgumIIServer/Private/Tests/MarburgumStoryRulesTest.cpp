#include "MarburgumMissionRules.h"

#include "MarburgumGameplayContract.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumVoicesOnTapeTest,
    "Marburgum.Server.Story.VoicesOnTape",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumOutcomeGateTest,
    "Marburgum.Server.Story.OutcomeGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumPhotographyRulesTest,
    "Marburgum.Server.Story.Photography",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    using MarburgumII::Missions::DecideChoice;
    using MarburgumII::Missions::DecideInteraction;
    using MarburgumII::Missions::DecidePhotograph;

    /** A character who has finished Chapters I to III, Chapter I in Outcome. */
    FMarburgumWorldRecord AfterChapterThree(const FString& Character, const FString& ChapterOneOutcome)
    {
        FMarburgumWorldRecord Record;
        Record.Revision = 40;
        for (const TCHAR* Done : { TEXT("lent_first_catch"), TEXT("raeda_names_no_longer_match"), TEXT("raeda_last_shift"), TEXT("raeda_city_for_sale") })
        {
            FMarburgumStoredMission& Mission = Record.Missions.FindOrAdd(Character).Add(Done);
            const MarburgumII::Gameplay::FMissionDefinition* Definition = MarburgumII::Gameplay::FindMission(Done);
            Mission.Stage = Definition ? Definition->Stages[Definition->StageCount - 1].Id : TEXT("");
            Mission.bCompleted = true;
            Mission.bRewardPaid = true;
        }
        Record.Missions.FindOrAdd(Character).FindOrAdd(TEXT("raeda_names_no_longer_match")).Outcome = ChapterOneOutcome;
        return Record;
    }

    bool Talk(FAutomationTestBase& Test, FMarburgumWorldRecord& Record, const FString& Character, const FString& Target,
        EMarburgumInteractionEffect Expected, const TCHAR* What)
    {
        FMarburgumWorldRecord Next;
        const FMarburgumMissionDecision Decision = DecideInteraction(Record, Character, Target, Next);
        Test.TestTrue(FString::Printf(TEXT("%s applied"), What), Decision.bApplied);
        Test.TestTrue(FString::Printf(TEXT("%s effect"), What), Decision.Effect == Expected);
        if (Decision.bApplied)
        {
            Test.TestEqual(FString::Printf(TEXT("%s: one revision"), What), Next.Revision, Record.Revision + 1);
            Record = Next;
        }
        return Decision.bApplied;
    }

    bool Shoot(FAutomationTestBase& Test, FMarburgumWorldRecord& Record, const FString& Character, const FString& Subject,
        EMarburgumInteractionEffect Expected, const TCHAR* What)
    {
        FMarburgumWorldRecord Next;
        const FMarburgumMissionDecision Decision = DecidePhotograph(Record, Character, Subject, Next);
        Test.TestTrue(FString::Printf(TEXT("%s applied"), What), Decision.bApplied);
        Test.TestTrue(FString::Printf(TEXT("%s effect"), What), Decision.Effect == Expected);
        if (Decision.bApplied)
        {
            Record = Next;
        }
        return Decision.bApplied;
    }

    const FMarburgumStoredMission* Progress(const FMarburgumWorldRecord& Record, const FString& Character, const TCHAR* Mission)
    {
        const TMap<FString, FMarburgumStoredMission>* Owned = Record.Missions.Find(Character);
        return Owned ? Owned->Find(Mission) : nullptr;
    }
}

bool FMarburgumVoicesOnTapeTest::RunTest(const FString& Parameters)
{
    const FString Ana(TEXT("ana"));
    const TCHAR* Mission = TEXT("raeda_voices_on_tape");
    FMarburgumWorldRecord Record = AfterChapterThree(Ana, TEXT("maja_informed"));

    // Before Chapter III is done, Vera has nothing new: the chapter waits on it.
    {
        FMarburgumWorldRecord Early = Record;
        Early.Missions.FindOrAdd(Ana).Remove(TEXT("raeda_city_for_sale"));
        FMarburgumWorldRecord Next;
        const FMarburgumMissionDecision Waiting = DecideInteraction(Early, Ana, TEXT("vera_horvat"), Next);
        TestFalse(TEXT("not offered before Chapter III"), Waiting.bApplied && Waiting.Mission == Mission);
    }

    // Vera offers Chapter IV; the household in Tabor comes first, Andrej has nothing until the tape exists.
    Talk(*this, Record, Ana, TEXT("vera_horvat"), EMarburgumInteractionEffect::MissionStarted, TEXT("Vera offers Voices on Tape"));
    TestEqual(TEXT("first stage"), Progress(Record, Ana, Mission)->Stage, FString(TEXT("find_recorder")));
    {
        FMarburgumWorldRecord Next;
        TestFalse(TEXT("Andrej is not the next step"), DecideInteraction(Record, Ana, TEXT("andrej_mlakar"), Next).bApplied);
    }
    Talk(*this, Record, Ana, TEXT("jozica_rozman"), EMarburgumInteractionEffect::StageCompleted, TEXT("the recorder"));
    TestEqual(TEXT("counter note handed over"), Record.Inventories.FindRef(Ana).FindRef(TEXT("raeda_recorder_counter_note")), 1);
    Talk(*this, Record, Ana, TEXT("drago_potocnik"), EMarburgumInteractionEffect::StageCompleted, TEXT("Drago names Tone"));
    Talk(*this, Record, Ana, TEXT("tone_vogrin"), EMarburgumInteractionEffect::StageCompleted, TEXT("Tone's cassette"));
    TestEqual(TEXT("cassette in custody"), Record.Inventories.FindRef(Ana).FindRef(TEXT("canteen_cassette_c60")), 1);

    // Andrej needs the cassette in hand: without it the step is refused and nothing changes.
    {
        FMarburgumWorldRecord Without = Record;
        Without.Inventories.FindOrAdd(Ana).Remove(TEXT("canteen_cassette_c60"));
        FMarburgumWorldRecord Next;
        const FMarburgumMissionDecision Refused = DecideInteraction(Without, Ana, TEXT("andrej_mlakar"), Next);
        TestFalse(TEXT("no cassette, no listening"), Refused.bApplied);
        TestTrue(TEXT("requirement named"), Refused.Reason == EMarburgumInteractionReason::RequirementNotMet);
    }
    Talk(*this, Record, Ana, TEXT("andrej_mlakar"), EMarburgumInteractionEffect::StageCompleted, TEXT("Andrej listens"));
    TestEqual(TEXT("the cassette stays (evidence)"), Record.Inventories.FindRef(Ana).FindRef(TEXT("canteen_cassette_c60")), 1);
    TestEqual(TEXT("tape notes"), Record.Inventories.FindRef(Ana).FindRef(TEXT("canteen_tape_notes")), 1);
    Talk(*this, Record, Ana, TEXT("vera_horvat"), EMarburgumInteractionEffect::StageCompleted, TEXT("Vera places the room"));
    TestTrue(TEXT("the canteen flag"), Progress(Record, Ana, Mission)->Flags.FindRef(TEXT("canteen_tape_place_melje")));

    // Tone: a decision is due, and both answers close the chapter.
    {
        FMarburgumWorldRecord Next;
        const FMarburgumMissionDecision Due = DecideInteraction(Record, Ana, TEXT("tone_vogrin"), Next);
        TestTrue(TEXT("decision due at Tone"), Due.Reason == EMarburgumInteractionReason::ChoiceRequired);
    }
    const int64 MoneyBefore = Record.Money.FindRef(Ana);
    for (const TPair<const TCHAR*, int64> Answer : { TPair<const TCHAR*, int64>(TEXT("ask_tone"), 10), TPair<const TCHAR*, int64>(TEXT("spare_tone"), 15) })
    {
        FMarburgumWorldRecord Next;
        const FMarburgumMissionDecision Decision = DecideChoice(Record, Ana, TEXT("tone_vogrin"), Mission, Answer.Key, Next);
        TestTrue(FString::Printf(TEXT("%s completes"), Answer.Key), Decision.bApplied && Decision.Effect == EMarburgumInteractionEffect::MissionCompleted);
        TestEqual(FString::Printf(TEXT("%s reward"), Answer.Key), Next.Money.FindRef(Ana), MoneyBefore + 60);
        TestEqual(FString::Printf(TEXT("%s reputation"), Answer.Key), Next.Skills.FindRef(Ana).FindRef(TEXT("reputation")), Answer.Value);
        const FMarburgumStoredMission* Done = Progress(Next, Ana, Mission);
        TestTrue(FString::Printf(TEXT("%s journal"), Answer.Key), Done && Done->bCompleted && Done->JournalEntry == TEXT("blue_ledger_voices_on_tape"));
    }
    return true;
}

bool FMarburgumOutcomeGateTest::RunTest(const FString& Parameters)
{
    const FString Bo(TEXT("bo"));
    const TCHAR* Folder = TEXT("raeda_mentor_folder");
    const auto WithChapterFour = [&Bo](const TCHAR* ChapterOne)
    {
        FMarburgumWorldRecord Record = AfterChapterThree(Bo, ChapterOne);
        FMarburgumStoredMission& Four = Record.Missions.FindOrAdd(Bo).Add(TEXT("raeda_voices_on_tape"));
        Four.Stage = TEXT("tone_question");
        Four.bCompleted = true;
        Four.Outcome = TEXT("tone_spared");
        return Record;
    };

    // Told Maja in Chapter I: she offers her mentor's folder after Chapter IV.
    {
        FMarburgumWorldRecord Record = WithChapterFour(TEXT("maja_informed"));
        Talk(*this, Record, Bo, TEXT("maja_koren"), EMarburgumInteractionEffect::MissionStarted, TEXT("folder offered"));
        TestEqual(TEXT("folder mission"), Progress(Record, Bo, Folder) ? Progress(Record, Bo, Folder)->Stage : FString(), FString(TEXT("collect_folder")));
        Talk(*this, Record, Bo, TEXT("mariborski_glas_archive"), EMarburgumInteractionEffect::StageCompleted, TEXT("folder collected"));
        Talk(*this, Record, Bo, TEXT("maja_koren"), EMarburgumInteractionEffect::MissionCompleted, TEXT("Maja reads"));
        TestTrue(TEXT("flag"), Progress(Record, Bo, Folder)->Flags.FindRef(TEXT("mentor_question_found")));
    }
    // Kept it private: the door is closed for good, without a refusal to explain.
    {
        FMarburgumWorldRecord Record = WithChapterFour(TEXT("investigation_private"));
        FMarburgumWorldRecord Next;
        const FMarburgumMissionDecision Decision = DecideInteraction(Record, Bo, TEXT("maja_koren"), Next);
        TestFalse(TEXT("no folder for the private route"), Decision.bApplied);
        TestTrue(TEXT("nothing to do, not a prerequisite"), Decision.Reason == EMarburgumInteractionReason::NothingToDo);
        TestNull(TEXT("never started"), Progress(Record, Bo, Folder));
    }
    // The gate reads the outcome only once Chapter I is finished.
    {
        FMarburgumWorldRecord Record = WithChapterFour(TEXT(""));
        Record.Missions.FindOrAdd(Bo).FindOrAdd(TEXT("raeda_names_no_longer_match")).bCompleted = false;
        FMarburgumWorldRecord Next;
        TestFalse(TEXT("unfinished Chapter I offers nothing"), DecideInteraction(Record, Bo, TEXT("maja_koren"), Next).bApplied);
    }
    return true;
}

bool FMarburgumPhotographyRulesTest::RunTest(const FString& Parameters)
{
    using namespace MarburgumII::Gameplay;
    const FString Eva(TEXT("eva"));
    FMarburgumWorldRecord Record;
    Record.Revision = 3;

    // No camera, no photograph: refused before anything is decided.
    {
        FMarburgumWorldRecord Next;
        const FMarburgumMissionDecision Refused = DecidePhotograph(Record, Eva, TEXT("vodni_stolp"), Next);
        TestTrue(TEXT("no camera"), !Refused.bApplied && Refused.Reason == EMarburgumInteractionReason::NoCamera);
        TestTrue(TEXT("unknown subject"), DecidePhotograph(Record, Eva, TEXT("nowhere"), Next).Reason == EMarburgumInteractionReason::UnknownTarget);
    }

    // Nika lends the camera with her prints; the first stage asks for the Water Tower.
    Talk(*this, Record, Eva, TEXT("nika_vidmar"), EMarburgumInteractionEffect::MissionStarted, TEXT("Nika lends a camera"));
    TestEqual(TEXT("camera"), Record.Inventories.FindRef(Eva).FindRef(CameraItem), 1);
    // Talking at Nika does not complete a photograph stage.
    {
        FMarburgumWorldRecord Next;
        TestFalse(TEXT("a photograph is not a conversation"), DecideInteraction(Record, Eva, TEXT("nika_vidmar"), Next).bApplied);
    }

    // A subject outside the mission: print and first-photograph XP, nothing else.
    const FPhotoSubjectDefinition* Synagogue = FindPhotoSubject(TEXT("sinagoga"));
    TestNotNull(TEXT("synagogue registered"), Synagogue);
    Shoot(*this, Record, Eva, TEXT("sinagoga"), EMarburgumInteractionEffect::Photographed, TEXT("synagogue"));
    TestEqual(TEXT("print"), Record.Inventories.FindRef(Eva).FindRef(TEXT("photo_sinagoga")), 1);
    TestEqual(TEXT("first XP"), Record.Skills.FindRef(Eva).FindRef(PhotographySkill), int64(Synagogue ? Synagogue->Xp : 0));
    TestTrue(TEXT("remembered"), Record.Discoveries.FindRef(Eva).Contains(TEXT("sinagoga")));
    // The same again, print still held: refused, nothing changes.
    {
        FMarburgumWorldRecord Next;
        const FMarburgumMissionDecision Again = DecidePhotograph(Record, Eva, TEXT("sinagoga"), Next);
        TestTrue(TEXT("already photographed"), !Again.bApplied && Again.Reason == EMarburgumInteractionReason::AlreadyPhotographed);
    }
    // Lost the print: a new one, but no second XP.
    {
        FMarburgumWorldRecord Without = Record;
        Without.Inventories.FindOrAdd(Eva).Remove(TEXT("photo_sinagoga"));
        FMarburgumWorldRecord Next;
        const FMarburgumMissionDecision Retake = DecidePhotograph(Without, Eva, TEXT("sinagoga"), Next);
        TestTrue(TEXT("retake applied"), Retake.bApplied && Retake.SkillXp.IsEmpty());
        TestEqual(TEXT("retake print"), Next.Inventories.FindRef(Eva).FindRef(TEXT("photo_sinagoga")), 1);
        TestEqual(TEXT("XP unchanged"), Next.Skills.FindRef(Eva).FindRef(PhotographySkill), Record.Skills.FindRef(Eva).FindRef(PhotographySkill));
    }

    // The mission's subjects complete its stages in order; out of order is only a photograph.
    Shoot(*this, Record, Eva, TEXT("stari_most"), EMarburgumInteractionEffect::Photographed, TEXT("bridge out of order"));
    TestEqual(TEXT("still at the Water Tower"), Progress(Record, Eva, TEXT("nika_then_and_now"))->Stage, FString(TEXT("shoot_vodni_stolp")));
    Shoot(*this, Record, Eva, TEXT("vodni_stolp"), EMarburgumInteractionEffect::StageCompleted, TEXT("Water Tower"));
    Shoot(*this, Record, Eva, TEXT("sodni_stolp"), EMarburgumInteractionEffect::StageCompleted, TEXT("Judgement Tower"));
    // The bridge print is already held, but the stage still asks for a photograph of it: it completes, no new XP.
    const int64 BeforeBridge = Record.Skills.FindRef(Eva).FindRef(PhotographySkill);
    Shoot(*this, Record, Eva, TEXT("stari_most"), EMarburgumInteractionEffect::StageCompleted, TEXT("bridge in order"));
    TestEqual(TEXT("no second bridge XP"), Record.Skills.FindRef(Eva).FindRef(PhotographySkill), BeforeBridge);
    TestEqual(TEXT("one bridge print"), Record.Inventories.FindRef(Eva).FindRef(TEXT("photo_stari_most")), 1);
    Talk(*this, Record, Eva, TEXT("nika_vidmar"), EMarburgumInteractionEffect::StageCompleted, TEXT("prints shown"));
    {
        FMarburgumWorldRecord Next;
        const FMarburgumMissionDecision Darkroom = DecideChoice(Record, Eva, TEXT("nika_vidmar"), TEXT("nika_then_and_now"), TEXT("darkroom_lesson"), Next);
        TestTrue(TEXT("darkroom completes"), Darkroom.bApplied && Darkroom.Effect == EMarburgumInteractionEffect::MissionCompleted);
        TestEqual(TEXT("darkroom XP"), Next.Skills.FindRef(Eva).FindRef(PhotographySkill), Record.Skills.FindRef(Eva).FindRef(PhotographySkill) + 40);
        TestEqual(TEXT("camera kept"), Next.Inventories.FindRef(Eva).FindRef(CameraItem), 1);
        Record = Next;
    }

    // Brane needs Nika's lesson first, then three photographs, one of them at the Melje yard.
    Talk(*this, Record, Eva, TEXT("brane_sign_painter"), EMarburgumInteractionEffect::MissionStarted, TEXT("Brane's signs"));
    Shoot(*this, Record, Eva, TEXT("kvik_lettering"), EMarburgumInteractionEffect::StageCompleted, TEXT("Kvik"));
    Shoot(*this, Record, Eva, TEXT("velika_kavarna_lettering"), EMarburgumInteractionEffect::StageCompleted, TEXT("Velika kavarna"));
    Shoot(*this, Record, Eva, TEXT("prehod_sign_melje"), EMarburgumInteractionEffect::StageCompleted, TEXT("Prehod sign"));
    Talk(*this, Record, Eva, TEXT("brane_sign_painter"), EMarburgumInteractionEffect::StageCompleted, TEXT("Brane sees the error"));
    TestTrue(TEXT("spacing error documented"), Progress(Record, Eva, TEXT("brane_old_letters"))->Flags.FindRef(TEXT("prehod_spacing_error_documented")));
    TestEqual(TEXT("seven subjects documented"), Record.Discoveries.FindRef(Eva).Num(), 7);
    return true;
}

#endif
