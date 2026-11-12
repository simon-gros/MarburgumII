#include "MarburgumShopSmokeSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumInteractable.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlayerController.h"
#include "MarburgumPlayerState.h"
#include "MarburgumRetail.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "UI/MarburgumUISubsystem.h"
#include "UnrealClient.h"

#define MB2_SHOP_LOG(Format, ...) \
    UE_LOG(LogMarburgumNet, Log, TEXT("MB2SHOP role=client tag=%s " Format), *Role, ##__VA_ARGS__)

namespace MarburgumShopSmoke
{
    constexpr double SettleSeconds = 1.5;
    constexpr double ToastSettleSeconds = 0.8;
    constexpr double ShotTimeoutSeconds = 10.0;
    // Replication of the purse, custody and shelves may land a little after the receipt.
    constexpr double ReplicationSeconds = 5.0;
    const FName Keeper(TEXT("petra_kralj"));
    const TCHAR* const Paper = TEXT("mariborski_glas");
    const TCHAR* const Postcard = TEXT("maribor_postcard");
    const TCHAR* const Pen = TEXT("ballpoint_pen");
    const TCHAR* const Key = TEXT("brass_key");
    // The smoke seeds this purse and the brass key (scripts/shop-smoke-linux.sh).
    constexpr int64 SeededMoney = 50;
}

using namespace MarburgumShopSmoke;
using MarburgumII::Gameplay::ToString;

bool UMarburgumShopSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
    return GameInstance && !GameInstance->IsDedicatedServerInstance() && FParse::Param(FCommandLine::Get(), TEXT("MB2ShopSmoke"));
}

void UMarburgumShopSmokeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const TCHAR* CommandLine = FCommandLine::Get();
    FParse::Value(CommandLine, TEXT("MB2ShopRole="), Role);
    FParse::Value(CommandLine, TEXT("MB2ShopShot="), ShotPrefix);
    FParse::Value(CommandLine, TEXT("MB2ShopTimeout="), TimeoutSeconds);
    Role = Role.ToUpper();
    StartSeconds = FPlatformTime::Seconds();
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMarburgumShopSmokeSubsystem::Tick), 0.f);
    MB2_SHOP_LOG("event=start shot=%d", ShotPrefix.IsEmpty() ? 0 : 1);
}

void UMarburgumShopSmokeSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    if (AMarburgumPlayerController* Bound = Controller.Get())
    {
        Bound->OnInteractionReceipt.RemoveAll(this);
    }
    Super::Deinitialize();
}

bool UMarburgumShopSmokeSubsystem::Bind()
{
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (!UMarburgumSessionSubsystem::IsAdmitted(World))
    {
        return false;
    }
    AMarburgumPlayerController* Local = Cast<AMarburgumPlayerController>(World->GetFirstPlayerController());
    const AMarburgumPlayerState* State = Local ? Local->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    const AMarburgumInteractable* Counter = nullptr;
    for (TActorIterator<AMarburgumInteractable> It(World); It; ++It)
    {
        Counter = It->InteractableId == Keeper ? *It : Counter;
    }
    if (!State || !State->IsInventoryKnown() || !State->IsProgressKnown() || !Local->GetPawn() || !Counter || !Counter->IsShopKnown())
    {
        return false;
    }
    if (Controller.Get() != Local)
    {
        Local->OnInteractionReceipt.AddUObject(this, &UMarburgumShopSmokeSubsystem::HandleReceipt);
        Controller = Local;
        StepSeconds = FPlatformTime::Seconds();
        MB2_SHOP_LOG("event=ready player=%d money=%lld trading_xp=%lld open=%d shelves=%s distance_m=%.1f", State->GetPlayerId(), State->GetMoney(),
            State->GetSkillXp(TEXT("trading")), Counter->IsShopOpen() ? 1 : 0, *Shelves(),
            FVector::Dist(Local->GetPawn()->GetActorLocation(), Counter->GetActorLocation()) / 100.f);
    }
    return true;
}

void UMarburgumShopSmokeSubsystem::HandleReceipt(const FMarburgumInteractionReceipt& Receipt)
{
    Receipts.Add(Receipt);
    MB2_SHOP_LOG("event=receipt op=%s outcome=%s reason=%s effect=%s item=%s quantity=%d money=%lld xp=%d shelf=%d revision=%lld",
        *Receipt.OperationId.ToString(EGuidFormats::Digits), ToString(Receipt.Outcome), ToString(Receipt.Reason), ToString(Receipt.Effect),
        *Receipt.ItemId.ToString(), Receipt.Quantity, Receipt.MoneyDelta, Receipt.SkillXp.Num(), Receipt.StockRemaining, Receipt.WorldRevision);
}

