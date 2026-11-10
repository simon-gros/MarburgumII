#include "MarburgumSwimSmokeSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "MarburgumCharacter.h"
#include "MarburgumCharacterMovement.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlayerController.h"
#include "MarburgumPlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "UI/MarburgumPlayerCondition.h"
#include "UI/MarburgumUISubsystem.h"
#include "UnrealClient.h"

#define MB2_SWIM_LOG(Format, ...) \
    UE_LOG(LogMarburgumNet, Log, TEXT("MB2SWIM role=client tag=%s " Format), *Role, ##__VA_ARGS__)

namespace MarburgumSwimSmoke
{
    constexpr double SettleSeconds = 1.0;
    constexpr double ShotTimeoutSeconds = 10.0;
    constexpr double SampleHistorySeconds = 5.0;
    // West from the arrival start the promenade slopes into the Drava about 20 m away (-X); east is back.
    const FVector West(-1.f, 0.f, 0.f);
    const FVector East(1.f, 0.f, 0.f);
    // Swim speed is 1.1 m/s; generous for the start of a stroke and frame timing.
    constexpr double SwimMin = 0.85;
    constexpr double SwimMax = 1.35;
    // A minute of breath per bar: ten seconds under costs about 16.7 points.
    constexpr double UnderSeconds = 10.0;
    constexpr int32 UnderCostMin = 13;
    constexpr int32 UnderCostMax = 21;
    // The recovery point is the arrival start on this map.
    constexpr double RecoveryPointCm = 300.0;
    // Leaving while down this long after drowning; W2 must arrive with the rest of the wait.
    constexpr double DownBeforeLeavingSeconds = 5.0;
}

using namespace MarburgumSwimSmoke;
using namespace MarburgumII::Gameplay;

bool UMarburgumSwimSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
    return GameInstance && !GameInstance->IsDedicatedServerInstance() && FParse::Param(FCommandLine::Get(), TEXT("MB2SwimSmoke"));
}

void UMarburgumSwimSmokeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const TCHAR* CommandLine = FCommandLine::Get();
    FParse::Value(CommandLine, TEXT("MB2SwimRole="), Role);
    FParse::Value(CommandLine, TEXT("MB2SwimShot="), ShotPrefix);
    FParse::Value(CommandLine, TEXT("MB2SwimTimeout="), TimeoutSeconds);
    Role = Role.ToUpper();
    StartSeconds = FPlatformTime::Seconds();
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMarburgumSwimSmokeSubsystem::Tick), 0.f);
    MB2_SWIM_LOG("event=start shot=%d", ShotPrefix.IsEmpty() ? 0 : 1);
}

void UMarburgumSwimSmokeSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    Super::Deinitialize();
}

const AMarburgumPlayerState* UMarburgumSwimSmokeSubsystem::LocalState() const
{
    const AMarburgumPlayerController* Local = Controller.Get();
    return Local ? Local->GetPlayerState<AMarburgumPlayerState>() : nullptr;
}

AMarburgumCharacter* UMarburgumSwimSmokeSubsystem::Body() const
{
    const AMarburgumPlayerController* Local = Controller.Get();
    return Local ? Cast<AMarburgumCharacter>(Local->GetPawn()) : nullptr;
}

UMarburgumCharacterMovement* UMarburgumSwimSmokeSubsystem::Movement() const
{
    const AMarburgumCharacter* Character = Body();
    return Character ? Cast<UMarburgumCharacterMovement>(Character->GetCharacterMovement()) : nullptr;
}

bool UMarburgumSwimSmokeSubsystem::Bind()
{
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (!UMarburgumSessionSubsystem::IsAdmitted(World))
    {
        return false;
    }
    AMarburgumPlayerController* Local = Cast<AMarburgumPlayerController>(World->GetFirstPlayerController());
    const AMarburgumPlayerState* State = Local ? Local->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    const AMarburgumCharacter* Character = Local ? Cast<AMarburgumCharacter>(Local->GetPawn()) : nullptr;
    const UMarburgumCharacterMovement* Moving = Character ? Cast<UMarburgumCharacterMovement>(Character->GetCharacterMovement()) : nullptr;
    if (!State || !State->IsConditionKnown() || !Moving || !Moving->IsConditionSynced())
    {
        return false;
    }
    if (Controller.Get() != Local)
    {
        Controller = Local;
        StepSeconds = FPlatformTime::Seconds();
        ArrivalAt = Character->GetActorLocation();
        // Look west over the river from behind the body, a little from above.
        Local->SetControlRotation(FRotator(-12.f, 180.f, 0.f));
        const FMarburgumCondition& Condition = State->GetCondition();
        MB2_SWIM_LOG("event=ready player=%d vitality=%d stamina=%d down=%d recovery_s=%d at=%.0f,%.0f,%.0f",
            State->GetPlayerId(), Condition.Vitality, Condition.Stamina, Condition.IsDown() ? 1 : 0, Condition.RecoverySeconds,
            ArrivalAt.X, ArrivalAt.Y, ArrivalAt.Z);
    }
    return true;
}

