#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MarburgumInteriorVolume.generated.h"

class UInstancedStaticMeshComponent;

namespace MarburgumInteriors
{
    struct FLayout;
}

/**
 * The ground floor of one building (ADR-076), built on this machine from the shared layout. Not replicated:
 * every machine runs the same plan. Floors, ceilings, partitions and lintels, one instanced component each, and a
 * lamp per room on lighting channel 1. The doors are AMarburgumDoor.
 */
UCLASS(NotPlaceable)
class MARBURGUMII_API AMarburgumInteriorVolume : public AActor
{
    GENERATED_BODY()

public:
    AMarburgumInteriorVolume();

    void Build(const MarburgumInteriors::FLayout& Layout);

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> Root;

    UPROPERTY()
    TObjectPtr<UInstancedStaticMeshComponent> Walls;

    UPROPERTY()
    TObjectPtr<UInstancedStaticMeshComponent> Floors;

    UPROPERTY()
    TObjectPtr<UInstancedStaticMeshComponent> Ceilings;

    UPROPERTY()
    FString BuildingId;
};
