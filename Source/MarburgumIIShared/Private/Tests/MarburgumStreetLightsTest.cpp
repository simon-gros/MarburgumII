#include "MarburgumStreetLights.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// The street lamps (ADR-065): the file tools/lent-ground writes, and when each lamp burns.

namespace
{
    using namespace MarburgumII;

    constexpr EAutomationTestFlags LightsTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
        | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumStreetLightsSwitchTest, "Marburgum.Shared.StreetLights.Switch", LightsTestFlags)
bool FMarburgumStreetLightsSwitchTest::RunTest(const FString& Parameters)
{
    using namespace StreetLights;

    // Solstice: the sun sets at 18:57 UTC (20:57 in Maribor). Still up at 18:30, the street dark at 19:40.
    TestFalse(TEXT("June, sun 3 degrees up: off"), OutputOf(SkyAt(FDateTime(2026, 6, 21, 18, 30)), 0).IsOn());
    const FOutput June = OutputOf(SkyAt(FDateTime(2026, 6, 21, 19, 40)), 0);
    TestTrue(TEXT("June, 40 minutes after sunset: on"), June.IsOn());
    TestEqual(TEXT("and long warm"), June.Output, 1.0f);
    TestEqual(TEXT("orange"), June.Kelvin, WarmKelvin);
    TestFalse(TEXT("noon: off"), OutputOf(SkyAt(FDateTime(2026, 6, 21, 10, 57)), 0).IsOn());

    // The November dusk of the daylight smoke: the sun 4.5 degrees under at 15:50 UTC, some minutes past every lamp's
    // threshold, so every lamp burns and all are warm or nearly.
    int32 OnAtDusk = 0;
    for (int32 Lamp = 0; Lamp < 200; ++Lamp)
    {
        const FOutput Dusk = OutputOf(SkyAt(FDateTime(2026, 11, 13, 15, 50)), Lamp);
        OnAtDusk += Dusk.IsOn() ? 1 : 0;
        TestTrue(TEXT("dusk: past the warm-up's middle"), Dusk.Output > 0.5f);
    }
    TestEqual(TEXT("November dusk: every lamp on"), OnAtDusk, 200);
    TestTrue(TEXT("the moonless night: on"), OutputOf(SkyAt(FDateTime(2026, 11, 13, 21, 0)), 7).IsOn());
    TestTrue(TEXT("Christmas midnight: on"), OutputOf(SkyAt(FDateTime(2026, 12, 24, 23, 0)), 7).IsOn());

    // The sky reads the right way round.
    TestTrue(TEXT("the evening sun sets"), SkyAt(FDateTime(2026, 6, 21, 18, 30)).SunRateDegPerSecond < 0.0);
    TestTrue(TEXT("the morning sun rises"), SkyAt(FDateTime(2026, 6, 21, 4, 0)).SunRateDegPerSecond > 0.0);

    // Weather: a closed sky and fog bring the lamps on with the sun still up.
    FSky Low;
    Low.SunElevationDeg = 0.5;
    Low.SunRateDegPerSecond = -0.003;
    TestEqual(TEXT("clear threshold"), SwitchOnDeg(Low), ClearSwitchOnDeg);
    TestFalse(TEXT("clear, sun just up: off"), OutputOf(Low, 0).IsOn());
    FSky Overcast = Low;
    Overcast.CloudCover = 1.0;
    TestEqual(TEXT("overcast threshold"), SwitchOnDeg(Overcast), ClearSwitchOnDeg + OvercastEarlierDeg);
    TestTrue(TEXT("overcast, sun just up: on"), OutputOf(Overcast, 0).IsOn());
    FSky Broken = Low;
    Broken.CloudCover = 0.4;
    TestTrue(TEXT("broken cloud hardly counts"), SwitchOnDeg(Broken) < ClearSwitchOnDeg + 0.2 * OvercastEarlierDeg);
    FSky Black = Overcast;
    Black.Fog = 1.0;
    Black.Thunder = 1.0;
    TestTrue(TEXT("never with the sun high"), SwitchOnDeg(Black) <= 6.0);

    // Photocells: fixed per lamp, within the spread, and not all alike.
    TestEqual(TEXT("fixed"), PhotocellOffsetDeg(41), PhotocellOffsetDeg(41));
    double Lowest = 1e9, Highest = -1e9;
    for (int32 Lamp = 0; Lamp < 1000; ++Lamp)
    {
        const double Offset = PhotocellOffsetDeg(Lamp);
        Lowest = FMath::Min(Lowest, Offset);
        Highest = FMath::Max(Highest, Offset);
    }
    TestTrue(TEXT("within the spread"), Lowest >= -PhotocellSpreadDeg && Highest <= PhotocellSpreadDeg);
    TestTrue(TEXT("spread over it"), Highest - Lowest > PhotocellSpreadDeg);

    // A lamp just struck is dim and pale, and warms to orange over WarmUpSeconds.
    FSky Struck;
    Struck.SunRateDegPerSecond = -0.003;
    const double Threshold = ClearSwitchOnDeg + PhotocellOffsetDeg(3);
    Struck.SunElevationDeg = Threshold - 0.003 * 1.0;
    const FOutput First = OutputOf(Struck, 3);
    TestTrue(TEXT("one second in: on"), First.IsOn());
    TestTrue(TEXT("one second in: near the strike"), First.Output < StrikeOutput + 0.01f);
    TestTrue(TEXT("one second in: pale"), First.Kelvin > 3000.0f);
    Struck.SunElevationDeg = Threshold - 0.003 * WarmUpSeconds * 0.5;
    const FOutput Half = OutputOf(Struck, 3);
    TestTrue(TEXT("half way: brighter"), Half.Output > First.Output && Half.Output < 1.0f);
    TestTrue(TEXT("half way: warmer"), Half.Kelvin < First.Kelvin && Half.Kelvin > WarmKelvin);
    TestTrue(TEXT("clock seconds since it struck"), FMath::IsNearlyEqual(Half.OnSeconds, WarmUpSeconds * 0.5, 0.01));
    Struck.SunElevationDeg = Threshold - 0.003 * WarmUpSeconds * 1.5;
    TestEqual(TEXT("warm"), OutputOf(Struck, 3).Output, 1.0f);

    // Morning: a photocell lets go a little later than it caught, so a lamp at its threshold does not flicker.
    FSky Dawn;
    Dawn.SunRateDegPerSecond = 0.003;
    Dawn.SunElevationDeg = Threshold + MorningLaterDeg * 0.5;
    TestTrue(TEXT("dawn, just over the evening threshold: still on"), OutputOf(Dawn, 3).IsOn());
    TestEqual(TEXT("and warm from the night"), OutputOf(Dawn, 3).Output, 1.0f);
    Dawn.SunRateDegPerSecond = -0.003;
    TestFalse(TEXT("the same sun in the evening: not yet"), OutputOf(Dawn, 3).IsOn());
    Dawn.SunRateDegPerSecond = 0.003;
    Dawn.SunElevationDeg = Threshold + MorningLaterDeg + 0.01;
    TestFalse(TEXT("dawn, past it: off"), OutputOf(Dawn, 3).IsOn());

    // The compressed light: about 0.6 lux on the pavement under a mast, 0.45 under a lantern, a wall beside either at
    // most about two and a half times its pavement, and nothing at the reach.
    const float UnderMast = LuxAt(EKind::Mast, MastLightHeightCm);
    const float UnderLantern = LuxAt(EKind::Lantern, LanternLightHeightCm);
    TestTrue(TEXT("under a mast: about 0.6 lux"), FMath::IsNearlyEqual(UnderMast, 0.6f, 0.08f));
    TestTrue(TEXT("under a lantern: about 0.45 lux"), FMath::IsNearlyEqual(UnderLantern, 0.45f, 0.06f));
    TestTrue(TEXT("both above the full moon's 0.41"), UnderMast > 0.41f && UnderLantern > 0.41f);
    TestTrue(TEXT("a wall 1.5 m from a mast: under three times its pavement"), LuxAt(EKind::Mast, 150.0f) < 3.0f * UnderMast);
    TestTrue(TEXT("a wall 1 m from a lantern: under twice its pavement"), LuxAt(EKind::Lantern, 100.0f) < 2.0f * UnderLantern);
    TestEqual(TEXT("nothing at a mast's reach"), LuxAt(EKind::Mast, MastReachLightCm), 0.0f);
    TestEqual(TEXT("a lamp just struck gives its share"), LuxAt(EKind::Lantern, 0.0f, StrikeOutput), LanternPeakLux * StrikeOutput);

    FSky Unknown;
    Unknown.SunElevationDeg = std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("an unknown sky lights nothing"), OutputOf(Unknown, 0).IsOn());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumStreetLightsFileTest, "Marburgum.Shared.StreetLights.File", LightsTestFlags)
