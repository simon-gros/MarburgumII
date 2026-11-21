#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/MarburgumSheetText.h"
#include "MarburgumInventoryPanel.generated.h"

class AMarburgumPlayerState;
class UHorizontalBox;
class UMarburgumSelectable;
class UTextBlock;
class UScrollBox;
class UVerticalBox;
class UWrapBox;
class AMarburgumPlayerController;
struct FMarburgumInteractionReceipt;

/**
 * UI-04 `inventory_view`, a page of the management shell (I): what the server has confirmed and saved for this
 * character, as a grid of item tiles that can be filtered by kind (tools, fish, evidence, goods), and a detail pane for
 * the focused item: its kind and quantity, the missions that ask for it or gave it, and the counters that buy it back.
 * Actions are shown only as they exist: an item is sold at a counter (B at its keeper) or handed over when a mission
 * asks, and something that can be worn or held (ADR-085) can be put in or taken out of its equipment slots from here;
 * a tile says in words which slot it is in. Equipping never changes custody: the server keeps the copy in the inventory
 * and only notes the slot, and the pane shows waiting, refused and done from the receipt of its own request.
 * Rebuilt on the inventory-changed event (equipment replicates with the inventory).
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
    /** Test driver: choose the card's action for SlotId (equip the shown item there, or take it out). False if not offered. */
    bool RequestForTest(FName SlotId);

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    void HandleInventoryChanged(AMarburgumPlayerState* State);
    void HandleReceipt(const FMarburgumInteractionReceipt& Receipt);
    void BindReceipts();
    void Request(FName SlotId, FName ItemId);
    void Refresh();
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
    FDelegateHandle ReceiptHandle;
    TWeakObjectPtr<AMarburgumPlayerController> ReceiptSource;
    /** The equipping request this pane sent, and the server's last answer to it (shown until the next request). */
    MarburgumUI::FEquipRequest Pending;
    FText Notice;
    MarburgumUI::ETone NoticeTone = MarburgumUI::ETone::Neutral;
    /** (slot, item) pairs the shown card offers; item None takes the shown item out of the slot. */
    TArray<TPair<FName, FName>> OfferedActions;
};
