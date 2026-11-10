#include "MarburgumInteractionSmokeSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumInteractable.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlayerController.h"
#include "MarburgumPlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "UI/MarburgumUISubsystem.h"
#include "UnrealClient.h"

#define MB2_INTERACT_LOG(Format, ...) \
    UE_LOG(LogMarburgumNet, Log, TEXT("MB2INTERACT role=client tag=%s " Format), *Role, ##__VA_ARGS__)

using namespace MarburgumII::Gameplay;

namespace
{
    constexpr double SettleSeconds = 1.0;
    constexpr double PromptSettleSeconds = 0.8;
    // First frames after a map load are not yet representative (exposure, streaming, first layout).
    constexpr double FirstShotSettleSeconds = 3.0;
    constexpr double ShotTimeoutSeconds = 10.0;
    const FName FishingRod(TEXT("fishing_rod"));
}

bool UMarburgumInteractionSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
    return GameInstance && !GameInstance->IsDedicatedServerInstance()
        && FParse::Param(FCommandLine::Get(), TEXT("MB2InteractSmoke"));
}

void UMarburgumInteractionSmokeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const TCHAR* CommandLine = FCommandLine::Get();
    FParse::Value(CommandLine, TEXT("MB2InteractRole="), Role);
    FParse::Value(CommandLine, TEXT("MB2InteractShot="), ShotPrefix);
    FParse::Value(CommandLine, TEXT("MB2InteractHoldFile="), HoldFile);
    FParse::Value(CommandLine, TEXT("MB2InteractExpect="), ExpectedItems);
    FParse::Value(CommandLine, TEXT("MB2InteractTimeout="), TimeoutSeconds);
    FString OperationText;
    if (FParse::Value(CommandLine, TEXT("MB2InteractOp="), OperationText))
    {
        FGuid::Parse(OperationText, ReplayOperation);
    }
    Role = Role.ToUpper();
    StartSeconds = FPlatformTime::Seconds();
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UMarburgumInteractionSmokeSubsystem::Tick), 0.05f);
    MB2_INTERACT_LOG("event=start op=%s expect=%d shot=%d", *ReplayOperation.ToString(EGuidFormats::Digits),
        ExpectedItems, ShotPrefix.IsEmpty() ? 0 : 1);
}

void UMarburgumInteractionSmokeSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    if (AMarburgumPlayerController* Bound = Controller.Get())
    {
        Bound->OnInteractionReceipt.RemoveAll(this);
    }
    Super::Deinitialize();
}

bool UMarburgumInteractionSmokeSubsystem::Bind()
{
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (!UMarburgumSessionSubsystem::IsAdmitted(World))
    {
        return false;
    }
    AMarburgumPlayerController* Local = Cast<AMarburgumPlayerController>(World->GetFirstPlayerController());
    const AMarburgumPlayerState* State = Local ? Local->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    if (!State || !State->IsInventoryKnown() || !Local->GetPawn())
    {
        return false;
    }
    if (!Crate.IsValid() || !FarCrate.IsValid())
    {
        for (TActorIterator<AMarburgumInteractable> It(World); It; ++It)
        {
            if (It->InteractableId == SupplyCrateId) Crate = *It;
            else if (It->InteractableId == FarSupplyCrateId) FarCrate = *It;
        }
    }
    if (!Crate.IsValid() || !FarCrate.IsValid() || !Crate->IsStockKnown() || !FarCrate->IsStockKnown())
    {
        return false;
    }
    if (Controller.Get() != Local)
    {
        Local->OnInteractionReceipt.AddUObject(this, &UMarburgumInteractionSmokeSubsystem::HandleReceipt);
        Controller = Local;
        StepSeconds = FPlatformTime::Seconds(); // Settle timers start at admission.
        MB2_INTERACT_LOG("event=ready player=%d character=%d items=%d crate_stock=%d far_stock=%d",
            State->GetPlayerId(), State->HasCharacter() ? 1 : 0, Items(), Stock(Crate.Get()), Stock(FarCrate.Get()));
    }
    return true;
}

