#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "Types/SlateEnums.h"
#include "MarburgumConnectionScreen.generated.h"

class UBorder;
class UButton;
class UEditableTextBox;
class UTextBlock;
class UWidget;

/**
 * Branded front-end and connection shell (UI-02, module `connection_status`, mode `connection`).
 *
 * Renders only what UMarburgumSessionSubsystem has observed: every phase, failure and retry state
 * comes from the session view; the screen itself never decides that a connection succeeded.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumConnectionScreen : public UUserWidget
{
    GENERATED_BODY()

public:
    void ApplyView(const FMarburgumSessionView& View);

    /** Widget that should receive keyboard/controller focus when the screen is shown. */
    UWidget* GetPreferredFocus() const;

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
    UFUNCTION()
    void HandleConnectClicked();
    UFUNCTION()
    void HandleRetryClicked();
    UFUNCTION()
    void HandleCancelClicked();
    UFUNCTION()
    void HandleAddressCommitted(const FText& Text, ETextCommit::Type CommitMethod);

    UMarburgumSessionSubsystem* GetSession() const;
    void UpdateSteps(MarburgumII::Session::EPhase Phase);
    void UpdateElapsed();

    UWidget* BuildBrand();
    UWidget* BuildConnectCard();
    UWidget* BuildFooter();

    UPROPERTY(Transient) TObjectPtr<UEditableTextBox> AddressField;
    UPROPERTY(Transient) TObjectPtr<UButton> ConnectButton;
    UPROPERTY(Transient) TObjectPtr<UButton> RetryButton;
    UPROPERTY(Transient) TObjectPtr<UButton> CancelButton;
    UPROPERTY(Transient) TObjectPtr<UBorder> PhasePill;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PhaseLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Headline;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Detail;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Elapsed;
    UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> StepBars;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> StepLabels;

    MarburgumII::Session::EPhase ShownPhase = MarburgumII::Session::EPhase::Starting;
    double ShownPhaseStarted = 0.0;
    int32 ReachedStep = -1;
    int32 LastElapsedSecond = -1;
};