void UMarburgumSwimSmokeSubsystem::Drive()
{
    AMarburgumCharacter* Character = Body();
    if (!Character)
    {
        return;
    }
    if (bHolding)
    {
        Character->AddMovementInput(HoldDirection, 1.f);
    }
    const double Now = FPlatformTime::Seconds();
    const FVector Where = Character->GetActorLocation() / 100.f;
    Samples.Add({ Now, FVector2D(Where.X, Where.Y) });
    while (Samples.Num() > 2 && Now - Samples[0].Seconds > SampleHistorySeconds)
    {
        Samples.RemoveAt(0);
    }
}

double UMarburgumSwimSmokeSubsystem::SpeedOver(double Window) const
{
    if (Samples.Num() < 2)
    {
        return -1.0;
    }
    const FSample& Last = Samples.Last();
    for (const FSample& Sample : Samples)
    {
        if (Last.Seconds - Sample.Seconds <= Window)
        {
            const double Span = Last.Seconds - Sample.Seconds;
            return Span >= Window * 0.8 ? FVector2D::Distance(Last.Where, Sample.Where) / Span : -1.0;
        }
    }
    return -1.0;
}

void UMarburgumSwimSmokeSubsystem::Hold(const FVector& Direction)
{
    bHolding = true;
    HoldDirection = Direction;
    if (AMarburgumPlayerController* Local = Controller.Get())
    {
        Local->SetControlRotation(FRotator(-12.f, Direction.X < 0.f ? 180.f : 0.f, 0.f));
    }
}

void UMarburgumSwimSmokeSubsystem::Release()
{
    bHolding = false;
}

void UMarburgumSwimSmokeSubsystem::Expect(bool bCondition, const FString& What)
{
    if (!bCondition)
    {
        Problems.Add(What);
        MB2_SWIM_LOG("event=expectation_failed what=%s", *What);
    }
}

void UMarburgumSwimSmokeSubsystem::Shot(const TCHAR* Label)
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

FString UMarburgumSwimSmokeSubsystem::HudState() const
{
    const UMarburgumUISubsystem* UI = GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>();
    const UMarburgumPlayerCondition* Hud = UI ? UI->GetPlayerCondition() : nullptr;
    return Hud ? Hud->GetShownState() : TEXT("-");
}

void UMarburgumSwimSmokeSubsystem::ExpectHud(const TCHAR* Prefix, const TCHAR* What)
{
    // Only a rendered client builds the HUD.
    if (!ShotPrefix.IsEmpty())
    {
        Expect(HudState().StartsWith(Prefix), FString::Printf(TEXT("%s_hud=%s"), What, *HudState()));
    }
}

void UMarburgumSwimSmokeSubsystem::Advance(int32 Next, double Now)
{
    Step = Next;
    StepSeconds = Now;
}

void UMarburgumSwimSmokeSubsystem::Report(const TCHAR* Event, double Now)
{
    const AMarburgumPlayerState* State = LocalState();
    const UMarburgumCharacterMovement* Moving = Movement();
    const AMarburgumCharacter* Character = Body();
    const FMarburgumCondition Condition = State ? State->GetCondition() : FMarburgumCondition();
    const FVector At = Character ? Character->GetActorLocation() : FVector::ZeroVector;
    MB2_SWIM_LOG("event=%s step_s=%.2f mps=%.2f pos=%.0f,%.0f,%.0f predicted_water=%d predicted_under=%d predicted_stamina=%.1f "
                 "server_swimming=%d server_under=%d server_stamina=%d server_vitality=%d recovery_s=%d clip=%d hud=\"%s\"",
        Event, Now - StepSeconds, SpeedOver(1.0), At.X, At.Y, At.Z, Moving && Moving->IsInWaterBody() ? 1 : 0,
        Moving && Moving->IsSubmerged() ? 1 : 0, Moving ? Moving->GetStamina() : -1.f, Condition.bSwimming ? 1 : 0,
        Condition.bSubmerged ? 1 : 0, Condition.Stamina, Condition.Vitality, Condition.RecoverySeconds,
        Character && Character->IsSwimClipPlaying() ? 1 : 0, *HudState());
}