bool FMarburgumStreetLightsFileTest::RunTest(const FString& Parameters)
{
    using namespace StreetLights;

    TArray<FLamp> Lamps;
    FString Error;
    const FString Two = TEXT(R"({"format":"marburgumii.street_lights","version":1,"lamps":[[10.5,-20.25,3.1,0,90.0,1],[0,0,0,1,0,3]]})");
    TestTrue(TEXT("a small file reads"), Parse(Two, Lamps, Error));
    if (TestEqual(TEXT("two lamps"), Lamps.Num(), 2))
    {
        TestEqual(TEXT("plan east is Unreal Y"), Lamps[0].Location.Y, 1050.0);
        TestEqual(TEXT("plan south is Unreal -X"), Lamps[0].Location.X, 2025.0);
        TestEqual(TEXT("the top is Z"), Lamps[0].Location.Z, 310.0);
        TestEqual(TEXT("a mast"), Lamps[0].Kind, EKind::Mast);
        TestEqual(TEXT("plan 90 (south) is Unreal 180"), Lamps[0].Yaw, 180.0f);
        TestEqual(TEXT("a lantern"), Lamps[1].Kind, EKind::Lantern);
        TestEqual(TEXT("round a square"), Lamps[1].Source, ESource::Square);
        TestEqual(TEXT("plan 0 (east) is Unreal 90"), Lamps[1].Yaw, 90.0f);
        // The mast's light hangs under its head, reaching south 1.5 m from the post.
        const FVector Light = LightPoint(Lamps[0]);
        TestTrue(TEXT("the mast's light is out over the road"), FVector::Dist2D(Light, Lamps[0].Location + FVector(-MastReachCm, 0.0, 0.0)) < 0.01);
        TestEqual(TEXT("and under the head"), Light.Z, 310.0 + MastLightHeightCm);
    }
    TestFalse(TEXT("a wrong format is refused"), Parse(TEXT(R"({"format":"marburgumii.walk_graph","version":1,"lamps":[]})"), Lamps, Error));
    TestFalse(TEXT("a bad kind is refused"), Parse(TEXT(R"({"format":"marburgumii.street_lights","version":1,"lamps":[[0,0,0,7,0,0]]})"), Lamps, Error));
    TestFalse(TEXT("a short row is refused"), Parse(TEXT(R"({"format":"marburgumii.street_lights","version":1,"lamps":[[0,0,0,0,0]]})"), Lamps, Error));

    // The shipped file.
    FString Text;
    const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), LampsFile);
    if (!TestTrue(TEXT("Content/Marburgum/Lights/lent_street_lights.json exists"), FFileHelper::LoadFileToString(Text, *Path)))
    {
        return false;
    }
    TestTrue(TEXT("and reads"), Parse(Text, Lamps, Error));
    TestTrue(TEXT("Lent and its neighbours are lit by a thousand lamps or more"), Lamps.Num() >= 1000);
    int32 Masts = 0;
    for (const FLamp& Lamp : Lamps)
    {
        Masts += Lamp.Kind == EKind::Mast ? 1 : 0;
        if (FMath::Abs(Lamp.Location.X) > 128000.0 || FMath::Abs(Lamp.Location.Y) > 128000.0)
        {
            AddError(FString::Printf(TEXT("a lamp outside the square at %s"), *Lamp.Location.ToString()));
            break;
        }
    }
    TestTrue(TEXT("masts and lanterns both"), Masts > 0 && Masts < Lamps.Num());
    return true;
}

