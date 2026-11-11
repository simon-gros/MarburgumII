#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MarburgumSkillsPanel.generated.h"

class AMarburgumPlayerState;
class UTextBlock;
class UVerticalBox;

/**
 * UI-04 `skills_panel` (ADR-047): every registered skill with its level on the original's 0-1990 curve, the XP
 * into the level and to the next, and the character's money. Levels are derived on the client from the
 * owner-only replicated XP with the same integer curve the server uses; the server alone awards XP. Designed
 * states for waiting and no character. Rebuilt on the progress-changed event; toggled with K.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumSkillsPanel : public UUserWidget
{
    GENERATED_BODY()

public:
    void Toggle();
    bool IsOpen() const;

    /** "Fishing 1 · 24 / 20 XP"-style line for one skill as shown (evidence capture); empty while closed. */
    FText GetShownLine(FName SkillId) const;

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

private:
    void HandleProgressChanged(AMarburgumPlayerState* State);
    void Rebuild();

    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Rows;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MoneyLabel;

    TMap<FName, FText> ShownLines;
    FDelegateHandle ProgressHandle;
};
