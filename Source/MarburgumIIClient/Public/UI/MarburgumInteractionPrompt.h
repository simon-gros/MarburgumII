#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MarburgumInteractionPrompt.generated.h"

class UBorder;
class UTextBlock;
class AMarburgumPlayerController;

/**
 * `interaction_prompt` module: one eligible verb, its key and the subject, for the interactable the
 * controller would request. It sits just above the object on screen so it never covers its target, and
 * falls back to the lower_center_context region when the object's top is off screen. Shows the server's replicated stock,
 * a waiting state while a request is unanswered, and an honest empty / no-character state. It never
 * predicts a result: the item appears only through the server receipt and replicated inventory.
 * Refreshes at 10 Hz and only touches widgets whose text changed.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumInteractionPrompt : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    void Refresh();
    void Place();

    UPROPERTY(Transient) TObjectPtr<UBorder> Card;
    UPROPERTY(Transient) TObjectPtr<UWidget> Placement;
    UPROPERTY(Transient) TObjectPtr<UBorder> KeyCap;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> KeyLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> VerbLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> SubjectLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailLabel;
    UPROPERTY(Transient) TObjectPtr<UBorder> FixturePill;

    float SinceRefresh = 0.f;
    FString LastSignature;
    TWeakObjectPtr<class AMarburgumInteractable> Shown;
};
