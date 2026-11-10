#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MarburgumInventoryPanel.generated.h"

class AMarburgumPlayerState;
class UTextBlock;
class UVerticalBox;

/**
 * First UI-04 `inventory_view` shell: the local character's server-confirmed custody, from the
 * owner-only replicated inventory. Distinct designed states for waiting (not yet received), no
 * character, empty and filled; it never lists an item the server has not committed. Updated by the
 * inventory-changed event, not polled. Toggled with I; it does not take input focus yet.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumInventoryPanel : public UUserWidget
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
    void HandleInventoryChanged(AMarburgumPlayerState* State);
    void Rebuild();

    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Rows;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> CharacterLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> SummaryLabel;

    FDelegateHandle InventoryHandle;
};
