#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MarburgumLocationBanner.generated.h"

class AMarburgumPlayerState;
class UBorder;
class UTextBlock;

/**
 * `location_banner` module (top-centre): a transient place name when the server-derived area of the
 * local player changes, then it fades away. It shows the area the server placed the pawn in
 * (owner-only AMarburgumPlayerState::GetAreaId), never a client guess, and nothing until the first
 * value arrives. Names come from the registered area catalogue; outside every area it reads "Maribor",
 * as in the original game. The caption carries the area's provenance note (e.g. a fallback blockout).
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumLocationBanner : public UUserWidget
{
    GENERATED_BODY()

public:
    /** True while a banner is on screen (for evidence capture). */
    bool IsShowing() const { return Elapsed >= 0.f; }

    /** The area id last shown; None for outside every area. */
    FName GetShownArea() const { return ShownArea; }

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    void HandleAreaChanged(AMarburgumPlayerState* State);
    void Show(FName AreaId);

    UPROPERTY(Transient) TObjectPtr<UBorder> Card;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> KindLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> NameLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> NoteLabel;

    FDelegateHandle AreaHandle;
    FName ShownArea;
    bool bHasShown = false;
    /** Seconds since the current banner appeared; negative while hidden. */
    float Elapsed = -1.f;
};
