#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/MarburgumInputDevice.h"
#include "MarburgumChoicePanel.generated.h"

class AMarburgumInteractable;
class UMarburgumSelectable;
class UTextBlock;
class UVerticalBox;
class UWrapBox;

/** Why the decision card closed: the subsystem gives the keys back to the street either way. */
enum class EMarburgumChoiceClose : uint8
{
    Chosen,
    Cancelled,
    /** Walked out of reach, the stage moved on elsewhere, or another screen opened. */
    Dismissed,
};

DECLARE_DELEGATE_OneParam(FMarburgumChoiceClosed, EMarburgumChoiceClose);

/**
 * A mission decision (ADR-049): the options of the current choice stage at the target, each with its money and
 * skill consequences as the catalogue states them. Options the character cannot afford are shown disabled; the server
 * checks again. While open the card owns the keys (UI-only input): each answer is a focusable row, picked by a click,
 * by moving the focus (arrows, D-pad or stick) and confirming (Enter, Space or A, on release), or by its number key.
 * Escape, E or the gamepad's B close it without deciding, as do walking out of reach and the stage ending.
 *
 * One card sends one choice: the first pick closes it, a held confirm or a repeated key never picks, and the press
 * that opened the card cannot confirm in it (rows act only on a press they saw go down). A deduction's answers
 * (ADR-084) look alike in every state: no chips, no colour, the same focus ring.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumChoicePanel : public UUserWidget
{
    GENERATED_BODY()

public:
    void Open(AMarburgumInteractable* Target, FName MissionId);
    void Close(EMarburgumChoiceClose Why = EMarburgumChoiceClose::Dismissed);
    bool IsOpen() const;

    /** Pick option Number (1-based). False when closed, already sent, out of range or unaffordable. */
    bool Choose(int32 Number);

    FName GetShownMission() const { return Mission; }
    int32 GetShownChoiceCount() const { return ChoiceIds.Num(); }
    /** The row with keyboard or gamepad focus (1-based), 0 when none. */
    int32 GetFocusedNumber() const { return FocusedIndex + 1; }
    /** The device whose keys the hints currently name. */
    EMarburgumInputDevice GetHintDevice() const { return HintDevice; }
    /** Row Number's widget (1-based), for focus on open and for the input smoke's pointer events; null when absent. */
    UMarburgumSelectable* GetRow(int32 Number) const;
    /** The number (1-based) of the answer Choice, 0 when the card does not offer it. */
    int32 FindNumber(FName Choice) const { return ChoiceIds.IndexOfByKey(Choice) + 1; }
    /** The row the keys should start on: the first one that can be chosen. */
    UWidget* GetPreferredFocus() const;

    FMarburgumChoiceClosed OnClosed;

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
    void Rebuild();
    /** The header's back key and the footer's keys, for the device in use. */
    void RefreshHints();
    void RefocusRow();

    UPROPERTY(Transient) TObjectPtr<UTextBlock> MissionLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ObjectiveLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> HintLabel;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Rows;
    UPROPERTY(Transient) TObjectPtr<UWrapBox> KeyHints;
    UPROPERTY(Transient) TObjectPtr<UWidget> HeaderHint;
    UPROPERTY(Transient) TObjectPtr<class UHorizontalBox> Header;
    UPROPERTY(Transient) TArray<TObjectPtr<UMarburgumSelectable>> RowWidgets;

    TWeakObjectPtr<AMarburgumInteractable> Target;
    FName Mission;
    FName Stage;
    TArray<FName> ChoiceIds;
    TArray<bool> Affordable;
    FString ObjectiveSource;
    int32 FocusedIndex = INDEX_NONE;
    bool bSubmitted = false;
    EMarburgumInputDevice HintDevice = EMarburgumInputDevice::KeyboardMouse;
};
