#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MarburgumWeatherLook.h"
#include "MarburgumWeatherPresentation.generated.h"

class AMarburgumWeatherEffects;
struct FMarburgumWeather;

/**
 * Draws and plays the server's weather in a client world (ADR-062): spawns one AMarburgumWeatherEffects once the
 * replicated game state carries a weather (AMarburgumGameState::Weather, ADR-061), and hands it every change. Nothing
 * runs on a dedicated server, which has no one to show it to.
 *
 * `-MB2NoWeatherLook` leaves the level's own daylight alone, for captures that compare against the level as built.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumWeatherPresentation : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual void Deinitialize() override;

    /** The effects actor, once the first weather has arrived. */
    AMarburgumWeatherEffects* GetEffects() const { return Effects.Get(); }

    /** The replicated weather as the look's plain input. */
    static MarburgumII::WeatherLook::FInput InputOf(const FMarburgumWeather& Weather);

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    UPROPERTY(Transient)
    TObjectPtr<AMarburgumWeatherEffects> Effects;

    /** The last weather handed on, so an unchanged replication costs nothing. */
    MarburgumII::WeatherLook::FInput Shown;
    bool bShown = false;
};
