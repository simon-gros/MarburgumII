#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MarburgumInventoryPanel.generated.h"

class AMarburgumPlayerState;
class UHorizontalBox;
class UMarburgumSelectable;
class UTextBlock;
class UScrollBox;
class UVerticalBox;
class UWrapBox;

/**
 * UI-04 `inventory_view`, a page of the management shell (I): what the server has confirmed and saved for this
 * character, as a grid of item tiles that can be filtered by kind (tools, fish, evidence, goods), and a detail pane for
 * the focused item: its kind and quantity, the missions that ask for it or gave it, and the counters that buy it back.
 * Actions are shown only as they exist: an item is sold at a counter (B at its keeper) or handed over when a mission
 * asks; there is no use-from-inventory action in this build, and the pane says so rather than offer a dead button.
 * Rebuilt on the inventory-changed event; nothing here changes custody.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumInventoryPanel : public UUserWidget
{
    GENERATED_BODY()

public:
    void SetOpen(bool bOpen);
    bool IsOpen() const;
    /** Scrolls the grid by Lines lines (mouse wheel, Page Up/Down). */
    void Scroll(float Lines);
    UWidget* GetPreferredFocus() const;
    /** Shows ItemId's card, as if its tile had been chosen (the UI capture tour's `inventory:<item>` step). */
    void Select(FName ItemId);

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

private:
    void HandleInventoryChanged(AMarburgumPlayerState* State);
    void Rebuild();
    void ShowDetail(FName ItemId);
    void SetFilter(int32 Filter);

    UPROPERTY(Transient) TObjectPtr<UWrapBox> Grid;
    UPROPERTY(Transient) TObjectPtr<UHorizontalBox> Filters;
    UPROPERTY(Transient) TObjectPtr<UScrollBox> Scroller;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> SummaryLabel;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> DetailBox;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> StateBox;
    UPROPERTY(Transient) TArray<TObjectPtr<UMarburgumSelectable>> FilterWidgets;
    UPROPERTY(Transient) TArray<TObjectPtr<UMarburgumSelectable>> TileWidgets;

    TArray<FName> TileItems;
    FName SelectedItem;
    /** 0 all, then EItemKind + 1. */
    int32 Filter = 0;
    FDelegateHandle InventoryHandle;
};
