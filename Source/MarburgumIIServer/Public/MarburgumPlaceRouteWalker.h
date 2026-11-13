#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "Subsystems/WorldSubsystem.h"
#include "MarburgumPlaceRouteWalker.generated.h"

/**
 * Test-only (ADR-063): with -MB2PlaceRoute on the server, moves the first player with a body along the place-name
 * smoke's route (MarburgumPlaceRoute.h), DwellSeconds at each stop, set down on whatever is walkable there (the bridge
 * deck, the square's paving). Logs `event=route stop=<id>` for each. Absent the option it does nothing.
 */
UCLASS()
class MARBURGUMIISERVER_API UMarburgumPlaceRouteWalker : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;

private:
    void Tick();

    FTimerHandle Timer;
    double BodySince = -1.0;
    int32 Next = 0;
    double NextAt = 0.0;
};
