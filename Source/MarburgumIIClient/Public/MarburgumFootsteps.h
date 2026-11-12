#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "MarburgumSurface.h"
#include "MarburgumFootsteps.generated.h"

class ACharacter;
class USoundAttenuation;
class USoundBase;
class USoundConcurrency;

/** The sounds of one footstep set, as loaded; a reflected struct so the garbage collector sees them. */
USTRUCT()
struct FMarburgumFootstepSounds
{
    GENERATED_BODY()

    UPROPERTY(Transient)
    TArray<TObjectPtr<USoundBase>> Sounds;
};

/**
 * Footsteps for everyone on foot near the listener (ADR-061): players, street people and fighters. A step plays when
 * a foot bone comes down in the drawn pose (MarburgumSurface.h FFootfallDetector), so steps follow any gait and play
 * rate. The sound follows what the foot stands on (MarburgumGround.h) and the server's weather: hard ground splashes
 * once the streets are wet, gravel and lying snow crunch, grass and earth are soft.
 *
 * The original's Unreal client read footfalls from each clip and played San Andreas placeholder banks; the banks are
 * Rockstar's and stay out (its docs/PLACEHOLDER_ASSETS.md), and Marburgum II's bodies are IK-corrected at run time, so
 * the feet are read from the pose itself.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumFootstepSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    /** Farther from the listener than this, a body's feet are not even looked at. */
    static constexpr double AudibleCm = 3000.0;
    /** Steps playing at once across every body; the farthest give way. */
    static constexpr int32 MaxConcurrentSteps = 12;
    /** A body slower than this is standing, whatever its feet do. */
    static constexpr double MovingCmPerSecond = 60.0;

    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual void Deinitialize() override;

    /** Sounds of a footstep set, loaded on first use; empty when nothing is imported for it. */
    const TArray<TObjectPtr<USoundBase>>& SoundsFor(FName Set);

    /** For smokes: steps resolved, by set and by surface, and the last surface under the local player's feet. */
    int32 GetStepsPlayed() const { return StepsPlayed; }
    int32 GetStepsWithoutSound() const { return StepsWithoutSound; }
    int32 GetStepsPlayedOn(FName Set) const { return StepsBySet.FindRef(Set); }
    const TMap<FName, int32>& GetStepsBySet() const { return StepsBySet; }
    const TMap<FString, int32>& GetStepsBySurface() const { return StepsBySurface; }
    int32 GetLocalSteps() const { return LocalSteps; }
    const TMap<FName, int32>& GetLocalStepsBySet() const { return LocalStepsBySet; }
    const FString& GetLocalSurface() const { return LocalSurface; }

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    struct FFeet
    {
        MarburgumII::Surface::FFootfallDetector Feet[2];
        double LastStepAt = -1.0;
    };

    bool ListenerLocation(FVector& OutLocation) const;
    void EnsureSettings();
    void Step(const ACharacter& Body, const FVector& Foot, double Speed, bool bLocal);

    UPROPERTY(Transient)
    TMap<FName, FMarburgumFootstepSounds> Sets;

    UPROPERTY(Transient)
    TObjectPtr<USoundAttenuation> Attenuation;

    UPROPERTY(Transient)
    TObjectPtr<USoundConcurrency> Concurrency;

    TMap<TObjectKey<ACharacter>, FFeet> Bodies;
    TMap<FName, int32> LastVariant;
    TMap<FName, int32> StepsBySet;
    TMap<FString, int32> StepsBySurface;
    TMap<FName, int32> LocalStepsBySet;
    FString LocalSurface;
    FRandomStream Rolls{ 0x5eed };
    int32 StepsPlayed = 0;
    int32 StepsWithoutSound = 0;
    int32 LocalSteps = 0;
    double Clock = 0.0;
    double PrunedAt = 0.0;
};
