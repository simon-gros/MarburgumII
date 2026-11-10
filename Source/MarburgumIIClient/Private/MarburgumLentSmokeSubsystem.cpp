#include "MarburgumLentSmokeSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumNpc.h"
#include "MarburgumPlayerController.h"
#include "MarburgumPlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "UI/MarburgumLocationBanner.h"
#include "UI/MarburgumPinnedObjective.h"
#include "UI/MarburgumUISubsystem.h"
#include "UnrealClient.h"

#define MB2_LENT_LOG(Format, ...) \
    UE_LOG(LogMarburgumNet, Log, TEXT("MB2LENT role=client tag=%s " Format), *Role, ##__VA_ARGS__)

namespace MarburgumLentSmoke
{
    constexpr double SettleSeconds = 1.0;
    constexpr double ToastSettleSeconds = 0.8;
    // First frames after a map load are not yet representative (exposure, streaming, first layout).
    constexpr double FirstShotSettleSeconds = 2.0;
    constexpr double ShotTimeoutSeconds = 10.0;
    constexpr double WalkTimeoutSeconds = 30.0;
    const FName FishingRod(TEXT("fishing_rod"));
    const FName Lent(TEXT("lent"));
    const FName Mission(TEXT("lent_first_catch"));
    const FName FirstStage(TEXT("catch_first_fish"));
}

using namespace MarburgumLentSmoke;
using MarburgumII::Gameplay::ToString;

bool UMarburgumLentSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
    return GameInstance && !GameInstance->IsDedicatedServerInstance()
        && FParse::Param(FCommandLine::Get(), TEXT("MB2LentSmoke"));
}

void UMarburgumLentSmokeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const TCHAR* CommandLine = FCommandLine::Get();
    FParse::Value(CommandLine, TEXT("MB2LentRole="), Role);
    FParse::Value(CommandLine, TEXT("MB2LentShot="), ShotPrefix);
    FParse::Value(CommandLine, TEXT("MB2LentSignal="), SignalFile);
    FParse::Value(CommandLine, TEXT("MB2LentHoldFile="), HoldFile);
    FParse::Value(CommandLine, TEXT("MB2LentTimeout="), TimeoutSeconds);
    FParse::Value(CommandLine, TEXT("MB2LentClockRate="), ClockRate);
    FString OperationText;
    if (FParse::Value(CommandLine, TEXT("MB2LentOp="), OperationText))
    {
        FGuid::Parse(OperationText, ReplayOperation);
    }
    Role = Role.ToUpper();
    StartSeconds = FPlatformTime::Seconds();
    // Every frame: walking feeds movement input, which the pawn consumes once per frame.
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UMarburgumLentSmokeSubsystem::Tick), 0.f);
    MB2_LENT_LOG("event=start op=%s shot=%d clock_rate=%.2f", *ReplayOperation.ToString(EGuidFormats::Digits),
        ShotPrefix.IsEmpty() ? 0 : 1, ClockRate);
}

void UMarburgumLentSmokeSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    if (AMarburgumPlayerController* Bound = Controller.Get())
    {
        Bound->OnInteractionReceipt.RemoveAll(this);
    }
    Super::Deinitialize();
}

bool UMarburgumLentSmokeSubsystem::Bind()
{
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (!UMarburgumSessionSubsystem::IsAdmitted(World))
    {
        return false;
    }
    AMarburgumPlayerController* Local = Cast<AMarburgumPlayerController>(World->GetFirstPlayerController());
    const AMarburgumPlayerState* State = Local ? Local->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    if (!State || !State->IsInventoryKnown() || !State->AreMissionsKnown() || !State->IsLocationKnown() || !Local->GetPawn())
    {
        return false;
    }
    if (!Angler.IsValid())
    {
        for (TActorIterator<AMarburgumNpc> It(World); It; ++It)
        {
            if (It->InteractableId == MarburgumII::Gameplay::LentAnglerId)
            {
                Angler = *It;
            }
        }
        if (!Angler.IsValid())
        {
            return false;
        }
    }
    if (Controller.Get() != Local)
    {
        Local->OnInteractionReceipt.AddUObject(this, &UMarburgumLentSmokeSubsystem::HandleReceipt);
        Controller = Local;
        StepSeconds = FPlatformTime::Seconds(); // Settle timers start at admission.
        if (ClockRate != 1.0 && World->GetWorldSettings())
        {
            // The adversary: dilating only this client's world clock makes its ServerMove timestamps run
            // ahead of real time, like a speed hack on the process clock. The server never sees the
            // dilation itself, only the timestamps (ADR-019).
            World->GetWorldSettings()->SetTimeDilation(static_cast<float>(ClockRate));
        }
        MB2_LENT_LOG("event=ready player=%d character=%d area=%s rods=%d missions=%d distance_m=%.1f",
            State->GetPlayerId(), State->HasCharacter() ? 1 : 0, *Area().ToString(), Rods(), State->GetMissions().Num(),
            FVector::Dist(Local->GetPawn()->GetActorLocation(), Angler->GetActorLocation()) / 100.f);
    }
    return true;
}

