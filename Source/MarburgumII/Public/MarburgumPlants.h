#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MarburgumVegetation.h"
#include "Subsystems/WorldSubsystem.h"

#include "MarburgumPlants.generated.h"

class UHierarchicalInstancedStaticMeshComponent;

/**
 * The trees and bushes standing in the world (GFX-01): every plant of Content/Marburgum/Vegetation/lent_plants.json,
 * grounded where it is loaded, as instances of the original game's species meshes, one component per species.
 *
 * The leaves never block. Each tree's trunk does, through an invisible cylinder in one more instanced component, so the
 * server and every client put up the same trunks from the same file without replicating them, as the street lamps do. A
 * dedicated server puts up only the trunks.
 */
UCLASS(NotPlaceable, Transient)
class MARBURGUMII_API AMarburgumPlants : public AActor
{
    GENERATED_BODY()

public:
    AMarburgumPlants();

    /** Puts the plants up. bDrawn false (a dedicated server) puts up the trunks only. */
    void Build(const TArray<MarburgumII::Vegetation::FSpecies>& Species, const TArray<MarburgumII::Vegetation::FPlant>& Plants, bool bDrawn);

    /** Species drawn, species whose mesh is missing, and trunks put up. */
    int32 NumDrawnSpecies() const { return DrawnSpecies; }
    int32 NumMissingMeshes() const { return MissingMeshes; }
    int32 NumTrunks() const { return Trunks; }

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> Root;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TrunkBlockers;

    UPROPERTY(VisibleAnywhere)
    TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> Kinds;

    int32 DrawnSpecies = 0;
    int32 MissingMeshes = 0;
    int32 Trunks = 0;
};

/**
 * Puts the plants up on the Lent map (/Game/World/lent), on the server and on every client alike. -MB2NoPlants leaves
 * them out, for captures of the level as it was and for A/B frame times.
 */
UCLASS()
class MARBURGUMII_API UMarburgumPlantsSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;

    AMarburgumPlants* GetPlants() const { return Plants; }
    /** Plants in the file, and how many found ground where they stand. */
    int32 NumRead() const { return Read; }
    int32 NumGrounded() const { return Grounded; }

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    UPROPERTY(Transient)
    TObjectPtr<AMarburgumPlants> Plants;

    int32 Read = 0;
    int32 Grounded = 0;
};
