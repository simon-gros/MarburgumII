#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/MarburgumUITheme.h"
#include "UI/MarburgumUIWidgets.h"
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
 * With a camera in custody and nothing else in reach, it names the photo subject [P] would photograph, and whether
 * the character has documented it yet (ADR-081); the server still decides what a photograph shows.
 * Refreshes at 10 Hz and only touches widgets whose text changed.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumInteractionPrompt : public UUserWidget
{
    GENERATED_BODY()

public:
    /** While a blocking screen or the chat composer owns the keys, no prompt may promise one (UI-P0B). */
    void SetSuppressed(bool bInSuppressed);

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    void Refresh();
    void RefreshAuthored(const AMarburgumInteractable& Target, const AMarburgumPlayerController& Controller,
        const AMarburgumPlayerState* State, bool bWaiting);
    void RefreshFishing(const AMarburgumInteractable& Spot, const AMarburgumPlayerState* State, bool bWaiting);
    void RefreshVehicle(const AMarburgumVehicle& Vehicle, FName Seat, bool bSeated, bool bWaiting);
    /** With a camera in custody and nothing else in reach: what [P] would photograph (ADR-081). False when nothing. */
    bool RefreshPhoto(const AMarburgumPlayerController& Controller);
    void Apply(const AMarburgumInteractable& Target, const FText& Verb, const FText& Detail, MarburgumUI::ETone Tone, bool bKeyActive,
        EMarburgumGlyph Icon);
    void ApplyTo(const AActor& Target, const FText& Subject, bool bFixture, const FText& Verb, const FText& Detail, MarburgumUI::ETone Tone,
        bool bKeyActive, EMarburgumGlyph Icon);
    void Place();

    UPROPERTY(Transient) TObjectPtr<UBorder> Card;
    UPROPERTY(Transient) TObjectPtr<UWidget> Placement;
    UPROPERTY(Transient) TObjectPtr<UBorder> KeyCap;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> KeyLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> VerbLabel;
    UPROPERTY(Transient) TObjectPtr<UMarburgumGlyph> VerbGlyph;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> SubjectLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailLabel;
    UPROPERTY(Transient) TObjectPtr<UBorder> FixturePill;

    float SinceRefresh = 0.f;
    bool Suppressed = false;
    FString LastSignature;
    TWeakObjectPtr<AActor> Shown;
    /** The photo subject's aim point when the card is about a photograph rather than an actor. */
    TOptional<FVector> ShownPoint;
};