void UMarburgumLentSmokeSubsystem::HandleReceipt(const FMarburgumInteractionReceipt& Receipt)
{
    Receipts.Add(Receipt.OperationId, Receipt);
    MB2_LENT_LOG("event=receipt op=%s outcome=%s reason=%s effect=%s mission=%s stage=%s item=%s quantity=%d revision=%lld",
        *Receipt.OperationId.ToString(EGuidFormats::Digits), ToString(Receipt.Outcome), ToString(Receipt.Reason),
        ToString(Receipt.Effect), *Receipt.MissionId.ToString(), *Receipt.MissionStage.ToString(),
        *Receipt.ItemId.ToString(), Receipt.Quantity, Receipt.WorldRevision);
}

const FMarburgumInteractionReceipt* UMarburgumLentSmokeSubsystem::ReceiptFor(const FGuid& OperationId) const
{
    return OperationId.IsValid() ? Receipts.Find(OperationId) : nullptr;
}

FGuid UMarburgumLentSmokeSubsystem::Submit(const TCHAR* Label, const FGuid& OperationId)
{
    const FGuid Id = OperationId.IsValid() ? OperationId : FGuid::NewGuid();
    Receipts.Remove(Id);
    if (AMarburgumPlayerController* Local = Controller.Get())
    {
        Local->SubmitInteraction(Id, Angler.Get());
    }
    MB2_LENT_LOG("event=submit kind=%s op=%s", Label, *Id.ToString(EGuidFormats::Digits));
    return Id;
}

const AMarburgumPlayerState* UMarburgumLentSmokeSubsystem::LocalState() const
{
    const AMarburgumPlayerController* Local = Controller.Get();
    return Local ? Local->GetPlayerState<AMarburgumPlayerState>() : nullptr;
}

int32 UMarburgumLentSmokeSubsystem::Rods() const
{
    const AMarburgumPlayerState* State = LocalState();
    return State && State->IsInventoryKnown() ? State->CountItem(FishingRod) : -1;
}

FName UMarburgumLentSmokeSubsystem::Area() const
{
    const AMarburgumPlayerState* State = LocalState();
    return State && State->IsLocationKnown() ? State->GetAreaId() : FName(TEXT("unknown"));
}

FName UMarburgumLentSmokeSubsystem::Stage() const
{
    const AMarburgumPlayerState* State = LocalState();
    const FMarburgumMissionState* Progress = State ? State->FindMission(Mission) : nullptr;
    return Progress ? Progress->StageId : NAME_None;
}

bool UMarburgumLentSmokeSubsystem::BannerShows(FName AreaId) const
{
    const UMarburgumUISubsystem* UI = GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>();
    const UMarburgumLocationBanner* Banner = UI ? UI->GetLocationBanner() : nullptr;
    return Banner && Banner->IsShowing() && Banner->GetShownArea() == AreaId;
}

FName UMarburgumLentSmokeSubsystem::PinnedMission() const
{
    const UMarburgumUISubsystem* UI = GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>();
    const UMarburgumPinnedObjective* Objective = UI ? UI->GetPinnedObjective() : nullptr;
    return Objective ? Objective->GetShownMission() : NAME_None;
}

void UMarburgumLentSmokeSubsystem::Expect(bool bCondition, const FString& What)
{
    if (!bCondition)
    {
        Problems.Add(What);
        MB2_LENT_LOG("event=expectation_failed what=%s", *What);
    }
}

void UMarburgumLentSmokeSubsystem::Shot(const TCHAR* Label)
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

