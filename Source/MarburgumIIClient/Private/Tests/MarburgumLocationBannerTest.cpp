#include "MarburgumPlaces.h"
#include "Misc/AutomationTest.h"
#include "UI/MarburgumLocationBanner.h"

#if WITH_DEV_AUTOMATION_TESTS

// The announcement half of the original's MarburgumLocationBannerTest.cpp (#263, ADR-063): when the banner comes up
// for a new place, street or landmark, and that pacing across a border, a junction or a shared wall brings it up once.
// The banner's own timer is the widget's; here an observation that announces is one coming up.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumLocationBannerTest, "Marburgum.Client.LocationBanner",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::CommandletContext
        | EAutomationTestFlags::ProductFilter)

bool FMarburgumLocationBannerTest::RunTest(const FString& Parameters)
{
    using namespace MarburgumII::Places;

    {
        FMarburgumPlaceAnnouncer Banner;
        TestTrue(TEXT("entering a place announces it"), Banner.Observe(TEXT("area:lent"), TEXT(""), TEXT("")));
        TestFalse(TEXT("staying in it does not"), Banner.Observe(TEXT("area:lent"), TEXT(""), TEXT("")));
        TestTrue(TEXT("leaving for another place does"), Banner.Observe(TEXT("quarter:1"), TEXT(""), TEXT("")));
        TestFalse(TEXT("an empty place announces nothing"), Banner.Observe(TEXT(""), TEXT(""), TEXT("")));
        Banner.Reset();
        TestTrue(TEXT("a reset lets the same place announce itself again"), Banner.Observe(TEXT("quarter:1"), TEXT(""), TEXT("")));
    }

    // Turning into a new street announces under the same place; losing the street does not, nor coming back to it.
    {
        FMarburgumPlaceAnnouncer Streets;
        TestTrue(TEXT("street: shown with the place"), Streets.Observe(TEXT("quarter:center"), TEXT("Stari most"), TEXT("")));
        TestTrue(TEXT("street: a new street announces again"), Streets.Observe(TEXT("quarter:center"), TEXT("Glavni trg"), TEXT("")));
        TestFalse(TEXT("street: walking off every street does not"), Streets.Observe(TEXT("quarter:center"), TEXT(""), TEXT("")));
        TestFalse(TEXT("street: back onto the street last shown does not"), Streets.Observe(TEXT("quarter:center"), TEXT("Glavni trg"), TEXT("")));
    }

    // A new landmark announces, walking away is quiet, and coming back to the one last shown says nothing new.
    {
        FMarburgumPlaceAnnouncer Sights;
        Sights.Observe(TEXT("area:lent"), TEXT("Usnjarska ulica"), TEXT(""));
        TestTrue(TEXT("sights: arriving at one announces"), Sights.Observe(TEXT("area:lent"), TEXT("Usnjarska ulica"), TEXT("1-00445")));
        TestFalse(TEXT("sights: walking away does not"), Sights.Observe(TEXT("area:lent"), TEXT("Usnjarska ulica"), TEXT("")));
        TestFalse(TEXT("sights: back to the one last shown does not"), Sights.Observe(TEXT("area:lent"), TEXT("Usnjarska ulica"), TEXT("1-00445")));
    }

    const FCatalogue& Data = Get();
    if (!TestTrue(TEXT("the places file loads"), Data.IsLoaded()))
    {
        return false;
    }
    const auto Pace = [&](const FVector2D& A, const FVector2D& B)
    {
        FPlaceTracker Walk;
        FMarburgumPlaceAnnouncer Banner;
        int32 Announcements = 0;
        for (int32 Step = 0; Step < 20; ++Step)
        {
            const FPlaceIds Ids = Walk.Resolve(Data, Step % 2 == 0 ? A : B, false);
            Announcements += Banner.Observe(Ids.District, Ids.Street, Ids.Landmark) ? 1 : 0;
        }
        return Announcements;
    };
    // Across the Magdalena–Tabor border 3 m either side, which the street also crosses.
    TestEqual(TEXT("pacing: 3 m either side of the border announces once"), Pace(FromPlan(0.0, 903.4), FromPlan(0.0, 909.4)), 1);
    // Across the crossing where Stari most meets Glavni trg.
    TestEqual(TEXT("pacing: across the crossing announces once"), Pace(FromPlan(1.9, -129.0), FromPlan(1.9, -132.5)), 1);
    // Between Koroška 24 and 0.19 m inside 26, and across the church's wall inside the Jesuit college.
    const auto Koroska = [](double East) { return FromPlan(East, -263.6 + (East + 288.48) * (5.28 / 21.76)); };
    TestEqual(TEXT("pacing: 24 to just inside 26 announces once"), Pace(Koroska(-298.1), Koroska(-301.6)), 1);
    TestEqual(TEXT("pacing: the church's wall announces once"), Pace(FromPlan(-111.96, -151.17), FromPlan(-112.96, -151.17)), 1);
    return true;
}

#endif