const FMarburgumInteractionReceipt* UMarburgumShopSmokeSubsystem::NextReceipt()
{
    return Receipts.IsValidIndex(ReceiptsSeen) ? &Receipts[ReceiptsSeen++] : nullptr;
}

AMarburgumInteractable* UMarburgumShopSmokeSubsystem::Petra() const
{
    const UWorld* World = Controller.IsValid() ? Controller->GetWorld() : nullptr;
    for (TActorIterator<AMarburgumInteractable> It(World); World && It; ++It)
    {
        if (It->InteractableId == Keeper)
        {
            return *It;
        }
    }
    return nullptr;
}

void UMarburgumShopSmokeSubsystem::Trade(EMarburgumTradeAction Action, const TCHAR* Item, int32 Quantity, bool bPanel, const TCHAR* Label)
{
    UMarburgumUISubsystem* UI = GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>();
    const MarburgumII::Gameplay::FShopDefinition* Shop = MarburgumII::Gameplay::FindShop(TEXT("kiosk_67"));
    int32 Line = 0;
    for (int32 Index = 0; Shop && Index < Shop->OfferCount; ++Index)
    {
        Line = FString(Shop->Offers[Index].Item) == Item ? Index + 1 : Line;
    }
    // The panel trades one of a line it shows; anything else goes straight to the RPC, as a modified client would.
    if (bPanel && UI && UI->IsShopOpen() && Line > 0 && Quantity == 1)
    {
        Expect(UI->TradeAtShop(Line, Action == EMarburgumTradeAction::Sell), FString::Printf(TEXT("panel_refused=%s"), Label));
        MB2_SHOP_LOG("event=submit kind=%s via=panel line=%d action=%s", Label, Line, ToString(Action));
        return;
    }
    if (!Op.IsValid())
    {
        Op = FGuid::NewGuid();
    }
    Controller->SubmitTrade(Op, Petra(), Action, FName(Item), Quantity);
    MB2_SHOP_LOG("event=submit kind=%s via=rpc op=%s action=%s item=%s quantity=%d", Label, *Op.ToString(EGuidFormats::Digits), ToString(Action),
        Item, Quantity);
}

const AMarburgumPlayerState* UMarburgumShopSmokeSubsystem::LocalState() const
{
    const AMarburgumPlayerController* Local = Controller.Get();
    return Local ? Local->GetPlayerState<AMarburgumPlayerState>() : nullptr;
}

int32 UMarburgumShopSmokeSubsystem::Count(const TCHAR* Item) const
{
    const AMarburgumPlayerState* State = LocalState();
    return State ? State->CountItem(FName(Item)) : -1;
}

int32 UMarburgumShopSmokeSubsystem::ShelfOf(const TCHAR* Item) const
{
    const AMarburgumInteractable* Counter = Petra();
    const MarburgumII::Gameplay::FShopDefinition* Shop = MarburgumII::Gameplay::FindShop(TEXT("kiosk_67"));
    for (int32 Index = 0; Counter && Shop && Index < Shop->OfferCount; ++Index)
    {
        if (FString(Shop->Offers[Index].Item) == Item)
        {
            return Counter->GetShelves().IsValidIndex(Index) ? Counter->GetShelves()[Index] : -1;
        }
    }
    return -1;
}

FString UMarburgumShopSmokeSubsystem::Shelves() const
{
    return FString::Printf(TEXT("%d,%d,%d"), ShelfOf(Paper), ShelfOf(Postcard), ShelfOf(Pen));
}

FString UMarburgumShopSmokeSubsystem::Snapshot() const
{
    const AMarburgumPlayerState* State = LocalState();
    return FString::Printf(TEXT("money=%lld paper=%d postcard=%d pen=%d key=%d xp=%lld shelves=%s"), State ? State->GetMoney() : -1,
        Count(Paper), Count(Postcard), Count(Pen), Count(Key), State ? State->GetSkillXp(TEXT("trading")) : -1, *Shelves());
}

void UMarburgumShopSmokeSubsystem::ExpectRefused(const FMarburgumInteractionReceipt& R, EMarburgumInteractionReason Reason, const FString& Prior,
    const TCHAR* What)
{
    Expect(R.Outcome == EMarburgumRequestOutcome::Rejected && R.Reason == Reason,
        FString::Printf(TEXT("%s=%s/%s want %s"), What, ToString(R.Outcome), ToString(R.Reason), ToString(Reason)));
    Expect(Snapshot() == Prior, FString::Printf(TEXT("%s_changed=%s was %s"), What, *Snapshot(), *Prior));
}

