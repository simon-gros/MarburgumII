#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "MarburgumInteractionTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarburgumSheetSmokeSubsystem.generated.h"

class AMarburgumPlayerController;
class AMarburgumPlayerState;

/**
 * Character sheet driver (ADR-085, LRP-01/02), active only with -MB2SheetSmoke. Each client process plays one role on
 * /Game/World/lent against a real dedicated server, through the Character sheet and the Inventory when they render and
 * the same RPC otherwise, and exits 0 (its expectations held) or 1. Roles (see scripts/sheet-smoke-linux.sh):
 *
 *   SHEET     attributes as migrated from the seeded legacy `wind` record (Might 12, Endurance 14, Eye 9, Poise 11) on
 *             the sheet; the rod put in the right hand from the sheet (custody unchanged); that operation replayed
 *             (duplicate); the rod in the left hand too, the camera on the head, an unknown slot, an unknown item and
 *             emptying the empty bag all refused with nothing changed; the camera put in the left hand from the
 *             Inventory. Attributes never move.
 *   PEER      a second character at the same time: its mixed record kept `endurance` (16); its own rod in its right hand;
 *             while it plays, the other player's attributes and equipment never reach it (owner-only).
 *   RESTORED  after a restart: attributes and both hands as left; the rod taken out of the right hand from the sheet.
 *
 * Options: -MB2SheetRole= -MB2SheetShot=<path prefix> -MB2SheetTimeout=<s>.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumSheetSmokeSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    bool Tick(float DeltaTime);
    bool Bind();
    void TickSheet(double Now);
    void TickPeer(double Now);
    void TickRestored(double Now);
    /** The other players' copies must carry none of their owners' private sheet. */
    void CheckOthers();

    void HandleReceipt(const FMarburgumInteractionReceipt& Receipt);
    const FMarburgumInteractionReceipt* NextReceipt();
    void Equip(FName Slot, FName Item, const TCHAR* Label);
    const AMarburgumPlayerState* LocalState() const;
    FString Snapshot() const;
    FString Scores() const;
    void ExpectRefused(const FMarburgumInteractionReceipt& R, EMarburgumInteractionReason Reason, const TCHAR* What);
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
    bool bSawOther = false;
    TArray<FString> Problems;

    FGuid Op;
    FGuid FirstEquip;
    FString Before;
    FString StartScores;
    TArray<FMarburgumInteractionReceipt> Receipts;
    int32 ReceiptsSeen = 0;
};
