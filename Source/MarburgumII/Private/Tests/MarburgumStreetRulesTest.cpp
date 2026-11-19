#include "MarburgumStreetContract.h"
#include "MarburgumWalkGraph.h"
#include "MarburgumWater.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumStreetRulesTest,
    "Marburgum.Gameplay.StreetRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarburgumStreetRulesTest::RunTest(const FString& Parameters)
{
    using namespace MarburgumII::Street;

    // Decisions: the roll walks the weights in flee, watch, ignore, fight order.
    TestTrue(TEXT("weak always flees being hit"), Decide(EPersonality::Weak, EEvent::Attacked, ESource::Player, 0.99) == EResponse::Flee);
    TestTrue(TEXT("norm hit: low roll flees"), Decide(EPersonality::Norm, EEvent::Attacked, ESource::Player, 0.0) == EResponse::Flee);
    TestTrue(TEXT("norm hit: high roll fights back"), Decide(EPersonality::Norm, EEvent::Attacked, ESource::Player, 0.95) == EResponse::Fight);
    TestTrue(TEXT("tough bumped: never flees"), Decide(EPersonality::Tough, EEvent::Bumped, ESource::Player, 0.0) == EResponse::Watch);
    for (int32 P = 0; P < 3; ++P)
    {
        for (const EEvent Event : { EEvent::Fight, EEvent::RunOver, EEvent::Body })
        {
            for (int32 Roll = 0; Roll < 100; ++Roll)
            {
                const EResponse Response = Decide(static_cast<EPersonality>(P), Event, ESource::Stranger, Roll / 100.0);
                TestTrue(TEXT("seeing something never starts a fight"), Response != EResponse::Fight);
            }
        }
    }
    // Fight weights: the share of rolls that flee matches the table.
    {
        int32 Flee = 0;
        for (int32 Roll = 0; Roll < 1000; ++Roll)
        {
            Flee += Decide(EPersonality::Norm, EEvent::Fight, ESource::Stranger, (Roll + 0.5) / 1000.0) == EResponse::Flee ? 1 : 0;
        }
        const FAnswer& Row = Decisions[0][0];
        const int32 Total = Row.Flee[0] + Row.Watch[0] + Row.Ignore[0] + Row.Fight[0];
        TestEqual(TEXT("norm flees a fight in the table's share"), Flee, 1000 * Row.Flee[0] / Total);
    }
    TestTrue(TEXT("a run-over is louder than a fight, a fight than a body"),
        Loudness(EEvent::RunOver) > Loudness(EEvent::Fight) && Loudness(EEvent::Fight) > Loudness(EEvent::Body));

    // Groups and looks: every group can be dressed, and a roll picks one of its own archetypes.
    for (const FGroup& Group : Groups)
    {
        for (int32 Roll = 0; Roll < 10; ++Roll)
        {
            const int32 Look = PickLook(Group, Roll / 10.0);
            TestTrue(FString::Printf(TEXT("%s has a look"), Group.Id), Look != INDEX_NONE);
        }
        TestTrue(TEXT("walk speeds ordered"), Group.WalkSpeedMin <= Group.WalkSpeedMax);
    }
    TestTrue(TEXT("pensioners are never tough"), PickPersonality(Groups[2], 0.36) != EPersonality::Tough);

    // Who is out when (ADR-067). Weekdays 0 Monday .. 6 Sunday.
    {
        const auto Is = [this](const TCHAR* What, int32 Weekday, double At, EPeriod Want, int32 WantDay)
        {
            const FStreetHour Hour = StreetHourAt(Weekday, At);
            TestTrue(FString::Printf(TEXT("%s is %s"), What, PeriodNames[static_cast<int32>(Want)]), Hour.Period == Want);
            TestEqual(FString::Printf(TEXT("%s belongs to day %d"), What, WantDay), Hour.Weekday, WantDay);
            return Hour;
        };
        const FStreetHour TuesdayDay = Is(TEXT("Tuesday 10:00"), 1, 10.0, EPeriod::Day, 1);
        TestEqual(TEXT("a weekday's day is full"), TuesdayDay.Density, 1.f);
        for (int32 Group = 0; Group < GroupCount; ++Group)
        {
            TestEqual(TEXT("a weekday's day is ADR-044's mix"), TuesdayDay.Mix[Group], ZoneMix[0][2][Group]);
        }
        const FStreetHour TuesdayNight = Is(TEXT("Tuesday 03:00"), 1, 3.0, EPeriod::Night, 1);
        TestEqual(TEXT("the small hours are thin"), TuesdayNight.Density, 0.15f);
        TestTrue(TEXT("the small hours are night owls"), TuesdayNight.Mix[NightPeople] > TuesdayNight.Mix[Residents] && TuesdayNight.Mix[Commuters] == 0);
        Is(TEXT("Tuesday 05:59"), 1, 5.99, EPeriod::Night, 1);
        const FStreetHour TuesdayMorning = Is(TEXT("Tuesday 06:00"), 1, 6.0, EPeriod::Morning, 1);
        TestEqual(TEXT("a weekday morning is commuters first"), TuesdayMorning.Mix[Commuters], 50);
        Is(TEXT("Tuesday 19:00"), 1, 19.0, EPeriod::Evening, 1);
        Is(TEXT("Tuesday 21:30"), 1, 21.5, EPeriod::Late, 1);
        // Friday and Saturday nights run an hour later; Saturday and Sunday mornings start an hour later.
        Is(TEXT("Friday 21:30"), 4, 21.5, EPeriod::Evening, 4);
        Is(TEXT("Friday 22:30"), 4, 22.5, EPeriod::Late, 4);
        Is(TEXT("Saturday 00:30"), 5, 0.5, EPeriod::Late, 4);
        Is(TEXT("Saturday 01:30"), 5, 1.5, EPeriod::Night, 5);
        Is(TEXT("Saturday 06:30"), 5, 6.5, EPeriod::Night, 5);
        const FStreetHour SaturdayMorning = Is(TEXT("Saturday 07:30"), 5, 7.5, EPeriod::Morning, 5);
        TestEqual(TEXT("nobody commutes on a Saturday"), SaturdayMorning.Mix[Commuters], 0);
        TestEqual(TEXT("the commuters' share walks as residents"), SaturdayMorning.Mix[Residents], 50);
        TestEqual(TEXT("Saturday morning's shops are open"), SaturdayMorning.Mix[Shoppers], 20);
        Is(TEXT("Saturday 21:30"), 5, 21.5, EPeriod::Evening, 5);
        Is(TEXT("Sunday 00:30"), 6, 0.5, EPeriod::Late, 5);
        const FStreetHour SundayDay = Is(TEXT("Sunday 12:00"), 6, 12.0, EPeriod::Day, 6);
        TestEqual(TEXT("the shops are shut on a Sunday"), SundayDay.Mix[Shoppers], 0);
        TestEqual(TEXT("nobody commutes on a Sunday"), SundayDay.Mix[Commuters], 0);
        TestEqual(TEXT("Sunday's day walks as residents"), SundayDay.Mix[Residents], 70);
        const FStreetHour SundayEvening = Is(TEXT("Sunday 19:00"), 6, 19.0, EPeriod::Evening, 6);
        TestEqual(TEXT("Sunday's evening is a weekday's"), SundayEvening.Mix[Shoppers], 30);
        Is(TEXT("Sunday 21:30"), 6, 21.5, EPeriod::Late, 6);
        Is(TEXT("Monday 00:30"), 0, 0.5, EPeriod::Night, 0);
        Is(TEXT("Monday 06:30"), 0, 6.5, EPeriod::Morning, 0);

        // Every hour of every day in every zone has somebody to send out, every group walks somewhere at some hour, and the
        // old town still walks as ADR-067 had it.
        int32 Walks[GroupCount] = {};
        for (int32 Zone = 0; Zone < ZoneCount; ++Zone)
        {
            for (int32 Weekday = 0; Weekday < 7; ++Weekday)
            {
                for (int32 Quarter = 0; Quarter < 96; ++Quarter)
                {
                    const FStreetHour Hour = StreetHourAt(Weekday, Quarter / 4.0, static_cast<EZone>(Zone));
                    int32 Total = 0;
                    for (int32 Group = 0; Group < GroupCount; ++Group)
                    {
                        Total += Hour.Mix[Group];
                        Walks[Group] += Hour.Mix[Group];
                    }
                    TestTrue(TEXT("every hour has a mix"), Total > 0 && Hour.Density >= 0.f && Hour.Density <= 1.f);
                    TestTrue(TEXT("the hour says its zone"), Hour.Zone == static_cast<EZone>(Zone));
                    if (Zone == static_cast<int32>(EZone::OldTown))
                    {
                        TestTrue(TEXT("the old town always has somebody out"), Hour.Density > 0.f);
                        TestTrue(TEXT("no workers or walkers out in the old town"), Hour.Mix[Workers] == 0 && Hour.Mix[Outdoor] == 0);
                    }
                }
            }
        }
        for (int32 Group = 0; Group < GroupCount; ++Group)
        {
            TestTrue(FString::Printf(TEXT("%s walk at some hour"), Groups[Group].Id), Walks[Group] > 0);
        }

        // The districts (ADR-068): the original's weekday tables, and Marburgum II's weekend over them.
        TestTrue(TEXT("a zone name reads back"), ZoneNamed(TEXT("residential_blocks")) == EZone::ResidentialBlocks);
        TestTrue(TEXT("an unknown zone is the old town"), ZoneNamed(TEXT("harbour")) == EZone::OldTown);
        TestTrue(TEXT("the default is the old town"), StreetHourAt(1, 10.0).Zone == EZone::OldTown);
        const FStreetHour HousesByDay = StreetHourAt(1, 10.0, EZone::Residential);
        TestEqual(TEXT("a street of houses by day keeps 0.4"), HousesByDay.Density, 0.4f);
        TestTrue(TEXT("houses by day: residents and pensioners"), HousesByDay.Mix[Residents] == 45 && HousesByDay.Mix[Pensioners] == 35 && HousesByDay.Mix[Commuters] == 0);
        const FStreetHour HousesEvening = StreetHourAt(1, 19.0, EZone::Residential);
        TestEqual(TEXT("an evening among the houses has people out walking"), HousesEvening.Mix[Outdoor], 20);
        const FStreetHour WorksMorning = StreetHourAt(1, 7.0, EZone::Industrial);
        TestEqual(TEXT("the works in the morning: workers"), WorksMorning.Mix[Workers], 80);
        const FStreetHour WorksSaturday = StreetHourAt(5, 10.0, EZone::Industrial);
        TestEqual(TEXT("workers keep their shifts at the weekend"), WorksSaturday.Mix[Workers], 85);
        TestEqual(TEXT("but nobody commutes to the works"), WorksSaturday.Mix[Commuters], 0);
        const FStreetHour ShopsDay = StreetHourAt(1, 12.0, EZone::Commercial);
        TestTrue(TEXT("a shopping street by day"), ShopsDay.Density == 0.9f && ShopsDay.Mix[Shoppers] == 50);
        const FStreetHour ShopsSunday = StreetHourAt(6, 12.0, EZone::Commercial);
        TestTrue(TEXT("a shopping street on a Sunday walks as residents"), ShopsSunday.Mix[Shoppers] == 0 && ShopsSunday.Mix[Commuters] == 0
            && ShopsSunday.Mix[Residents] == 75);
        const FStreetHour SchoolTuesday = StreetHourAt(1, 8.0, EZone::School);
        TestTrue(TEXT("a school morning is students"), SchoolTuesday.Density == 0.9f && SchoolTuesday.Mix[Students] == 75);
        const FStreetHour SchoolSaturday = StreetHourAt(5, 8.0, EZone::School);
        TestTrue(TEXT("no school on a Saturday"), SchoolSaturday.Mix[Students] == 0 && SchoolSaturday.Mix[Residents] == 100);
        const FStreetHour SchoolSaturdayLate = StreetHourAt(5, 22.5, EZone::School);
        TestEqual(TEXT("the students are about on a Saturday night"), SchoolSaturdayLate.Mix[Students], 50);
        const FStreetHour ParkEvening = StreetHourAt(1, 19.0, EZone::Park);
        TestTrue(TEXT("a park in the evening is people out walking"), ParkEvening.Mix[Outdoor] == 55 && ParkEvening.Density == 0.5f);
        TestEqual(TEXT("nobody in the countryside at night"), StreetHourAt(1, 3.0, EZone::Rural).Density, 0.f);
        TestTrue(TEXT("the blocks are fuller in the evening than by day"),
            StreetHourAt(1, 19.0, EZone::ResidentialBlocks).Density > StreetHourAt(1, 12.0, EZone::ResidentialBlocks).Density);

        // The weather and the count.
        TestEqual(TEXT("a dry street keeps everyone"), WeatherShare(0.f), 1.f);
        TestEqual(TEXT("a downpour keeps a quarter"), WeatherShare(1.f), 0.25f);
        TestEqual(TEXT("out of range falls clamp"), WeatherShare(3.f), 0.25f);
        TestEqual(TEXT("one player by day"), PeopleFor(1, 1.f), PeoplePerPlayer);
        TestEqual(TEXT("one player in the small hours"), PeopleFor(1, 0.15f), 3);
        TestEqual(TEXT("three players by day hit the cap"), PeopleFor(3, 1.f), MaxPeople);
        TestEqual(TEXT("nobody about, nobody out"), PeopleFor(0, 1.f), 0);
        TestEqual(TEXT("a late evening in the rain"), PeopleFor(1, 0.45f * WeatherShare(0.6f)), 5);
        TestEqual(TEXT("two players, one in the old town and one among the houses, by day"), PeopleFor(2, (1.f + 0.4f) / 2.f), 28);
    }

    // Run over: the swept body.
    const FVector2D Forward(1.0, 0.0);
    TestTrue(TEXT("inside the body"), InSweptBody(FVector2D(100.0, 50.0), FVector2D::ZeroVector, Forward, 180.0, 75.0, 0.0));
    TestFalse(TEXT("beside the body, past the margin"), InSweptBody(FVector2D(0.0, 120.0), FVector2D::ZeroVector, Forward, 180.0, 75.0, 0.0));
    TestFalse(TEXT("behind the body"), InSweptBody(FVector2D(-400.0, 0.0), FVector2D::ZeroVector, Forward, 180.0, 75.0, 0.0));
    TestTrue(TEXT("swept over since the last frame"), InSweptBody(FVector2D(-400.0, 0.0), FVector2D::ZeroVector, Forward, 180.0, 75.0, 300.0));
    TestTrue(TEXT("just ahead, about to be struck"), InSweptBody(FVector2D(300.0, 0.0), FVector2D::ZeroVector, Forward, 180.0, 75.0, -150.0));
    TestEqual(TEXT("a nudge does nothing"), RunOverDamage(200.f), 0.f);
    TestEqual(TEXT("10 per m/s"), RunOverDamage(700.f), 70.f);
    TestTrue(TEXT("7 m/s puts a street person out"), RunOverDamage(700.f) >= StreetVitality);

    // Dodging: a car heading for them, one passing wide, one going away.
    TestTrue(TEXT("car coming"), WillPassClose(FVector2D(1000.0, 0.0), FVector2D::ZeroVector, FVector2D(800.0, 0.0), DodgeLookaheadSeconds, DodgeClearanceCm));
    TestFalse(TEXT("car passing wide"), WillPassClose(FVector2D(1000.0, 400.0), FVector2D::ZeroVector, FVector2D(800.0, 0.0), DodgeLookaheadSeconds, DodgeClearanceCm));
    TestFalse(TEXT("car going away"), WillPassClose(FVector2D(-1000.0, 0.0), FVector2D::ZeroVector, FVector2D(800.0, 0.0), DodgeLookaheadSeconds, DodgeClearanceCm));
    TestFalse(TEXT("car too far to matter yet"), WillPassClose(FVector2D(3000.0, 0.0), FVector2D::ZeroVector, FVector2D(800.0, 0.0), DodgeLookaheadSeconds, DodgeClearanceCm));

    // The walk graph: nodes without edges go, components and the nearest node.
    {
        TArray<FMarburgumWalkGraph::FNode> Nodes;
        for (const FVector& At : { FVector(0.0, 0.0, 0.0), FVector(500.0, 0.0, 0.0), FVector(1000.0, 0.0, 0.0), FVector(5000.0, 0.0, 0.0),
                 FVector(9000.0, 0.0, 0.0), FVector(9500.0, 0.0, 0.0) })
        {
            FMarburgumWalkGraph::FNode Node;
            Node.Location = At;
            Nodes.Add(Node);
        }
        TArray<FMarburgumWalkGraph::FEdge> Edges;
        Edges.Add({ 0, 1, 500.0, 1 });
        Edges.Add({ 1, 2, 500.0, 1 });
        Edges.Add({ 4, 5, 500.0, 0 });
        FMarburgumWalkGraph Graph;
        Graph.Build(Nodes, Edges);
        TestEqual(TEXT("the lone node is dropped"), Graph.GetNodes().Num(), 5);
        TestEqual(TEXT("edges kept"), Graph.GetEdges().Num(), 3);
        const int32 Near = Graph.NearestNode(FVector(480.0, 30.0, 0.0), 300.0);
        TestTrue(TEXT("nearest node found"), Near != INDEX_NONE && Graph.GetNodes()[Near].Location.Equals(FVector(500.0, 0.0, 0.0)));
        TestEqual(TEXT("nothing near the gap"), Graph.NearestNode(FVector(5000.0, 0.0, 0.0), 300.0), INDEX_NONE);
        TestEqual(TEXT("too high above it"), Graph.NearestNode(FVector(0.0, 0.0, 900.0), 300.0, 400.0), INDEX_NONE);
        TestEqual(TEXT("the largest network is the three-node one"), Graph.GetNodes()[Graph.NearestNode(FVector::ZeroVector, 100.0)].Component,
            Graph.LargestComponent());
        TestEqual(TEXT("the middle node has two edges"), Graph.Touching(Graph.NearestNode(FVector(500.0, 0.0, 0.0), 100.0)).Num(), 2);
        TestTrue(TEXT("a node without a zone is the old town's, fully busy"),
            Graph.GetNodes()[0].Zone == static_cast<uint8>(EZone::OldTown) && Graph.GetNodes()[0].Activity == 1.f);
    }

    // The water's footprint (ADR-046): over a column, from its bottom up to a drop above its surface.
    {
        FMarburgumWaterColumns Water;
        TArray<int32, TInlineAllocator<4>> Under;
        Water.Under(FVector::ZeroVector, 5000.0, Under);
        TestEqual(TEXT("no water, nothing under anyone"), Under.Num(), 0);
        Water.Add(FBox(FVector(0.0, 0.0, -250.0), FVector(1000.0, 400.0, 6.0)));
        Water.Add(FBox(FVector(1000.0, 0.0, -100.0), FVector(1100.0, 400.0, 6.0)));
        Water.Under(FVector(500.0, 200.0, -40.0), 5000.0, Under);
        TestEqual(TEXT("wading in the first column"), Under.Num(), 1);
        Water.Under(FVector(500.0, 200.0, 180.0), 5000.0, Under);
        TestEqual(TEXT("on a quay's height over it, still over the water"), Under.Num(), 1);
        Water.Under(FVector(500.0, 200.0, 180.0), 100.0, Under);
        TestEqual(TEXT("higher than the drop allowed"), Under.Num(), 0);
        Water.Under(FVector(1050.0, 10.0, 0.0), 5000.0, Under);
        TestTrue(TEXT("the second column"), Under.Num() == 1 && Under[0] == 1);
        Water.Under(FVector(500.0, 450.0, 0.0), 5000.0, Under);
        TestEqual(TEXT("on the bank beside it"), Under.Num(), 0);
        Water.Under(FVector(500.0, 200.0, -400.0), 5000.0, Under);
        TestEqual(TEXT("under the bed"), Under.Num(), 0);
    }
    return true;
}

#endif
