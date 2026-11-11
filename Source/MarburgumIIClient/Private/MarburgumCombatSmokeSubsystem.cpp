#include "MarburgumCombatSmokeSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "MarburgumCharacter.h"
#include "MarburgumCharacterMovement.h"
#include "MarburgumCombatContract.h"
#include "MarburgumFighter.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlayerController.h"
#include "MarburgumPlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "UnrealClient.h"

#define MB2_COMBAT_SMOKE_LOG(Format, ...) \
    UE_LOG(LogMarburgumNet, Log, TEXT("MB2CSMOKE role=client tag=%s " Format), *Role, ##__VA_ARGS__)

namespace MarburgumCombatSmoke
{
    const FName DrinkerId(TEXT("lent_drunk"));
    const FName WalkerId(TEXT("lent_passerby"));
    constexpr double SettleSeconds = 2.0;
    constexpr double AnswerTimeoutSeconds = 5.0;
    // Close enough for a fist (reach 200 cm) with room to spare, and facing him within this.
    constexpr double StrikeDistanceCm = 140.0;
    constexpr double FacingToleranceDegrees = 25.0;
    // His jab is 6 every 1.1 s; a guarded one is a quarter of that, rounded up to whole points.
    constexpr int32 GuardedLossMin = 1;
    constexpr int32 GuardedLossMax = 2;
    constexpr int32 UnguardedLossMin = 5;
    constexpr int32 UnguardedLossMax = 7;
    constexpr double BlowTimeoutSeconds = 6.0;
    // A raised or lowered guard reaches the server with the next moves; blows before then are not counted.
    constexpr double GuardSettleSeconds = 0.3;
    // A blow reported this soon after the guard changed may have been thrown before it did.
    constexpr double InFlightSeconds = 0.8;
    // Between our own swings in the last round: past the jab's cooldown, with frame slack.
    constexpr double SwingSpacingSeconds = 0.45;
}

using namespace MarburgumCombatSmoke;
namespace Combat = MarburgumII::Combat;

bool UMarburgumCombatSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
    return GameInstance && !GameInstance->IsDedicatedServerInstance() && FParse::Param(FCommandLine::Get(), TEXT("MB2CombatSmoke"));
}

void UMarburgumCombatSmokeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const TCHAR* CommandLine = FCommandLine::Get();
    FParse::Value(CommandLine, TEXT("MB2CombatRole="), Role);
    FParse::Value(CommandLine, TEXT("MB2CombatShot="), ShotPrefix);
    FParse::Value(CommandLine, TEXT("MB2CombatTimeout="), TimeoutSeconds);
    Role = Role.ToUpper();
    StartSeconds = FPlatformTime::Seconds();
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMarburgumCombatSmokeSubsystem::Tick), 0.f);
    MB2_COMBAT_SMOKE_LOG("event=start shot=%d", ShotPrefix.IsEmpty() ? 0 : 1);
}

void UMarburgumCombatSmokeSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    Super::Deinitialize();
}

AMarburgumCharacter* UMarburgumCombatSmokeSubsystem::Body() const
{
    const AMarburgumPlayerController* Local = Controller.Get();
    return Local ? Cast<AMarburgumCharacter>(Local->GetPawn()) : nullptr;
}

UMarburgumCharacterMovement* UMarburgumCombatSmokeSubsystem::Movement() const
{
    const AMarburgumCharacter* Character = Body();
    return Character ? Cast<UMarburgumCharacterMovement>(Character->GetCharacterMovement()) : nullptr;
}

int32 UMarburgumCombatSmokeSubsystem::Vitality() const
{
    const AMarburgumPlayerController* Local = Controller.Get();
    const AMarburgumPlayerState* State = Local ? Local->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    return State ? State->GetCondition().Vitality : -1;
}

int32 UMarburgumCombatSmokeSubsystem::Stamina() const
{
    const AMarburgumPlayerController* Local = Controller.Get();
    const AMarburgumPlayerState* State = Local ? Local->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    return State ? State->GetCondition().Stamina : -1;
}

