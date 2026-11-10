#include "MarburgumConditionSmokeSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "MarburgumCharacterMovement.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlayerController.h"
#include "MarburgumPlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "UI/MarburgumCharacterPanel.h"
#include "UI/MarburgumPlayerCondition.h"
#include "UI/MarburgumUISubsystem.h"
#include "UnrealClient.h"

#define MB2_COND_LOG(Format, ...) \
    UE_LOG(LogMarburgumNet, Log, TEXT("MB2COND role=client tag=%s " Format), *Role, ##__VA_ARGS__)

namespace MarburgumConditionSmoke
{
    constexpr double SettleSeconds = 1.0;
    // First frames after a map load are not yet representative (exposure, streaming, first layout).
    constexpr double FirstShotSettleSeconds = 2.0;
    constexpr double PanelSettleSeconds = 0.8;
    constexpr double ShotTimeoutSeconds = 10.0;
    constexpr double SampleHistorySeconds = 5.0;
    // Speeds in metres per second: a run is 4.4 and a walk 2.2. The bands allow for acceleration at the
    // edges of a window, frame timing and, for the adversary, server corrections arriving every 0.1 s.
    constexpr double RunMin = 3.6;
    constexpr double RunMax = 4.9;
    constexpr double WalkMax = 2.75;
    constexpr double AdversaryWalkMax = 2.9;
    // Ten seconds of breath from a full bar; generous bounds for frame and move timing.
    constexpr double WindedAfterMin = 9.0;
    constexpr double WindedAfterMax = 11.5;
    constexpr double AdversaryWindedAfterMax = 12.0;
    constexpr double ServerAnswerSeconds = 2.0;
    // East along the Lent promenade (+X); open ground for well over a hundred metres from the arrival starts.
    const FVector RunDirection(1.f, 0.f, 0.f);
}

using namespace MarburgumConditionSmoke;

bool UMarburgumConditionSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
    return GameInstance && !GameInstance->IsDedicatedServerInstance()
        && FParse::Param(FCommandLine::Get(), TEXT("MB2ConditionSmoke"));
}

void UMarburgumConditionSmokeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const TCHAR* CommandLine = FCommandLine::Get();
    FParse::Value(CommandLine, TEXT("MB2ConditionRole="), Role);
    FParse::Value(CommandLine, TEXT("MB2ConditionShot="), ShotPrefix);
    FParse::Value(CommandLine, TEXT("MB2ConditionSignal="), SignalFile);
    FParse::Value(CommandLine, TEXT("MB2ConditionTimeout="), TimeoutSeconds);
    Role = Role.ToUpper();
    StartSeconds = FPlatformTime::Seconds();
    // Every frame: running feeds movement input, which the pawn consumes once per frame.
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UMarburgumConditionSmokeSubsystem::Tick), 0.f);
    MB2_COND_LOG("event=start shot=%d", ShotPrefix.IsEmpty() ? 0 : 1);
}

void UMarburgumConditionSmokeSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    Super::Deinitialize();
}

const AMarburgumPlayerState* UMarburgumConditionSmokeSubsystem::LocalState() const
{
    const AMarburgumPlayerController* Local = Controller.Get();
    return Local ? Local->GetPlayerState<AMarburgumPlayerState>() : nullptr;
}

UMarburgumCharacterMovement* UMarburgumConditionSmokeSubsystem::Movement() const
{
    const AMarburgumPlayerController* Local = Controller.Get();
    const ACharacter* Body = Local ? Cast<ACharacter>(Local->GetPawn()) : nullptr;
    return Body ? Cast<UMarburgumCharacterMovement>(Body->GetCharacterMovement()) : nullptr;
}

bool UMarburgumConditionSmokeSubsystem::ServerWinded() const
{
    const AMarburgumPlayerState* State = LocalState();
    return State && State->IsConditionKnown() && State->GetCondition().bWinded;
}

int32 UMarburgumConditionSmokeSubsystem::ServerStamina() const
{
    const AMarburgumPlayerState* State = LocalState();
    return State && State->IsConditionKnown() ? State->GetCondition().Stamina : -1;
}

