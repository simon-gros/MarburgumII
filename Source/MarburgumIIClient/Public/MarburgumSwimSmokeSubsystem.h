#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarburgumSwimSmokeSubsystem.generated.h"

class AMarburgumCharacter;
class AMarburgumPlayerController;
class AMarburgumPlayerState;
class UMarburgumCharacterMovement;

/**
 * Swimming driver (ADR-032), active only with -MB2SwimSmoke. Each client process plays one role against a
 * real dedicated server on /Game/World/lent, starting on the Lent promenade at the arrival start, 20 m east
 * of the Drava, and exits 0 (its expectations held) or 1. See scripts/swim-smoke-linux.sh.
 *
 *   W   walks west into the river and swims at swim speed with the mouth up and the swim clip playing;
 *       lets go and slips under, holding breath at a minute per bar; swims back up and east and climbs
 *       out onto the bank; walks back in, lets go and drowns. Down, it is at the recovery point, cannot
 *       move, and leaves while still down.
 *   W2  W's character after a server restart: still down at the recovery point for the time it had left,
 *       cannot move, then recovers with full vitality and breath and walks again.
 *   O   an observer (-nullrhi) that sees another player swim: the remote body is in the water and plays
 *       the swim clip.
 *
 * Options: -MB2SwimRole= -MB2SwimShot=<path prefix> -MB2SwimTimeout=<s>
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumSwimSmokeSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    bool Tick(float DeltaTime);
    bool Bind();
    void Drive();
    void TickW(double Now);
    void TickW2(double Now);
    void TickO(double Now);

    const AMarburgumPlayerState* LocalState() const;
    AMarburgumCharacter* Body() const;
    UMarburgumCharacterMovement* Movement() const;
    double SpeedOver(double Window) const;
    void Hold(const FVector& Direction);
    void Release();
    void Expect(bool bCondition, const FString& What);
    void Shot(const TCHAR* Label);
    /** The condition card's state pill text, "-" without a HUD. */
    FString HudState() const;
    void ExpectHud(const TCHAR* Prefix, const TCHAR* What);
    void Advance(int32 Next, double Now);
    void Report(const TCHAR* Event, double Now);
    void Pass();
    void Fail(const FString& Reason);

    FTSTicker::FDelegateHandle TickerHandle;
    TWeakObjectPtr<AMarburgumPlayerController> Controller;

    FString Role = TEXT("W");
    FString ShotPrefix;
    double TimeoutSeconds = 300.0;
    double StartSeconds = 0.0;

    int32 Step = 0;
    double StepSeconds = 0.0;
    double MarkSeconds = 0.0;
    int32 MarkStamina = -1;
    FVector ArrivalAt = FVector::ZeroVector;
    FVector HoldDirection = FVector::ZeroVector;
    bool bHolding = false;

    struct FSample
    {
        double Seconds;
        FVector2D Where;
    };
    TArray<FSample> Samples;

    FString PendingShot;
    double ShotRequestedSeconds = 0.0;
    bool bDone = false;
    TArray<FString> Problems;
};