void UMarburgumLentSmokeSubsystem::Advance(int32 Next, double Now)
{
    Step = Next;
    StepSeconds = Now;
}

bool UMarburgumLentSmokeSubsystem::Tick(float DeltaTime)
{
    if (bDone)
    {
        return true;
    }
    const double Now = FPlatformTime::Seconds();
    FrameWallSeconds = LastTickSeconds > 0.0 ? Now - LastTickSeconds : 0.0;
    LastTickSeconds = Now;
    if (Now - StartSeconds > TimeoutSeconds)
    {
        Fail(FString::Printf(TEXT("timeout step=%d area=%s rods=%d stage=%s"), Step, *Area().ToString(), Rods(), *Stage().ToString()));
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
        MB2_LENT_LOG("event=shot file=%s saved=%d", *FPaths::GetCleanFilename(PendingShot), bSaved ? 1 : 0);
        Expect(bSaved, FString::Printf(TEXT("shot_not_saved=%s"), *FPaths::GetCleanFilename(PendingShot)));
        PendingShot.Reset();
    }
    if (!Bind())
    {
        return true;
    }
    if (Role == TEXT("M")) TickM(Now);
    else if (Role == TEXT("W")) TickW(Now);
    else if (Role == TEXT("K")) TickK(Now);
    else if (Role == TEXT("M2")) TickM2(Now);
    else if (Role == TEXT("T")) TickT(Now);
    else if (Role == TEXT("F")) TickF(Now);
    else Fail(TEXT("unknown_role"));
    return true;
}

void UMarburgumLentSmokeSubsystem::TickM(double Now)
{
    const AMarburgumPlayerState* State = LocalState();
    const FMarburgumInteractionReceipt* R = nullptr;
    switch (Step)
    {
    case 0:
        if (Now - StepSeconds < FirstShotSettleSeconds) return;
        Expect(Area() == Lent, FString::Printf(TEXT("arrival_area=%s"), *Area().ToString()));
        Expect(Rods() == 0 && State->GetMissions().IsEmpty(), FString::Printf(TEXT("start rods=%d missions=%d"), Rods(), State->GetMissions().Num()));
        if (!ShotPrefix.IsEmpty())
        {
            Expect(BannerShows(Lent), TEXT("arrival_banner_not_showing"));
        }
        Expect(Controller->FindInteractionCandidate() == Angler.Get(), TEXT("angler_not_the_candidate"));
        Shot(TEXT("arrival"));
        Advance(1, Now);
        return;
    case 1:
        FaultOp = Submit(TEXT("fault"));
        Advance(2, Now);
        return;
    case 2:
        if (!(R = ReceiptFor(FaultOp))) return;
        // The injected write failure must leave neither the mission nor its start item behind.
        Expect(R->Outcome == EMarburgumRequestOutcome::Rejected && R->Reason == EMarburgumInteractionReason::StoreWriteFailed,
            FString::Printf(TEXT("fault_receipt=%s/%s"), ToString(R->Outcome), ToString(R->Reason)));
        Expect(Rods() == 0 && Stage().IsNone(), FString::Printf(TEXT("fault_changed_state rods=%d stage=%s"), Rods(), *Stage().ToString()));
        Submit(TEXT("accept"), ReplayOperation);
        Advance(3, Now);
        return;
    case 3:
        if (!(R = ReceiptFor(ReplayOperation)) || Rods() < 1 || Stage().IsNone()) return;
        Expect(R->Outcome == EMarburgumRequestOutcome::Applied && R->Effect == EMarburgumInteractionEffect::MissionStarted
            && R->MissionId == Mission && R->MissionStage == FirstStage && R->ItemId == FishingRod && R->Quantity == 1,
            FString::Printf(TEXT("accept_receipt=%s/%s/%s/%s"), ToString(R->Outcome), ToString(R->Effect), *R->MissionId.ToString(), *R->ItemId.ToString()));
        Expect(Rods() == 1 && Stage() == FirstStage && State->GetMissions().Num() == 1,
            FString::Printf(TEXT("accept_state rods=%d stage=%s"), Rods(), *Stage().ToString()));
        if (!SignalFile.IsEmpty())
        {
            FFileHelper::SaveStringToFile(TEXT("mission_saved"), *SignalFile);
        }
        Advance(4, Now);
        return;
    case 4:
        // Let the toasts and the objective card fade in so the evidence shows them at full strength.
        if (Now - StepSeconds < ToastSettleSeconds) return;
        if (!ShotPrefix.IsEmpty())
        {
            Expect(PinnedMission() == Mission, FString::Printf(TEXT("objective_shows=%s"), *PinnedMission().ToString()));
        }
        Shot(TEXT("mission-started"));
        Advance(5, Now);
        return;
    case 5:
        Submit(TEXT("replay"), ReplayOperation);
        Advance(6, Now);
        return;
    case 6:
        if (!(R = ReceiptFor(ReplayOperation)) || Now - StepSeconds < SettleSeconds) return;
        Expect(R->Outcome == EMarburgumRequestOutcome::Duplicate && R->Effect == EMarburgumInteractionEffect::MissionStarted,
            FString::Printf(TEXT("replay_receipt=%s/%s"), ToString(R->Outcome), ToString(R->Effect)));
        Expect(Rods() == 1 && Stage() == FirstStage, FString::Printf(TEXT("replay_changed_state rods=%d stage=%s"), Rods(), *Stage().ToString()));
        FreshOp = Submit(TEXT("show_catch"));
        Advance(7, Now);
        return;
    case 7:
        if (!(R = ReceiptFor(FreshOp))) return;
        Expect(R->Outcome == EMarburgumRequestOutcome::Rejected && R->Reason == EMarburgumInteractionReason::RequirementNotMet
            && R->MissionStage == FirstStage, FString::Printf(TEXT("show_catch=%s/%s/%s"), ToString(R->Outcome), ToString(R->Reason), *R->MissionStage.ToString()));
        Expect(Rods() == 1 && Stage() == FirstStage, FString::Printf(TEXT("show_catch_changed_state rods=%d stage=%s"), Rods(), *Stage().ToString()));
        Advance(8, Now);
        return;
    case 8:
        if (Now - StepSeconds < ToastSettleSeconds) return;
        Shot(TEXT("not-yet"));
        if (UMarburgumUISubsystem* UI = GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>(); UI && !UI->IsJournalOpen())
        {
            UI->ToggleJournal();
        }
        Advance(9, Now);
        return;
    case 9:
        if (Now - StepSeconds < ToastSettleSeconds) return;
        Shot(TEXT("journal"));
        MB2_LENT_LOG("event=holding rods=%d stage=%s", Rods(), *Stage().ToString());
        Advance(10, Now);
        return;
    case 10:
        // Stay online while W checks that it cannot see this character's progress.
        if (!HoldFile.IsEmpty() && !IFileManager::Get().FileExists(*HoldFile)) return;
        Pass();
        return;
    default:
        return;
    }
}