bool UMarburgumCombatSmokeSubsystem::Bind()
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
    if (!Drinker.IsValid() || !Walker.IsValid())
    {
        for (TActorIterator<AMarburgumFighter> It(World); It; ++It)
        {
            if (It->GetNpcId() == DrinkerId)
            {
                Drinker = *It;
            }
            else if (It->GetNpcId() == WalkerId)
            {
                Walker = *It;
            }
        }
        if (!Drinker.IsValid() || !Walker.IsValid())
        {
            return false;
        }
    }
    if (Controller.Get() != Local)
    {
        Controller = Local;
        Local->OnCombatResult.AddUObject(this, &UMarburgumCombatSmokeSubsystem::OnCombatResult);
        StepSeconds = FPlatformTime::Seconds();
        // Look at the drinker over the body's shoulder.
        const FVector Toward = Drinker->GetActorLocation() - Character->GetActorLocation();
        Local->SetControlRotation(FRotator(-12.f, Toward.Rotation().Yaw, 0.f));
        const FVector At = Character->GetActorLocation();
        MB2_COMBAT_SMOKE_LOG("event=ready player=%d at=%.0f,%.0f,%.0f vitality=%d stamina=%d drinker_cm=%.0f drinker_state=%s walker_cm=%.0f",
            State->GetPlayerId(), At.X, At.Y, At.Z, Vitality(), Stamina(), FVector::Dist2D(At, Drinker->GetActorLocation()),
            AMarburgumFighter::StateName(Drinker->GetFighterState()), FVector::Dist2D(At, Walker->GetActorLocation()));
    }
    return true;
}

void UMarburgumCombatSmokeSubsystem::OnCombatResult(const FMarburgumCombatResult& Result)
{
    Answers.Add(Result);
    MB2_COMBAT_SMOKE_LOG("event=answer heavy=%d ok=%d reason=%s outcome=%s target=%s damage=%.1f target_vitality=%d", Result.bHeavy ? 1 : 0,
        Result.bOk ? 1 : 0, Combat::WireWord(Result.Reason), Combat::WireWord(Result.Outcome),
        Result.TargetId.IsNone() ? TEXT("-") : *Result.TargetId.ToString(), Result.Damage, Result.TargetVitality);
}

const FMarburgumCombatResult* UMarburgumCombatSmokeSubsystem::AnswerSince(int32 Since) const
{
    return Answers.IsValidIndex(Since) ? &Answers[Since] : nullptr;
}

void UMarburgumCombatSmokeSubsystem::Expect(bool bCondition, const FString& What)
{
    if (!bCondition)
    {
        Problems.Add(What);
        MB2_COMBAT_SMOKE_LOG("event=expectation_failed what=%s", *What);
    }
}

void UMarburgumCombatSmokeSubsystem::Shot(const TCHAR* Label)
{
    if (ShotPrefix.IsEmpty())
    {
        return;
    }
    PendingShot = FString::Printf(TEXT("%s-%s.png"), *ShotPrefix, Label);
    IFileManager::Get().Delete(*PendingShot, false, true, true);
    FScreenshotRequest::RequestScreenshot(PendingShot, true, false);
}

void UMarburgumCombatSmokeSubsystem::Advance(int32 Next, double Now)
{
    Step = Next;
    StepSeconds = Now;
}

void UMarburgumCombatSmokeSubsystem::Report(const TCHAR* Event, double Now)
{
    const AMarburgumCharacter* Character = Body();
    const UMarburgumCharacterMovement* Moving = Movement();
    const AMarburgumFighter* Him = Drinker.Get();
    const AMarburgumFighter* Other = Walker.Get();
    const FVector At = Character ? Character->GetActorLocation() : FVector::ZeroVector;
    MB2_COMBAT_SMOKE_LOG("event=%s step=%d step_s=%.2f pos=%.0f,%.0f,%.0f vitality=%d stamina=%d guarding=%d down=%d clip=%s "
                         "drinker_cm=%.0f drinker_state=%s drinker_intent=%s drinker_vitality=%d drinker_clip=%s walker_intent=%s walker_cm=%.0f",
        Event, Step, Now - StepSeconds, At.X, At.Y, At.Z, Vitality(), Stamina(), Moving && Moving->IsGuarding() ? 1 : 0,
        Moving && Moving->IsDown() ? 1 : 0, Character ? *Character->GetPlayingFightClip().ToString() : TEXT("-"),
        Him ? FVector::Dist2D(At, Him->GetActorLocation()) : -1.0, Him ? AMarburgumFighter::StateName(Him->GetFighterState()) : TEXT("-"),
        Him ? AMarburgumFighter::IntentName(Him->GetIntent()) : TEXT("-"), Him ? Him->GetVitalityPoints() : -1,
        Him ? *Him->GetPlayingFightClip().ToString() : TEXT("-"), Other ? AMarburgumFighter::IntentName(Other->GetIntent()) : TEXT("-"),
        Other && Him ? FVector::Dist2D(Other->GetActorLocation(), Him->GetActorLocation()) : -1.0);
}

