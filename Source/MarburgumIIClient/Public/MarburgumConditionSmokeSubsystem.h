#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarburgumConditionSmokeSubsystem.generated.h"

class AMarburgumPlayerController;
class AMarburgumPlayerState;
class UMarburgumCharacterMovement;

/**
 * Player-condition driver (ADR-020), active only with -MB2ConditionSmoke. Each client process plays one
 * role against a real dedicated server on /Game/Maps/Lent, running east along the promenade with the
 * same run request Shift sends, and exits 0 (its expectations held) or 1. Speeds are measured from the
 * client's own pawn, which the server corrects. Roles (see scripts/condition-smoke-linux.sh):
 *
 *   R   a rested runner: full condition on arrival (HUD and Character panel), runs at run speed until
 *       the server reports it winded after about ten seconds, walks while winded although it still asks
 *       to run, runs again once breath is back, and leaves the moment it is winded a second time.
 *   C   the adversary (-nullrhi): ignores its breath, predicts running throughout and reports full
 *       stamina. The server must still wind it after about ten seconds, correct its stamina, and hold it
 *       to walking speed while winded.
 *   R2  R's character after a server restart: arrives winded with the saved stamina and asks to run at
 *       once; it never predicts a run while winded (the script checks from the server log that the
 *       server never ran it), runs once breath is back, then rests to full before leaving.
 *   S   a runner (-nullrhi) still connected when the script stops the server; it signals once running
 *       and passes when the session is lost.
 *
 * Options: -MB2ConditionRole= -MB2ConditionShot=<path prefix> -MB2ConditionSignal=<file S creates once
 * running> -MB2ConditionTimeout=<s>
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumConditionSmokeSubsystem : public UGameInstanceSubsystem
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
    void TickR(double Now);
    void TickC(double Now);
    void TickR2(double Now);
    void TickS(double Now);

    const AMarburgumPlayerState* LocalState() const;
    UMarburgumCharacterMovement* Movement() const;
    /** Horizontal metres per second of the local pawn over the last Window seconds; -1 without samples. */
    double SpeedOver(double Window) const;
    bool ServerWinded() const;
    int32 ServerStamina() const;
    void Hold(bool bRun);
    void Release();
    void Expect(bool bCondition, const FString& What);
    void ExpectSpeed(double Window, double Min, double Max, const TCHAR* What);
    void Shot(const TCHAR* Label);
    void Advance(int32 Next, double Now);
    void Pass();
    void Fail(const FString& Reason);

    FTSTicker::FDelegateHandle TickerHandle;
    TWeakObjectPtr<AMarburgumPlayerController> Controller;

    FString Role = TEXT("R");
    FString ShotPrefix;
    FString SignalFile;
    double TimeoutSeconds = 90.0;
    double StartSeconds = 0.0;

    int32 Step = 0;
    double StepSeconds = 0.0;
    double MarkSeconds = 0.0;
    double RunStartSeconds = 0.0;
    int32 ArrivalStamina = -1;
    bool bArrivedWinded = false;
    bool bHolding = false;
    bool bRunRequested = false;
    bool bWasAdmitted = false;

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
