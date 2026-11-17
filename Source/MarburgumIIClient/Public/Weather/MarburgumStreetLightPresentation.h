#pragma once

#include "CoreMinimal.h"
#include "MarburgumStreetLights.h"
#include "Subsystems/WorldSubsystem.h"

#include "MarburgumStreetLightPresentation.generated.h"

class AActor;
class AMarburgumStreetLamps;
class USpotLightComponent;
class UPointLightComponent;

/**
 * The street lamps burning (ADR-065), in a client world: from the world clock and the server's weather, each lamp's
 * photocell and warm-up (MarburgumStreetLights.h) decide its output and colour, which its glass glows by; and the lamps
 * nearest the eye that are burning cast real light on the street, from a fixed pool of lights moved from lamp to lamp.
 * A spot for a mast, thrown down onto the road; a point for a lantern; both with the compressed falloff of
 * MarburgumStreetLights.h. No shadows: a street of lamps would cost a shadow each.
 *
 * Nothing is sent: every client works the same lamps out from the same clock and weather. Nothing runs on a dedicated
 * server. -MB2NoStreetLights leaves the lamps standing but dark, for comparison captures.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumStreetLightPresentation : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual void Deinitialize() override;

    /** For smokes and evidence: what is burning now. */
    int32 NumLamps() const { return Outputs.Num(); }
    int32 NumBurning() const { return Burning; }
    int32 NumCasting() const { return Casting; }
    bool HasSky() const { return bHasSky; }
    const MarburgumII::StreetLights::FSky& GetSky() const { return Sky; }
    double SwitchOnDeg() const { return MarburgumII::StreetLights::SwitchOnDeg(Sky); }
    /** The lamp nearest the eye, its distance (cm) and what it is doing; INDEX_NONE before there are lamps. */
    int32 NearestLamp() const { return Nearest; }
    float NearestDistanceCm() const { return NearestCm; }
    const MarburgumII::StreetLights::FOutput* OutputOf(int32 Lamp) const { return Outputs.IsValidIndex(Lamp) ? &Outputs[Lamp] : nullptr; }
    /** The strongest light being cast now, peak lux; 0 when none. */
    float BrightestLux() const { return Brightest; }

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    void UpdateLamps(AMarburgumStreetLamps& Lamps);
    void UpdatePool(AMarburgumStreetLamps& Lamps);
    bool Eye(FVector& OutEye) const;

    UPROPERTY(Transient)
    TObjectPtr<AActor> PoolActor;

    UPROPERTY(Transient)
    TArray<TObjectPtr<USpotLightComponent>> Spots;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UPointLightComponent>> Points;

    TArray<MarburgumII::StreetLights::FOutput> Outputs;
    /** What each lamp's instance was last given, so an unchanged lamp is not written again. */
    TArray<float> Written;
    MarburgumII::StreetLights::FSky Sky;
    bool bHasSky = false;
    bool bDark = false;
    double SinceUpdate = 1e9;
    int32 Burning = 0;
    int32 Casting = 0;
    int32 Nearest = INDEX_NONE;
    float NearestCm = 0.0f;
    float Brightest = 0.0f;
};