void UMarburgumShopSmokeSubsystem::Expect(bool bCondition, const FString& What)
{
    if (!bCondition)
    {
        Problems.Add(What);
        MB2_SHOP_LOG("event=expectation_failed what=%s", *What);
    }
}

void UMarburgumShopSmokeSubsystem::Shot(const TCHAR* Label)
{
    if (ShotPrefix.IsEmpty())
    {
        return;
    }
    PendingShot = FString::Printf(TEXT("%s-%s.png"), *ShotPrefix, Label);
    IFileManager::Get().Delete(*PendingShot, false, true, true);
    FScreenshotRequest::RequestScreenshot(PendingShot, true, false);
    ShotRequestedSeconds = FPlatformTime::Seconds();
}

void UMarburgumShopSmokeSubsystem::Advance(int32 Next, double Now)
{
    Step = Next;
    StepSeconds = Now;
    Op.Invalidate();
}

bool UMarburgumShopSmokeSubsystem::Tick(float DeltaTime)
{
    if (bDone)
    {
        return true;
    }
    const double Now = FPlatformTime::Seconds();
    if (Now - StartSeconds > TimeoutSeconds)
    {
        Fail(FString::Printf(TEXT("timeout step=%d"), Step));
        return true;
    }
    if (!PendingShot.IsEmpty())
    {
        const bool bSaved = IFileManager::Get().FileSize(*PendingShot) > 0;
        if (!bSaved && Now - ShotRequestedSeconds < ShotTimeoutSeconds)
        {
            return true;
        }
        MB2_SHOP_LOG("event=shot file=%s saved=%d", *FPaths::GetCleanFilename(PendingShot), bSaved ? 1 : 0);
        Expect(bSaved, FString::Printf(TEXT("shot_not_saved=%s"), *FPaths::GetCleanFilename(PendingShot)));
        PendingShot.Reset();
    }
    if (!Bind())
    {
        return true;
    }
    if (Role == TEXT("BUY")) TickBuy(Now);
    else if (Role == TEXT("FAR")) TickFar(Now);
    else if (Role == TEXT("CLOSED")) TickClosed(Now);
    else Fail(TEXT("unknown_role"));
    return true;
}

