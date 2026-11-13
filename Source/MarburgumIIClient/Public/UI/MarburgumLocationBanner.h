#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MarburgumLocationBanner.generated.h"

class AMarburgumPlayerState;
struct FMarburgumWeather;
class UBorder;
class UTextBlock;

/**
 * When the banner announces itself (the original's FLocationBanner, issue #263): a new place does, and so does turning
 * into a newly named street or walking up to a new landmark, with the place above them. Walking off every street or
 * away from every landmark drops its line without announcing, and coming back to the one last shown says nothing new:
 * a courtyard is not a journey. The same place does not announce itself again until the player has left it.
 */
struct MARBURGUMIICLIENT_API FMarburgumPlaceAnnouncer
{
    /** True when this observation should bring the banner up. An empty place announces nothing. */
    bool Observe(const FString& Place, const FString& Street, const FString& Landmark);
    void Reset();

private:
    FString LastPlace;
    FString LastStreet;
    FString LastLandmark;
};

/**
 * `location_banner` module (top-centre): a transient place name when the server-derived place of the
 * local player changes, then it fades away. It shows where the server placed the pawn (owner-only
 * AMarburgumPlayerState::GetAreaId and GetPlace), never a client guess, and nothing until the first
 * value arrives. The place is the named area (Lent) if the player is in one, else the register's quarter or
 * village (ADR-063), else "Maribor", as in the original game. Under it come the street or square and the
 * landmark the player is at, which follow the player while the banner is up and bring it back when new
 * (FMarburgumPlaceAnnouncer). Names come from the area catalogue and the places catalogue
 * (MarburgumPlaces.h). The caption carries the area's provenance note (e.g. a fallback blockout).
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
    /** The world clock's line as shown ("Wed 13 Nov 1996  ·  20:15"); empty before the clock is known. */
    FString GetClockText() const;

    /** The place, street and landmark lines as shown; empty when a line is hidden (for evidence capture). */
    FString GetPlaceText() const;
    FString GetStreetText() const;
    FString GetLandmarkText() const;
    /** How many times the banner has come up, from any cause (for evidence capture). */
    int32 GetAnnouncements() const { return Announcements; }

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    void HandleAreaChanged(AMarburgumPlayerState* State);
    /** Fill the place, street and landmark lines for where the server puts the player. */
    void Describe(FName AreaId, const struct FMarburgumPlace& Place);
    void Show();
    /** Fills the clock line from the world clock (ADR-064). */
    void UpdateClock();
    void HandleWeatherChanged(const FMarburgumWeather& Weather);

    UPROPERTY(Transient) TObjectPtr<UBorder> Card;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> KindLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> NameLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StreetLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> LandmarkLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> NoteLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> WeatherLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ClockLabel;

    FDelegateHandle AreaHandle;
    FDelegateHandle WeatherHandle;
    /** The hourly state and precipitation type last shown, so only a turn in the weather brings the banner back. */
    FName WeatherState;
    FName WeatherType;
    FName ShownArea;
    FMarburgumPlaceAnnouncer Announcer;
    int32 Announcements = 0;
    bool bHasShown = false;
    /** Seconds since the current banner appeared; negative while hidden. */
    float Elapsed = -1.f;
};
