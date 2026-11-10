#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "MarburgumInteractionTypes.h"
#include "MarburgumPlayerState.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FMarburgumInventoryChanged, class AMarburgumPlayerState*);

/**
 * Network-visible per-player state. The development character key and inventory are server-owned;
 * the inventory replicates to its owner only (custody is private), never to other players.
 * Account identity is not implemented: the key is an unauthenticated development identity (ADR-014).
 */
UCLASS()
class MARBURGUMII_API AMarburgumPlayerState : public APlayerState
{
    GENERATED_BODY()

public:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** Server only: the development character key from the join URL; empty if none was supplied. */
    const FString& GetCharacterKey() const { return CharacterKey; }
    void SetCharacterKeyAuthority(const FString& Key);

    /** Owner-visible: whether this connection has a character that can hold items. */
    bool HasCharacter() const { return bHasCharacter; }

    /** Owner-visible: false until the server has loaded the durable inventory for this character. */
    bool IsInventoryKnown() const { return bInventoryKnown; }

    const TArray<FMarburgumItemStack>& GetInventory() const { return Inventory; }
    int32 CountItem(FName ItemId) const;

    /** Authority only: replace the replicated inventory with the durable record. */
    void SetInventoryAuthority(const TArray<FMarburgumItemStack>& Items);

    /** Fired on the owning client when the replicated inventory changes, and on the server when set. */
    static FMarburgumInventoryChanged& OnInventoryChanged();

protected:
    UFUNCTION()
    void OnRep_Inventory();

private:
    FString CharacterKey;

    UPROPERTY(ReplicatedUsing = OnRep_Inventory)
    bool bHasCharacter = false;

    UPROPERTY(ReplicatedUsing = OnRep_Inventory)
    bool bInventoryKnown = false;

    UPROPERTY(ReplicatedUsing = OnRep_Inventory)
    TArray<FMarburgumItemStack> Inventory;
};
