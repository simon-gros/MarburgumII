#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "MarburgumInteractionTypes.h"
#include "MarburgumPlayerState.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FMarburgumInventoryChanged, class AMarburgumPlayerState*);
DECLARE_MULTICAST_DELEGATE_OneParam(FMarburgumPlayerStateChanged, class AMarburgumPlayerState*);

/**
 * Network-visible per-player state. The development character key, inventory, missions, current area,
 * condition, skills, money and fishing are server-owned and replicate to their owner only (custody and progress are private),
 * never to other players.
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

    /**
     * The signed-in character's identity (ADR-055): first and last name (the replicated player name is "First Last")
     * and body model, visible to everyone. Empty/None for a development key.
     */
    bool HasIdentity() const { return !Model.IsNone(); }
    const FString& GetFirstName() const { return FirstName; }
    const FString& GetLastName() const { return LastName; }
    FName GetModel() const { return Model; }
    /** Authority only, once at admission. */
    void SetIdentityAuthority(const FString& InFirstName, const FString& InLastName, FName InModel);
    /** Fired wherever the replicated model arrives, and on the server when set. */
    static FMarburgumPlayerStateChanged& OnIdentityChanged();

    /** Owner-visible: whether this connection has a character that can hold items. */
    bool HasCharacter() const { return bHasCharacter; }

    /** Owner-visible: false until the server has loaded the durable inventory for this character. */
    bool IsInventoryKnown() const { return bInventoryKnown; }

    const TArray<FMarburgumItemStack>& GetInventory() const { return Inventory; }
    int32 CountItem(FName ItemId) const;

    /** Authority only: replace the replicated inventory with the durable record. */
    void SetInventoryAuthority(const TArray<FMarburgumItemStack>& Stacks);

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

    /**
     * Owner-visible: the body's condition in whole points. Only meaningful once IsConditionKnown(),
     * which the server sets after restoring the character's saved condition (ADR-020).
     */
    const FMarburgumCondition& GetCondition() const { return Condition; }
    bool IsConditionKnown() const { return bConditionKnown; }

    /**
     * Authority only. RestoreConditionAuthority installs the saved condition (the pawn's movement adopts
     * it); UpdateConditionAuthority mirrors the movement component's simulation. Both are no-ops when
     * nothing visible changed.
     */
    void RestoreConditionAuthority(const FMarburgumCondition& Restored);
    void UpdateConditionAuthority(const FMarburgumCondition& Simulated);

    /** Fired on the owning client when the replicated condition changes, and on the server when set. */
    static FMarburgumPlayerStateChanged& OnConditionChanged();

    /** Server only: increments on every RestoreConditionAuthority, so a pawn knows to adopt it again. */
    int32 GetConditionRestoreCount() const { return ConditionRestoreCount; }

    /**
     * Owner-visible: skill XP and money from the durable record (ADR-047, ADR-049). Only meaningful once
     * IsProgressKnown(). Skills with no XP are absent; level and progress come from MarburgumSkillCurve.
     */
    bool IsProgressKnown() const { return bProgressKnown; }
    const TArray<FMarburgumSkillProgress>& GetSkills() const { return Skills; }
    int64 GetSkillXp(FName SkillId) const;
    int64 GetMoney() const { return Money; }

    /** Authority only: replace the replicated skills and money with the durable record. */
    void SetProgressAuthority(const TArray<FMarburgumSkillProgress>& InSkills, int64 InMoney);

    /** Fired on the owning client when replicated skills or money change, and on the server when set. */
    static FMarburgumPlayerStateChanged& OnProgressChanged();

    /** Owner-visible: the character's line in the water (ADR-048). */
    const FMarburgumFishingStatus& GetFishing() const { return Fishing; }

    /** Authority only. No-op when nothing changed. */
    void SetFishingAuthority(const FMarburgumFishingStatus& InFishing);

    /** Fired on the owning client when the replicated fishing status changes, and on the server when set. */
    static FMarburgumPlayerStateChanged& OnFishingChanged();

protected:
    UFUNCTION()
    void OnRep_Inventory();

    UFUNCTION()
    void OnRep_Missions();

    UFUNCTION()
    void OnRep_Area();

    UFUNCTION()
    void OnRep_Condition();

    UFUNCTION()
    void OnRep_Progress();

    UFUNCTION()
    void OnRep_Fishing();
    UFUNCTION()
    void OnRep_Identity();

private:
    FString CharacterKey;

    UPROPERTY(ReplicatedUsing = OnRep_Identity)
    FString FirstName;
    UPROPERTY(ReplicatedUsing = OnRep_Identity)
    FString LastName;
    UPROPERTY(ReplicatedUsing = OnRep_Identity)
    FName Model;

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

    UPROPERTY(ReplicatedUsing = OnRep_Condition)
    bool bConditionKnown = false;

    UPROPERTY(ReplicatedUsing = OnRep_Condition)
    FMarburgumCondition Condition;

    int32 ConditionRestoreCount = 0;

    UPROPERTY(ReplicatedUsing = OnRep_Progress)
    bool bProgressKnown = false;

    UPROPERTY(ReplicatedUsing = OnRep_Progress)
    TArray<FMarburgumSkillProgress> Skills;

    UPROPERTY(ReplicatedUsing = OnRep_Progress)
    int64 Money = 0;

    UPROPERTY(ReplicatedUsing = OnRep_Fishing)
    FMarburgumFishingStatus Fishing;
};
