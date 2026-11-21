#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/MarburgumSheetText.h"
#include "MarburgumCharacterPanel.generated.h"

class AMarburgumPlayerState;
class UBorder;
class UMarburgumCharacterPreview;
class UMarburgumMeter;
class UMarburgumSelectable;
class UTextBlock;
class UScrollBox;
class UVerticalBox;
class UWrapBox;
struct FMarburgumInteractionReceipt;

/**
 * UI-04 `character_panel`, a page of the management shell (C), recomposed as a character sheet (ADR-085):
 *
 *   identity      name, body and the account it is kept with, the body itself in 3D (the creator's stage, rendered only
 *                 while the sheet is open), and where the character is (quarter, street, area, the 1990s date and time);
 *   equipment     the original eight slots in their two columns (Head, Coat, Trousers, Shoes | Hands, Right hand, Left
 *                 hand, Bag), each saying in words whether it is filled, empty, waiting for the server, refused, or has
 *                 nothing that fits it yet; the chosen slot's card lists what you carry that fits and asks the server to
 *                 put it in or take it out;
 *   attributes    Might, Endurance, Eye and Poise as the server keeps them: score, modifier and the parts that make the
 *                 total (base, equipment, other), with what each stands for;
 *   condition     vitality and stamina as the server simulates them (Endurance is not stamina and does not move with it);
 *   saved record  items, missions, money and skill levels, each a way to the screen that holds the details.
 *
 * The cards wrap to the page width, so the sheet reflows rather than overlaps at large text and small screens. Only actual
 * state is shown: a slot changes only when the server's replicated equipment does, and its pending and refused states come
 * from the receipt of the request this sheet sent. Condition updates in place; other changes rebuild the page.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumCharacterPanel : public UUserWidget
{
    GENERATED_BODY()

public:
    void SetOpen(bool bOpen);
    bool IsOpen() const;
    /** Scrolls the content by Lines lines (mouse wheel, Page Up/Down). */
    void Scroll(float Lines);
    UWidget* GetPreferredFocus() const;
    /** The stamina line as shown (for evidence capture); empty while closed or waiting. */
    FText GetShownStaminaText() const;

    /** Shows SlotId's card, as if its tile had been chosen (the UI capture tour's `character:<slot>` step). */
    void SelectSlot(FName SlotId);
    /** Test and capture evidence: the slot whose card is shown, the line its tile shows, and the attribute scores shown. */
    FName GetSelectedSlot() const { return SelectedSlot; }
    FText GetShownSlotState(FName SlotId) const;
    TArray<int32> GetShownScores() const { return ShownScores; }
    /** Test driver: what choosing the card's action for ItemId (None: take out) would send. False when it is not offered. */
    bool RequestForTest(FName ItemId);

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    void HandleStateChanged(AMarburgumPlayerState* State);
    void HandleConditionChanged(AMarburgumPlayerState* State);
    void HandleReceipt(const FMarburgumInteractionReceipt& Receipt);
    void Rebuild();
    void UpdateCondition();
    void ShowSlot(FName SlotId);
    void Request(FName SlotId, FName ItemId);
    void BindReceipts();

    /** Refills the persistent identity card (the 3D body stays where it is). */
    void BuildIdentity(const class FMarburgumSheetContext& Context);
    UWidget* BuildEquipment(const class FMarburgumSheetContext& Context);
    UWidget* BuildAttributes(const class FMarburgumSheetContext& Context);
    UWidget* BuildCondition(const class FMarburgumSheetContext& Context);
    UWidget* BuildRecord(const class FMarburgumSheetContext& Context);

    UPROPERTY(Transient) TObjectPtr<UWrapBox> Cards;
    UPROPERTY(Transient) TObjectPtr<UScrollBox> Scroller;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> SlotDetail;
    UPROPERTY(Transient) TObjectPtr<UMarburgumCharacterPreview> Preview;
    UPROPERTY(Transient) TObjectPtr<UBorder> PreviewFrame;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> IdentityInfo;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StaminaValue;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> VitalityValue;
    UPROPERTY(Transient) TObjectPtr<UMarburgumMeter> StaminaMeter;
    UPROPERTY(Transient) TObjectPtr<UMarburgumMeter> VitalityMeter;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> BreathValue;
    UPROPERTY(Transient) TObjectPtr<UMarburgumSelectable> FirstLink;
    UPROPERTY(Transient) TArray<TObjectPtr<UMarburgumSelectable>> SlotTiles;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SlotStates;

    TArray<FName> SlotIds;
    FName SelectedSlot;
    TArray<int32> ShownScores;
    /** The equipping request this sheet sent, and the last answer per slot until the slot changes again. */
    MarburgumUI::FEquipRequest Pending;
    TMap<FName, TPair<FText, MarburgumUI::ETone>> SlotNotices;
    /** The card's actions as offered, item None = take out; for RequestForTest. */
    TArray<FName> OfferedItems;

    TArray<FDelegateHandle> Handles;
    FDelegateHandle ConditionHandle;
    FDelegateHandle ReceiptHandle;
    TWeakObjectPtr<class AMarburgumPlayerController> ReceiptSource;
};
