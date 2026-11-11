#pragma once

#include "CoreMinimal.h"
#include "CollisionQueryParams.h"
#include "Subsystems/WorldSubsystem.h"
#include "MarburgumWater.generated.h"

class APhysicsVolume;

/**
 * The footprints of the level's water (ADR-046): one column per water physics volume, its bounds, from the bed
 * up to the surface. Pure bookkeeping, so the tests can build one without a level.
 */
class MARBURGUMII_API FMarburgumWaterColumns
{
public:
    struct FColumn
    {
        FBox Box = FBox(ForceInit);
        TWeakObjectPtr<APhysicsVolume> Volume;
    };

    void Reset();
    void Add(const FBox& Box, APhysicsVolume* Volume = nullptr);
    bool IsEmpty() const { return Columns.IsEmpty(); }
    const TArray<FColumn>& Get() const { return Columns; }

    /**
     * The columns under Feet: its plan point inside the footprint, Feet no lower than the column's bottom and no
     * more than DropCm above its surface.
     */
    void Under(const FVector& Feet, double DropCm, TArray<int32, TInlineAllocator<4>>& Out) const;

private:
    TArray<FColumn> Columns;
    FBox Bounds = FBox(ForceInit);
};

/**
 * Where the water is, for anyone who walks (ADR-046): the Drava's water physics volumes (ADR-032). Street people
 * and the Lent cast ask it before every step, and the walk graph before it keeps a node or an edge, so nobody
 * walks into the river. A swimmer is not stopped; they are already in.
 *
 * A point is wet when it is over water and there is nothing to stand on above the surface there: a bridge deck
 * or a quay over the river is dry, the river under the quay's edge is not.
 */
UCLASS()
class MARBURGUMII_API UMarburgumWaterSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;

    /** Standing with the feet at Feet would put a body in the water. */
    bool IsWet(const FVector& Feet) const;
    /** Some point of the straight way from FromFeet to ToFeet (FromFeet itself aside) is wet. */
    bool IsWetWay(const FVector& FromFeet, const FVector& ToFeet) const;
    int32 NumVolumes() const;

    /** Samples along a way are this far apart. */
    static constexpr double WayStepCm = 50.0;

private:
    void Collect() const;

    mutable FMarburgumWaterColumns Columns;
    /** Traces for the ground over the water look through the water volumes themselves. */
    mutable FCollisionQueryParams GroundParams;
    mutable bool bCollected = false;
};
