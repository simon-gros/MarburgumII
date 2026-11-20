#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include <vector>

#include "MarburgumInteriorSubsystem.generated.h"

namespace MarburgumInteriors
{
    struct FPlan;
    struct FLayout;
}

/**
 * Streams ground floors for /Game/World/lent (ADR-076). A building is built when a player (or, on a client, its
 * own camera) is within 22 m of it, or inside it, and taken down again past 40 m. The geometry is local. The
 * server also spawns the doors, which replicate. -MB2NoInteriors leaves the shells as they are.
 *
 * A building gets rooms only when a player's capsule passes through its street doorway in the world's own
 * collision and the street meets the threshold within a step. Until its shell is rebuilt with the door cut it
 * stays closed: no rooms hidden inside, no door hung on a solid wall.
 *
 * A server started with -MB2DoorStarts also puts two player starts in front of every eligible street door, tagged
 * door_<building id>, for test drivers and playtests (?MB2Start=door_r1539756 is Hotel Lent).
 */
UCLASS()
class MARBURGUMII_API UMarburgumInteriorSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;

    int32 NumEligible() const { return Eligible; }
    int32 NumExceptions() const { return Exceptions; }
    int32 NumStreamed() const;
    bool IsStreamed(const FString& BuildingId) const;

    /** The probe through one building's street door. */
    struct FEntry
    {
        bool bOpen = false;
        // Street level in front of the door minus the threshold, centimetres.
        float StepCm = 0.f;
        FString Reason;
        FString Blocker;
    };

    // The character's capsule (MarburgumCharacter.cpp) and the movement component's default step.
    static constexpr float PlayerRadiusCm = 34.f;
    static constexpr float PlayerHalfHeightCm = 88.f;
    static constexpr float MaxStepCm = 40.f;

    /**
     * buildings.json, prepared the way the façade commandlet sees it. Names are the buildings' own, or empty. With
     * bMeasured, each building takes its floor, street and any refusal from interior_floors.json (MarburgumInteriorAudit
     * -WriteFloors), measured against the level's ground.
     */
    static bool ReadPlans(std::vector<MarburgumInteriors::FPlan>& OutPlans, TArray<FString>* OutNames, bool bMeasured = true);

    /**
     * A walk through one building, in world centimetres at floor level: the street in front of the door, just
     * inside, then for each room door the near side, the far side and the room's middle and back, and out to the
     * street again. The same legs MarburgumInteriorAudit sweeps. False for a building without rooms.
     */
    bool GetWalkRoute(const FString& BuildingId, TArray<FVector>& OutPoints) const;

    /** Sweep a player's capsule from the street through the door, against the world's static collision. */
    static FEntry ProbeEntrance(UWorld& World, const MarburgumInteriors::FLayout& Layout, const AActor* Ignore = nullptr);

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    UFUNCTION()
    void UpdateNear();

    struct FData;
    FData* Data = nullptr;

    FTimerHandle Timer;
    int32 Eligible = 0;
    int32 Exceptions = 0;
};
