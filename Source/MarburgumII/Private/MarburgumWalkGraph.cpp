#include "MarburgumWalkGraph.h"

#include "CollisionQueryParams.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace MarburgumWalkGraphCheck
{
    // A standing body: the capsule street people use, kept a little slimmer so a node by a wall survives.
    constexpr float BodyRadiusCm = 30.f;
    constexpr float BodyHalfHeightCm = 85.f;
    // Ground is looked for from this far above the surveyed top, down to this far below it.
    constexpr double GroundAboveCm = 250.0;
    constexpr double GroundBelowCm = 400.0;
    // A bridge deck is part of the level, not of the survey: looked for from this high (cm above the Drava).
    constexpr double BridgeTopCm = 3000.0;
    constexpr double BridgeBottomCm = -300.0;
    // Walkable ground faces up at least this much.
    constexpr double WalkableNormalZ = 0.7;
    // An edge is swept at knee height and above, so kerbs and steps do not block it.
    constexpr float SweepRadiusCm = 26.f;
    constexpr float SweepHalfHeightCm = 45.f;
    constexpr double SweepLiftCm = 100.0;

    // The original's frame onto Unreal's (MarburgumCombatContract.h GodotPositionToUnreal): plan x east and y
    // south are Godot x and z; Unreal = 100 (-z, x, y).
    FVector PlanToUnreal(double X, double Y, double Z)
    {
        return FVector(-Y * 100.0, X * 100.0, Z * 100.0);
    }
}

