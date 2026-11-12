#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "MarburgumInteractionTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarburgumShopSmokeSubsystem.generated.h"

class AMarburgumInteractable;
class AMarburgumPlayerController;
class AMarburgumPlayerState;

/**
 * Kiosk 67 driver (ADR-060), active only with -MB2ShopSmoke. Each client process plays one role at Petra Kralj's
 * counter on /Game/World/lent against a real dedicated server, through the shop panel when it renders and the same
 * RPC otherwise, and exits 0 (its expectations held) or 1. Roles (see scripts/shop-smoke-linux.sh):
 *
 *   BUY     the counter is open with its starting shelves; a newspaper is bought (price, shelf, first-purchase
 *           Trading XP); replaying that operation changes nothing; a postcard it cannot afford, six newspapers and
 *           selling the brass key are refused; the newspaper is sold back (buy-back price, shelf full again);
 *           selling it twice is refused; a pen is bought with no further XP.
 *   FAR     from the arrival start, a purchase at Petra is refused as out of range and nothing changes.
 *   CLOSED  after a restart on a Sunday: the shelves, money, pen and XP are as left, the counter shows closed and a
 *           purchase is refused as shop_closed.
 *
 * Options: -MB2ShopRole= -MB2ShopShot=<path prefix> -MB2ShopTimeout=<s>.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumShopSmokeSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    bool Tick(float DeltaTime);
    bool Bind();
    void TickBuy(double Now);
    void TickFar(double Now);
    void TickClosed(double Now);

    void HandleReceipt(const FMarburgumInteractionReceipt& Receipt);
    /** The newest receipt this client has not looked at yet, if any. */
    const FMarburgumInteractionReceipt* NextReceipt();
    /** Buy or sell through the panel when it is open (its own operation id), else the RPC with Op. */
    void Trade(EMarburgumTradeAction Action, const TCHAR* Item, int32 Quantity, bool bPanel, const TCHAR* Label);
    AMarburgumInteractable* Petra() const;
    const AMarburgumPlayerState* LocalState() const;
    int32 Count(const TCHAR* Item) const;
    int32 ShelfOf(const TCHAR* Item) const;
    FString Shelves() const;
    /** Expect a refusal with Reason and no change to money, custody or shelves since Before. */
    void ExpectRefused(const FMarburgumInteractionReceipt& R, EMarburgumInteractionReason Reason, const FString& Before, const TCHAR* What);
    FString Snapshot() const;
    void Expect(bool bCondition, const FString& What);
    void Shot(const TCHAR* Label);
    void Advance(int32 Next, double Now);
    void Pass();
    void Fail(const FString& Reason);

    FTSTicker::FDelegateHandle TickerHandle;
    TWeakObjectPtr<AMarburgumPlayerController> Controller;

    FString Role;
    FString ShotPrefix;
    double TimeoutSeconds = 120.0;
    double StartSeconds = 0.0;

    int32 Step = 0;
    double StepSeconds = 0.0;
    FString PendingShot;
    double ShotRequestedSeconds = 0.0;
    bool bDone = false;
    TArray<FString> Problems;

    FGuid Op;
    FGuid FirstPurchase;
    FString Before;
    TArray<FMarburgumInteractionReceipt> Receipts;
    int32 ReceiptsSeen = 0;
};