void UMarburgumShopSmokeSubsystem::TickBuy(double Now)
{
    const AMarburgumPlayerState* State = LocalState();
    const AMarburgumInteractable* Counter = Petra();
    UMarburgumUISubsystem* UI = GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>();
    switch (Step)
    {
    case 0:
        if (Now - StepSeconds < SettleSeconds) return;
        Expect(Counter->IsShopOpen(), TEXT("counter_not_open"));
        Expect(Shelves() == TEXT("12,8,6"), FString::Printf(TEXT("start_shelves=%s"), *Shelves()));
        Expect(State->GetMoney() == SeededMoney && Count(Key) == 1 && State->GetSkillXp(TEXT("trading")) == 0,
            FString::Printf(TEXT("start=%s"), *Snapshot()));
        if (UI)
        {
            UI->ToggleShop(Petra());
            Expect(UI->IsShopOpen(), TEXT("shop_panel_not_open"));
        }
        Advance(1, Now);
        return;
    case 1:
        if (Now - StepSeconds < ToastSettleSeconds) return;
        Shot(TEXT("counter"));
        Trade(EMarburgumTradeAction::Buy, Paper, 1, true, TEXT("buy_paper"));
        Advance(2, Now);
        return;
    case 2:
    {
        const FMarburgumInteractionReceipt* R = Receipts.IsValidIndex(0) ? &Receipts[0] : nullptr;
        const bool bSeen = R && State->GetMoney() == SeededMoney - 20 && Count(Paper) == 1 && ShelfOf(Paper) == 11;
        if (!bSeen && Now - StepSeconds < ReplicationSeconds) return;
        ReceiptsSeen = Receipts.Num();
        Expect(R && R->Outcome == EMarburgumRequestOutcome::Applied && R->Effect == EMarburgumInteractionEffect::ItemBought
                && R->MoneyDelta == -20 && R->StockRemaining == 11 && R->SkillXp.Num() == 1 && R->SkillXp[0].Xp == 10,
            FString::Printf(TEXT("buy=%s/%s/%s money=%lld"), R ? ToString(R->Outcome) : TEXT("-"), R ? ToString(R->Reason) : TEXT("-"),
                R ? ToString(R->Effect) : TEXT("-"), R ? R->MoneyDelta : 0));
        Expect(bSeen && State->GetSkillXp(TEXT("trading")) == 10, FString::Printf(TEXT("after_buy=%s"), *Snapshot()));
        FirstPurchase = R ? R->OperationId : FGuid();
        Before = Snapshot();
        // The same operation again: answered from the stored receipt, nothing moves twice.
        Op = FirstPurchase;
        Controller->SubmitTrade(Op, Petra(), EMarburgumTradeAction::Buy, FName(Paper), 1);
        MB2_SHOP_LOG("event=submit kind=replay via=rpc op=%s", *Op.ToString(EGuidFormats::Digits));
        Step = 3;
        StepSeconds = Now;
        return;
    }
    case 3:
    {
        const FMarburgumInteractionReceipt* R = NextReceipt();
        if (!R) return;
        Expect(R->Outcome == EMarburgumRequestOutcome::Duplicate && R->Effect == EMarburgumInteractionEffect::ItemBought && R->MoneyDelta == -20,
            FString::Printf(TEXT("replay=%s/%s"), ToString(R->Outcome), ToString(R->Effect)));
        Expect(Snapshot() == Before, FString::Printf(TEXT("replay_changed=%s"), *Snapshot()));
        Advance(4, Now);
        Trade(EMarburgumTradeAction::Buy, Postcard, 1, true, TEXT("too_poor"));
        return;
    }
    case 4:
    {
        const FMarburgumInteractionReceipt* R = NextReceipt();
        if (!R) return;
        ExpectRefused(*R, EMarburgumInteractionReason::InsufficientMoney, Before, TEXT("too_poor"));
        Advance(5, Now);
        Trade(EMarburgumTradeAction::Buy, Paper, 6, false, TEXT("six"));
        return;
    }
    case 5:
    {
        const FMarburgumInteractionReceipt* R = NextReceipt();
        if (!R) return;
        ExpectRefused(*R, EMarburgumInteractionReason::InvalidQuantity, Before, TEXT("six"));
        Advance(6, Now);
        Trade(EMarburgumTradeAction::Sell, Key, 1, false, TEXT("sell_key"));
        return;
    }
    case 6:
    {
        const FMarburgumInteractionReceipt* R = NextReceipt();
        if (!R) return;
        ExpectRefused(*R, EMarburgumInteractionReason::ItemNotAccepted, Before, TEXT("sell_key"));
        Advance(7, Now);
        Trade(EMarburgumTradeAction::Sell, Paper, 1, true, TEXT("sell_paper"));
        return;
    }
    case 7:
    {
        const FMarburgumInteractionReceipt* R = ReceiptsSeen < Receipts.Num() ? &Receipts[ReceiptsSeen] : nullptr;
        const bool bSeen = R && State->GetMoney() == SeededMoney - 20 + 8 && Count(Paper) == 0 && ShelfOf(Paper) == 12;
        if (!bSeen && Now - StepSeconds < ReplicationSeconds) return;
        NextReceipt();
        Expect(R && R->Outcome == EMarburgumRequestOutcome::Applied && R->Effect == EMarburgumInteractionEffect::ItemSold && R->MoneyDelta == 8
                && R->SkillXp.IsEmpty(),
            FString::Printf(TEXT("sell=%s/%s/%s"), R ? ToString(R->Outcome) : TEXT("-"), R ? ToString(R->Reason) : TEXT("-"), R ? ToString(R->Effect) : TEXT("-")));
        Expect(bSeen, FString::Printf(TEXT("after_sell=%s"), *Snapshot()));
        Before = Snapshot();
        Advance(8, Now);
        Trade(EMarburgumTradeAction::Sell, Paper, 1, true, TEXT("sell_again"));
        return;
    }
    case 8:
    {
        const FMarburgumInteractionReceipt* R = NextReceipt();
        if (!R) return;
        ExpectRefused(*R, EMarburgumInteractionReason::InsufficientItems, Before, TEXT("sell_again"));
        Advance(9, Now);
        Trade(EMarburgumTradeAction::Buy, Pen, 1, true, TEXT("buy_pen"));
        return;
    }
    case 9:
    {
        const FMarburgumInteractionReceipt* R = ReceiptsSeen < Receipts.Num() ? &Receipts[ReceiptsSeen] : nullptr;
        const bool bSeen = R && State->GetMoney() == SeededMoney - 20 + 8 - 15 && Count(Pen) == 1 && ShelfOf(Pen) == 5;
        if (!bSeen && Now - StepSeconds < ReplicationSeconds) return;
        NextReceipt();
        Expect(R && R->Outcome == EMarburgumRequestOutcome::Applied && R->Effect == EMarburgumInteractionEffect::ItemBought && R->SkillXp.IsEmpty(),
            FString::Printf(TEXT("pen=%s/%s/%s xp=%d"), R ? ToString(R->Outcome) : TEXT("-"), R ? ToString(R->Reason) : TEXT("-"),
                R ? ToString(R->Effect) : TEXT("-"), R ? R->SkillXp.Num() : -1));
        Expect(bSeen && State->GetSkillXp(TEXT("trading")) == 10, FString::Printf(TEXT("after_pen=%s"), *Snapshot()));
        MB2_SHOP_LOG("event=done %s", *Snapshot());
        Advance(10, Now);
        return;
    }
    case 10:
        if (Now - StepSeconds < ToastSettleSeconds) return;
        Shot(TEXT("bought"));
        Advance(11, Now);
        return;
    default:
        Pass();
        return;
    }
}

