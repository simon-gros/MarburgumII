#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MarburgumContentPad.generated.h"

class UStaticMeshComponent;

/**
 * A plain temporary floor under authored places whose map window is not built yet (ADR-052). The server
 * spawns it; it replicates so every client stands on the same floor the server simulates.
 */
UCLASS(NotPlaceable)
class MARBURGUMII_API AMarburgumContentPad : public AActor
{
    GENERATED_BODY()

public:
    AMarburgumContentPad();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** Server, before FinishSpawning: half size of the floor in centimetres. */
    void SetExtentAuthority(const FVector& InExtent);

private:
    UFUNCTION()
    void OnRep_Extent();

    void ApplyExtent();

    UPROPERTY(VisibleAnywhere, Category = "Marburgum|World")
    TObjectPtr<UStaticMeshComponent> Mesh;

    UPROPERTY(ReplicatedUsing = OnRep_Extent)
    FVector_NetQuantize Extent = FVector(50.0, 50.0, 50.0);
};
