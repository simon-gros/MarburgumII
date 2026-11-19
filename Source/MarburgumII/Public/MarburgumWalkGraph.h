#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * The walk graph street people follow (ADR-044), read from the file tools/lent-ground builds from the paved
 * streets (Content/Marburgum/Street/lent_walk_graph.json) and checked against the level it is loaded into:
 * every node is put on the ground found there and must leave room for a body, and every edge must be clear
 * for one walking along it, and neither may be in the river (UMarburgumWaterSubsystem, ADR-046). Whatever fails
 * is dropped, so the graph never leads anyone into a wall or the water.
 *
 * Each node also carries the original's zone and activity (ADR-068, tools/lent-ground street-zones), when the file has them,
 * and the graph the places that draw people in (ADR-069, tools/lent-ground --mode attractors): each a bus stop, a venue's
 * door or a sight, reached from one node, with up to AttractorCapacity spots to stand at, every one checked like a node,
 * and the way to it from the node like an edge. A place whose first spot fails is dropped.
 *
 * Positions are Unreal centimetres, on the walking surface. Edges are undirected.
 */
class MARBURGUMII_API FMarburgumWalkGraph
{
public:
    struct FNode
    {
        FVector Location = FVector::ZeroVector;
        /** Over the carriageway (a crossing). */
        bool bRoad = false;
        int32 Component = INDEX_NONE;
        /** What kind of street it is on (ADR-068): MarburgumStreetContract.h EZone, the old town in a graph without zones. */
        uint8 Zone = 0;
        /** How busy that street is with people, 0..1 (the original's activity); 1 in a graph without zones. */
        float Activity = 1.f;
    };

    /** A place that draws people in (ADR-069). */
    struct FAttractor
    {
        /** MarburgumStreetContract.h EAttractor. */
        uint8 Kind = 0;
        /** The node it is reached from. */
        int32 Node = INDEX_NONE;
        /** The place itself: the door, the stop, the sight; at standing height. */
        FVector Place = FVector::ZeroVector;
        /** Where its visitors stand, on the ground: the first spot, then along the kerb either side (venues have one). */
        TArray<FVector, TInlineAllocator<3>> Spots;
    };

    struct FEdge
    {
        int32 A = 0;
        int32 B = 0;
        double LengthCm = 0.0;
        uint8 Kind = 0;
    };

    /** Reads and checks the graph; OutReport says what was kept. False with OutError when there is none. */
    bool Load(UWorld& World, const FString& Path, FString& OutReport, FString& OutError);
    /** From already-grounded nodes (tests); attractors name nodes by their index in InNodes. */
    void Build(TArray<FNode> InNodes, TArray<FEdge> InEdges, TArray<FAttractor> InAttractors = {});

    bool IsEmpty() const { return Nodes.IsEmpty(); }
    const TArray<FNode>& GetNodes() const { return Nodes; }
    const TArray<FEdge>& GetEdges() const { return Edges; }
    /** Indices into GetEdges() touching a node. */
    const TArray<int32>& Touching(int32 Node) const { return NodeEdges[Node]; }
    /** The far end of an edge from a node. */
    int32 Other(int32 Edge, int32 Node) const { return Edges[Edge].A == Node ? Edges[Edge].B : Edges[Edge].A; }
    /** The nodes of the largest component. */
    int32 LargestComponent() const { return Largest; }

    /** The nearest node within RadiusCm (horizontal, and within HeightCm of Z), or INDEX_NONE. */
    int32 NearestNode(const FVector& At, double RadiusCm, double HeightCm = 400.0) const;
    /** Every node within RadiusCm (horizontal). */
    void NodesWithin(const FVector& At, double RadiusCm, TArray<int32>& Out) const;

    const TArray<FAttractor>& GetAttractors() const { return Attractors; }
    /** The places that draw people at a node: its own and those of the nodes joined to it within AttractorReachCm. */
    const TArray<int32>& AttractorsFrom(int32 Node) const;
    /** Every place within RadiusCm (horizontal) of a point. */
    void AttractorsWithin(const FVector& At, double RadiusCm, TArray<int32>& Out) const;

    static constexpr double CellCm = 2000.0;

private:
    void Index();
    FIntPoint CellOf(const FVector& At) const;

    TArray<FNode> Nodes;
    TArray<FEdge> Edges;
    TArray<TArray<int32>> NodeEdges;
    TMap<FIntPoint, TArray<int32>> Cells;
    int32 Largest = INDEX_NONE;
    TArray<FAttractor> Attractors;
    TArray<TArray<int32>> NodeAttractors;
    TMap<FIntPoint, TArray<int32>> AttractorCells;
};
