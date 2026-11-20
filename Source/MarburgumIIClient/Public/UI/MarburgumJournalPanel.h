#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MarburgumJournalPanel.generated.h"

class AMarburgumPlayerState;
class UMarburgumSelectable;
class UTextBlock;
class UScrollBox;
class UVerticalBox;

/**
 * UI-04 `journal_panel`, a page of the management shell (J): the character's missions as an archive, under way first
 * (the pinned one marked) then completed, and a dossier for the focused one on a paper surface: what the mission is
 * about, the steps already taken and the current objective (later steps stay unrevealed), the evidence in hand that it
 * asks for, and for a completed one the journal entry it unlocked, the decision taken and the reward the server paid.
 * Everything comes from the server's replicated mission state and the shared catalogue; nothing here changes progress.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumJournalPanel : public UUserWidget
{
    GENERATED_BODY()

public:
    void SetOpen(bool bOpen);
    bool IsOpen() const;
    /** Scrolls the dossier by Lines lines (mouse wheel, Page Up/Down). */
    void Scroll(float Lines);
    UWidget* GetPreferredFocus() const;

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

private:
    void HandleMissionsChanged(AMarburgumPlayerState* State);
    void Rebuild();
    void ShowDossier(FName MissionId);

    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Rows;
    UPROPERTY(Transient) TObjectPtr<UScrollBox> ListScroller;
    UPROPERTY(Transient) TObjectPtr<UScrollBox> Scroller;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> SummaryLabel;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Dossier;
    UPROPERTY(Transient) TArray<TObjectPtr<UMarburgumSelectable>> RowWidgets;

    TArray<FName> RowMissions;
    FName SelectedMission;
    FDelegateHandle MissionsHandle;
};
