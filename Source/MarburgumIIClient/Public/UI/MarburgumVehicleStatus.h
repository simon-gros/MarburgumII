#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MarburgumVehicleStatus.generated.h"

class UBorder;
class UMarburgumMeter;
class UTextBlock;

/**
 * `vehicle_status` module (lower right, ADR-039): while the local character sits in a car, its name, the
 * seat, and the road speed in km/h with REV when reversing, as the original HUD's vehicle panel showed
 * (game_hud.cpp refresh_vehicle_status). The speed is the server's, as replicated with the car. Hidden on
 * foot. Drawn as a driver's readout: large numerals, KM/H and the gear (DRIVE, STOPPED, REVERSE) beside them, a thin
 * speed bar and the car and seat underneath.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumVehicleStatus : public UUserWidget
{
    GENERATED_BODY()

public:
    /** What the card shows (for evidence capture): empty when hidden. */
    FString GetShownSpeed() const { return ShownSpeed; }
    FString GetShownName() const { return ShownName; }
    /** True while the local character sits in a car (the HUD then gives the lower corners to driving). */
    bool IsSeated() const;

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    void Refresh();

    UPROPERTY(Transient) TObjectPtr<UBorder> Card;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> NameLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> SeatLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> SpeedLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> GearLabel;
    UPROPERTY(Transient) TObjectPtr<UBorder> GearChip;
    UPROPERTY(Transient) TObjectPtr<UMarburgumMeter> SpeedBar;

    FString ShownSpeed;
    FString ShownName;
    FString ShownSeat;
};