void UMarburgumLentSmokeSubsystem::TickW(double Now)
{
    const FMarburgumInteractionReceipt* R = nullptr;
    switch (Step)
    {
    case 0:
        Expect(Area().IsNone(), FString::Printf(TEXT("bridge_area=%s"), *Area().ToString()));
        FreshOp = Submit(TEXT("from_bridge"));
        Advance(1, Now);
        return;
    case 1:
        if (!(R = ReceiptFor(FreshOp))) return;
        Expect(R->Outcome == EMarburgumRequestOutcome::Rejected && R->Reason == EMarburgumInteractionReason::OutOfRange,
            FString::Printf(TEXT("from_bridge=%s/%s"), ToString(R->Outcome), ToString(R->Reason)));
        Expect(Stage().IsNone() && Rods() == 0, FString::Printf(TEXT("from_bridge_changed_state rods=%d stage=%s"), Rods(), *Stage().ToString()));
        Advance(2, Now);
        return;
    case 2:
    {
        // Progress is private: once M's mission is saved, W's copy of M's player state carries none of it.
        if (SignalFile.IsEmpty() || !IFileManager::Get().FileExists(*SignalFile)) return;
        if (Now - StepSeconds < SettleSeconds) return; // Give any (wrongly) replicated update time to arrive.
        const AGameStateBase* GameState = Controller->GetWorld()->GetGameState();
        int32 Others = 0;
        for (const APlayerState* Player : GameState ? GameState->PlayerArray : TArray<TObjectPtr<APlayerState>>())
        {
            const AMarburgumPlayerState* Other = Cast<AMarburgumPlayerState>(Player);
            if (!Other || Other == LocalState())
            {
                continue;
            }
            ++Others;
            Expect(!Other->AreMissionsKnown() && Other->GetMissions().IsEmpty(), TEXT("sees_other_missions"));
            Expect(!Other->IsInventoryKnown() && Other->GetInventory().IsEmpty(), TEXT("sees_other_inventory"));
            Expect(!Other->IsLocationKnown(), TEXT("sees_other_location"));
        }
        Expect(Others >= 1, FString::Printf(TEXT("others_online=%d"), Others));
        MB2_LENT_LOG("event=privacy_checked others=%d", Others);
        Advance(3, Now);
        return;
    }
    case 3:
        if (!WalkIntoLent()) return;
        Pass();
        return;
    default:
        return;
    }
}

