#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "Subsystems/WorldSubsystem.h"
#include "MarburgumClockAuthority.generated.h"

/**
 * The world clock on the server (ADR-064, MarburgumWorldClock.h): trusted UTC, which every server rule that cares what
 * time it is reads (the weather, the counters' hours), and which UMarburgumClockAuthority puts on the replicated game
 * state for clients to draw the sky and show the date from. Nothing a client says moves it.
 *
 * Options, read once per process:
 *   -MB2Clock=<ISO 8601 UTC>   start the clock at that instant (reviews and tests)
 *   -MB2ClockSpeed=<factor>    run it faster than real time, e.g. 1440 for a day a minute
 * ADR-061's -MB2WeatherClock and -MB2WeatherSpeed are the same options under their old names.
 */
namespace MarburgumII::ServerClock
{
    /** The world clock now. */
    MARBURGUMIISERVER_API FDateTime Now();
    /** Clock seconds per real second. */
    MARBURGUMIISERVER_API double Speed();
    /** True when an option moved or sped the clock off real UTC. */
    MARBURGUMIISERVER_API bool IsReview();
}

/** Puts the server's world clock on the game state at begin play and keeps the copy from drifting. */
UCLASS()
class MARBURGUMIISERVER_API UMarburgumClockAuthority : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;

private:
    void Publish();

    FTimerHandle Timer;
};
