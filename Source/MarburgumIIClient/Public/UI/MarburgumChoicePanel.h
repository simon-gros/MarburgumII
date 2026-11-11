#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MarburgumChoicePanel.generated.h"

class AMarburgumInteractable;
class UTextBlock;
class UVerticalBox;

/**
 * A mission decision (ADR-049): the options of the current choice stage at the target, each with its money and
 * skill consequences as the catalogue states them. Number keys pick one; the panel sends the choice and closes,
 * and the server's receipt reports what was recorded. E, walking out of reach or the stage ending closes it
 * without deciding. Options the character cannot afford are shown disabled; the server checks again.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumChoicePanel : public UUserWidget
{
    GENERATED_BODY()

public:
    void Open(AMarburgumInteractable* Target, FName MissionId);
    void Close();
    bool IsOpen() const;

    /** Pick option Number (1-based). False when closed, out of range or unaffordable. */
    bool Choose(int32 Number);

    FName GetShownMission() const { return Mission; }
    int32 GetShownChoiceCount() const { return ChoiceIds.Num(); }

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    void Rebuild();

    UPROPERTY(Transient) TObjectPtr<UTextBlock> MissionLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ObjectiveLabel;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Rows;

    TWeakObjectPtr<AMarburgumInteractable> Target;
    FName Mission;
    FName Stage;
    TArray<FName> ChoiceIds;
    TArray<bool> Affordable;
};
