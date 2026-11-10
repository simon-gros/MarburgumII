#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MarburgumInteractable.generated.h"

class UStaticMeshComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FMarburgumInteractableChanged, class AMarburgumInteractable*);

/**
 * Something the player can interact with (ontology `InteractionDefinition` projected into the level),
 * placed in maps with a stable InteractableId. The base class is an item source: it grants an item from
 * a limited, server-owned stock; clients see the replicated remaining stock but can only request, never
 * take. AMarburgumNpc is the other kind. The server decides what every request means.
 */
UCLASS()
class MARBURGUMII_API AMarburgumInteractable : public AActor
{
    GENERATED_BODY()

public:
    AMarburgumInteractable();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void BeginPlay() override;

    /** Stable content id (snake_case); the persistence key for this object's stock. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marburgum|Interaction")
    FName InteractableId;

    /** Item definition granted per successful interaction. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marburgum|Interaction")
    FName GrantedItemId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marburgum|Interaction")
    FText DisplayName;

    /** Verb shown in the prompt, e.g. "Take". */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marburgum|Interaction")
    FText Verb;

    /** Authored stock used only when the world store has no record for this object yet. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marburgum|Interaction")
    int32 InitialStock = 1;

    /** Interaction reach from the pawn's location, in centimetres. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marburgum|Interaction")
    float InteractionRange = 300.f;

    /** Development fixture rather than authored Marburgum content; always labeled in the HUD. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marburgum|Interaction")
    bool bTestFixture = true;

    /** Whether this grants items from a stock (true for a placed item source with a GrantedItemId). */
    virtual bool IsItemSource() const { return !GrantedItemId.IsNone(); }

    /** Current stock; -1 until the server has applied its durable record. */
    int32 GetRemainingStock() const { return RemainingStock; }
    bool IsStockKnown() const { return RemainingStock >= 0; }

    /** Authority only: set by the world authority after a load or a durable commit. */
    void SetRemainingStockAuthority(int32 NewStock);

    /** Fired on clients when replicated stock changes, and on the server when it is set. */
    static FMarburgumInteractableChanged& OnChanged();

protected:
    UFUNCTION()
    void OnRep_RemainingStock();

    UPROPERTY(VisibleAnywhere, Category = "Marburgum|Interaction")
    TObjectPtr<UStaticMeshComponent> Mesh;

private:
    UPROPERTY(ReplicatedUsing = OnRep_RemainingStock)
    int32 RemainingStock = -1;
};