void UMarburgumInteractionSmokeSubsystem::HandleReceipt(const FMarburgumInteractionReceipt& Receipt)
{
    Receipts.Add(Receipt.OperationId, Receipt);
    ++ReceiptCounts.FindOrAdd(Receipt.OperationId);
    MB2_INTERACT_LOG("event=receipt op=%s outcome=%s reason=%s target=%s item=%s quantity=%d stock=%d revision=%lld",
        *Receipt.OperationId.ToString(EGuidFormats::Digits), ToString(Receipt.Outcome), ToString(Receipt.Reason),
        *Receipt.InteractableId.ToString(), *Receipt.ItemId.ToString(), Receipt.Quantity, Receipt.StockRemaining,
        Receipt.WorldRevision);
}

const FMarburgumInteractionReceipt* UMarburgumInteractionSmokeSubsystem::ReceiptFor(const FGuid& OperationId) const
{
    return OperationId.IsValid() ? Receipts.Find(OperationId) : nullptr;
}

FGuid UMarburgumInteractionSmokeSubsystem::Submit(AMarburgumInteractable* Target, const TCHAR* Label, const FGuid& OperationId)
{
    const FGuid Id = OperationId.IsValid() ? OperationId : FGuid::NewGuid();
    Receipts.Remove(Id);
    if (AMarburgumPlayerController* Local = Controller.Get())
    {
        Local->SubmitInteraction(Id, Target);
    }
    MB2_INTERACT_LOG("event=submit kind=%s op=%s target=%s", Label, *Id.ToString(EGuidFormats::Digits),
        Target ? *Target->InteractableId.ToString() : TEXT("-"));
    return Id;
}

int32 UMarburgumInteractionSmokeSubsystem::Items() const
{
    const AMarburgumPlayerController* Local = Controller.Get();
    const AMarburgumPlayerState* State = Local ? Local->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    return State && State->IsInventoryKnown() ? State->CountItem(FishingRod) : -1;
}

int32 UMarburgumInteractionSmokeSubsystem::Stock(const AMarburgumInteractable* Target) const
{
    return Target ? Target->GetRemainingStock() : -1;
}

void UMarburgumInteractionSmokeSubsystem::Expect(bool bCondition, const FString& What)
{
    if (!bCondition)
    {
        Problems.Add(What);
        MB2_INTERACT_LOG("event=expectation_failed what=%s", *What);
    }
}

void UMarburgumInteractionSmokeSubsystem::Shot(const TCHAR* Label)
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

void UMarburgumInteractionSmokeSubsystem::Advance(int32 Next, double Now)
{
    Step = Next;
    StepSeconds = Now;
}

bool UMarburgumInteractionSmokeSubsystem::Tick(float DeltaTime)
{
    if (bDone)
    {
        return true;
    }
    const double Now = FPlatformTime::Seconds();
    if (Now - StartSeconds > TimeoutSeconds)
    {
        Fail(FString::Printf(TEXT("timeout step=%d items=%d stock=%d"), Step, Items(), Stock(Crate.Get())));
        return true;
    }
    if (!PendingShot.IsEmpty())
    {
        // One screenshot at a time; later steps wait so each image shows the state it is named for.
        const bool bSaved = IFileManager::Get().FileSize(*PendingShot) > 0;
        if (!bSaved && Now - ShotRequestedSeconds < ShotTimeoutSeconds)
        {
            return true;
        }
        MB2_INTERACT_LOG("event=shot file=%s saved=%d", *FPaths::GetCleanFilename(PendingShot), bSaved ? 1 : 0);
        PendingShot.Reset();
    }

    if (Role == TEXT("D") || Role == TEXT("X"))
    {
        TickRefused(Now);
        return true;
    }
    if (!Bind())
    {
        return true;
    }
    if (Role == TEXT("A")) TickA(Now);
    else if (Role == TEXT("B")) TickB(Now);
    else if (Role == TEXT("A2")) TickA2(Now);
    else if (Role == TEXT("A3")) TickA3(Now);
    else if (Role == TEXT("B2")) TickB2(Now);
    else if (Role == TEXT("N")) TickN(Now);
    else Fail(TEXT("unknown_role"));
    return true;
}

