#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MarburgumPlayerCondition.generated.h"

class AMarburgumPlayerState;
class UBorder;
class UProgressBar;
class UTextBlock;
class UVerticalBox;

/**
 * `player_condition` module (lower left, above the chat panel): vitality and stamina of the local
 * character, with a pill while running or winded. Until the server has restored the character's
 * condition it shows a designed waiting state, never a guessed full bar. Stamina follows the owning
 * client's movement prediction, which the server corrects (ADR-020), so the bar moves smoothly while
 * running; vitality and the known state come from the owner-only replicated condition. A rested body
 * dims the card so it stays out of the way of exploration.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumPlayerCondition : public UUserWidget
{
    GENERATED_BODY()

public:
    /** What the card shows (for evidence capture): -1 while waiting for the server. */
    int32 GetShownStamina() const { return ShownStamina; }
    int32 GetShownVitality() const { return ShownVitality; }
    bool IsShowingWinded() const { return bShowingWinded; }
    bool IsShowingRunning() const { return bShowingRunning; }

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    void Refresh();

    UPROPERTY(Transient) TObjectPtr<UBorder> Card;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Bars;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> WaitingLabel;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> VitalityMeter;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> VitalityValue;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> StaminaMeter;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StaminaValue;
    UPROPERTY(Transient) TObjectPtr<UBorder> StatePill;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StateLabel;

    int32 ShownStamina = -1;
    int32 ShownVitality = -1;
    bool bShowingWinded = false;
    bool bShowingRunning = false;
};