bool UMarburgumCombatSmokeSubsystem::Approach(double DistanceCm)
{
    AMarburgumCharacter* Character = Body();
    const AMarburgumFighter* Him = Drinker.Get();
    if (!Character || !Him)
    {
        return false;
    }
    FVector Toward = Him->GetActorLocation() - Character->GetActorLocation();
    Toward.Z = 0.0;
    const double Distance = Toward.Size();
    const double Off = FMath::Abs(FRotator::NormalizeAxis(Toward.Rotation().Yaw - Character->GetActorRotation().Yaw));
    if (Distance > DistanceCm)
    {
        Character->AddMovementInput(Toward.GetSafeNormal(), 1.f);
        return false;
    }
    if (Off > FacingToleranceDegrees)
    {
        // A slow shuffle turns the body toward him without closing in (the body turns toward its movement).
        Character->AddMovementInput(Toward.GetSafeNormal(), 0.12f);
        return false;
    }
    return true;
}

void UMarburgumCombatSmokeSubsystem::Observe()
{
    const AMarburgumCharacter* Character = Body();
    if (Walker.IsValid() && Walker->GetIntent() == EMarburgumFighterIntent::Avoid)
    {
        bSawWalkerAvoid = true;
    }
    if (Drinker.IsValid() && Drinker->GetPlayingFightClip() == TEXT("jab"))
    {
        bSawDrinkerJab = true;
    }
    if (Character && Character->GetPlayingFightClip() == TEXT("jab"))
    {
        bSawOwnJab = true;
    }
    if (Character && Character->GetPlayingFightClip() == TEXT("guard"))
    {
        bSawOwnGuard = true;
    }
}

bool UMarburgumCombatSmokeSubsystem::Tick(float DeltaTime)
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
    if (!Bind())
    {
        return true;
    }
    Observe();
    if (Role == TEXT("B"))
    {
        TickB(Now);
    }
    else
    {
        TickA(Now);
    }
    return true;
}

void UMarburgumCombatSmokeSubsystem::TickB(double Now)
{
    AMarburgumCharacter* Character = Body();
    UMarburgumCharacterMovement* Moving = Movement();
    AMarburgumFighter* Him = Drinker.Get();
    if (!Character || !Moving || !Him)
    {
        return;
    }
    switch (Step)
    {
    case 0:
        if (Now - StepSeconds < SettleSeconds)
        {
            return;
        }
        if (!Approach(StrikeDistanceCm))
        {
            if (Now - StepSeconds > 20.0) Fail(TEXT("never_reached_drinker"));
            return;
        }
        Report(TEXT("in_reach"), Now);
        AnswerMark = Answers.Num();
        Character->Jab();
        Advance(1, Now);
        return;
    case 1:
    {
        const FMarburgumCombatResult* Answer = AnswerSince(AnswerMark);
        if (!Answer)
        {
            if (Now - StepSeconds > AnswerTimeoutSeconds) Fail(TEXT("no_answer_for_jab"));
            return;
        }
        Expect(Answer->bOk && Answer->Outcome == Combat::OutcomeHit && Answer->TargetId == DrinkerId,
            FString::Printf(TEXT("first_jab=%d,%s,%s"), Answer->bOk ? 1 : 0, *Answer->Outcome.ToString(), *Answer->TargetId.ToString()));
        Report(TEXT("jabbed"), Now);
        Advance(2, Now);
        return;
    }
    case 2:
        // Hands down and still: his blows land in full until there is nothing left.
        if (!Moving->IsDown())
        {
            if (Now - StepSeconds > 45.0) Fail(TEXT("never_beaten_down"));
            return;
        }
        Report(TEXT("beaten_down"), Now);
        Expect(Vitality() == 0 || Vitality() == -1, FString::Printf(TEXT("down_with_vitality=%d"), Vitality()));
        Advance(3, Now);
        return;
    case 3:
        // He gives up on somebody on the floor, and the body shows the fall.
        if (Now - StepSeconds < 1.5)
        {
            return;
        }
        Report(TEXT("after_down"), Now);
        Expect(Him->GetFighterState() == EMarburgumFighterState::Idle,
            FString::Printf(TEXT("drinker_after_down=%s"), AMarburgumFighter::StateName(Him->GetFighterState())));
        Expect(Character->GetPlayingFightClip() == TEXT("down"), FString::Printf(TEXT("own_clip_down=%s"), *Character->GetPlayingFightClip().ToString()));
        AnswerMark = Answers.Num();
        Character->Jab();
        Advance(4, Now);
        return;
    case 4:
    {
        const FMarburgumCombatResult* Answer = AnswerSince(AnswerMark);
        if (!Answer)
        {
            if (Now - StepSeconds > AnswerTimeoutSeconds) Fail(TEXT("no_answer_from_floor"));
            return;
        }
        Expect(!Answer->bOk && Answer->Reason == Combat::ErrorIncapacitated, FString::Printf(TEXT("floor_swing=%d,%s"), Answer->bOk ? 1 : 0,
            *Answer->Reason.ToString()));
        Pass();
        return;
    }
    default:
        return;
    }
}