void UMarburgumInteractionSmokeSubsystem::TickA(double Now)
{
    AMarburgumInteractable* Near = Crate.Get();
    AMarburgumInteractable* Far = FarCrate.Get();
    const FMarburgumInteractionReceipt* R = nullptr;
    switch (Step)
    {
    case 0:
    {
        const UWorld* World = Controller->GetWorld();
        const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
        if (!GameState || GameState->PlayerArray.Num() < 2)
        {
            StepSeconds = Now;
            return; // B must be online for the race.
        }
        if (Now - StepSeconds < FirstShotSettleSeconds)
        {
            return; // Let the world and the prompt settle before the first evidence image.
        }
        Expect(Items() == 0, FString::Printf(TEXT("start_items=%d"), Items()));
        Expect(Stock(Near) == 3, FString::Printf(TEXT("start_stock=%d"), Stock(Near)));
        Shot(TEXT("prompt"));
        Advance(1, Now);
        return;
    }
    case 1:
        FaultOp = Submit(Near, TEXT("fault"));
        Advance(2, Now);
        return;
    case 2:
        if (!(R = ReceiptFor(FaultOp))) return;
        // The injected write failure must leave no trace anywhere.
        Expect(R->Outcome == EMarburgumRequestOutcome::Rejected && R->Reason == EMarburgumInteractionReason::StoreWriteFailed,
            FString::Printf(TEXT("fault_receipt=%s/%s"), ToString(R->Outcome), ToString(R->Reason)));
        Expect(Items() == 0 && Stock(Near) == 3, FString::Printf(TEXT("fault_changed_state items=%d stock=%d"), Items(), Stock(Near)));
        Submit(Near, TEXT("first"), ReplayOperation);
        Advance(3, Now);
        return;
    case 3:
        if (!(R = ReceiptFor(ReplayOperation)) || Items() < 1 || Stock(Near) > 2) return;
        Expect(R->Outcome == EMarburgumRequestOutcome::Applied, FString::Printf(TEXT("first_receipt=%s"), ToString(R->Outcome)));
        Expect(Items() == 1 && Stock(Near) == 2, FString::Printf(TEXT("first items=%d stock=%d"), Items(), Stock(Near)));
        Advance(4, Now);
        return;
    case 4:
        // Let the receipt toast finish fading in so the evidence shows it at full strength.
        if (Now - StepSeconds < PromptSettleSeconds) return;
        Shot(TEXT("applied"));
        Advance(40, Now);
        return;
    case 40:
        Submit(Near, TEXT("replay"), ReplayOperation);
        Advance(5, Now);
        return;
    case 5:
        if (!(R = ReceiptFor(ReplayOperation)) || Now - StepSeconds < SettleSeconds) return;
        Expect(R->Outcome == EMarburgumRequestOutcome::Duplicate && R->Quantity == 1,
            FString::Printf(TEXT("replay_receipt=%s"), ToString(R->Outcome)));
        Expect(Items() == 1 && Stock(Near) == 2, FString::Printf(TEXT("replay_changed_state items=%d stock=%d"), Items(), Stock(Near)));
        FarOp = Submit(Far, TEXT("far"));
        Advance(6, Now);
        return;
    case 6:
        if (!(R = ReceiptFor(FarOp))) return;
        Expect(R->Outcome == EMarburgumRequestOutcome::Rejected && R->Reason == EMarburgumInteractionReason::OutOfRange,
            FString::Printf(TEXT("far_receipt=%s/%s"), ToString(R->Outcome), ToString(R->Reason)));
        Expect(Stock(Far) == Far->InitialStock && Items() == 1, FString::Printf(TEXT("far_changed_state far_stock=%d items=%d"), Stock(Far), Items()));
        SecondOp = Submit(Near, TEXT("second"));
        Advance(7, Now);
        return;
    case 7:
        if (!(R = ReceiptFor(SecondOp))) return;
        Expect(R->Outcome == EMarburgumRequestOutcome::Applied, FString::Printf(TEXT("second_receipt=%s"), ToString(R->Outcome)));
        // Contend for the last unit at once; B fires when it sees the stock reach one.
        RaceOp = Submit(Near, TEXT("race"));
        Advance(8, Now);
        return;
    case 8:
        if (!ReceiptFor(RaceOp) || Stock(Near) != 0) return;
        if (Now - StepSeconds < SettleSeconds) return;
        {
            R = ReceiptFor(RaceOp);
            const bool bWon = R->Outcome == EMarburgumRequestOutcome::Applied;
            Expect(bWon || R->Reason == EMarburgumInteractionReason::OutOfStock,
                FString::Printf(TEXT("race_receipt=%s/%s"), ToString(R->Outcome), ToString(R->Reason)));
            Expect(Items() == (bWon ? 3 : 2), FString::Printf(TEXT("final_items=%d won=%d"), Items(), bWon ? 1 : 0));
            MB2_INTERACT_LOG("event=final items=%d stock=%d race=%s", Items(), Stock(Near), bWon ? TEXT("won") : TEXT("lost"));
        }
        if (UMarburgumUISubsystem* UI = GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>(); UI && !UI->IsInventoryOpen())
        {
            UI->ToggleInventory();
        }
        Advance(9, Now);
        return;
    case 9:
        if (Now - StepSeconds < PromptSettleSeconds) return;
        Shot(TEXT("inventory"));
        Advance(10, Now);
        return;
    default:
        Pass();
        return;
    }
}

