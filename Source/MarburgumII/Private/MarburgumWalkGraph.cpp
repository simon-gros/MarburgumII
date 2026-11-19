#include "MarburgumWalkGraph.h"

#include "CollisionQueryParams.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "MarburgumStreetContract.h"
#include "MarburgumWater.h"
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

    bool IsBridgeStructure(const AActor* Actor, const UPrimitiveComponent* Component)
    {
        if (Actor && (Actor->GetName().StartsWith(TEXT("Bridge_")) || Actor->GetFolderPath().ToString().Contains(TEXT("Bridge"))))
        {
            return true;
        }
        const UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Component);
        const UStaticMesh* Mesh = MeshComponent ? MeshComponent->GetStaticMesh() : nullptr;
        return Mesh && Mesh->GetPathName().Contains(TEXT("/World/Bridges/"));
    }

    bool IsPaving(const UPrimitiveComponent* Component)
    {
        const UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Component);
        const UStaticMesh* Mesh = MeshComponent ? MeshComponent->GetStaticMesh() : nullptr;
        if (!Mesh)
        {
            return false;
        }
        const FString Path = Mesh->GetPathName();
        return Path.Contains(TEXT("LentPaved")) || Path.Contains(TEXT("/Ground/"));
    }

    // The deck, the paving at its ends, and the landscape the abutment sits on. Not a building.
    bool IsDeckOrGround(const AActor* Actor, const UPrimitiveComponent* Component)
    {
        if (IsBridgeStructure(Actor, Component) || IsPaving(Component) || (Actor && Actor->GetName().StartsWith(TEXT("Landscape"))))
        {
            return true;
        }
        return false;
    }

    FString HitLabel(const FHitResult& Hit)
    {
        const UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Hit.GetComponent());
        const UStaticMesh* Mesh = MeshComponent ? MeshComponent->GetStaticMesh() : nullptr;
        if (Mesh)
        {
            return Mesh->GetName();
        }
        const AActor* Actor = Hit.GetActor();
        return Actor ? Actor->GetName() : TEXT("none");
    }
    // An edge is swept at knee height and above, so kerbs and steps do not block it.
    constexpr float SweepRadiusCm = 26.f;
    constexpr float SweepHalfHeightCm = 45.f;
    constexpr double SweepLiftCm = 100.0;
    // A place's spot that fails (a step up to a door, a sill, a sign the footprint does not know) is tried again this
    // much nearer its node at a time, up to this far (ADR-069).
    constexpr double SpotPullStepCm = 25.0;
    constexpr double SpotPullMostCm = 150.0;

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

    // The original's zones (ADR-068), by name, so a file written in another order still reads right.
    TArray<uint8> ZoneOf;
    // Not ZoneNames: in a unity build MarburgumStreetSubsystem.cpp's file-scope using-directive makes
    // MarburgumII::Street::ZoneNames visible here, and -Wshadow is an error.
    const TArray<TSharedPtr<FJsonValue>>* ZoneNameValues = nullptr;
    if (Root->TryGetArrayField(TEXT("zones"), ZoneNameValues))
    {
        for (const TSharedPtr<FJsonValue>& Name : *ZoneNameValues)
        {
            ZoneOf.Add(static_cast<uint8>(MarburgumII::Street::ZoneNamed(Name.IsValid() ? Name->AsString() : FString())));
        }
    }
    int32 Zoned = 0;

    FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumWalkGraph), true);
    FCollisionObjectQueryParams Static(FCollisionObjectQueryParams::AllStaticObjects);
    const FCollisionShape Body = FCollisionShape::MakeCapsule(BodyRadiusCm, BodyHalfHeightCm);
    const FCollisionShape Sweep = FCollisionShape::MakeCapsule(SweepRadiusCm, SweepHalfHeightCm);
    // Nobody is led into the river (ADR-046): a node or an edge in the water goes too.
    const UMarburgumWaterSubsystem* Water = World.GetSubsystem<UMarburgumWaterSubsystem>();

    const TArray<TSharedPtr<FJsonValue>>& JsonNodes = Root->GetArrayField(TEXT("nodes"));
    TArray<int32> Index;
    Index.Init(INDEX_NONE, JsonNodes.Num());
    TArray<FNode> Kept;
    int32 NoGround = 0;
    int32 NoRoom = 0;
    int32 Wet = 0;
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
        // A bridge railing is not a wall: a body on the deck may touch it. A building still is.
        bool bNoRoom = false;
        TArray<FOverlapResult> Overlaps;
        if (World.OverlapMultiByObjectType(Overlaps, At + FVector(0.0, 0.0, BodyHalfHeightCm + 8.0), FQuat::Identity, Static, Body, Params))
        {
            for (const FOverlapResult& Overlap : Overlaps)
            {
                // The thick paving collision at a bridgehead is not a wall. The landscape still is: a body inside it is not standing.
                const bool bDeckOrPaving = IsBridgeStructure(Overlap.GetActor(), Overlap.GetComponent()) || IsPaving(Overlap.GetComponent());
                if (!(bBridge && bDeckOrPaving))
                {
                    bNoRoom = true;
                    break;
                }
            }
        }
        if (bNoRoom)
        {
            ++NoRoom;
            continue;
        }
        if (Water && Water->IsWet(At))
        {
            ++Wet;
            continue;
        }
        FNode Node;
        Node.Location = At;
        Node.bRoad = (NodeFlags & RoadFlag) != 0;
        Node.bBridge = bBridge;
        if (Row.Num() >= 6 && ZoneOf.IsValidIndex(static_cast<int32>(Row[4]->AsNumber())))
        {
            Node.Zone = ZoneOf[static_cast<int32>(Row[4]->AsNumber())];
            Node.Activity = FMath::Clamp(static_cast<float>(Row[5]->AsNumber()), 0.f, 1.f);
            ++Zoned;
        }
        Index[Source] = Kept.Add(Node);
    }

    int32 BridgeKind = 4;
    const TArray<TSharedPtr<FJsonValue>>* KindNames = nullptr;
    if (Root->TryGetArrayField(TEXT("kinds"), KindNames))
    {
        for (int32 Kind = 0; Kind < KindNames->Num(); ++Kind)
        {
            if ((*KindNames)[Kind].IsValid() && (*KindNames)[Kind]->AsString() == TEXT("bridge"))
            {
                BridgeKind = Kind;
            }
        }
    }
    TArray<FEdge> KeptEdges;
    int32 Blocked = 0;
    int32 Missing = 0;
    int32 WetEdges = 0;
    int32 BridgeEdgesRead = 0;
    int32 BridgeEdgesKept = 0;
    int32 BridgeSpans = 0;
    FString BridgeBlocks;
    int32 BridgeBlocksNoted = 0;
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
        const int32 Kind = static_cast<int32>(Row[2]->AsNumber());
        const bool bBridgeEdge = Kind == BridgeKind || (Kept[Index[SourceA]].bBridge && Kept[Index[SourceB]].bBridge);
        BridgeEdgesRead += bBridgeEdge ? 1 : 0;
        const FVector Lift(0.0, 0.0, SweepLiftCm);
        FVector SweepFrom = A + Lift;
        const FVector SweepTo = B + Lift;
        bool bCut = false;
        // The paving at a bridgehead is the ground, not a wall. Step past the deck and the paving and keep
        // looking, so a building behind them still cuts the edge. An ordinary edge stops at the first hit.
        for (int32 Attempt = 0; Attempt < 6 && !bCut; ++Attempt)
        {
            FHitResult Hit;
            if (!World.SweepSingleByObjectType(Hit, SweepFrom, SweepTo, FQuat::Identity, Static, Sweep, Params))
            {
                break;
            }
            const bool bPass = MarburgumII::Street::BridgeEdgeMayPass(bBridgeEdge, IsDeckOrGround(Hit.GetActor(), Hit.GetComponent()));
            if (!bPass)
            {
                bCut = true;
                ++Blocked;
                if (bBridgeEdge && BridgeBlocksNoted < 4)
                {
                    BridgeBlocks += FString::Printf(TEXT("%s%s"), BridgeBlocksNoted ? TEXT(",") : TEXT(""), *HitLabel(Hit));
                    ++BridgeBlocksNoted;
                }
                break;
            }
            const FVector Step = (SweepTo - SweepFrom).GetSafeNormal() * 40.0;
            SweepFrom = Hit.Location + Step;
            if (FVector::DistSquared(SweepFrom, SweepTo) <= FMath::Square(40.0) || FVector::DotProduct(SweepTo - SweepFrom, Step) <= 0.0)
            {
                break;
            }
        }
        if (bCut)
        {
            continue;
        }
        if (Water && Water->IsWetWay(A, B))
        {
            ++WetEdges;
            continue;
        }
        FEdge Edge;
        Edge.A = Index[SourceA];
        Edge.B = Index[SourceB];
        Edge.LengthCm = FVector::Dist(A, B);
        Edge.Kind = static_cast<uint8>(Kind);
        // The river runs east-west in plan, so a deck that crosses it changes Unreal X.
        BridgeSpans += bBridgeEdge && FMath::Abs(A.X - B.X) >= 800.0 ? 1 : 0;
        BridgeEdgesKept += bBridgeEdge ? 1 : 0;
        KeptEdges.Add(Edge);
    }

    // The places that draw people in (ADR-069), by kind name so a file written in another order still reads right.
    TArray<FAttractor> KeptAttractors;
    int32 AttractorsRead = 0;
    int32 AttractorNoNode = 0;
    int32 AttractorNoSpot = 0;
    int32 SpotsPulled = 0;
    const TSharedPtr<FJsonObject>* AttractorObject = nullptr;
    if (Root->TryGetObjectField(TEXT("attractors"), AttractorObject))
    {
        using namespace MarburgumII::Street;
        TArray<EAttractor> KindOf;
        const TArray<TSharedPtr<FJsonValue>>* AttractorKindNames = nullptr;
        if ((*AttractorObject)->TryGetArrayField(TEXT("kinds"), AttractorKindNames))
        {
            for (const TSharedPtr<FJsonValue>& Name : *AttractorKindNames)
            {
                KindOf.Add(AttractorNamed(Name.IsValid() ? Name->AsString() : FString()));
            }
        }
        // Somewhere to stand: walkable ground under it, room for a body, out of the river, and a clear way from From.
        const auto Spot = [&](const FVector& From, const FVector& Plan, FVector& Out)
        {
            const FVector Top(Plan.X, Plan.Y, From.Z + GroundAboveCm);
            const FVector Bottom(Plan.X, Plan.Y, From.Z - GroundBelowCm);
            FHitResult Hit;
            if (!World.LineTraceSingleByObjectType(Hit, Top, Bottom, Static, Params) || Hit.ImpactNormal.Z < WalkableNormalZ)
            {
                return false;
            }
            const FVector At = Hit.ImpactPoint;
            const FVector Lift(0.0, 0.0, SweepLiftCm);
            FHitResult Blocked;
            if (World.OverlapAnyTestByObjectType(At + FVector(0.0, 0.0, BodyHalfHeightCm + 8.0), FQuat::Identity, Static, Body, Params)
                || World.SweepSingleByObjectType(Blocked, From + Lift, At + Lift, FQuat::Identity, Static, Sweep, Params)
                || (Water && (Water->IsWet(At) || Water->IsWetWay(From, At))))
            {
                return false;
            }
            Out = At;
            return true;
        };
        // Not Items: in a unity build a file-scope using-directive makes MarburgumII::Gameplay::Items visible here.
        const TArray<TSharedPtr<FJsonValue>>* AttractorRows = nullptr;
        if ((*AttractorObject)->TryGetArrayField(TEXT("items"), AttractorRows))
        {
            for (const TSharedPtr<FJsonValue>& Value : *AttractorRows)
            {
                const TArray<TSharedPtr<FJsonValue>>& Row = Value->AsArray();
                if (Row.Num() < 6)
                {
                    continue;
                }
                ++AttractorsRead;
                const int32 KindIndex = static_cast<int32>(Row[2]->AsNumber());
                const int32 SourceNode = static_cast<int32>(Row[3]->AsNumber());
                if (!KindOf.IsValidIndex(KindIndex) || KindOf[KindIndex] == EAttractor::Count || !Index.IsValidIndex(SourceNode) || Index[SourceNode] == INDEX_NONE)
                {
                    ++AttractorNoNode;
                    continue;
                }
                const FVector NodeAt = Kept[Index[SourceNode]].Location;
                FAttractor Attractor;
                Attractor.Kind = static_cast<uint8>(KindOf[KindIndex]);
                Attractor.Node = Index[SourceNode];
                const FVector PlacePlan = PlanToUnreal(Row[0]->AsNumber(), Row[1]->AsNumber(), 0.0);
                Attractor.Place = FVector(PlacePlan.X, PlacePlan.Y, NodeAt.Z);
                const FVector SpotPlan = PlanToUnreal(Row[4]->AsNumber(), Row[5]->AsNumber(), 0.0);
                const FVector2D Toward = FVector2D(NodeAt - SpotPlan);
                const double Reach = Toward.Size();
                FVector First;
                bool bFound = false;
                for (double Pull = 0.0; Pull <= FMath::Min(SpotPullMostCm, Reach) && !bFound; Pull += SpotPullStepCm)
                {
                    const FVector2D At = FVector2D(SpotPlan) + Toward.GetSafeNormal() * Pull;
                    bFound = Spot(NodeAt, FVector(At, 0.0), First);
                    SpotsPulled += bFound && Pull > 0.0 ? 1 : 0;
                }
                if (!bFound)
                {
                    ++AttractorNoSpot;
                    continue;
                }
                Attractor.Spots.Add(First);
                if (AttractorKinds[Attractor.Kind].Visit != EVisit::Enter)
                {
                    for (int32 Slot = 1; Slot < AttractorCapacity; ++Slot)
                    {
                        const FVector2D Offset = AttractorSlotOffset(FVector2D(First - NodeAt), Slot);
                        FVector Next;
                        if (Spot(First, First + FVector(Offset, 0.0), Next))
                        {
                            Attractor.Spots.Add(Next);
                        }
                    }
                }
                KeptAttractors.Add(MoveTemp(Attractor));
            }
        }
    }

    const int32 Read = JsonNodes.Num();
    const int32 EdgesRead = Root->GetArrayField(TEXT("edges")).Num();
    Build(MoveTemp(Kept), MoveTemp(KeptEdges), MoveTemp(KeptAttractors));
    int32 LargestCount = 0;
    for (const FNode& Node : Nodes)
    {
        LargestCount += Node.Component == Largest ? 1 : 0;
    }
    int32 ByZone[MarburgumII::Street::ZoneCount] = {};
    for (const FNode& Node : Nodes)
    {
        ++ByZone[FMath::Clamp<int32>(Node.Zone, 0, MarburgumII::Street::ZoneCount - 1)];
    }
    FString Zones;
    for (int32 Zone = 0; Zone < MarburgumII::Street::ZoneCount; ++Zone)
    {
        Zones += FString::Printf(TEXT("%s%s:%d"), Zone ? TEXT(",") : TEXT(""), MarburgumII::Street::ZoneNames[Zone], ByZone[Zone]);
    }
    int32 ByKind[MarburgumII::Street::AttractorCount] = {};
    int32 Spots = 0;
    for (const FAttractor& Attractor : Attractors)
    {
        ++ByKind[FMath::Clamp<int32>(Attractor.Kind, 0, MarburgumII::Street::AttractorCount - 1)];
        Spots += Attractor.Spots.Num();
    }
    FString Kinds;
    for (int32 Kind = 0; Kind < MarburgumII::Street::AttractorCount; ++Kind)
    {
        Kinds += FString::Printf(TEXT("%s%s:%d"), Kind ? TEXT(",") : TEXT(""), MarburgumII::Street::AttractorNames[Kind], ByKind[Kind]);
    }
    int32 CrossRiver = 0;
    if (Largest != INDEX_NONE)
    {
        double MinX = TNumericLimits<double>::Max();
        double MaxX = TNumericLimits<double>::Lowest();
        for (const FNode& Node : Nodes)
        {
            if (Node.Component != Largest)
            {
                continue;
            }
            MinX = FMath::Min(MinX, Node.Location.X);
            MaxX = FMath::Max(MaxX, Node.Location.X);
        }
        // Stari most is the origin. A component on both banks reaches well north and south of it (Unreal X).
        CrossRiver = MinX < -2000.0 && MaxX > 2000.0 ? 1 : 0;
    }
    OutReport = FString::Printf(TEXT("nodes=%d/%d edges=%d/%d no_ground=%d no_room=%d edges_blocked=%d edges_missing_end=%d largest_component=%d water_volumes=%d wet=%d edges_wet=%d zoned=%d zones=%s attractors=%d/%d attractor_no_node=%d attractor_no_spot=%d attractor_spots=%d attractor_spots_pulled=%d attractor_kinds=%s bridge_edges=%d/%d bridge_spans=%d cross_river=%d bridge_blocks=%s"),
        Nodes.Num(), Read, Edges.Num(), EdgesRead, NoGround, NoRoom, Blocked, Missing, LargestCount, Water ? Water->NumVolumes() : 0, Wet, WetEdges,
        Zoned, *Zones, Attractors.Num(), AttractorsRead, AttractorNoNode, AttractorNoSpot, Spots, SpotsPulled, *Kinds,
        BridgeEdgesKept, BridgeEdgesRead, BridgeSpans, CrossRiver, BridgeBlocks.IsEmpty() ? TEXT("-") : *BridgeBlocks);
    if (Nodes.IsEmpty())
    {
        OutError = TEXT("no node of the walk graph is on walkable ground in this level");
        return false;
    }
    return true;
}