// The lamps' ground worked out on the whole level (ADR-091), for clients that hold only the cells round their player.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumStreetLightsGroundTest, "Marburgum.Shared.StreetLights.Ground", LightsTestFlags)
bool FMarburgumStreetLightsGroundTest::RunTest(const FString& Parameters)
{
    using namespace StreetLights;

    const FString LampsText = TEXT(R"({"format":"marburgumii.street_lights","version":1,"lamps":[[0,0,3,0,0,1],[1,0,3,1,0,2]]})");
    const FString For = Fingerprint(LampsText);
    TestEqual(TEXT("the fingerprint is SHA-1 in hex"), For.Len(), 40);
    TestNotEqual(TEXT("and changes with the lamps file"), Fingerprint(LampsText + TEXT(" ")), For);

    TArray<TOptional<double>> Ground;
    FString Error;
    const FString Written = WriteGround(For, { 312.34, TOptional<double>() });
    TestTrue(TEXT("the written file reads back"), ParseGround(Written, For, 2, Ground, Error));
    if (TestEqual(TEXT("a height for each lamp"), Ground.Num(), 2))
    {
        TestTrue(TEXT("the first stands"), Ground[0].IsSet() && FMath::IsNearlyEqual(*Ground[0], 312.3));
        TestFalse(TEXT("the second has no ground"), Ground[1].IsSet());
    }
    TestFalse(TEXT("another lamps file's ground is refused"), ParseGround(Written, Fingerprint(TEXT("{}")), 2, Ground, Error));
    TestTrue(TEXT("and says so"), Error.StartsWith(TEXT("made for lamps file")));
    TestFalse(TEXT("a count that does not match is refused"), ParseGround(Written, For, 3, Ground, Error));
    TestTrue(TEXT("and leaves nothing behind"), Ground.IsEmpty());

    // The shipped file, once the editor has written it, belongs to the shipped lamps.
    FString Shipped;
    if (FFileHelper::LoadFileToString(Shipped, *FPaths::Combine(FPaths::ProjectContentDir(), GroundFile)))
    {
        FString Text;
        TArray<FLamp> Lamps;
        if (TestTrue(TEXT("the lamps file reads"), FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectContentDir(), LampsFile))
                && Parse(Text, Lamps, Error)))
        {
            const bool bMatches = ParseGround(Shipped, Fingerprint(Text), Lamps.Num(), Ground, Error);
            TestTrue(FString::Printf(TEXT("the ground file is the lamps file's (%s; scripts/partition-world.sh lamps)"), bMatches ? TEXT("yes") : *Error), bMatches);
        }
    }
    return true;
}

#endif
