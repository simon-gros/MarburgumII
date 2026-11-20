#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MarburgumPlayerCondition.generated.h"

class AMarburgumPlayerState;
class UBorder;
class UMarburgumGlyph;
class UMarburgumMeter;
class UTextBlock;
class UVerticalBox;
class UWidget;

/**
 * `player_condition` module (lower left): vitality and stamina of the local character as two slim vector meters with
 * their glyphs, and one state chip (running, winded, swimming, holding breath) above them. Until the server has
 * restored the character's condition it shows a designed waiting line, never a guessed full bar. Stamina follows the
 * owning client's movement prediction, which the server corrects (ADR-020), so the bar moves smoothly while running;
 * vitality and the known state come from the owner-only replicated condition.
 *
 * A rested, unhurt body is not news: the cluster fades out after a few quiet seconds and comes back the moment
 * anything changes (Settings can keep it always on). Going down (knocked down, drowned) is critical: a centred notice
 * with the recovery countdown appears above the street, in text and shape as well as colour. Text is re-set only when
 * a shown whole number or the state changes (QA U14); the meters take the smooth values.
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
    /** The state as shown, in stable English capitals ("" when none): DROWNED · n s, KNOCKED DOWN · n s, HOLDING BREATH,
     *  SWIMMING, WINDED or RUNNING. */
    FString GetShownState() const { return ShownState; }
    /** True while the cluster is faded out because nothing about the body is news (for evidence capture). */
    bool IsResting() const { return bResting; }

    /** Hides the cluster while another context owns the lower left (a management screen, the driver's seat). */
    void SetSuppressed(bool bInSuppressed) { bSuppressed = bInSuppressed; }

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    enum class EState : uint8 { None, Running, Winded, Swimming, HoldingBreath, KnockedDown, Drowned };

    void Refresh(float DeltaTime);
    void ShowState(EState State, int32 RecoverySeconds);

    UPROPERTY(Transient) TObjectPtr<UWidget> Cluster;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> WaitingLabel;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Bars;
    UPROPERTY(Transient) TObjectPtr<UMarburgumMeter> VitalityMeter;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> VitalityValue;
    UPROPERTY(Transient) TObjectPtr<UMarburgumGlyph> VitalityGlyph;
    UPROPERTY(Transient) TObjectPtr<UMarburgumMeter> StaminaMeter;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StaminaValue;
    UPROPERTY(Transient) TObjectPtr<UMarburgumGlyph> StaminaGlyph;
    UPROPERTY(Transient) TObjectPtr<UBorder> StateChip;
    UPROPERTY(Transient) TObjectPtr<UMarburgumGlyph> StateGlyph;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StateLabel;
    UPROPERTY(Transient) TObjectPtr<UWidget> DownNotice;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DownTitle;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DownDetail;
    UPROPERTY(Transient) TObjectPtr<UMarburgumMeter> DownMeter;

    int32 ShownStamina = -1;
    int32 ShownVitality = -1;
    bool bShowingWinded = false;
    bool bShowingRunning = false;
    FString ShownState;
    EState Shown = EState::None;
    int32 ShownRecovery = -1;
    int32 DownTotalSeconds = 0;
    /** Seconds the body has been unremarkable; past QuietSeconds the cluster fades out. */
    float QuietFor = 0.f;
    float Opacity = 1.f;
    bool bResting = false;
    bool bSuppressed = false;
    /** Vitality shown a moment ago, so a hit leaves a fading ghost on the bar. */
    float VitalityGhost = -1.f;
};