bool UMarburgumConditionSmokeSubsystem::Bind()
{
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (!UMarburgumSessionSubsystem::IsAdmitted(World))
    {
        return false;
    }
    AMarburgumPlayerController* Local = Cast<AMarburgumPlayerController>(World->GetFirstPlayerController());
    const AMarburgumPlayerState* State = Local ? Local->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    if (!State || !State->IsConditionKnown() || !State->IsLocationKnown() || !Local->GetPawn())
    {
        return false;
    }
    const ACharacter* Body = Cast<ACharacter>(Local->GetPawn());
    UMarburgumCharacterMovement* Moving = Body ? Cast<UMarburgumCharacterMovement>(Body->GetCharacterMovement()) : nullptr;
    if (!Moving || !Moving->IsConditionSynced())
    {
        return false;
    }
    if (Controller.Get() != Local)
    {
        Controller = Local;
        bWasAdmitted = true;
        StepSeconds = FPlatformTime::Seconds(); // Settle timers start at admission.
        ArrivalStamina = State->GetCondition().Stamina;
        bArrivedWinded = State->GetCondition().bWinded;
        if (Role == TEXT("C"))
        {
            Moving->SetIgnoreStaminaForTest(true);
        }
        MB2_COND_LOG("event=ready player=%d character=%d vitality=%d stamina=%d winded=%d predicted_stamina=%.1f predicted_winded=%d cheat=%d",
            State->GetPlayerId(), State->HasCharacter() ? 1 : 0, State->GetCondition().Vitality, ArrivalStamina,
            bArrivedWinded ? 1 : 0, Moving->GetStamina(), Moving->IsWinded() ? 1 : 0, Moving->IsIgnoringStaminaForTest() ? 1 : 0);
    }
    return true;
}

void UMarburgumConditionSmokeSubsystem::Drive()
{
    const AMarburgumPlayerController* Local = Controller.Get();
    APawn* Body = Local ? Local->GetPawn() : nullptr;
    if (!Body)
    {
        return;
    }
    if (bHolding)
    {
        Body->AddMovementInput(RunDirection, 1.f);
    }
    if (UMarburgumCharacterMovement* Moving = Movement())
    {
        Moving->SetWantsToRun(bHolding && bRunRequested);
    }
    const double Now = FPlatformTime::Seconds();
    const FVector Where = Body->GetActorLocation() / 100.f;
    Samples.Add({ Now, FVector2D(Where.X, Where.Y) });
    while (Samples.Num() > 2 && Now - Samples[0].Seconds > SampleHistorySeconds)
    {
        Samples.RemoveAt(0);
    }
}

double UMarburgumConditionSmokeSubsystem::SpeedOver(double Window) const
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
            // A window the samples do not cover is no measurement.
            return Span >= Window * 0.8 ? FVector2D::Distance(Last.Where, Sample.Where) / Span : -1.0;
        }
    }
    return -1.0;
}

void UMarburgumConditionSmokeSubsystem::Hold(bool bRun)
{
    bHolding = true;
    bRunRequested = bRun;
}

void UMarburgumConditionSmokeSubsystem::Release()
{
    bHolding = false;
    bRunRequested = false;
}

void UMarburgumConditionSmokeSubsystem::Expect(bool bCondition, const FString& What)
{
    if (!bCondition)
    {
        Problems.Add(What);
        MB2_COND_LOG("event=expectation_failed what=%s", *What);
    }
}

void UMarburgumConditionSmokeSubsystem::ExpectSpeed(double Window, double Min, double Max, const TCHAR* What)
{
    const double Speed = SpeedOver(Window);
    const UMarburgumCharacterMovement* Moving = Movement();
    MB2_COND_LOG("event=speed what=%s window_s=%.2f mps=%.2f min=%.2f max=%.2f running=%d predicted_stamina=%.1f server_stamina=%d server_winded=%d",
        What, Window, Speed, Min, Max, Moving && Moving->IsRunning() ? 1 : 0, Moving ? Moving->GetStamina() : -1.f,
        ServerStamina(), ServerWinded() ? 1 : 0);
    Expect(Speed >= Min && Speed <= Max, FString::Printf(TEXT("%s_speed=%.2f"), What, Speed));
}

void UMarburgumConditionSmokeSubsystem::Shot(const TCHAR* Label)
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

void UMarburgumConditionSmokeSubsystem::Advance(int32 Next, double Now)
{
    Step = Next;
    StepSeconds = Now;
}

