#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MarburgumLocationBanner.generated.h"

class AMarburgumPlayerState;
struct FMarburgumWeather;
class UBorder;
class UTextBlock;

/**
 * `location_banner` module (top-centre): a transient place name when the server-derived area of the
 * local player changes, then it fades away. It shows the area the server placed the pawn in
 * (owner-only AMarburgumPlayerState::GetAreaId), never a client guess, and nothing until the first
 * value arrives. Names come from the registered area catalogue; outside every area it reads "Maribor",
 * as in the original game. The caption carries the area's provenance note (e.g. a fallback blockout).
 *
 * Under it, the temporal context: the server's weather in a few words ("Light rain  ·  11 °C  ·  wet streets",
 * ADR-061). When the weather turns (a new hourly state or precipitation type) the banner comes back for the place
 * the player is in, so a change in the sky is noticed without a permanent weather panel.
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

    /** The weather line as shown; empty before the server's weather arrives. */
    FString GetWeatherText() const;

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    void HandleAreaChanged(AMarburgumPlayerState* State);
    void Show(FName AreaId);
    void HandleWeatherChanged(const FMarburgumWeather& Weather);

    UPROPERTY(Transient) TObjectPtr<UBorder> Card;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> KindLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> NameLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> NoteLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> WeatherLabel;

    FDelegateHandle AreaHandle;
    FDelegateHandle WeatherHandle;
    /** The hourly state and precipitation type last shown, so only a turn in the weather brings the banner back. */
    FName WeatherState;
    FName WeatherType;
    FName ShownArea;
    bool bHasShown = false;
    /** Seconds since the current banner appeared; negative while hidden. */
    float Elapsed = -1.f;
};