bool UMarburgumSwimSmokeSubsystem::Tick(float DeltaTime)
{
    if (bDone)
    {
        return true;
    }
    const double Now = FPlatformTime::Seconds();
    if (Now - StartSeconds > TimeoutSeconds)
    {
        Report(TEXT("timeout_state"), Now);
        Fail(FString::Printf(TEXT("timeout step=%d"), Step));
        return true;
    }
    if (Role == TEXT("O"))
    {
        UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
        if (UMarburgumSessionSubsystem::IsAdmitted(World))
        {
            TickO(Now);
        }
        return true;
    }
    if (!Bind())
    {
        return true;
    }
    Drive();
    if (!PendingShot.IsEmpty())
    {
        const bool bSaved = IFileManager::Get().FileSize(*PendingShot) > 0;
        if (!bSaved && Now - ShotRequestedSeconds < ShotTimeoutSeconds)
        {
            return true;
        }
        MB2_SWIM_LOG("event=shot file=%s saved=%d", *FPaths::GetCleanFilename(PendingShot), bSaved ? 1 : 0);
        Expect(bSaved, FString::Printf(TEXT("shot_not_saved=%s"), *FPaths::GetCleanFilename(PendingShot)));
        PendingShot.Reset();
    }
    if (Role == TEXT("W"))
    {
        TickW(Now);
    }
    else if (Role == TEXT("W2"))
    {
        TickW2(Now);
    }
    else
    {
        Fail(FString::Printf(TEXT("unknown_role=%s"), *Role));
    }
    return true;
}