void UMarburgumLentSmokeSubsystem::TickK(double Now)
{
    switch (Step)
    {
    case 0:
        Expect(!LocalState()->HasCharacter(), TEXT("keyless_has_character"));
        FreshOp = Submit(TEXT("keyless"));
        Advance(1, Now);
        return;
    case 1:
    {
        const FMarburgumInteractionReceipt* R = ReceiptFor(FreshOp);
        if (!R) return;
        Expect(R->Outcome == EMarburgumRequestOutcome::Rejected && R->Reason == EMarburgumInteractionReason::NoCharacter,
            FString::Printf(TEXT("keyless=%s/%s"), ToString(R->Outcome), ToString(R->Reason)));
        Expect(Rods() == 0 && LocalState()->GetMissions().IsEmpty(), TEXT("keyless_changed_state"));
        Pass();
        return;
    }
    default:
        return;
    }
}

void UMarburgumLentSmokeSubsystem::TickM2(double Now)
{
    switch (Step)
    {
    case 0:
        // Everything below comes from the restarted server's durable record.
        Expect(Rods() == 1 && Stage() == FirstStage, FString::Printf(TEXT("restored rods=%d stage=%s"), Rods(), *Stage().ToString()));
        Expect(Area() == Lent, FString::Printf(TEXT("restored_area=%s"), *Area().ToString()));
        MB2_LENT_LOG("event=restored rods=%d stage=%s", Rods(), *Stage().ToString());
        Submit(TEXT("replay_after_restart"), ReplayOperation);
        Advance(1, Now);
        return;
    case 1:
    {
        const FMarburgumInteractionReceipt* R = ReceiptFor(ReplayOperation);
        if (!R || Now - StepSeconds < FirstShotSettleSeconds) return;
        Expect(R->Outcome == EMarburgumRequestOutcome::Duplicate && R->Effect == EMarburgumInteractionEffect::MissionStarted,
            FString::Printf(TEXT("replay_after_restart=%s/%s"), ToString(R->Outcome), ToString(R->Effect)));
        Expect(Rods() == 1 && Stage() == FirstStage, FString::Printf(TEXT("replay_changed_state rods=%d stage=%s"), Rods(), *Stage().ToString()));
        if (!ShotPrefix.IsEmpty())
        {
            Expect(PinnedMission() == Mission, FString::Printf(TEXT("restored_objective=%s"), *PinnedMission().ToString()));
        }
        Shot(TEXT("restored"));
        Advance(2, Now);
        return;
    }
    default:
        Pass();
        return;
    }
}

void UMarburgumLentSmokeSubsystem::TickT(double Now)
{
    switch (Step)
    {
    case 0:
        Expect(Area().IsNone(), FString::Printf(TEXT("bridge_area=%s"), *Area().ToString()));
        Expect(LocalState()->GetMissions().IsEmpty() && PinnedMission().IsNone(), TEXT("new_character_has_objective"));
        Advance(1, Now);
        return;
    case 1:
        if (!WalkIntoLent()) return;
        Advance(2, Now);
        return;
    case 2:
        // Shortly after the crossing the banner has faded in and is still holding.
        if (Now - StepSeconds < 0.6) return;
        if (!ShotPrefix.IsEmpty())
        {
            Expect(BannerShows(Lent), TEXT("entering_banner_not_showing"));
        }
        Shot(TEXT("entering-lent"));
        Advance(3, Now);
        return;
    default:
        Pass();
        return;
    }
}

