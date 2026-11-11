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
    /** From already-grounded nodes (tests). */
    void Build(TArray<FNode> InNodes, TArray<FEdge> InEdges);

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

    static constexpr double CellCm = 2000.0;

private:
    void Index();
    FIntPoint CellOf(const FVector& At) const;

    TArray<FNode> Nodes;
    TArray<FEdge> Edges;
    TArray<TArray<int32>> NodeEdges;
    TMap<FIntPoint, TArray<int32>> Cells;
    int32 Largest = INDEX_NONE;
};