bool UMarburgumConditionSmokeSubsystem::Tick(float DeltaTime)
{
    if (bDone)
    {
        return true;
    }
    const double Now = FPlatformTime::Seconds();
    if (Now - StartSeconds > TimeoutSeconds)
    {
        Fail(FString::Printf(TEXT("timeout step=%d server_stamina=%d server_winded=%d"), Step, ServerStamina(), ServerWinded() ? 1 : 0));
        return true;
    }
    // Input keeps flowing while a screenshot is pending, so a run is never interrupted by the evidence.
    Drive();
    if (!PendingShot.IsEmpty())
    {
        const bool bSaved = IFileManager::Get().FileSize(*PendingShot) > 0;
        if (!bSaved && Now - ShotRequestedSeconds < ShotTimeoutSeconds)
        {
            return true;
        }
        MB2_COND_LOG("event=shot file=%s saved=%d", *FPaths::GetCleanFilename(PendingShot), bSaved ? 1 : 0);
        Expect(bSaved, FString::Printf(TEXT("shot_not_saved=%s"), *FPaths::GetCleanFilename(PendingShot)));
        PendingShot.Reset();
    }
    if (!Bind())
    {
        // S stays connected until the script stops the server; losing the session is its success.
        if (Role == TEXT("S") && bWasAdmitted && Step >= 2)
        {
            const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
            if (!UMarburgumSessionSubsystem::IsAdmitted(World))
            {
                MB2_COND_LOG("event=session_lost");
                Pass();
            }
        }
        return true;
    }
    if (Role == TEXT("R")) TickR(Now);
    else if (Role == TEXT("C")) TickC(Now);
    else if (Role == TEXT("R2")) TickR2(Now);
    else if (Role == TEXT("S")) TickS(Now);
    else Fail(TEXT("unknown_role"));
    return true;
}

void UMarburgumConditionSmokeSubsystem::TickR(double Now)
{
    UMarburgumCharacterMovement* Moving = Movement();
    const AMarburgumPlayerState* State = LocalState();
    const UMarburgumUISubsystem* UI = GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>();
    const UMarburgumPlayerCondition* Hud = UI ? UI->GetPlayerCondition() : nullptr;
    const bool bRendered = !ShotPrefix.IsEmpty();
    switch (Step)
    {
    case 0:
        if (Now - StepSeconds < FirstShotSettleSeconds) return;
        // A new character arrives rested: full bars on the server, in the prediction and on screen.
        Expect(State->GetCondition().Vitality == 100 && ServerStamina() == 100 && !ServerWinded(),
            FString::Printf(TEXT("arrival vitality=%d stamina=%d winded=%d"), State->GetCondition().Vitality, ServerStamina(), ServerWinded() ? 1 : 0));
        Expect(Moving->GetStamina() == MarburgumII::Gameplay::DefaultStamina && !Moving->IsWinded(),
            FString::Printf(TEXT("arrival_predicted=%.1f"), Moving->GetStamina()));
        if (bRendered)
        {
            Expect(Hud && Hud->GetShownStamina() == 100 && Hud->GetShownVitality() == 100 && !Hud->IsShowingWinded(),
                FString::Printf(TEXT("arrival_hud stamina=%d"), Hud ? Hud->GetShownStamina() : -2));
        }
        Shot(TEXT("rested"));
        if (UMarburgumUISubsystem* MutableUI = GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>())
        {
            MutableUI->ToggleCharacter();
        }
        Advance(1, Now);
        return;
    case 1:
        if (Now - StepSeconds < PanelSettleSeconds) return;
        if (bRendered)
        {
            const UMarburgumCharacterPanel* Panel = UI ? UI->GetCharacterPanel() : nullptr;
            const FString Shown = Panel ? Panel->GetShownStaminaText().ToString() : FString();
            Expect(UI && UI->IsCharacterOpen() && Shown == TEXT("100 / 100"), FString::Printf(TEXT("character_panel stamina=\"%s\""), *Shown));
        }
        Shot(TEXT("character"));
        Advance(2, Now);
        return;
    case 2:
        if (UMarburgumUISubsystem* MutableUI = GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>(); MutableUI && MutableUI->IsCharacterOpen())
        {
            MutableUI->ToggleCharacter();
        }
        Hold(true);
        RunStartSeconds = Now;
        MB2_COND_LOG("event=run_start");
        Advance(3, Now);
        return;
    case 3:
        if (Now - RunStartSeconds < 3.0) return;
        ExpectSpeed(2.0, RunMin, RunMax, TEXT("running"));
        Expect(Moving->IsRunning(), TEXT("not_running_after_3s"));
        Expect(Moving->GetStamina() >= 62.f && Moving->GetStamina() <= 76.f, FString::Printf(TEXT("stamina_after_3s=%.1f"), Moving->GetStamina()));
        if (bRendered)
        {
            Expect(Hud && Hud->IsShowingRunning(), TEXT("hud_not_running"));
        }
        Shot(TEXT("running"));
        Advance(4, Now);
        return;
    case 4:
        if (!Moving->IsWinded())
        {
            if (Now - RunStartSeconds > WindedAfterMax + 2.0) Fail(TEXT("never_winded"));
            return;
        }
        MB2_COND_LOG("event=winded_predicted after_s=%.2f", Now - RunStartSeconds);
        Expect(Now - RunStartSeconds >= WindedAfterMin && Now - RunStartSeconds <= WindedAfterMax,
            FString::Printf(TEXT("winded_after_s=%.2f"), Now - RunStartSeconds));
        MarkSeconds = Now;
        Advance(5, Now);
        return;
    case 5:
        // Still asking to run: the body walks until breath is back (1.36 s from empty).
        if (Now - MarkSeconds < 1.0) return;
        Expect(ServerWinded(), TEXT("server_not_winded_after_1s"));
        ExpectSpeed(0.7, 1.2, WalkMax, TEXT("winded_walk"));
        Expect(!Moving->IsRunning(), TEXT("running_while_winded"));
        if (bRendered)
        {
            Expect(Hud && Hud->IsShowingWinded(), TEXT("hud_not_winded"));
        }
        Shot(TEXT("winded"));
        Advance(6, Now);
        return;
    case 6:
        if (Moving->IsWinded())
        {
            if (Now - StepSeconds > 3.0) Fail(TEXT("breath_never_back"));
            return;
        }
        MB2_COND_LOG("event=breath_back_predicted after_winded_s=%.2f stamina=%.1f", Now - MarkSeconds, Moving->GetStamina());
        MarkSeconds = Now;
        Advance(7, Now);
        return;
    case 7:
        if (Now - MarkSeconds < 1.0) return;
        ExpectSpeed(0.6, RunMin, RunMax, TEXT("running_again"));
        Advance(8, Now);
        return;
    case 8:
        // Leave the moment the server has the body winded again, so the save holds almost no breath.
        if (!Moving->IsWinded() || !ServerWinded())
        {
            if (Now - StepSeconds > 5.0) Fail(FString::Printf(TEXT("not_winded_again predicted=%d server=%d"), Moving->IsWinded() ? 1 : 0, ServerWinded() ? 1 : 0));
            return;
        }
        Release();
        MB2_COND_LOG("event=leaving predicted_stamina=%.1f server_stamina=%d corrections=%d", Moving->GetStamina(), ServerStamina(),
            Moving->GetConditionCorrections());
        // An honest client predicts its breath as the server simulates it.
        Expect(Moving->GetConditionCorrections() <= 2, FString::Printf(TEXT("honest_corrections=%d"), Moving->GetConditionCorrections()));
        Pass();
        return;
    default:
        return;
    }
}

