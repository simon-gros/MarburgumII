#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "MarburgumInteractionTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarburgumInteractionSmokeSubsystem.generated.h"

class AMarburgumInteractable;
class AMarburgumPlayerController;
class AMarburgumPlayerState;

/**
 * Gate 1 interaction driver, active only with -MB2InteractSmoke. Each client process plays one role
 * against a real dedicated server through the same RPC the E key uses, and exits 0 (its expectations
 * held) or 1. Roles (see scripts/interaction-smoke-linux.sh):
 *
 *   A   first session: injected write fault refused, grant, replay of the same operation id,
 *       out-of-range request, second grant, then a race for the last unit with B.
 *   B   waits until one unit is left and requests it at the same moment as A.
 *   A2  after a server restart: inventory and stock restored, replay of A's first operation answered
 *       Duplicate, a new request refused out of stock. Holds its session for D.
 *   A3  A's character again after A2 logged out: admitted, restored inventory shown in the HUD.
 *   B2  after the restart: inventory restored.
 *   N   no character key: admitted, but every interaction is refused.
 *   D   joins with A's key while A2 is online: must be refused at admission.
 *   X   joins with a malformed key: must be refused at admission.
 *
 * Options: -MB2InteractRole= -MB2InteractOp=<32 hex digits, A's replayable operation>
 * -MB2InteractExpect=<items expected after restart> -MB2InteractHoldFile=<file that releases A2>
 * -MB2InteractShot=<path prefix for screenshots> -MB2InteractTimeout=<s>
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumInteractionSmokeSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    bool Tick(float DeltaTime);
    bool Bind();
    void TickA(double Now);
    void TickB(double Now);
    void TickA2(double Now);
    void TickA3(double Now);
    void TickB2(double Now);
    void TickN(double Now);
    void TickRefused(double Now);

    void HandleReceipt(const FMarburgumInteractionReceipt& Receipt);
    const FMarburgumInteractionReceipt* ReceiptFor(const FGuid& OperationId) const;
    FGuid Submit(AMarburgumInteractable* Target, const TCHAR* Label, const FGuid& OperationId = FGuid());

    int32 Items() const;
    int32 Stock(const AMarburgumInteractable* Target) const;
    void Expect(bool bCondition, const FString& What);
    void Shot(const TCHAR* Label);
    void Advance(int32 Next, double Now);
    void Pass();
    void Fail(const FString& Reason);
    void Finish(int32 ExitCode);

    FTSTicker::FDelegateHandle TickerHandle;
    TWeakObjectPtr<AMarburgumPlayerController> Controller;
    TWeakObjectPtr<AMarburgumInteractable> Crate;
    TWeakObjectPtr<AMarburgumInteractable> FarCrate;

    FString Role = TEXT("A");
    FString ShotPrefix;
    FString HoldFile;
    FGuid ReplayOperation;
    int32 ExpectedItems = -1;
    double TimeoutSeconds = 120.0;
    double StartSeconds = 0.0;

    int32 Step = 0;
    double StepSeconds = 0.0;
    FString PendingShot;
    double ShotRequestedSeconds = 0.0;
    bool bDone = false;
    TArray<FString> Problems;

    FGuid FaultOp;
    FGuid FarOp;
    FGuid SecondOp;
    FGuid RaceOp;
    FGuid FreshOp;
    TMap<FGuid, FMarburgumInteractionReceipt> Receipts;
    TMap<FGuid, int32> ReceiptCounts;
};