bool FMarburgumWalkGraph::Load(UWorld& World, const FString& Path, FString& OutReport, FString& OutError)
{
    using namespace MarburgumWalkGraphCheck;
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *Path))
    {
        OutError = FString::Printf(TEXT("cannot read %s"), *Path);
        return false;
    }
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
    {
        OutError = FString::Printf(TEXT("%s is not JSON"), *Path);
        return false;
    }
    if (Root->GetStringField(TEXT("format")) != TEXT("marburgumii.walk_graph"))
    {
        OutError = FString::Printf(TEXT("%s is not a walk graph"), *Path);
        return false;
    }
    int64 RoadFlag = 2;
    int64 BridgeFlag = 1;
    const TSharedPtr<FJsonObject>* Flags = nullptr;
    if (Root->TryGetObjectField(TEXT("flags"), Flags))
    {
        (*Flags)->TryGetNumberField(TEXT("road"), RoadFlag);
        (*Flags)->TryGetNumberField(TEXT("bridge"), BridgeFlag);
    }

    FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumWalkGraph), true);
    FCollisionObjectQueryParams Static(FCollisionObjectQueryParams::AllStaticObjects);
    const FCollisionShape Body = FCollisionShape::MakeCapsule(BodyRadiusCm, BodyHalfHeightCm);
    const FCollisionShape Sweep = FCollisionShape::MakeCapsule(SweepRadiusCm, SweepHalfHeightCm);

    const TArray<TSharedPtr<FJsonValue>>& JsonNodes = Root->GetArrayField(TEXT("nodes"));
    TArray<int32> Index;
    Index.Init(INDEX_NONE, JsonNodes.Num());
    TArray<FNode> Kept;
    int32 NoGround = 0;
    int32 NoRoom = 0;
    for (int32 Source = 0; Source < JsonNodes.Num(); ++Source)
    {
        const TArray<TSharedPtr<FJsonValue>>& Row = JsonNodes[Source]->AsArray();
        if (Row.Num() < 4)
        {
            continue;
        }
        const double X = Row[0]->AsNumber();
        const double Y = Row[1]->AsNumber();
        const double Z = Row[2]->AsNumber();
        const int64 NodeFlags = static_cast<int64>(Row[3]->AsNumber());
        const bool bBridge = (NodeFlags & BridgeFlag) != 0;
        const FVector Plan = PlanToUnreal(X, Y, bBridge ? 0.0 : Z);
        const FVector From = bBridge ? FVector(Plan.X, Plan.Y, BridgeTopCm) : Plan + FVector(0.0, 0.0, GroundAboveCm);
        const FVector To = bBridge ? FVector(Plan.X, Plan.Y, BridgeBottomCm) : Plan - FVector(0.0, 0.0, GroundBelowCm);
        TArray<FHitResult> Hits;
        World.LineTraceMultiByObjectType(Hits, From, To, Static, Params);
        const FHitResult* Ground = nullptr;
        for (const FHitResult& Hit : Hits)
        {
            if (Hit.bBlockingHit && Hit.ImpactNormal.Z >= WalkableNormalZ)
            {
                Ground = &Hit;
                break;
            }
        }
        if (!Ground)
        {
            FHitResult Single;
            if (World.LineTraceSingleByObjectType(Single, From, To, Static, Params) && Single.ImpactNormal.Z >= WalkableNormalZ)
            {
                Hits = { Single };
                Ground = &Hits[0];
            }
        }
        if (!Ground)
        {
            ++NoGround;
            continue;
        }
        const FVector At = Ground->ImpactPoint;
        // Standing a hand's breadth clear of the ground, so a kerb or the slope under the feet does not count.
        if (World.OverlapAnyTestByObjectType(At + FVector(0.0, 0.0, BodyHalfHeightCm + 8.0), FQuat::Identity, Static, Body, Params))
        {
            ++NoRoom;
            continue;
        }
        FNode Node;
        Node.Location = At;
        Node.bRoad = (NodeFlags & RoadFlag) != 0;
        Index[Source] = Kept.Add(Node);
    }

    TArray<FEdge> KeptEdges;
    int32 Blocked = 0;
    int32 Missing = 0;
    for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("edges")))
    {
        const TArray<TSharedPtr<FJsonValue>>& Row = Value->AsArray();
        if (Row.Num() < 3)
        {
            continue;
        }
        const int32 SourceA = static_cast<int32>(Row[0]->AsNumber());
        const int32 SourceB = static_cast<int32>(Row[1]->AsNumber());
        if (!Index.IsValidIndex(SourceA) || !Index.IsValidIndex(SourceB) || Index[SourceA] == INDEX_NONE || Index[SourceB] == INDEX_NONE)
        {
            ++Missing;
            continue;
        }
        const FVector A = Kept[Index[SourceA]].Location;
        const FVector B = Kept[Index[SourceB]].Location;
        const FVector Lift(0.0, 0.0, SweepLiftCm);
        FHitResult Hit;
        if (World.SweepSingleByObjectType(Hit, A + Lift, B + Lift, FQuat::Identity, Static, Sweep, Params))
        {
            ++Blocked;
            continue;
        }
        FEdge Edge;
        Edge.A = Index[SourceA];
        Edge.B = Index[SourceB];
        Edge.LengthCm = FVector::Dist(A, B);
        Edge.Kind = static_cast<uint8>(Row[2]->AsNumber());
        KeptEdges.Add(Edge);
    }

    const int32 Read = JsonNodes.Num();
    const int32 EdgesRead = Root->GetArrayField(TEXT("edges")).Num();
    Build(MoveTemp(Kept), MoveTemp(KeptEdges));
    int32 LargestCount = 0;
    for (const FNode& Node : Nodes)
    {
        LargestCount += Node.Component == Largest ? 1 : 0;
    }
    OutReport = FString::Printf(TEXT("nodes=%d/%d edges=%d/%d no_ground=%d no_room=%d edges_blocked=%d edges_missing_end=%d largest_component=%d"),
        Nodes.Num(), Read, Edges.Num(), EdgesRead, NoGround, NoRoom, Blocked, Missing, LargestCount);
    if (Nodes.IsEmpty())
    {
        OutError = TEXT("no node of the walk graph is on walkable ground in this level");
        return false;
    }
    return true;
}

