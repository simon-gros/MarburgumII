#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "Subsystems/WorldSubsystem.h"
#include "MarburgumDoorTour.generated.h"

class APawn;

/**
 * Test-only (ADR-076 door smoke): with -MB2DoorTour=<building id> on a server started with -MB2DoorStarts, takes each
 * player whose character key ends in "-tour" to that building's street door (its door_<id> start) and, after a
 * dwell, back to where it stood. The door smoke's tourist checks that its own building streams out and in again
 * around the trip. Absent the option it does nothing.
 */
UCLASS()
class MARBURGUMIISERVER_API UMarburgumDoorTour : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;

private:
    struct FTrip
    {
        FVector Home = FVector::ZeroVector;
        FRotator HomeRotation = FRotator::ZeroRotator;
        double SeenSeconds = 0.0;
        /** 0 at home, 1 away, 2 back. */
        int32 Leg = 0;
    };

    void Tick();
    bool Move(APawn* Body, const FVector& At, const FRotator& Rotation) const;

    FTimerHandle Timer;
    FString Away;
    TMap<TWeakObjectPtr<APawn>, FTrip> Trips;
};
