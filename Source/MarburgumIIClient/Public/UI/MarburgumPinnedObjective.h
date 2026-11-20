#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/MarburgumUITheme.h"
#include "UI/MarburgumUIWidgets.h"
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
 * Updated by the missions-changed event, not polled. When the server moves the mission on, an "updated" chip shows
 * for a moment; when it completes, the tracker ticks it off for a moment before showing the next one.
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
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    void HandleMissionsChanged(AMarburgumPlayerState* State);
    void Refresh();
    void ShowNews(const FText& Label, MarburgumUI::ETone Tone, EMarburgumGlyph Icon);

    UPROPERTY(Transient) TObjectPtr<UBorder> Card;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> KindLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> NameLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ObjectiveLabel;
    UPROPERTY(Transient) TObjectPtr<UBorder> NewsChip;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> NewsLabel;
    UPROPERTY(Transient) TObjectPtr<UMarburgumGlyph> StepGlyph;
    float NewsLeft = 0.f;
    bool bShowingCompletion = false;
    bool bHasShownOnce = false;

    FDelegateHandle MissionsHandle;
    FName ShownMission;
    FName ShownStage;
};
