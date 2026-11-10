#pragma once

#include "CoreMinimal.h"
#include "MarburgumAuthorityService.h"
#include "MarburgumWorldAuthority.generated.h"

class AGameModeBase;
class AMarburgumPlayerState;
class APlayerController;
class FMarburgumWorldStore;

/**
 * Server-only interaction authority for one networked world (Gate 1). Owns the durable world store,
 * restores interactable stock and character inventories from it, and decides every interaction:
 *
 *   validate (operation id, store, character, receipt replay, target, range, stock)
 *     -> build the next record -> make it durable -> apply it to replicated actors -> receipt
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
    void ApplyInventory(AMarburgumPlayerState& State) const;
    void ApplyStocks() const;
    int32 CurrentStock(const AMarburgumInteractable& Target) const;
    AMarburgumInteractable* FindInteractable(const FString& Id) const;
    FMarburgumInteractionReceipt Finish(FMarburgumInteractionReceipt Receipt, const AMarburgumPlayerController& Requester) const;

    TSharedPtr<FMarburgumWorldStore> Store;
    bool bActive = false;
    bool bStoreAvailable = false;
    FDelegateHandle PostLoginHandle;
};