void UMarburgumConditionSmokeSubsystem::TickC(double Now)
{
    UMarburgumCharacterMovement* Moving = Movement();
    switch (Step)
    {
    case 0:
        if (Now - StepSeconds < SettleSeconds) return;
        Expect(Moving->IsIgnoringStaminaForTest(), TEXT("cheat_not_enabled"));
        Hold(true);
        RunStartSeconds = Now;
        MB2_COND_LOG("event=run_start cheat=1");
        Advance(1, Now);
        return;
    case 1:
        // The client claims a full bar throughout; the server's own simulation decides.
        if (!ServerWinded())
        {
            if (Now - RunStartSeconds > AdversaryWindedAfterMax + 2.0) Fail(TEXT("server_never_winded_the_cheater"));
            return;
        }
        MB2_COND_LOG("event=server_winded after_s=%.2f predicted_stamina=%.1f corrections=%d", Now - RunStartSeconds,
            Moving->GetStamina(), Moving->GetConditionCorrections());
        Expect(Now - RunStartSeconds >= WindedAfterMin && Now - RunStartSeconds <= AdversaryWindedAfterMax,
            FString::Printf(TEXT("server_winded_after_s=%.2f"), Now - RunStartSeconds));
        Expect(Moving->GetStamina() == MarburgumII::Gameplay::DefaultStamina, TEXT("cheat_did_not_keep_full_bar"));
        MarkSeconds = Now;
        Advance(2, Now);
        return;
    case 2:
        if (Now - MarkSeconds < 1.0) return;
        // Still predicting a run, but the server walks it and corrects every move it gets wrong.
        ExpectSpeed(0.7, 0.5, AdversaryWalkMax, TEXT("cheater_held_to_walking"));
        Expect(Moving->GetConditionCorrections() >= 1, TEXT("cheater_never_corrected"));
        Advance(3, Now);
        return;
    case 3:
        if (Now - StepSeconds < 2.0) return;
        Release();
        MB2_COND_LOG("event=leaving server_stamina=%d server_winded=%d corrections=%d", ServerStamina(), ServerWinded() ? 1 : 0,
            Moving->GetConditionCorrections());
        Pass();
        return;
    default:
        return;
    }
}

