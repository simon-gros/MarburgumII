#include "MarburgumLitWindows.h"
#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Lit windows (ADR-066): which household a pane belongs to, and when its room is lit.

namespace
{
    using namespace MarburgumII;

    constexpr EAutomationTestFlags WindowsTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
        | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter;

    Clock::FCivil Civil(int32 Weekday, int32 Hour, int32 Minute = 0)
    {
        Clock::FCivil C;
        C.Year = 1996;
        C.Month = 11;
        C.Day = 13;
        C.Weekday = Weekday;
        C.Hour = Hour;
        C.Minute = Minute;
        return C;
    }

    StreetLights::FSky Dark()
    {
        StreetLights::FSky Sky;
        Sky.SunElevationDeg = -30.0;
        Sky.SunRateDegPerSecond = -0.001;
        return Sky;
    }

    /** The share of many households of upper floors lit under a state. */
    double LitShare(const LitWindows::FState& State, bool bGround = false)
    {
        int32 Lit = 0;
        constexpr int32 Count = 4000;
        for (int32 Index = 0; Index < Count; ++Index)
        {
            const LitWindows::FTag Tag = LitWindows::TagOf(FString::Printf(TEXT("w%d"), 100000 + Index), Index % 4, bGround ? 0 : 1 + Index % 3, 0);
            Lit += LitWindows::IsLit(Tag, State) ? 1 : 0;
        }
        return static_cast<double>(Lit) / Count;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumLitWindowsHouseholdTest, "Marburgum.Shared.LitWindows.Households", WindowsTestFlags)
bool FMarburgumLitWindowsHouseholdTest::RunTest(const FString& Parameters)
{
    using namespace LitWindows;

    // Fixed: the same pane is the same household on every machine and every rebuild.
    const FTag A = TagOf(TEXT("w106076714"), 2, 3, 4);
    const FTag B = TagOf(TEXT("w106076714"), 2, 3, 4);
    TestEqual(TEXT("fixed evening rank"), A.Evening, B.Evening);
    TestEqual(TEXT("fixed morning rank"), A.Morning, B.Morning);
    TestEqual(TEXT("fixed kind"), A.Kind, B.Kind);

    // Ranks are (2k + 1) / 512: exact in a half float, strictly inside 0..1.
    for (int32 Index = 0; Index < 500; ++Index)
    {
        const FTag Tag = TagOf(FString::Printf(TEXT("w%d"), Index), Index % 5, Index % 4, Index % 7);
        for (const float R : { Tag.Evening, Tag.Morning, Tag.Kind })
        {
            const float Scaled = R * 512.0f;
            TestTrue(TEXT("an odd 512th"), FMath::IsNearlyEqual(Scaled, FMath::RoundToFloat(Scaled), 1e-4f) && (static_cast<int32>(Scaled) & 1) == 1);
            TestEqual(TEXT("survives a half float"), FFloat16(R).GetFloat(), R);
        }
    }

    // A flat spans neighbouring panes of one storey: two or three to a flat.
    const FString Id = TEXT("w31553247");
    const int32 Per = PanesPerFlat(Id);
    TestTrue(TEXT("two or three panes to a flat"), Per == 2 || Per == 3);
    TestEqual(TEXT("neighbours in one flat"), TagOf(Id, 0, 2, 0).Evening, TagOf(Id, 0, 2, Per - 1).Evening);
    int32 Differ = 0;
    for (int32 Storey = 1; Storey < 6; ++Storey)
    {
        Differ += TagOf(Id, 0, Storey, 0).Evening != TagOf(Id, 0, Storey, Per).Evening ? 1 : 0;
    }
    TestTrue(TEXT("the next flat is another household"), Differ >= 3);
    TestTrue(TEXT("the ground floor is a shop"), TagOf(Id, 0, 0, 0).bGround && RoomOf(TagOf(Id, 0, 0, 0)) == ERoom::Shop);
    TestFalse(TEXT("upstairs is a home"), TagOf(Id, 0, 1, 0).bGround);

    // Heterogeneity: mostly bulbs, some tubes, a few televisions.
    int32 Rooms[4] = {};
    for (int32 Index = 0; Index < 4000; ++Index)
    {
        Rooms[static_cast<int32>(RoomOf(TagOf(FString::Printf(TEXT("w%d"), Index), 0, 1, 0)))]++;
    }
    TestTrue(TEXT("bulbs ~74%"), FMath::Abs(Rooms[0] / 4000.0 - TungstenShare) < 0.03);
    TestTrue(TEXT("tubes ~16%"), FMath::Abs(Rooms[1] / 4000.0 - FluorescentShare) < 0.03);
    TestTrue(TEXT("televisions ~10%"), FMath::Abs(Rooms[2] / 4000.0 - (1.0 - TungstenShare - FluorescentShare)) < 0.03);
    TestTrue(TEXT("a bulb is warm"), GlowOf(TagOf(TEXT("w1"), 0, 1, 0)).A == 1.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumLitWindowsScheduleTest, "Marburgum.Shared.LitWindows.Schedule", WindowsTestFlags)
bool FMarburgumLitWindowsScheduleTest::RunTest(const FString& Parameters)
{
    using namespace LitWindows;

    // Noon: daylight, nothing shows, whatever the share.
    StreetLights::FSky Noon;
    Noon.SunElevationDeg = 22.0;
    TestEqual(TEXT("noon: no dusk"), DuskOf(Noon), 0.0f);
    TestEqual(TEXT("noon: nothing lit"), LitShare(StateAt(Civil(2, 12), Noon)), 0.0);

    // A Wednesday evening at eight: most homes lit, by the shares (Monday is weekday 0).
    const FState Eight = StateAt(Civil(2, 20), Dark());
    TestEqual(TEXT("dark: full dusk"), Eight.Dusk, 1.0f);
    TestTrue(TEXT("Wednesday 20:00 about 72% lit"), FMath::Abs(LitShare(Eight) - 0.72) < 0.03);
    // Falling away towards and past midnight, a few all night.
    const double Eleven = LitShare(StateAt(Civil(2, 23), Dark()));
    const double Three = LitShare(StateAt(Civil(3, 3), Dark()));
    TestTrue(TEXT("23:00 fewer"), Eleven < LitShare(Eight) && Eleven > 0.15);
    TestTrue(TEXT("03:00 a few"), Three > 0.0 && Three < 0.06);
    // A Friday night goes on longer than a Wednesday's.
    TestTrue(TEXT("Friday 23:00 more than Wednesday's"), LitShare(StateAt(Civil(4, 23), Dark())) > Eleven + 0.1);
    TestTrue(TEXT("Saturday 01:00 is still Friday night"), EveningShareAt(Civil(5, 1)) > EveningShareAt(Civil(3, 1)));
    // The early wave: a workday at 06:30 in a November dark; a Sunday sleeps.
    const double Workday = LitShare(StateAt(Civil(3, 6, 30), Dark()));
    TestTrue(TEXT("Thursday 06:30 the early wave"), Workday > 0.35);
    TestTrue(TEXT("Sunday 06:30 dark"), LitShare(StateAt(Civil(6, 6, 30), Dark())) < 0.06);

    // Monotonic: a household lit later in the night was lit earlier, so as the share falls the same windows go out,
    // and none comes back on.
    int32 Relit = 0;
    for (int32 Index = 0; Index < 2000; ++Index)
    {
        const FTag Tag = TagOf(FString::Printf(TEXT("w%d"), Index), 0, 2, 0);
        bool bWasOut = false;
        for (int32 Minute = 21 * 60; Minute < 28 * 60; Minute += 10)
        {
            const Clock::FCivil At = Minute < 24 * 60 ? Civil(2, Minute / 60, Minute % 60) : Civil(3, Minute / 60 - 24, Minute % 60);
            const bool bLit = IsLit(Tag, StateAt(At, Dark()));
            Relit += bWasOut && bLit ? 1 : 0;
            bWasOut |= !bLit;
        }
    }
    TestEqual(TEXT("from 21:00 to 04:00 no window comes back on"), Relit, 0);

    // Shops: open in shop hours, a night light otherwise; a Saturday afternoon and a Sunday are shut.
    TestTrue(TEXT("Tuesday 17:00 open"), ShopsOpen(1, 17.0));
    TestFalse(TEXT("Tuesday 19:00 shut"), ShopsOpen(1, 19.0));
    TestFalse(TEXT("Saturday 14:00 shut"), ShopsOpen(5, 14.0));
    TestFalse(TEXT("Sunday shut"), ShopsOpen(6, 10.0));
    TestTrue(TEXT("November 17:00, shops lit"), LitShare(StateAt(Civil(1, 17), Dark()), true) > 0.8);
    TestTrue(TEXT("22:00, shops dark"), LitShare(StateAt(Civil(1, 22), Dark()), true) < 0.1);

    // Dusk comes on from just before sunset, earlier under a closed sky; the June evening of the daylight smoke (sun
    // 2.8 degrees up) shows nothing yet.
    StreetLights::FSky JuneEvening;
    JuneEvening.SunElevationDeg = 2.83;
    JuneEvening.CloudCover = 0.15;
    TestEqual(TEXT("June evening: none"), DuskOf(JuneEvening), 0.0f);
    StreetLights::FSky Sunset;
    Sunset.SunElevationDeg = 0.0;
    const float ClearSunset = DuskOf(Sunset);
    TestTrue(TEXT("sunset: windows begin to show"), ClearSunset > 0.15f && ClearSunset < 0.9f);
    StreetLights::FSky Grey = Sunset;
    Grey.CloudCover = 1.0;
    TestTrue(TEXT("overcast sunset: darker"), DuskOf(Grey) > ClearSunset);
    StreetLights::FSky Afternoon;
    Afternoon.SunElevationDeg = 7.0;
    TestEqual(TEXT("clear afternoon: none"), DuskOf(Afternoon), 0.0f);
    Afternoon.CloudCover = 1.0;
    Afternoon.Fog = 1.0;
    TestTrue(TEXT("foggy grey afternoon: some show"), DuskOf(Afternoon) > 0.0f);
    StreetLights::FSky Broken;
    Broken.SunElevationDeg = std::numeric_limits<double>::quiet_NaN();
    TestEqual(TEXT("no sky: none"), DuskOf(Broken), 0.0f);
    return true;
}

#endif
