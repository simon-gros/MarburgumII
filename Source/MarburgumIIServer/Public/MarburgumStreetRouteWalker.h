#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "MarburgumStreetRoute.h"
#include "Subsystems/WorldSubsystem.h"
#include "MarburgumStreetRouteWalker.generated.h"

/**
 * Test-only (ADR-068): with -MB2StreetRoute on the server, sets the first player with a body down at each stop of the
 * street-district smoke's route (MarburgumStreetRoute.h) in turn, DwellSeconds at each, on whatever is walkable there.
 * Logs `event=route stop=<id>` with the zone the street finds there. Absent the option it does nothing. With
 * -MB2StreetRouteName=places it walks the street-places smoke's stops instead (ADR-069).
 */
UCLASS()
class MARBURGUMIISERVER_API UMarburgumStreetRouteWalker : public UWorldSubsystem
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
    /** The route walked: the districts' stops, or the places'. */
    TArrayView<const MarburgumII::StreetRoute::FStop> Route;
    double Dwell = 0.0;
};
