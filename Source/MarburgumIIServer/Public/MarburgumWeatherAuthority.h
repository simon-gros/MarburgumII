#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "MarburgumWeather.h"
#include "Subsystems/WorldSubsystem.h"
#include "MarburgumWeatherAuthority.generated.h"

struct FMarburgumWeather;

/**
 * Server-only weather (ADR-061): the original's automatic Maribor weather (MarburgumWeather.h) run once a second on
 * trusted UTC, written onto the replicated game state. Nothing a client says feeds it.
 *
 * Options:
 *   -MB2Weather=<auto|clear_dry|overcast|rain_wet|winter_cold>  default auto; auto without the climate files falls back
 *                                                                 to clear_dry with a warning
 *   -MB2WeatherClock=<ISO 8601 UTC>   start the weather clock at that instant (reviews and tests)
 *   -MB2WeatherSpeed=<factor>         run that clock faster than real time, e.g. 60 for an hour a minute
 *   -MB2WorldSeed=<n>                 another weather history (default 1996)
 */
UCLASS()
class MARBURGUMIISERVER_API UMarburgumWeatherAuthority : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;

    /** The weather clock now: trusted UTC, or the review clock. */
    FDateTime WeatherUtc() const;

    /** The replicated form of a weather state, rounded so a slow drift does not resend it every second. */
    static FMarburgumWeather ToReplicated(const MarburgumII::Weather::FState& State);

private:
    void Update();

    MarburgumII::Weather::FClimate Climate;
    MarburgumII::Weather::FSimulation Simulation;
    /** Set for a fixed profile; unset runs the climate. */
    TOptional<MarburgumII::Weather::FState> Fixed;
    FDateTime ClockStart;
    double RealStart = 0.0;
    double Speed = 1.0;
    bool bReviewClock = false;
    FString LoggedState;
    FString LoggedType;
    int32 UpdatesSinceLog = 0;
    FTimerHandle Timer;
};
