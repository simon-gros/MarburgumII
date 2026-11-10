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

/**
 * Server-only interaction authority for one networked world. Owns the durable world store, restores
 * interactable stock, character inventories and mission progress from it, and decides every
 * interaction:
 *
 *   validate (operation id, store, character, receipt replay, target, range)
 *     -> item source: stock check | NPC: mission rules (MarburgumMissionRules.h)
 *     -> build the next record -> make it durable -> apply it to replicated actors -> receipt
 *
 * It also derives each player's named area (AMarburgumArea) from the pawn position four times a
 * second; location is never persisted.
 *
 * Condition (ADR-020): at login it restores the character's saved condition into the player state,
 * where the pawn's movement adopts it; at logout, and for everyone still connected when the world
 * tears down, it saves the condition if it differs from what the store holds. A full, rested body is
 * stored as no entry, so characters that never ran never cause a commit.
 *
 * Nothing becomes visible to any client before it is on disk; a refusal or failed write changes
 * nothing. Options: -MB2WorldStore=<file> (default Saved/MarburgumII/WorldStore/<Map>.json),
 * -MB2StoreFaultInjection (test-only write faults, see FMarburgumWorldStore).
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

private:
    void HandlePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer);
    void HandleLogout(AGameModeBase* GameMode, AController* Exiting);
    void HandleWorldTearDown(UWorld* World);
    void RestoreCondition(AMarburgumPlayerState& State) const;
    void SaveCondition(const AMarburgumPlayerState& State, const TCHAR* Why);
    void ApplyInventory(AMarburgumPlayerState& State) const;
    void ApplyStocks() const;
    void UpdateLocations();
    FMarburgumInteractionReceipt HandleNpc(FMarburgumInteractionReceipt Receipt, const AMarburgumPlayerController& Requester,
        AMarburgumPlayerState& State, const AMarburgumNpc& Npc);
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
};