void UMarburgumShopSmokeSubsystem::TickFar(double Now)
{
    switch (Step)
    {
    case 0:
    {
        if (Now - StepSeconds < SettleSeconds) return;
        const AMarburgumInteractable* Counter = Petra();
        const float Distance = FVector::Dist(Controller->GetPawn()->GetActorLocation(), Counter->GetActorLocation());
        Expect(Distance > Counter->InteractionRange + MarburgumII::Gameplay::RangeToleranceCm, FString::Printf(TEXT("not_far=%.0fcm"), Distance));
        Before = Snapshot();
        Trade(EMarburgumTradeAction::Buy, Paper, 1, false, TEXT("far"));
        Advance(1, Now);
        return;
    }
    case 1:
    {
        const FMarburgumInteractionReceipt* R = NextReceipt();
        if (!R) return;
        ExpectRefused(*R, EMarburgumInteractionReason::OutOfRange, Before, TEXT("far"));
        Advance(2, Now);
        return;
    }
    default:
        Pass();
        return;
    }
}

void UMarburgumShopSmokeSubsystem::TickClosed(double Now)
{
    const AMarburgumPlayerState* State = LocalState();
    UMarburgumUISubsystem* UI = GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>();
    switch (Step)
    {
    case 0:
        if (Now - StepSeconds < SettleSeconds) return;
        Expect(!Petra()->IsShopOpen(), TEXT("counter_open_on_sunday"));
        Expect(Shelves() == TEXT("12,8,5"), FString::Printf(TEXT("restored_shelves=%s"), *Shelves()));
        Expect(State->GetMoney() == SeededMoney - 20 + 8 - 15 && Count(Pen) == 1 && Count(Paper) == 0 && Count(Key) == 1
                && State->GetSkillXp(TEXT("trading")) == 10,
            FString::Printf(TEXT("restored=%s"), *Snapshot()));
        MB2_SHOP_LOG("event=restored %s", *Snapshot());
        if (UI)
        {
            UI->ToggleShop(Petra());
            Expect(UI->IsShopOpen(), TEXT("shop_panel_not_open"));
        }
        Advance(1, Now);
        return;
    case 1:
        if (Now - StepSeconds < ToastSettleSeconds) return;
        Before = Snapshot();
        Trade(EMarburgumTradeAction::Buy, Paper, 1, true, TEXT("closed"));
        Advance(2, Now);
        return;
    case 2:
    {
        const FMarburgumInteractionReceipt* R = NextReceipt();
        if (!R) return;
        ExpectRefused(*R, EMarburgumInteractionReason::ShopClosed, Before, TEXT("closed"));
        Advance(3, Now);
        return;
    }
    case 3:
        if (Now - StepSeconds < ToastSettleSeconds) return;
        Shot(TEXT("closed"));
        Advance(4, Now);
        return;
    default:
        Pass();
        return;
    }
}

void UMarburgumShopSmokeSubsystem::Pass()
{
    if (Problems.IsEmpty())
    {
        MB2_SHOP_LOG("event=pass");
        bDone = true;
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }
    else
    {
        Fail(TEXT("expectations"));
    }
}

void UMarburgumShopSmokeSubsystem::Fail(const FString& Reason)
{
    if (bDone)
    {
        return;
    }
    Problems.Add(Reason);
    MB2_SHOP_LOG("event=fail reason=%s", *FString::Join(Problems, TEXT(",")));
    bDone = true;
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

#undef MB2_SHOP_LOG
