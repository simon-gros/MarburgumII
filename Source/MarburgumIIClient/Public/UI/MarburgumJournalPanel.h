#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MarburgumJournalPanel.generated.h"

class AMarburgumPlayerState;
class UTextBlock;
class UVerticalBox;

/**
 * First UI-04 `journal_panel` shell: the local character's missions from the owner-only replicated,
 * server-saved mission record, each with its summary and current objective; the pinned one is marked.
 * Designed states for waiting (not yet received), no character, no missions and filled; it never lists
 * a mission the server has not saved. Updated by the missions-changed event, not polled. Toggled with
 * J; it does not take input focus yet. Completed history and evidence arrive with later stages.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumJournalPanel : public UUserWidget
{
    GENERATED_BODY()

public:
    void Toggle();
    bool IsOpen() const;

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

private:
    void HandleMissionsChanged(AMarburgumPlayerState* State);
    void Rebuild();

    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Rows;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> SummaryLabel;

    FDelegateHandle MissionsHandle;
};
