#include "MarburgumGameplayContract.h"
#include "MarburgumNpcContract.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumAnglerCycleTest, "Marburgum.Shared.Npc.AnglerCycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext
        | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumPhotoCatalogueTest, "Marburgum.Shared.Photography.Catalogue",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext
        | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

bool FMarburgumAnglerCycleTest::RunTest(const FString& Parameters)
{
    using namespace MarburgumII::Npc;
    double Period = 0.0;
    for (const double Seconds : AnglerPhaseSeconds)
    {
        Period += Seconds;
    }
    // Seed 0 starts the cycle on the server clock's zero: the cast, the long wait, the reel, the hold.
    const struct
    {
        double At;
        EAnglerPhase Phase;
        double Elapsed;
        int64 Cycle;
    } Expected[] = {
        { 0.0, EAnglerPhase::Cast, 0.0, 0 },
        { 1.0, EAnglerPhase::Cast, 1.0, 0 },
        { FishCastSeconds + 0.5, EAnglerPhase::Wait, 0.5, 0 },
        { FishCastSeconds + 18.25, EAnglerPhase::Reel, 0.25, 0 },
        { FishCastSeconds + 24.5, EAnglerPhase::Hold, 0.5, 0 },
        { Period + 0.1, EAnglerPhase::Cast, 0.1, 1 },
        { -5.0, EAnglerPhase::Cast, 0.0, 0 },
    };
    for (const auto& Row : Expected)
    {
        const FAnglerMoment Moment = AnglerMomentAt(Row.At, 0);
        TestEqual(*FString::Printf(TEXT("phase at %.2f"), Row.At), FString(ToString(Moment.Phase)), FString(ToString(Row.Phase)));
        TestTrue(*FString::Printf(TEXT("elapsed at %.2f"), Row.At), FMath::IsNearlyEqual(Moment.Elapsed, Row.Elapsed, 1e-6));
        TestEqual(*FString::Printf(TEXT("cycle at %.2f"), Row.At), Moment.Cycle, Row.Cycle);
    }
    // A seed shifts the cycle by tenths of a second, the same on every machine.
    const FAnglerMoment Shifted = AnglerMomentAt(0.0, 5);
    TestTrue(TEXT("seed 5 is half a second in"), Shifted.Phase == EAnglerPhase::Cast && FMath::IsNearlyEqual(Shifted.Elapsed, 0.5, 1e-6));
    // Every moment of a cycle is inside its phase.
    for (double T = 0.0; T < 3.0 * Period; T += 0.37)
    {
        const FAnglerMoment Moment = AnglerMomentAt(T, 1234567u);
        const double Length = AnglerPhaseSeconds[static_cast<int32>(Moment.Phase)];
        if (Moment.Elapsed < 0.0 || Moment.Elapsed > Length)
        {
            AddError(FString::Printf(TEXT("elapsed %.3f outside %s at %.2f"), Moment.Elapsed, ToString(Moment.Phase), T));
            break;
        }
    }
    TestTrue(TEXT("the cast clip fits the cast phase"), FMath::IsNearlyEqual(AnglerPhaseSeconds[0], FishCastSeconds));
    return true;
}

