#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MarburgumUIGallery.generated.h"

class UVerticalBox;

/**
 * UI-01 component gallery (-MB2UIGallery on the front-end). Renders every token and primitive with
 * fixture values under a visible SAMPLE DATA marker; it issues no commands and reads no game state.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumUIGallery : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeOnInitialized() override;

private:
    UVerticalBox* BuildTypography();
    UVerticalBox* BuildColors();
    UVerticalBox* BuildControls();
    UVerticalBox* BuildFeedback();
};

/**
 * Opt-in developer overlay (-MB2UIDiagnostics): live replicated session values for evidence captures.
 * Clearly labeled DIAGNOSTIC and never part of the default HUD.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumDiagnosticsOverlay : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    UPROPERTY(Transient) TObjectPtr<class UTextBlock> ServerValue;
    UPROPERTY(Transient) TObjectPtr<class UTextBlock> ContractValue;
    UPROPERTY(Transient) TObjectPtr<class UTextBlock> HeartbeatValue;
    UPROPERTY(Transient) TObjectPtr<class UTextBlock> PlayersValue;
    UPROPERTY(Transient) TObjectPtr<class UTextBlock> OutcomesValue;
    double NextRefreshSeconds = 0.0;
};