void UMarburgumCombatSmokeSubsystem::TickA(double Now)
{
    AMarburgumCharacter* Character = Body();
    UMarburgumCharacterMovement* Moving = Movement();
    AMarburgumFighter* Him = Drinker.Get();
    if (!Character || !Moving || !Him)
    {
        return;
    }
    switch (Step)
    {
    case 0:
        // He is at peace (watching whoever is near) before anything happens.
        if (Now - StepSeconds < SettleSeconds || Him->GetFighterState() != EMarburgumFighterState::Idle || Him->IsDown())
        {
            if (Now - StepSeconds > 60.0) Fail(TEXT("drinker_never_idle"));
            return;
        }
        Report(TEXT("idle_drinker"), Now);
        Shot(TEXT("before"));
        // From here he is out of reach: the swing happens and costs, and reaches nobody.
        AnswerMark = Answers.Num();
        StaminaMark = Stamina();
        Character->Jab();
        Advance(1, Now);
        return;
    case 1:
    {
        const FMarburgumCombatResult* Answer = AnswerSince(AnswerMark);
        if (!Answer)
        {
            if (Now - StepSeconds > AnswerTimeoutSeconds) Fail(TEXT("no_answer_for_far_jab"));
            return;
        }
        Expect(Answer->bOk && Answer->Outcome == Combat::OutcomeMiss, FString::Printf(TEXT("far_jab=%d,%s"), Answer->bOk ? 1 : 0, *Answer->Outcome.ToString()));
        if (Now - StepSeconds < 1.0)
        {
            return;
        }
        Expect(Him->GetFighterState() == EMarburgumFighterState::Idle, TEXT("a_miss_made_him_hostile"));
        Advance(2, Now);
        return;
    }
    case 2:
        if (!Approach(StrikeDistanceCm))
        {
            if (Now - StepSeconds > 20.0) Fail(TEXT("never_reached_drinker"));
            return;
        }
        Report(TEXT("in_reach"), Now);
        AnswerMark = Answers.Num();
        Character->Jab();
        LastSwingSeconds = Now;
        Advance(3, Now);
        return;
    case 3:
    {
        const FMarburgumCombatResult* Answer = AnswerSince(AnswerMark);
        if (!Answer)
        {
            if (Now - StepSeconds > AnswerTimeoutSeconds) Fail(TEXT("no_answer_for_jab"));
            return;
        }
        Expect(Answer->bOk && Answer->Outcome == Combat::OutcomeHit && Answer->TargetId == DrinkerId
                && FMath::IsNearlyEqual(Answer->Damage, Combat::FistDamage),
            FString::Printf(TEXT("jab=%d,%s,%s,%.1f"), Answer->bOk ? 1 : 0, *Answer->Outcome.ToString(), *Answer->TargetId.ToString(), Answer->Damage));
        Shot(TEXT("jab"));
        // Straight away again: still inside the jab's cooldown.
        AnswerMark = Answers.Num();
        Character->Jab();
        Advance(4, Now);
        return;
    }
    case 4:
    {
        const FMarburgumCombatResult* Answer = AnswerSince(AnswerMark);
        if (!Answer)
        {
            if (Now - StepSeconds > AnswerTimeoutSeconds) Fail(TEXT("no_answer_for_second_jab"));
            return;
        }
        Expect(!Answer->bOk && Answer->Reason == Combat::ErrorTooSoon, FString::Printf(TEXT("second_jab=%d,%s,%s"), Answer->bOk ? 1 : 0,
            *Answer->Reason.ToString(), *Answer->Outcome.ToString()));
        Advance(5, Now);
        return;
    }
    case 5:
        // He answers it; the walker steps away from the fight.
        if (Him->GetFighterState() != EMarburgumFighterState::Hostile)
        {
            if (Now - StepSeconds > 3.0) Fail(TEXT("drinker_never_hostile"));
            return;
        }
        Report(TEXT("drinker_hostile"), Now);
        Expect(bSawOwnJab, TEXT("own_jab_clip_never_played"));
        Character->StartGuard();
        Advance(6, Now);
        return;
    case 6:
        // Give the raised guard a moment to reach the server before counting blows against it.
        if (Now - StepSeconds < GuardSettleSeconds)
        {
            return;
        }
        PrevVitality = Vitality();
        PrevStamina = Stamina();
        bGuardShot = false;
        Advance(7, Now);
        return;
    case 7:
    {
        // Hands up and standing still: his next jab is blocked. Vitality and stamina arrive together in the
        // replicated condition, so one change between two frames is one blow.
        const int32 Loss = PrevVitality - Vitality();
        const int32 Paid = PrevStamina - Stamina();
        PrevVitality = Vitality();
        PrevStamina = Stamina();
        if (Loss <= 0)
        {
            if (Now - StepSeconds > BlowTimeoutSeconds) Fail(TEXT("no_blow_on_the_guard"));
            if (!bGuardShot && bSawOwnGuard && Now - StepSeconds > 0.3)
            {
                bGuardShot = true;
                Shot(TEXT("guard"));
            }
            return;
        }
        if (Loss >= UnguardedLossMin && Now - StepSeconds < InFlightSeconds)
        {
            // A blow thrown before the guard went up, reported late.
            Report(TEXT("blow_before_guard"), Now);
            return;
        }
        Report(TEXT("blow_on_guard"), Now);
        Expect(Loss >= GuardedLossMin && Loss <= GuardedLossMax, FString::Printf(TEXT("guarded_loss=%d"), Loss));
        Expect(Paid >= static_cast<int32>(Combat::GuardStaminaCost) - 1, FString::Printf(TEXT("guard_paid=%d"), Paid));
        Expect(bSawOwnGuard, TEXT("own_guard_clip_never_played"));
        Character->StopGuard();
        Advance(8, Now);
        return;
    }
    case 8:
        if (Now - StepSeconds < GuardSettleSeconds)
        {
            return;
        }
        PrevVitality = Vitality();
        Advance(9, Now);
        return;
    case 9:
    {
        const int32 Loss = PrevVitality - Vitality();
        PrevVitality = Vitality();
        if (Loss <= 0)
        {
            if (Now - StepSeconds > BlowTimeoutSeconds) Fail(TEXT("no_unguarded_blow"));
            return;
        }
        if (Loss <= GuardedLossMax && Now - StepSeconds < InFlightSeconds)
        {
            // A blow that met the guard before it came down, reported late.
            Report(TEXT("blow_before_lowering"), Now);
            return;
        }
        Report(TEXT("blow_unguarded"), Now);
        Expect(Loss >= UnguardedLossMin && Loss <= UnguardedLossMax, FString::Printf(TEXT("unguarded_loss=%d"), Loss));
        Expect(bSawDrinkerJab, TEXT("drinker_jab_clip_never_played"));
        Advance(10, Now);
        return;
    }
    case 10:
        // A committed swing, once there is breath for it and he is in front.
        if (!Approach(StrikeDistanceCm) || Stamina() < Combat::HeavyStaminaCost + 2 || Moving->GetSwingCooldown() > 0.f)
        {
            if (Now - StepSeconds > 15.0) Fail(TEXT("never_ready_for_heavy"));
            return;
        }
        AnswerMark = Answers.Num();
        Character->HeavySwing();
        LastSwingSeconds = Now;
        Advance(11, Now);
        return;
    case 11:
    {
        const FMarburgumCombatResult* Answer = AnswerSince(AnswerMark);
        if (!Answer)
        {
            if (Now - StepSeconds > AnswerTimeoutSeconds) Fail(TEXT("no_answer_for_heavy"));
            return;
        }
        if (!Answer->bOk && Answer->Reason == Combat::ErrorTooSoon)
        {
            // His blow was still going through us; try again.
            Advance(10, Now);
            return;
        }
        Expect(Answer->bOk && Answer->bHeavy && Answer->Outcome == Combat::OutcomeHit
                && FMath::IsNearlyEqual(Answer->Damage, Combat::FistDamage * Combat::HeavyMultiplier, 0.01f),
            FString::Printf(TEXT("heavy=%d,%s,%.2f"), Answer->bOk ? 1 : 0, *Answer->Outcome.ToString(), Answer->Damage));
        Shot(TEXT("heavy"));
        Advance(12, Now);
        return;
    }
    case 12:
        // Keep swinging until he is on the floor.
        if (Him->IsDown())
        {
            Report(TEXT("drinker_down"), Now);
            Advance(13, Now);
            return;
        }
        if (Now - StepSeconds > 40.0)
        {
            Fail(TEXT("drinker_never_down"));
            return;
        }
        if (Approach(StrikeDistanceCm) && Now - LastSwingSeconds > SwingSpacingSeconds && Stamina() >= Combat::FistStaminaCost + 1)
        {
            if (Stamina() >= Combat::HeavyStaminaCost + 20 && Moving->GetSwingCooldown() <= 0.f)
            {
                Character->HeavySwing();
            }
            else
            {
                Character->Jab();
            }
            LastSwingSeconds = Now;
        }
        return;
    case 13:
        if (Now - StepSeconds < 1.0)
        {
            return;
        }
        Report(TEXT("after_down"), Now);
        Expect(Him->GetFighterState() == EMarburgumFighterState::Incapacitated,
            FString::Printf(TEXT("drinker_state=%s"), AMarburgumFighter::StateName(Him->GetFighterState())));
        Expect(Him->GetVitalityPoints() == 0, FString::Printf(TEXT("drinker_vitality=%d"), Him->GetVitalityPoints()));
        Expect(Him->GetPlayingFightClip() == TEXT("down"), FString::Printf(TEXT("drinker_clip=%s"), *Him->GetPlayingFightClip().ToString()));
        Expect(Answers.Num() > 0 && Answers.Last().TargetVitality == 0, TEXT("last_blow_not_the_floor"));
        Expect(bSawWalkerAvoid, TEXT("walker_never_stepped_away"));
        Shot(TEXT("down"));
        Advance(14, Now);
        return;
    case 14:
        // Somebody on the floor is not a target: a jab at him reaches nobody.
        if (!Approach(StrikeDistanceCm) || Moving->GetSwingCooldown() > 0.f || Stamina() < Combat::FistStaminaCost + 1)
        {
            if (Now - StepSeconds > 15.0) Fail(TEXT("never_ready_for_floor_jab"));
            return;
        }
        AnswerMark = Answers.Num();
        Character->Jab();
        Advance(15, Now);
        return;
    case 15:
    {
        const FMarburgumCombatResult* Answer = AnswerSince(AnswerMark);
        if (!Answer)
        {
            if (Now - StepSeconds > AnswerTimeoutSeconds) Fail(TEXT("no_answer_for_floor_jab"));
            return;
        }
        Expect(Answer->bOk && Answer->Outcome == Combat::OutcomeMiss, FString::Printf(TEXT("floor_jab=%d,%s,%s"), Answer->bOk ? 1 : 0,
            *Answer->Outcome.ToString(), *Answer->Reason.ToString()));
        Report(TEXT("done"), Now);
        Advance(16, Now);
        return;
    }
    case 16:
        // Let the last screenshot land.
        if (Now - StepSeconds > 1.5)
        {
            Pass();
        }
        return;
    default:
        return;
    }
}

void UMarburgumCombatSmokeSubsystem::Pass()
{
    if (Problems.IsEmpty())
    {
        MB2_COMBAT_SMOKE_LOG("event=pass");
        bDone = true;
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }
    else
    {
        Fail(TEXT("expectations"));
    }
}

void UMarburgumCombatSmokeSubsystem::Fail(const FString& Reason)
{
    if (bDone)
    {
        return;
    }
    Problems.Add(Reason);
    MB2_COMBAT_SMOKE_LOG("event=fail reason=%s", *FString::Join(Problems, TEXT(",")));
    bDone = true;
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

#undef MB2_COMBAT_SMOKE_LOG
