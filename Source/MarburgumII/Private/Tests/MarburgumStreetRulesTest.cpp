#include "MarburgumStreetContract.h"
#include "MarburgumWalkGraph.h"

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
    }
    return true;
}

#endif