void UMarburgumConditionSmokeSubsystem::TickR2(double Now)
{
    UMarburgumCharacterMovement* Moving = Movement();
    const UMarburgumUISubsystem* UI = GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>();
    const UMarburgumPlayerCondition* Hud = UI ? UI->GetPlayerCondition() : nullptr;
    switch (Step)
    {
    case 0:
        // No settling: the saved winded state only lasts until breath is back. The server starts
        // recovering at login, before this client is bound, so how much winded time is left here is the
        // server's business; the script checks from the server log that it never ran the body while
        // winded. This client checks that it arrived winded and never predicts a run while winded.
        Expect(bArrivedWinded && ArrivalStamina <= 5, FString::Printf(TEXT("restored stamina=%d winded=%d"), ArrivalStamina, bArrivedWinded ? 1 : 0));
        Expect(Moving->IsWinded() && Moving->GetStamina() < MarburgumII::Gameplay::RunResumeStamina,
            FString::Printf(TEXT("restored_predicted=%.1f winded=%d"), Moving->GetStamina(), Moving->IsWinded() ? 1 : 0));
        Hold(true);
        Advance(1, Now);
        return;
    case 1:
        // One frame later the HUD has drawn the restored state.
        if (!ShotPrefix.IsEmpty() && Moving->IsWinded())
        {
            Expect(Hud && Hud->IsShowingWinded(), TEXT("restored_hud_not_winded"));
        }
        Advance(2, Now);
        return;
    case 2:
        if (Moving->IsWinded())
        {
            Expect(!Moving->IsRunning(), TEXT("restored_running_while_winded"));
            if (Now - StepSeconds > 3.0) Fail(TEXT("restored_breath_never_back"));
            return;
        }
        MB2_COND_LOG("event=breath_back_predicted after_bind_s=%.2f stamina=%.1f", Now - StepSeconds, Moving->GetStamina());
        MarkSeconds = Now;
        Advance(3, Now);
        return;
    case 3:
        if (Now - MarkSeconds < 1.0) return;
        ExpectSpeed(0.6, RunMin, RunMax, TEXT("restored_running_again"));
        Release();
        Advance(4, Now);
        return;
    case 4:
        // Rest: a full bar is saved as no entry at all.
        if (ServerStamina() < 100 || ServerWinded())
        {
            if (Now - StepSeconds > 15.0) Fail(FString::Printf(TEXT("never_rested stamina=%d"), ServerStamina()));
            return;
        }
        if (!ShotPrefix.IsEmpty())
        {
            Expect(Hud && Hud->GetShownStamina() == 100 && !Hud->IsShowingWinded(), TEXT("rested_hud"));
        }
        Shot(TEXT("rested-again"));
        Advance(5, Now);
        return;
    case 5:
        MB2_COND_LOG("event=leaving server_stamina=%d corrections=%d", ServerStamina(), Moving->GetConditionCorrections());
        Expect(Moving->GetConditionCorrections() <= 2, FString::Printf(TEXT("honest_corrections=%d"), Moving->GetConditionCorrections()));
        Pass();
        return;
    default:
        return;
    }
}

void UMarburgumConditionSmokeSubsystem::TickS(double Now)
{
    switch (Step)
    {
    case 0:
        if (Now - StepSeconds < SettleSeconds) return;
        Hold(true);
        Advance(1, Now);
        return;
    case 1:
        if (Now - StepSeconds < 2.0) return;
        Expect(ServerStamina() < 100, FString::Printf(TEXT("running_did_not_cost server_stamina=%d"), ServerStamina()));
        if (!SignalFile.IsEmpty())
        {
            FFileHelper::SaveStringToFile(TEXT("running"), *SignalFile);
        }
        MB2_COND_LOG("event=signalled server_stamina=%d", ServerStamina());
        Advance(2, Now);
        return;
    default:
        // Keep running until the script stops the server (see Tick).
        return;
    }
}

void UMarburgumConditionSmokeSubsystem::Pass()
{
    if (Problems.IsEmpty())
    {
        MB2_COND_LOG("event=pass");
        bDone = true;
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }
    else
    {
        Fail(TEXT("expectations"));
    }
}

void UMarburgumConditionSmokeSubsystem::Fail(const FString& Reason)
{
    if (bDone)
    {
        return;
    }
    Problems.Add(Reason);
    MB2_COND_LOG("event=fail reason=%s", *FString::Join(Problems, TEXT(",")));
    bDone = true;
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

#undef MB2_COND_LOG