void UMarburgumInteractionSmokeSubsystem::TickB(double Now)
{
    AMarburgumInteractable* Near = Crate.Get();
    switch (Step)
    {
    case 0:
        if (Stock(Near) > 1) return;
        RaceOp = Submit(Near, TEXT("race"));
        Advance(1, Now);
        return;
    case 1:
    {
        const FMarburgumInteractionReceipt* R = ReceiptFor(RaceOp);
        if (!R || Stock(Near) != 0 || Now - StepSeconds < SettleSeconds) return;
        const bool bWon = R->Outcome == EMarburgumRequestOutcome::Applied;
        Expect(bWon || R->Reason == EMarburgumInteractionReason::OutOfStock,
            FString::Printf(TEXT("race_receipt=%s/%s"), ToString(R->Outcome), ToString(R->Reason)));
        Expect(Items() == (bWon ? 1 : 0), FString::Printf(TEXT("final_items=%d won=%d"), Items(), bWon ? 1 : 0));
        MB2_INTERACT_LOG("event=final items=%d stock=%d race=%s", Items(), Stock(Near), bWon ? TEXT("won") : TEXT("lost"));
        Pass();
        return;
    }
    default:
        return;
    }
}

void UMarburgumInteractionSmokeSubsystem::TickA2(double Now)
{
    AMarburgumInteractable* Near = Crate.Get();
    const FMarburgumInteractionReceipt* R = nullptr;
    switch (Step)
    {
    case 0:
        // Everything below comes from the restarted server's durable record.
        Expect(Items() == ExpectedItems, FString::Printf(TEXT("restored_items=%d want=%d"), Items(), ExpectedItems));
        Expect(Stock(Near) == 0, FString::Printf(TEXT("restored_stock=%d"), Stock(Near)));
        MB2_INTERACT_LOG("event=restored items=%d stock=%d", Items(), Stock(Near));
        Submit(Near, TEXT("replay_after_restart"), ReplayOperation);
        Advance(1, Now);
        return;
    case 1:
        if (!(R = ReceiptFor(ReplayOperation))) return;
        Expect(R->Outcome == EMarburgumRequestOutcome::Duplicate,
            FString::Printf(TEXT("replay_after_restart=%s/%s"), ToString(R->Outcome), ToString(R->Reason)));
        FreshOp = Submit(Near, TEXT("after_restart"));
        Advance(2, Now);
        return;
    case 2:
        if (!(R = ReceiptFor(FreshOp))) return;
        Expect(R->Outcome == EMarburgumRequestOutcome::Rejected && R->Reason == EMarburgumInteractionReason::OutOfStock,
            FString::Printf(TEXT("after_restart=%s/%s"), ToString(R->Outcome), ToString(R->Reason)));
        if (UMarburgumUISubsystem* UI = GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>(); UI && !UI->IsInventoryOpen())
        {
            UI->ToggleInventory();
        }
        Advance(3, Now);
        return;
    case 3:
        if (Now - StepSeconds < PromptSettleSeconds) return;
        Expect(Items() == ExpectedItems, FString::Printf(TEXT("items_after_refusals=%d"), Items()));
        Shot(TEXT("restored"));
        MB2_INTERACT_LOG("event=holding");
        Advance(4, Now);
        return;
    case 4:
        // Stay online so a second login with this character can be refused (role D).
        if (!HoldFile.IsEmpty() && !IFileManager::Get().FileExists(*HoldFile)) return;
        Pass();
        return;
    default:
        return;
    }
}

