#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MarburgumPinnedObjective.generated.h"

class AMarburgumPlayerState;
class UBorder;
class UTextBlock;
struct FMarburgumMissionState;

namespace MarburgumII::Gameplay
{
    struct FMissionDefinition;
}

/**
 * `pinned_objective` module (upper left, clear of the notification stack on the right): one brief
 * actionable objective for the pinned mission, from the owner-only replicated mission state. Hidden
 * when no mission is under way or the server has not sent the record yet; it never shows a mission the
 * server has not saved. Until a pin choice exists, the earliest-started active mission is pinned.
 * Updated by the missions-changed event, not polled.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumPinnedObjective : public UUserWidget
{
    GENERATED_BODY()

public:
    /** The mission currently pinned on screen; None when the module is hidden. */
    FName GetShownMission() const { return ShownMission; }
    FName GetShownStage() const { return ShownStage; }

    /** The mission this HUD pins for a player state, if any (shared with the Journal). */
    static const FMarburgumMissionState* FindPinned(const AMarburgumPlayerState& State);

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

private:
    void HandleMissionsChanged(AMarburgumPlayerState* State);
    void Refresh();

    UPROPERTY(Transient) TObjectPtr<UBorder> Card;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> KindLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> NameLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ObjectiveLabel;

    FDelegateHandle MissionsHandle;
    FName ShownMission;
    FName ShownStage;
};
