#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "MarburgumInteractionTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarburgumLentSmokeSubsystem.generated.h"

class AMarburgumNpc;
class AMarburgumPlayerController;
class AMarburgumPlayerState;

/**
 * Lent slice driver, active only with -MB2LentSmoke. Each client process plays one role against a real
 * dedicated server on /Game/Maps/Lent through the same RPC the E key uses, and exits 0 (its
 * expectations held) or 1. Roles (see scripts/lent-smoke-linux.sh):
 *
 *   M   at the angler: arrives in Lent, an injected write fault refused with no effect, the mission
 *       accepted (start item granted in the same commit), the same operation replayed (Duplicate), the
 *       next step refused for want of a fish, then the journal. Holds its session for W.
 *   W   on the bridge, outside Lent: talking to the angler from there is refused out of range; once M
 *       has its mission, W must see none of M's progress; then W walks north until the server places it
 *       in Lent.
 *   K   no character key: talking is refused.
 *   M2  M's character after a server restart: mission, objective and start item restored; the first
 *       operation replayed is still a Duplicate.
 *   T   a new character on the bridge walks into Lent: the location banner appears.
 *   F   the fast-clock adversary (with -MB2LentClockRate=, e.g. 2.5): its world clock and so its move
 *       timestamps run ahead of real time while it walks from the bridge into Lent; the server must not
 *       let it cover the ground faster than MaxWalkSpeed allows (ADR-019, checked from the server log).
 *
 * Walkers (W, T, F) log their start, wall and world seconds; an honest walker fails if its world clock
 * ran ahead of real time.
 *
 * Options: -MB2LentRole= -MB2LentOp=<32 hex digits, M's replayable operation> -MB2LentSignal=<file M
 * creates once its mission is saved> -MB2LentHoldFile=<file that releases M> -MB2LentShot=<path prefix>
 * -MB2LentTimeout=<s> -MB2LentClockRate=<local world time dilation, default 1>
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumLentSmokeSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    bool Tick(float DeltaTime);
    bool Bind();
    void TickM(double Now);
    void TickW(double Now);
    void TickK(double Now);
    void TickM2(double Now);
    void TickT(double Now);
    void TickF(double Now);

    /** Walk north (towards Lent) until the server reports the Lent area; true once it has. */
    bool WalkIntoLent();

    void HandleReceipt(const FMarburgumInteractionReceipt& Receipt);
    const FMarburgumInteractionReceipt* ReceiptFor(const FGuid& OperationId) const;
    FGuid Submit(const TCHAR* Label, const FGuid& OperationId = FGuid());

    const AMarburgumPlayerState* LocalState() const;
    int32 Rods() const;
    FName Area() const;
    FName Stage() const;
    bool BannerShows(FName AreaId) const;
    FName PinnedMission() const;
    void Expect(bool bCondition, const FString& What);
    void Shot(const TCHAR* Label);
    void Advance(int32 Next, double Now);
    void Pass();
    void Fail(const FString& Reason);

    FTSTicker::FDelegateHandle TickerHandle;
    TWeakObjectPtr<AMarburgumPlayerController> Controller;
    TWeakObjectPtr<AMarburgumNpc> Angler;

    FString Role = TEXT("M");
    FString ShotPrefix;
    FString SignalFile;
    FString HoldFile;
    FGuid ReplayOperation;
    double TimeoutSeconds = 150.0;
    double ClockRate = 1.0;
    double StartSeconds = 0.0;

    int32 Step = 0;
    double StepSeconds = 0.0;
    double WalkStartSeconds = 0.0;
    double WalkStartWorldSeconds = 0.0;
    double LastTickSeconds = 0.0;
    double FrameWallSeconds = 0.0;
    double LongestWalkFrameSeconds = 0.0;
    FVector WalkStart = FVector::ZeroVector;
    FString PendingShot;
    double ShotRequestedSeconds = 0.0;
    bool bDone = false;
    TArray<FString> Problems;

    FGuid FaultOp;
    FGuid FreshOp;
    TMap<FGuid, FMarburgumInteractionReceipt> Receipts;
};
