#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "MarburgumAuthorityService.h"
#include "MarburgumWorldAuthority.generated.h"

class AGameModeBase;
class AMarburgumNpc;
class AMarburgumPlayerState;
class APlayerController;
class FMarburgumWorldStore;
struct FMarburgumStoredReceipt;
struct FMarburgumWorldRecord;

/**
 * Server-only interaction authority for one networked world. Owns the durable world store, restores
 * interactable stock, character inventories and mission progress from it, and decides every
 * interaction:
 *
 *   validate (operation id, store, character, receipt replay, target, range)
 *     -> item source: stock check | fishing spot: cast or strike (MarburgumFishingRules.h)
 *        | NPC or authored place: mission rules (MarburgumMissionRules.h) | decision: the choice rules
 *     -> build the next record -> make it durable -> apply it to replicated actors -> receipt
 *
 * On the Lent levels it spawns the original game's mission people and places from the catalogue (ADR-049):
 * at their original positions, or at their original offset from the angler where the map moved him.
 *
 * Fishing (ADR-048): a cast is transient server state, never stored. Ten times a second the server moves each
 * line from waiting to a bite and lets the fish escape when the window passes; walking off, sitting in a car,
 * swimming or going down ends the cast. Only a landed fish (with its Fishing XP) is committed.
 *
 * It also derives each player's named area (AMarburgumArea) from the pawn position four times a
 * second; location is never persisted.
 *
 * Condition (ADR-020): at login it restores the character's saved condition into the player state,
 * where the pawn's movement adopts it; at logout, and for everyone still connected when the world
 * tears down, it saves the condition if it differs from what the store holds. A full, rested body is
 * stored as no entry, so characters that never ran never cause a commit.
 *
 * Shop counters (ADR-060): the keeper of a counter carries its shelves and whether it is open on the server's
 * Europe/Ljubljana clock; a purchase or sale is decided by MarburgumRetailRules.h and stored with the character in
 * one commit.
 *
 * Nothing becomes visible to any client before it is on disk; a refusal or failed write changes
 * nothing. Options: -MB2WorldStore=<file> (default Saved/MarburgumII/WorldStore/<Map>.json),
 * -MB2StoreFaultInjection (test-only write faults, see FMarburgumWorldStore), -MB2ClockHour=<h> and
 * -MB2ClockWeekday=<0-6> (the counters' clock, for tests).
 */
UCLASS()
class MARBURGUMIISERVER_API UMarburgumWorldAuthority : public UMarburgumAuthorityService
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;

    virtual FMarburgumInteractionReceipt HandleInteraction(
        AMarburgumPlayerController& Requester, const FGuid& OperationId, AMarburgumInteractable* Target) override;

    virtual FMarburgumInteractionReceipt HandleMissionChoice(AMarburgumPlayerController& Requester, const FGuid& OperationId,
        AMarburgumInteractable* Target, FName MissionId, FName ChoiceId) override;

    virtual FMarburgumInteractionReceipt HandleTrade(AMarburgumPlayerController& Requester, const FGuid& OperationId,
        AMarburgumInteractable* Target, EMarburgumTradeAction Action, FName ItemId, int32 Quantity) override;

private:
    /** One player's line in the water. */
    struct FCast
    {
        FString Spot;
        FVector CastAt = FVector::ZeroVector;
        double BiteAt = 0.0;
        double EscapeAt = 0.0;
        bool bBite = false;
    };

    void HandlePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer);
    void HandleLogout(AGameModeBase* GameMode, AController* Exiting);
    void HandleWorldTearDown(UWorld* World);
    void RestoreCondition(AMarburgumPlayerState& State) const;
    void SaveCondition(const AMarburgumPlayerState& State, const TCHAR* Why);
    void ApplyInventory(AMarburgumPlayerState& State) const;
    void ApplyStocks() const;
    /** Put every counter's shelves and open state on its keeper; also run when the hours may have turned. */
    void ApplyShops();
    void UpdateLocations();
    FMarburgumInteractionReceipt HandleMissionTarget(FMarburgumInteractionReceipt Receipt, const AMarburgumPlayerController& Requester,
        AMarburgumPlayerState& State, const AMarburgumInteractable& Target);
    FMarburgumInteractionReceipt HandleFishing(FMarburgumInteractionReceipt Receipt, AMarburgumPlayerController& Requester,
        AMarburgumPlayerState& State, const AMarburgumInteractable& Spot);
    /** Shared validation of interaction and choice requests; false with Receipt set to the refusal or replay. */
    bool Admit(FMarburgumInteractionReceipt& Receipt, AMarburgumPlayerController& Requester, AMarburgumInteractable* Target,
        AMarburgumPlayerState*& OutState);
    bool Commit(FMarburgumInteractionReceipt& Receipt, FMarburgumWorldRecord& Next, const FMarburgumStoredReceipt& Stored);
    void SpawnAuthoredContent(UWorld& World);
    void TickFishing();
    void EndCast(AMarburgumPlayerController& Player, EMarburgumFishingEnd End, FName Catch, const TCHAR* Why);
    /** Why this player cannot hold a line now, or nullptr. */
    const TCHAR* WhyCannotFish(const AMarburgumPlayerController& Player) const;
    int32 CurrentStock(const AMarburgumInteractable& Target) const;
    AMarburgumInteractable* FindInteractable(const FString& Id) const;
    FMarburgumInteractionReceipt Finish(FMarburgumInteractionReceipt Receipt, const AMarburgumPlayerController& Requester) const;

    TSharedPtr<FMarburgumWorldStore> Store;
    bool bActive = false;
    bool bStoreAvailable = false;
    FDelegateHandle PostLoginHandle;
    FDelegateHandle LogoutHandle;
    FDelegateHandle TearDownHandle;
    FTimerHandle LocationTimer;
    FTimerHandle FishingTimer;
    FTimerHandle ShopTimer;
    TMap<TWeakObjectPtr<AMarburgumPlayerController>, FCast> Casts;
    TMap<TWeakObjectPtr<AMarburgumPlayerController>, double> LastCastAt;
    FRandomStream Rolls;
};
