#include "MarburgumSkillCurve.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumSkillCurveTest, "Marburgum.Shared.SkillCurve",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext
        | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

bool FMarburgumSkillCurveTest::RunTest(const FString& Parameters)
{
    using namespace MarburgumII::SkillCurve;

    // The original's own checks (UnrealMarburgum MarburgumSkillCurveTest).
    TestEqual(TEXT("minimum level"), LevelForXp(-100), int64(0));
    TestEqual(TEXT("zero XP"), LevelForXp(0), int64(0));
    TestEqual(TEXT("first level costs 20"), XpToNextLevel(0), int64(20));
    TestEqual(TEXT("level one threshold"), TotalXpForLevel(1), int64(20));
    TestEqual(TEXT("19 XP is still level zero"), LevelForXp(19), int64(0));
    TestEqual(TEXT("20 XP reaches level one"), LevelForXp(20), int64(1));
    for (const int64 Level : { int64(0), int64(1), int64(25), int64(100), int64(995), MaxLevel - 1 })
    {
        const int64 Threshold = TotalXpForLevel(Level);
        TestEqual(*FString::Printf(TEXT("level %lld threshold round trip"), Level), LevelForXp(Threshold), Level);
        const int64 Cost = XpToNextLevel(Level);
        TestTrue(*FString::Printf(TEXT("level %lld cost positive"), Level), Cost > 0);
        TestEqual(*FString::Printf(TEXT("level %lld next threshold"), Level), TotalXpForLevel(Level + 1), Threshold + Cost);
        TestEqual(*FString::Printf(TEXT("level %lld progress start"), Level), XpIntoLevel(Threshold), int64(0));
    }
    TestEqual(TEXT("max level caps"), LevelForXp(MAX_int64), MaxLevel);
    TestEqual(TEXT("max has no next XP"), XpToNextLevel(MaxLevel), int64(0));

    // Golden values computed independently from the formula (exact integer arithmetic).
    TestEqual(TEXT("an exact half rounds to even: level 24 costs 28"), XpToNextLevel(24), int64(28));
    TestEqual(TEXT("level 99 costs 80"), XpToNextLevel(99), int64(80));
    TestEqual(TEXT("level 1989 costs 5346"), XpToNextLevel(1989), int64(5346));
    TestEqual(TEXT("level 10 at 208 XP"), TotalXpForLevel(10), int64(208));
    TestEqual(TEXT("level 100 at 4429 XP"), TotalXpForLevel(100), int64(4429));
    TestEqual(TEXT("level 1000 at 779900 XP"), TotalXpForLevel(1000), int64(779900));
    TestEqual(TEXT("level 1990 at 4282249 XP"), TotalXpForLevel(1990), int64(4282249));
    TestEqual(TEXT("one XP short of level 100"), LevelForXp(4428), int64(99));
    TestEqual(TEXT("progress inside level 10"), XpIntoLevel(208 + 7), int64(7));
    return true;
}

#endif
