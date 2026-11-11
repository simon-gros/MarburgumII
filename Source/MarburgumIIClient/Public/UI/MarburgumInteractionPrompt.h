#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/MarburgumUITheme.h"
#include "MarburgumInteractionPrompt.generated.h"

class UBorder;
class UTextBlock;
class AMarburgumPlayerController;
class AMarburgumPlayerState;
class AMarburgumInteractable;
class AMarburgumNpc;
class AMarburgumVehicle;

/**
 * `interaction_prompt` module: one eligible verb, its key and the subject, for the interactable the
 * controller would request. It sits just above the object on screen so it never covers its target, and
 * falls back to the lower_center_context region when the object's top is off screen. For an item source
 * it shows the server's replicated stock; for an NPC, the mission step it offers the local character
 * (from the owner-only mission record), greyed out when a requirement is visibly missing. Also a waiting
 * state while a request is unanswered and an honest empty / no-character state. It never predicts a
 * result: items and missions appear only through the server receipt and replicated state.
 * Cars come first, as the same key gets in and out of them (ADR-039): a car in reach offers its seat,
 * and in a car the prompt offers the way out and names the controls.
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
    void RefreshAuthored(const AMarburgumInteractable& Target, const AMarburgumPlayerController& Controller,
        const AMarburgumPlayerState* State, bool bWaiting);
    void RefreshFishing(const AMarburgumInteractable& Spot, const AMarburgumPlayerState* State, bool bWaiting);
    void RefreshVehicle(const AMarburgumVehicle& Vehicle, FName Seat, bool bSeated, bool bWaiting);
    void Apply(const AMarburgumInteractable& Target, const FText& Verb, const FText& Detail, MarburgumUI::ETone Tone, bool bKeyActive);
    void ApplyTo(const AActor& Target, const FText& Subject, bool bFixture, const FText& Verb, const FText& Detail, MarburgumUI::ETone Tone,
        bool bKeyActive);
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
    TWeakObjectPtr<AActor> Shown;
};