void UMarburgumSwimSmokeSubsystem::TickW(double Now)
{
    UMarburgumCharacterMovement* Moving = Movement();
    const AMarburgumPlayerState* State = LocalState();
    AMarburgumCharacter* Character = Body();
    if (!Moving || !State || !Character)
    {
        return;
    }
    const FMarburgumCondition& Condition = State->GetCondition();
    switch (Step)
    {
    case 0:
        if (Now - StepSeconds < SettleSeconds) return;
        Expect(!Condition.IsDown() && Condition.Stamina == 100, TEXT("not_rested_on_arrival"));
        Hold(West);
        Advance(1, Now);
        return;
    case 1:
        // Down the bank into the Drava, walking.
        if (!Moving->IsInWaterBody())
        {
            if (Now - StepSeconds > 45.0) Fail(TEXT("never_reached_water"));
            return;
        }
        Report(TEXT("in_water"), Now);
        Advance(2, Now);
        return;
    case 2:
        // Swimming: swim speed, mouth up, the clip playing, and the server agrees.
        if (Now - StepSeconds < 5.0) return;
        Report(TEXT("swimming"), Now);
        Expect(SpeedOver(2.0) >= SwimMin && SpeedOver(2.0) <= SwimMax, FString::Printf(TEXT("swim_speed=%.2f"), SpeedOver(2.0)));
        Expect(Moving->IsInWaterBody() && !Moving->IsSubmerged(), TEXT("swimmer_not_at_surface"));
        Expect(Condition.bSwimming && !Condition.bSubmerged, TEXT("server_swimmer_not_at_surface"));
        Expect(Character->IsSwimClipPlaying(), TEXT("swim_clip_not_playing"));
        ExpectHud(TEXT("SWIMMING"), TEXT("swimming"));
        Shot(TEXT("swimming"));
        Advance(30, Now);
        return;
    case 30:
        // The stroke from the side, still swimming west.
        if (AMarburgumPlayerController* Local = Controller.Get())
        {
            Local->SetControlRotation(FRotator(-6.f, 90.f, 0.f));
        }
        if (Now - StepSeconds < 0.6) return;
        Shot(TEXT("swimming-side"));
        Advance(3, Now);
        return;
    case 3:
        // Let go: the body slips under.
        if (Now - StepSeconds < 0.5) return;
        Release();
        if (!Condition.bSubmerged)
        {
            if (Now - StepSeconds > 6.0) Fail(TEXT("never_went_under"));
            return;
        }
        MarkSeconds = Now;
        MarkStamina = Condition.Stamina;
        Report(TEXT("under"), Now);
        Expect(Character->IsSwimClipPlaying(), TEXT("swim_clip_stopped_under"));
        Advance(4, Now);
        return;
    case 4:
        // Holding breath: about a minute per bar.
        if (Now - MarkSeconds < UnderSeconds) return;
        Report(TEXT("held_breath"), Now);
        Expect(Condition.bSubmerged && Moving->IsSubmerged(), TEXT("surfaced_without_swimming"));
        Expect(SpeedOver(2.0) < 0.15, FString::Printf(TEXT("drifting_under=%.2f"), SpeedOver(2.0)));
        Expect(MarkStamina - Condition.Stamina >= UnderCostMin && MarkStamina - Condition.Stamina <= UnderCostMax,
            FString::Printf(TEXT("breath_cost=%d"), MarkStamina - Condition.Stamina));
        ExpectHud(TEXT("HOLDING BREATH"), TEXT("under"));
        Shot(TEXT("under"));
        Hold(East);
        Advance(5, Now);
        return;
    case 5:
        // Swimming again brings the mouth back up.
        if (Condition.bSubmerged)
        {
            if (Now - StepSeconds > 3.0) Fail(TEXT("never_surfaced"));
            return;
        }
        Report(TEXT("surfaced"), Now);
        Advance(6, Now);
        return;
    case 6:
        // Back east to the Lent bank and out of the water.
        if (Moving->IsInWaterBody() || !Moving->IsMovingOnGround())
        {
            if (Now - StepSeconds > 60.0) Fail(TEXT("never_climbed_out"));
            return;
        }
        Report(TEXT("ashore"), Now);
        Expect(!Condition.IsDown(), TEXT("down_ashore"));
        Shot(TEXT("ashore"));
        Hold(West);
        Advance(7, Now);
        return;
    case 7:
        if (!Moving->IsInWaterBody())
        {
            if (Now - StepSeconds > 30.0) Fail(TEXT("never_back_in_water"));
            return;
        }
        // A few strokes out, then let go and stay under.
        if (Now - StepSeconds < 3.0) return;
        Release();
        Report(TEXT("letting_go"), Now);
        MarkSeconds = Now;
        MarkStamina = Condition.Stamina;
        Advance(8, Now);
        return;
    case 8:
        // Under with none left is drowning: all vitality, taken to the recovery point.
        if (!Condition.IsDown())
        {
            if (Now - StepSeconds > BreathHoldSeconds + 20.0) Fail(TEXT("never_drowned"));
            return;
        }
        Report(TEXT("drowned"), Now);
        Expect(Now - MarkSeconds >= BreathHoldSeconds * MarkStamina / 100.0 - 3.0,
            FString::Printf(TEXT("drowned_too_soon=%.1f"), Now - MarkSeconds));
        Expect(Condition.RecoverySeconds >= 55 && Condition.RecoverySeconds <= RecoverySeconds,
            FString::Printf(TEXT("recovery_s=%d"), Condition.RecoverySeconds));
        Advance(9, Now);
        return;
    case 9:
        if (FVector::Dist2D(Character->GetActorLocation(), ArrivalAt) > RecoveryPointCm || Character->GetCharacterMovement()->IsFalling())
        {
            if (Now - StepSeconds > 5.0) Fail(TEXT("not_taken_to_recovery_point"));
            return;
        }
        Report(TEXT("at_recovery_point"), Now);
        Expect(Moving->IsDown(), TEXT("client_not_down"));
        Hold(West);
        Advance(10, Now);
        return;
    case 10:
        // A body with no vitality does not move, however the player pushes.
        if (Now - StepSeconds < 3.0) return;
        Report(TEXT("down_pushing"), Now);
        Expect(SpeedOver(2.0) < 0.1, FString::Printf(TEXT("moved_while_down=%.2f"), SpeedOver(2.0)));
        ExpectHud(TEXT("DROWNED"), TEXT("down"));
        Shot(TEXT("down"));
        Release();
        Advance(11, Now);
        return;
    case 11:
        // Leave while still down: the wait is saved with the body.
        if (Now - StepSeconds < DownBeforeLeavingSeconds) return;
        Report(TEXT("leaving_down"), Now);
        Expect(Condition.IsDown(), TEXT("recovered_too_soon"));
        Pass();
        return;
    default:
        return;
    }
}