void UMarburgumInteractionSmokeSubsystem::TickA3(double Now)
{
    switch (Step)
    {
    case 0:
        // A later session of the same character: admitted again once the earlier one logged out.
        Expect(Items() == ExpectedItems, FString::Printf(TEXT("rejoin_items=%d want=%d"), Items(), ExpectedItems));
        Expect(Stock(Crate.Get()) == 0, FString::Printf(TEXT("rejoin_stock=%d"), Stock(Crate.Get())));
        MB2_INTERACT_LOG("event=restored items=%d stock=%d", Items(), Stock(Crate.Get()));
        Advance(1, Now);
        return;
    case 1:
        // The HUD mounts when the session reports Ready, which can be a tick after admission.
        if (UMarburgumUISubsystem* UI = GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>(); UI && !UI->IsInventoryOpen())
        {
            UI->ToggleInventory();
            StepSeconds = Now;
            return;
        }
        if (Now - StepSeconds < FirstShotSettleSeconds) return;
        Shot(TEXT("restored"));
        Advance(2, Now);
        return;
    default:
        Pass();
        return;
    }
}

void UMarburgumInteractionSmokeSubsystem::TickB2(double Now)
{
    Expect(Items() == ExpectedItems, FString::Printf(TEXT("restored_items=%d want=%d"), Items(), ExpectedItems));
    MB2_INTERACT_LOG("event=restored items=%d stock=%d", Items(), Stock(Crate.Get()));
    Pass();
}

void UMarburgumInteractionSmokeSubsystem::TickN(double Now)
{
    switch (Step)
    {
    case 0:
    {
        const AMarburgumPlayerState* State = Controller->GetPlayerState<AMarburgumPlayerState>();
        Expect(State && !State->HasCharacter(), TEXT("keyless_has_character"));
        FreshOp = Submit(Crate.Get(), TEXT("keyless"));
        Advance(1, Now);
        return;
    }
    case 1:
    {
        const FMarburgumInteractionReceipt* R = ReceiptFor(FreshOp);
        if (!R) return;
        Expect(R->Outcome == EMarburgumRequestOutcome::Rejected && R->Reason == EMarburgumInteractionReason::NoCharacter,
            FString::Printf(TEXT("keyless_receipt=%s/%s"), ToString(R->Outcome), ToString(R->Reason)));
        Expect(Items() == 0, FString::Printf(TEXT("keyless_items=%d"), Items()));
        Pass();
        return;
    }
    default:
        return;
    }
}

void UMarburgumInteractionSmokeSubsystem::TickRefused(double Now)
{
    const UMarburgumSessionSubsystem* Session = GetGameInstance()->GetSubsystem<UMarburgumSessionSubsystem>();
    if (!Session)
    {
        Fail(TEXT("no_session_subsystem"));
        return;
    }
    const FMarburgumSessionView& View = Session->GetView();
    if (View.Phase == MarburgumII::Session::EPhase::Ready)
    {
        Fail(TEXT("admitted_but_should_be_refused"));
        return;
    }
    if (View.Phase != MarburgumII::Session::EPhase::Error)
    {
        return;
    }
    switch (Step)
    {
    case 0:
        Expect(View.Fault == MarburgumII::Session::EFault::Refused,
            FString::Printf(TEXT("fault=%s"), MarburgumII::Session::ToString(View.Fault)));
        MB2_INTERACT_LOG("event=refused fault=%s detail=\"%s\"", MarburgumII::Session::ToString(View.Fault), *View.Detail);
        Advance(1, Now);
        return;
    case 1:
        // The refusal can arrive before the front-end has drawn its first frame.
        if (Now - StepSeconds < FirstShotSettleSeconds) return;
        Shot(TEXT("refused"));
        Advance(2, Now);
        return;
    default:
        Pass();
        return;
    }
}

void UMarburgumInteractionSmokeSubsystem::Pass()
{
    if (Problems.IsEmpty())
    {
        MB2_INTERACT_LOG("event=pass");
        Finish(0);
    }
    else
    {
        MB2_INTERACT_LOG("event=fail reason=%s", *FString::Join(Problems, TEXT(",")));
        Finish(1);
    }
}

void UMarburgumInteractionSmokeSubsystem::Fail(const FString& Reason)
{
    Problems.Add(Reason);
    MB2_INTERACT_LOG("event=fail reason=%s", *FString::Join(Problems, TEXT(",")));
    Finish(1);
}

void UMarburgumInteractionSmokeSubsystem::Finish(int32 ExitCode)
{
    if (bDone)
    {
        return;
    }
    bDone = true;
    FPlatformMisc::RequestExitWithStatus(false, static_cast<uint8>(ExitCode));
}

#undef MB2_INTERACT_LOG
