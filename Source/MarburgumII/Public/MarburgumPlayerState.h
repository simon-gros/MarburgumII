#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "MarburgumInteractionTypes.h"
#include "MarburgumPlayerState.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FMarburgumInventoryChanged, class AMarburgumPlayerState*);
DECLARE_MULTICAST_DELEGATE_OneParam(FMarburgumPlayerStateChanged, class AMarburgumPlayerState*);

/**
 * Network-visible per-player state. The development character key, inventory, missions and current
 * area are server-owned and replicate to their owner only (custody and progress are private), never to
 * other players.
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

    /** Owner-visible: false until the server has loaded this character's durable mission record. */
    bool AreMissionsKnown() const { return bMissionsKnown; }
    const TArray<FMarburgumMissionState>& GetMissions() const { return Missions; }
    const FMarburgumMissionState* FindMission(FName MissionId) const;

    /** Authority only: replace the replicated missions with the durable record. */
    void SetMissionsAuthority(const TArray<FMarburgumMissionState>& InMissions);

    /** Fired on the owning client when replicated missions change, and on the server when set. */
    static FMarburgumPlayerStateChanged& OnMissionsChanged();

    /**
     * Owner-visible: the named area the server places this player's pawn in (None = outside every
     * area). Only meaningful once IsLocationKnown().
     */
    FName GetAreaId() const { return AreaId; }
    bool IsLocationKnown() const { return bLocationKnown; }

    /** Authority only: record the area derived from the pawn position. No-op if unchanged. */
    void SetAreaAuthority(FName InAreaId);

    /** Fired on the owning client when the replicated area changes, and on the server when set. */
    static FMarburgumPlayerStateChanged& OnAreaChanged();

protected:
    UFUNCTION()
    void OnRep_Inventory();

    UFUNCTION()
    void OnRep_Missions();

    UFUNCTION()
    void OnRep_Area();

private:
    FString CharacterKey;

    UPROPERTY(ReplicatedUsing = OnRep_Inventory)
    bool bHasCharacter = false;

    UPROPERTY(ReplicatedUsing = OnRep_Inventory)
    bool bInventoryKnown = false;

    UPROPERTY(ReplicatedUsing = OnRep_Inventory)
    TArray<FMarburgumItemStack> Inventory;

    UPROPERTY(ReplicatedUsing = OnRep_Missions)
    bool bMissionsKnown = false;

    UPROPERTY(ReplicatedUsing = OnRep_Missions)
    TArray<FMarburgumMissionState> Missions;

    UPROPERTY(ReplicatedUsing = OnRep_Area)
    bool bLocationKnown = false;

    UPROPERTY(ReplicatedUsing = OnRep_Area)
    FName AreaId;
};