void FMarburgumWalkGraph::Build(TArray<FNode> InNodes, TArray<FEdge> InEdges)
{
    // Nodes without an edge cannot be walked from; drop them and renumber.
    TArray<int32> Degree;
    Degree.Init(0, InNodes.Num());
    for (const FEdge& Edge : InEdges)
    {
        ++Degree[Edge.A];
        ++Degree[Edge.B];
    }
    TArray<int32> Remap;
    Remap.Init(INDEX_NONE, InNodes.Num());
    Nodes.Reset();
    for (int32 Node = 0; Node < InNodes.Num(); ++Node)
    {
        if (Degree[Node] > 0)
        {
            Remap[Node] = Nodes.Add(InNodes[Node]);
        }
    }
    Edges.Reset();
    for (FEdge Edge : InEdges)
    {
        Edge.A = Remap[Edge.A];
        Edge.B = Remap[Edge.B];
        if (Edge.A != Edge.B)
        {
            Edges.Add(Edge);
        }
    }
    Index();
}

void FMarburgumWalkGraph::Index()
{
    NodeEdges.Reset();
    NodeEdges.SetNum(Nodes.Num());
    for (int32 Edge = 0; Edge < Edges.Num(); ++Edge)
    {
        NodeEdges[Edges[Edge].A].Add(Edge);
        NodeEdges[Edges[Edge].B].Add(Edge);
    }
    Cells.Reset();
    for (int32 Node = 0; Node < Nodes.Num(); ++Node)
    {
        Cells.FindOrAdd(CellOf(Nodes[Node].Location)).Add(Node);
    }
    // Components, and the largest of them.
    TArray<int32> Sizes;
    for (FNode& Node : Nodes)
    {
        Node.Component = INDEX_NONE;
    }
    for (int32 Start = 0; Start < Nodes.Num(); ++Start)
    {
        if (Nodes[Start].Component != INDEX_NONE)
        {
            continue;
        }
        const int32 Label = Sizes.Add(0);
        TArray<int32> Stack = { Start };
        Nodes[Start].Component = Label;
        while (!Stack.IsEmpty())
        {
            const int32 Node = Stack.Pop(EAllowShrinking::No);
            ++Sizes[Label];
            for (const int32 Edge : NodeEdges[Node])
            {
                const int32 Next = Other(Edge, Node);
                if (Nodes[Next].Component == INDEX_NONE)
                {
                    Nodes[Next].Component = Label;
                    Stack.Add(Next);
                }
            }
        }
    }
    Largest = INDEX_NONE;
    int32 Best = 0;
    for (int32 Label = 0; Label < Sizes.Num(); ++Label)
    {
        if (Sizes[Label] > Best)
        {
            Best = Sizes[Label];
            Largest = Label;
        }
    }
}

FIntPoint FMarburgumWalkGraph::CellOf(const FVector& At) const
{
    return FIntPoint(FMath::FloorToInt(At.X / CellCm), FMath::FloorToInt(At.Y / CellCm));
}

void FMarburgumWalkGraph::NodesWithin(const FVector& At, double RadiusCm, TArray<int32>& Out) const
{
    Out.Reset();
    const int32 Reach = FMath::CeilToInt(RadiusCm / CellCm);
    const FIntPoint Centre = CellOf(At);
    for (int32 DX = -Reach; DX <= Reach; ++DX)
    {
        for (int32 DY = -Reach; DY <= Reach; ++DY)
        {
            if (const TArray<int32>* Cell = Cells.Find(Centre + FIntPoint(DX, DY)))
            {
                for (const int32 Node : *Cell)
                {
                    if (FVector::DistSquared2D(Nodes[Node].Location, At) <= RadiusCm * RadiusCm)
                    {
                        Out.Add(Node);
                    }
                }
            }
        }
    }
    Out.Sort();
}

int32 FMarburgumWalkGraph::NearestNode(const FVector& At, double RadiusCm, double HeightCm) const
{
    TArray<int32> Near;
    NodesWithin(At, RadiusCm, Near);
    int32 Best = INDEX_NONE;
    double BestDistance = TNumericLimits<double>::Max();
    for (const int32 Node : Near)
    {
        if (FMath::Abs(Nodes[Node].Location.Z - At.Z) > HeightCm)
        {
            continue;
        }
        const double Distance = FVector::DistSquared2D(Nodes[Node].Location, At);
        if (Distance < BestDistance)
        {
            BestDistance = Distance;
            Best = Node;
        }
    }
    return Best;
}