void FMarburgumWalkGraph::Build(TArray<FNode> InNodes, TArray<FEdge> InEdges, TArray<FAttractor> InAttractors)
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
    // A place whose node went with it goes too.
    Attractors.Reset();
    for (FAttractor& Attractor : InAttractors)
    {
        if (Remap.IsValidIndex(Attractor.Node) && Remap[Attractor.Node] != INDEX_NONE && !Attractor.Spots.IsEmpty())
        {
            Attractor.Node = Remap[Attractor.Node];
            Attractors.Add(MoveTemp(Attractor));
        }
    }
    Index();
}

int32 FMarburgumWalkGraph::ComponentSize(int32 Node) const
{
    if (!Nodes.IsValidIndex(Node))
    {
        return 0;
    }
    const int32 Label = Nodes[Node].Component;
    return ComponentSizes.IsValidIndex(Label) ? ComponentSizes[Label] : 0;
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
    ComponentSizes = MoveTemp(Sizes);
    // A place draws people at its own node and at the nodes joined to it within reach, so passers-by on either side
    // of it are drawn in (the original's WalkAttractors).
    NodeAttractors.Reset();
    NodeAttractors.SetNum(Nodes.Num());
    AttractorCells.Reset();
    for (int32 Attractor = 0; Attractor < Attractors.Num(); ++Attractor)
    {
        const int32 Node = Attractors[Attractor].Node;
        NodeAttractors[Node].AddUnique(Attractor);
        for (const int32 Edge : NodeEdges[Node])
        {
            if (Edges[Edge].LengthCm <= MarburgumII::Street::AttractorReachCm)
            {
                NodeAttractors[Other(Edge, Node)].AddUnique(Attractor);
            }
        }
        AttractorCells.FindOrAdd(CellOf(Attractors[Attractor].Spots[0])).Add(Attractor);
    }
}

const TArray<int32>& FMarburgumWalkGraph::AttractorsFrom(int32 Node) const
{
    static const TArray<int32> None;
    return NodeAttractors.IsValidIndex(Node) ? NodeAttractors[Node] : None;
}

void FMarburgumWalkGraph::AttractorsWithin(const FVector& At, double RadiusCm, TArray<int32>& Out) const
{
    Out.Reset();
    const int32 Reach = FMath::CeilToInt(RadiusCm / CellCm);
    const FIntPoint Centre = CellOf(At);
    for (int32 DX = -Reach; DX <= Reach; ++DX)
    {
        for (int32 DY = -Reach; DY <= Reach; ++DY)
        {
            if (const TArray<int32>* Cell = AttractorCells.Find(Centre + FIntPoint(DX, DY)))
            {
                for (const int32 Attractor : *Cell)
                {
                    if (FVector::DistSquared2D(Attractors[Attractor].Spots[0], At) <= RadiusCm * RadiusCm)
                    {
                        Out.Add(Attractor);
                    }
                }
            }
        }
    }
    Out.Sort();
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