bool FMarburgumPhotoCatalogueTest::RunTest(const FString& Parameters)
{
    using namespace MarburgumII::Gameplay;
    const FItemDefinition* Camera = FindItem(CameraItem);
    TestTrue(TEXT("the camera is a tool"), Camera && Camera->Kind == EItemKind::Tool);
    TestNotNull(TEXT("Photography is registered"), FindSkill(PhotographySkill));
    TSet<FString> Ids;
    for (const FPhotoSubjectDefinition& Subject : PhotoSubjects)
    {
        TestFalse(*FString::Printf(TEXT("%s listed once"), Subject.Id), Ids.Contains(Subject.Id));
        Ids.Add(Subject.Id);
        const FItemDefinition* Print = FindItem(Subject.Item);
        TestTrue(*FString::Printf(TEXT("%s print is a photo"), Subject.Id), Print && Print->Kind == EItemKind::Photo);
        TestTrue(*FString::Printf(TEXT("%s XP positive"), Subject.Id), Subject.Xp > 0);
        TestTrue(*FString::Printf(TEXT("%s range and cone sane"), Subject.Id), Subject.WithinCm > 0.f && Subject.ConeDegrees > 0.f && Subject.ConeDegrees <= 45.f);
        TestNull(*FString::Printf(TEXT("%s is not an interactable"), Subject.Id), FindInteractable(Subject.Id));
    }
    // Every Photograph stage names a registered subject.
    for (const FMissionDefinition& Mission : Missions)
    {
        for (int32 Index = 0; Index < Mission.StageCount; ++Index)
        {
            const FMissionStageDefinition& Stage = Mission.Stages[Index];
            if (Stage.Objective == EObjectiveType::Photograph)
            {
                TestNotNull(*FString::Printf(TEXT("%s/%s subject"), Mission.Id, Stage.Id), FindPhotoSubject(Stage.CompleteTarget));
            }
        }
    }
    return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumDayPostTest, "Marburgum.Shared.Npc.DayPosts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext
        | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumSloveneTextTest, "Marburgum.Shared.Gameplay.SloveneText",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext
        | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

bool FMarburgumDayPostTest::RunTest(const FString& Parameters)
{
    using namespace MarburgumII::Gameplay;
    const FDayPost Posts[] = { { 19 * 60, 7 * 60, 6.0, 0.0, TEXT("night") }, { 12 * 60, 13 * 60, 0.0, 4.0, TEXT("lunch") } };
    TestEqual(TEXT("morning at the authored spot"), DayPostAt(Posts, 2, 10 * 60), int32(INDEX_NONE));
    TestEqual(TEXT("lunch"), DayPostAt(Posts, 2, 12 * 60 + 30), 1);
    TestEqual(TEXT("lunch ends at 13:00"), DayPostAt(Posts, 2, 13 * 60), int32(INDEX_NONE));
    TestEqual(TEXT("evening, past midnight"), DayPostAt(Posts, 2, 23 * 60), 0);
    TestEqual(TEXT("after midnight"), DayPostAt(Posts, 2, 3 * 60), 0);
    TestEqual(TEXT("night ends at 07:00"), DayPostAt(Posts, 2, 7 * 60), int32(INDEX_NONE));
    // Authored people with a day are all within reach of their spot, and at it in the working morning (the smokes rely on it).
    for (const FInteractableDefinition& Entry : Interactables)
    {
        for (int32 Index = 0; Index < Entry.PostCount; ++Index)
        {
            const FDayPost& Post = Entry.Posts[Index];
            TestTrue(*FString::Printf(TEXT("%s post %d within 15 m"), Entry.Id, Index), FMath::Sqrt(Post.OffsetEastM * Post.OffsetEastM + Post.OffsetSouthM * Post.OffsetSouthM) <= 15.0);
        }
        TestEqual(*FString::Printf(TEXT("%s at the authored spot at 10:00"), Entry.Id), DayPostAt(Entry.Posts, Entry.PostCount, 10 * 60), int32(INDEX_NONE));
    }
    return true;
}

bool FMarburgumSloveneTextTest::RunTest(const FString& Parameters)
{
    using namespace MarburgumII::Gameplay;
    for (const FTextTranslation& Entry : SloveneText)
    {
        TestTrue(*FString::Printf(TEXT("Slovene for \"%s\""), Entry.English), Entry.Slovene && *Entry.Slovene);
    }
    const FString Before = FInternationalization::Get().GetCurrentLanguage()->GetName();
    FInternationalization::Get().SetCurrentLanguage(TEXT("en"));
    TestEqual(TEXT("English stays English"), FString(Localized(TEXT("Fishing rod"))), FString(TEXT("Fishing rod")));
    if (FInternationalization::Get().SetCurrentLanguage(TEXT("sl")))
    {
        TestEqual(TEXT("Slovene item"), FString(Localized(TEXT("Fishing rod"))), FString(TEXT("Ribiška palica")));
        TestEqual(TEXT("Slovene verb"), FString(Localized(TEXT("Talk"))), FString(TEXT("Govori")));
        TestEqual(TEXT("Unknown text unchanged"), FString(Localized(TEXT("not in the catalogue"))), FString(TEXT("not in the catalogue")));
    }
    else
    {
        AddWarning(TEXT("The sl culture is not available in this build; only the table was checked."));
    }
    FInternationalization::Get().SetCurrentLanguage(Before);
    return true;
}

#endif