void UMarburgumSwimSmokeSubsystem::TickW2(double Now)
{
    UMarburgumCharacterMovement* Moving = Movement();
    const AMarburgumPlayerState* State = LocalState();
    AMarburgumCharacter* Character = Body();
    if (!Moving || !State || !Character)
    {
        return;
    }
    const FMarburgumCondition& Condition = State->GetCondition();
    switch (Step)
    {
    case 0:
        if (Now - StepSeconds < SettleSeconds) return;
        Report(TEXT("restored"), Now);
        Expect(Condition.IsDown(), TEXT("restored_alive"));
        Expect(Condition.RecoverySeconds >= 40 && Condition.RecoverySeconds < 58, FString::Printf(TEXT("restored_recovery_s=%d"), Condition.RecoverySeconds));
        Hold(West);
        Advance(1, Now);
        return;
    case 1:
        if (Now - StepSeconds < 3.0) return;
        Report(TEXT("restored_pushing"), Now);
        Expect(SpeedOver(2.0) < 0.1, FString::Printf(TEXT("moved_while_restored_down=%.2f"), SpeedOver(2.0)));
        Release();
        Advance(2, Now);
        return;
    case 2:
        if (Condition.IsDown() || Moving->IsDown())
        {
            if (Now - StepSeconds > RecoverySeconds + 10.0) Fail(TEXT("never_recovered"));
            return;
        }
        Report(TEXT("recovered"), Now);
        Expect(Condition.Vitality == Condition.MaxVitality && Condition.Stamina == 100, TEXT("not_recovered_in_full"));
        // West, down the level promenade toward the river, as W walked it.
        Hold(West);
        Advance(3, Now);
        return;
    case 3:
        if (Now - StepSeconds < 3.0) return;
        Report(TEXT("walking_again"), Now);
        Expect(SpeedOver(2.0) > 1.8 && SpeedOver(2.0) < 2.6, FString::Printf(TEXT("walk_after_recovery=%.2f"), SpeedOver(2.0)));
        Expect(ShotPrefix.IsEmpty() || HudState().IsEmpty(), FString::Printf(TEXT("recovered_hud=%s"), *HudState()));
        Shot(TEXT("recovered"));
        Release();
        Advance(4, Now);
        return;
    case 4:
        Pass();
        return;
    default:
        return;
    }
}

void UMarburgumSwimSmokeSubsystem::TickO(double Now)
{
    UWorld* World = GetGameInstance()->GetWorld();
    const APawn* Own = World->GetFirstPlayerController() ? World->GetFirstPlayerController()->GetPawn() : nullptr;
    for (TActorIterator<AMarburgumCharacter> It(World); It; ++It)
    {
        if (*It == Own || !It->GetCharacterMovement()->IsSwimming())
        {
            continue;
        }
        if (Step == 0)
        {
            Step = 1;
            StepSeconds = Now;
            MB2_SWIM_LOG("event=remote_swimming pos=%.0f,%.0f,%.0f", It->GetActorLocation().X, It->GetActorLocation().Y, It->GetActorLocation().Z);
        }
        if (It->IsSwimClipPlaying())
        {
            MB2_SWIM_LOG("event=remote_swim_clip after_s=%.2f", Now - StepSeconds);
            Pass();
            return;
        }
        if (Now - StepSeconds > 3.0)
        {
            Fail(TEXT("remote_swimmer_without_clip"));
        }
        return;
    }
}

void UMarburgumSwimSmokeSubsystem::Pass()
{
    if (Problems.IsEmpty())
    {
        MB2_SWIM_LOG("event=pass");
        bDone = true;
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }
    else
    {
        Fail(TEXT("expectations"));
    }
}

void UMarburgumSwimSmokeSubsystem::Fail(const FString& Reason)
{
    if (bDone)
    {
        return;
    }
    Problems.Add(Reason);
    MB2_SWIM_LOG("event=fail reason=%s", *FString::Join(Problems, TEXT(",")));
    bDone = true;
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

#undef MB2_SWIM_LOG
