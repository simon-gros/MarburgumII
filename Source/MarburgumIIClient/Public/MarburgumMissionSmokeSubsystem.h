#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "MarburgumInteractionTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarburgumMissionSmokeSubsystem.generated.h"

class AMarburgumInteractable;
class AMarburgumPlayerController;
class AMarburgumPlayerState;

/**
 * Mission, fishing and skills driver (ADR-047..049), active only with -MB2MissionSmoke. Each client process plays
 * one role on /Game/World/lent against a real dedicated server, through the same RPCs the E key and the choice
 * panel use, and exits 0 (its expectations held) or 1. Roles (see scripts/mission-smoke-linux.sh):
 *
 *   FISH    at the angler: the mission starts with its rod; showing no fish is refused; a strike before any
 *           bite loses the cast; a recast inside the interval is refused; then a real cast, the bite, the
 *           strike and the catch (one fish, Fishing XP); showing the fish completes the first stage.
 *   STEP    one interaction with -MB2MissionTarget=<id>, expecting the effect -MB2MissionExpect=<wire name>.
 *   CHOICE  at -MB2MissionTarget: the decision panel (or, without a renderer, the RPC) chooses
 *           -MB2MissionChoice=<id> for -MB2MissionId=<mission>; expects the mission completed.
 *   CHECK   after a server restart: completed missions, decision, money, skills and fish restored.
 *
 * Options: -MB2MissionRole= -MB2MissionShot=<path prefix> -MB2MissionTimeout=<s> and the per-role ones above.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumMissionSmokeSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    bool Tick(float DeltaTime);
    bool Bind();
    void TickFish(double Now);
    void TickStep(double Now);
    void TickChoice(double Now);
    void TickCheck(double Now);

    void HandleReceipt(const FMarburgumInteractionReceipt& Receipt);
    const FMarburgumInteractionReceipt* ReceiptFor(const FGuid& OperationId) const;
    FGuid Submit(AMarburgumInteractable* At, const TCHAR* Label);
    AMarburgumInteractable* Find(FName Id) const;
    const AMarburgumPlayerState* LocalState() const;
    int32 Count(const TCHAR* Item) const;
    int32 Fish() const;
    const FMarburgumMissionState* Progress(const TCHAR* Mission) const;
    void Expect(bool bCondition, const FString& What);
    void Shot(const TCHAR* Label);
    void Advance(int32 Next, double Now);
    void Pass();
    void Fail(const FString& Reason);

    FTSTicker::FDelegateHandle TickerHandle;
    TWeakObjectPtr<AMarburgumPlayerController> Controller;

    FString Role;
    FString ShotPrefix;
    FName TargetId;
    FName ExpectEffect;
    FName MissionId;
    FName ChoiceId;
    double TimeoutSeconds = 150.0;
    double StartSeconds = 0.0;

    int32 Step = 0;
    double StepSeconds = 0.0;
    FString PendingShot;
    double ShotRequestedSeconds = 0.0;
    bool bDone = false;
    TArray<FString> Problems;

    FGuid Op;
    int32 FishBefore = 0;
    int32 AttemptBefore = 0;
    TMap<FGuid, FMarburgumInteractionReceipt> Receipts;
};