void UMarburgumLentSmokeSubsystem::TickF(double Now)
{
    switch (Step)
    {
    case 0:
        Expect(Area().IsNone(), FString::Printf(TEXT("bridge_area=%s"), *Area().ToString()));
        Expect(ClockRate > 1.5, FString::Printf(TEXT("adversary_needs_fast_clock rate=%.2f"), ClockRate));
        Advance(1, Now);
        return;
    case 1:
        // The server, not this client, decides how far the pawn got; the script checks the server's log.
        if (!WalkIntoLent()) return;
        Pass();
        return;
    default:
        return;
    }
}

bool UMarburgumLentSmokeSubsystem::WalkIntoLent()
{
    APawn* Body = Controller.IsValid() ? Controller->GetPawn() : nullptr;
    const double Now = FPlatformTime::Seconds();
    if (!Body)
    {
        return false;
    }
    // World time is read after the world ticked this frame and wall time later in the same frame, so
    // the two can differ by up to one frame's length; the honest-clock check allows the longest one.
    LongestWalkFrameSeconds = FMath::Max(LongestWalkFrameSeconds, FrameWallSeconds);
    if (WalkStartSeconds <= 0.0)
    {
        WalkStartSeconds = Now;
        WalkStartWorldSeconds = Body->GetWorld()->GetTimeSeconds();
        WalkStart = Body->GetActorLocation();
        const ACharacter* Walker = Cast<ACharacter>(Body);
        MB2_LENT_LOG("event=walk_start area=%s x_m=%.2f y_m=%.2f max_walk_mps=%.2f max_run_mps=%.2f clock_rate=%.2f", *Area().ToString(),
            WalkStart.X / 100.f, WalkStart.Y / 100.f,
            Walker && Walker->GetCharacterMovement() ? Walker->GetCharacterMovement()->MaxWalkSpeed / 100.f : 0.f,
            MarburgumII::Gameplay::RunSpeedCm / 100.f, ClockRate);
    }
    if (Area() == Lent)
    {
        // World time next to wall time: the clock this client stamped its moves with (ADR-019).
        const double WallSeconds = Now - WalkStartSeconds;
        const double WorldSeconds = Body->GetWorld()->GetTimeSeconds() - WalkStartWorldSeconds;
        MB2_LENT_LOG("event=walk_arrived area=lent walked_m=%.1f seconds=%.2f world_seconds=%.2f longest_frame_s=%.3f",
            FVector::Dist2D(WalkStart, Body->GetActorLocation()) / 100.f, WallSeconds, WorldSeconds, LongestWalkFrameSeconds);
        if (ClockRate == 1.0)
        {
            // An honest client's clock must not run ahead of real time, whatever its frame rate.
            Expect(WorldSeconds <= WallSeconds * 1.02 + LongestWalkFrameSeconds + 0.02,
                FString::Printf(TEXT("clock_runs_fast world_s=%.2f wall_s=%.2f longest_frame_s=%.3f"), WorldSeconds,
                    WallSeconds, LongestWalkFrameSeconds));
        }
        else
        {
            // The adversary only proves something if its clock really did run fast.
            Expect(WorldSeconds >= WallSeconds * ClockRate * 0.9,
                FString::Printf(TEXT("adversary_clock_not_fast world_s=%.2f wall_s=%.2f"), WorldSeconds, WallSeconds));
        }
        return true;
    }
    if (Now - WalkStartSeconds > WalkTimeoutSeconds)
    {
        Fail(FString::Printf(TEXT("walk_timeout area=%s walked_m=%.1f"), *Area().ToString(),
            FVector::Dist2D(WalkStart, Body->GetActorLocation()) / 100.f));
        return false;
    }
    // North is -Y in the Lent map (original z points south). The server places the pawn, not the client.
    Body->AddMovementInput(FVector(0.f, -1.f, 0.f), 1.f);
    return false;
}

void UMarburgumLentSmokeSubsystem::Pass()
{
    if (Problems.IsEmpty())
    {
        MB2_LENT_LOG("event=pass");
        bDone = true;
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }
    else
    {
        Fail(TEXT("expectations"));
    }
}

void UMarburgumLentSmokeSubsystem::Fail(const FString& Reason)
{
    if (bDone)
    {
        return;
    }
    Problems.Add(Reason);
    MB2_LENT_LOG("event=fail reason=%s", *FString::Join(Problems, TEXT(",")));
    bDone = true;
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

#undef MB2_LENT_LOG
