#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MarburgumCharacterPanel.generated.h"

class AMarburgumPlayerState;
class UBorder;
class UMarburgumMeter;
class UMarburgumSelectable;
class UTextBlock;
class UScrollBox;
class UVerticalBox;

/**
 * UI-04 `character_panel`, a page of the management shell (C): who the character is (name, body, the account it is
 * kept with), where they are (quarter, street, area and the 1990s date and time), their condition as the server
 * simulates it (vitality and stamina meters, breath and recovery state), and the saved record in brief: items, missions,
 * money and skill levels, each with the key of the screen that holds the details. Only actual state is shown; equipment
 * has no system yet and says so. Condition updates in place; other changes rebuild the page.
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

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

private:
    void HandleStateChanged(AMarburgumPlayerState* State);
    void HandleConditionChanged(AMarburgumPlayerState* State);
    void Rebuild();
    void UpdateCondition();

    UPROPERTY(Transient) TObjectPtr<UVerticalBox> IdentityBox;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Sections;
    UPROPERTY(Transient) TObjectPtr<UScrollBox> Scroller;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StaminaValue;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> VitalityValue;
    UPROPERTY(Transient) TObjectPtr<UMarburgumMeter> StaminaMeter;
    UPROPERTY(Transient) TObjectPtr<UMarburgumMeter> VitalityMeter;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> BreathValue;
    UPROPERTY(Transient) TObjectPtr<UMarburgumSelectable> FirstLink;

    TArray<FDelegateHandle> Handles;
    FDelegateHandle ConditionHandle;
};
