#include "MarburgumFighter.h"

#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MarburgumCharacter.h"
#include "MarburgumCharacterMovement.h"
#include "MarburgumCombatContract.h"
#include "MarburgumFightPresentation.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumStreetSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

using namespace MarburgumII::Combat;

namespace
{
    constexpr float FighterCapsuleRadius = 34.f;
    constexpr float FighterCapsuleHalfHeight = 88.f;
    // Close enough to a walking goal to stand still on it.
    constexpr float ArriveCm = 12.f;
    // The original's step sizes, in Unreal centimetres.
    constexpr double SideStepCm = 115.0;
    constexpr double SideStepForwardCm = 40.0;
    constexpr double BackOffStepCm = 80.0;
    constexpr double AvoidStepCm = 140.0;
    constexpr double WanderArriveCm = 35.0;
    constexpr int32 WanderStuckTicks = 6;
    // Under this share of a tick's stride since the last tick, a walk that was asked for did not happen.
    constexpr double StuckStrideShare = 0.2;
    // Chest height for the wall test between two bodies.
    constexpr double ChestCm = 50.0;
    constexpr float TurnDegreesPerSecond = 540.f;
    const FName PaintTint(TEXT("Paint Tint"));

    bool IsStanding(const AMarburgumCharacter& Person)
    {
        const UMarburgumCharacterMovement* Movement = Cast<UMarburgumCharacterMovement>(Person.GetCharacterMovement());
        return !Person.GetSeatedVehicle() && Movement && !Movement->IsDown();
    }
}

AMarburgumFighter::AMarburgumFighter()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    AutoPossessAI = EAutoPossessAI::Disabled;
    GetCapsuleComponent()->InitCapsuleSize(FighterCapsuleRadius, FighterCapsuleHalfHeight);

    UCharacterMovementComponent* Movement = GetCharacterMovement();
    // Nobody possesses an NPC: the server moves it with input it adds itself.
    Movement->bRunPhysicsWithNoController = true;
    Movement->MaxWalkSpeed = MarburgumII::Gameplay::WalkSpeedCm;
    Movement->bOrientRotationToMovement = true;
    Movement->RotationRate = FRotator(0.f, TurnDegreesPerSecond, 0.f);
    bUseControllerRotationYaw = false;

    USkeletalMeshComponent* Body = GetMesh();
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> Manny(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
    if (Manny.Succeeded())
    {
        Body->SetSkeletalMesh(Manny.Object);
    }
    static ConstructorHelpers::FClassFinder<UAnimInstance> Unarmed(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"));
    if (Unarmed.Succeeded())
    {
        Body->SetAnimationMode(EAnimationMode::AnimationBlueprint);
        Body->SetAnimInstanceClass(Unarmed.Class);
    }
    Body->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -FighterCapsuleHalfHeight), FRotator(0.f, -90.f, 0.f));
    Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
}

void AMarburgumFighter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMarburgumFighter, NpcId);
    DOREPLIFETIME(AMarburgumFighter, FightPose);
    DOREPLIFETIME(AMarburgumFighter, VitalityPoints);
    DOREPLIFETIME(AMarburgumFighter, State);
    DOREPLIFETIME(AMarburgumFighter, Intent);
}

const FNpcDefinition* AMarburgumFighter::GetDefinition() const
{
    return FindNpc(NpcId.ToString());
}

FString AMarburgumFighter::GetDisplayName() const
{
    const FNpcDefinition* Npc = GetDefinition();
    return Npc ? FString(Npc->Name) : NpcId.ToString();
}

bool AMarburgumFighter::IsHostileRole() const
{
    const FNpcDefinition* Npc = GetDefinition();
    return Npc && FCString::Strcmp(Npc->Role, RoleHostile) == 0;
}

const TCHAR* AMarburgumFighter::StateName(EMarburgumFighterState Value)
{
    switch (Value)
    {
    case EMarburgumFighterState::Hostile: return StateHostile;
    case EMarburgumFighterState::Staggered: return StateStaggered;
    case EMarburgumFighterState::Incapacitated: return StateIncapacitated;
    default: return StateIdle;
    }
}

const TCHAR* AMarburgumFighter::IntentName(EMarburgumFighterIntent Value)
{
    switch (Value)
    {
    case EMarburgumFighterIntent::Wander: return IntentWander;
    case EMarburgumFighterIntent::Watch: return MarburgumII::Combat::IntentWatch;
    case EMarburgumFighterIntent::Avoid: return IntentAvoid;
    case EMarburgumFighterIntent::Return: return IntentReturn;
    case EMarburgumFighterIntent::Close: return IntentClose;
    case EMarburgumFighterIntent::Strike: return IntentStrike;
    default: return IntentWait;
    }
}

void AMarburgumFighter::BeginPlay()
{
    Super::BeginPlay();
    ApplyStats();
}

void AMarburgumFighter::ApplyStats()
{
    const FNpcDefinition* Npc = GetDefinition();
    if (Npc)
    {
        GetMesh()->SetRelativeScale3D(FVector(Npc->Scale));
    }
    if (GetNetMode() != NM_DedicatedServer)
    {
        Tint();
    }
    if (!HasAuthority())
    {
        return;
    }
    if (!Npc)
    {
        UE_LOG(LogMarburgumNet, Error, TEXT("%s role=server event=npc_unknown npc=%s"), CombatLogPrefix, *NpcId.ToString());
        SetActorTickEnabled(false);
        return;
    }
    Home = GetActorLocation();
    HomeYaw = GetActorRotation().Yaw;
    IdHash = GodotIdHash(NpcId.ToString());
    MaxVitality = Vitality = Npc->Vitality;
    Damage = Npc->Damage;
    ReachCm = Npc->ReachCm;
    PursuitRangeCm = Npc->PursuitRangeCm;
    AttackIntervalTicks = TicksForSeconds(Npc->AttackIntervalSeconds);
    VitalityPoints = static_cast<uint8>(FMath::Clamp(FMath::CeilToInt(Vitality), 0, 255));
    LastThinkLocation = Home;
    LogEvent(TEXT("npc_ready"), FString::Printf(TEXT("role=%s vitality=%.0f home=%.0f,%.0f,%.0f yaw=%.1f"), Npc->Role, Vitality, Home.X, Home.Y,
        Home.Z, HomeYaw));
}

void AMarburgumFighter::Tint()
{
    const FNpcDefinition* Npc = GetDefinition();
    USkeletalMeshComponent* Body = GetMesh();
    if (!Npc || !Body)
    {
        return;
    }
    // The mannequin's two materials: the body takes the person's main colour, the joints and trim the second.
    const uint32 Colors[] = { Npc->PrimaryColor, Npc->SecondaryColor };
    for (int32 Slot = 0; Slot < FMath::Min(Body->GetNumMaterials(), 2); ++Slot)
    {
        if (UMaterialInstanceDynamic* Material = Body->CreateDynamicMaterialInstance(Slot))
        {
            Material->SetVectorParameterValue(PaintTint, ProfileColor(Colors[Slot]));
        }
    }
}

void AMarburgumFighter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (HasAuthority() && GetDefinition())
    {
        ThinkAccumulator += DeltaSeconds;
        // The original thinks on a fixed 100 ms tick; a long frame catches up, but never more than a second.
        ThinkAccumulator = FMath::Min(ThinkAccumulator, 1.f);
        while (ThinkAccumulator >= TickSeconds)
        {
            ThinkAccumulator -= TickSeconds;
            Think();
        }
        if (bWalking)
        {
            const FVector Toward = FVector(WalkGoal.X - GetActorLocation().X, WalkGoal.Y - GetActorLocation().Y, 0.0);
            if (Toward.Size() <= ArriveCm)
            {
                Stop();
            }
            else
            {
                AddMovementInput(Toward.GetSafeNormal());
            }
        }
        else if (bHasFacing)
        {
            const FRotator Now = GetActorRotation();
            const float Yaw = FMath::FixedTurn(Now.Yaw, static_cast<float>(FacingYaw), TurnDegreesPerSecond * DeltaSeconds);
            SetActorRotation(FRotator(0.f, Yaw, 0.f));
        }
        FightPose.bGuarding = State == EMarburgumFighterState::Hostile && Intent == EMarburgumFighterIntent::Wait;
    }
    if (GetNetMode() != NM_DedicatedServer)
    {
        FightAnimator.Update(GetMesh() ? GetMesh()->GetAnimInstance() : nullptr, FightPose, GetVelocity().Size2D(), true);
    }
}

FName AMarburgumFighter::GetPlayingFightClip() const
{
    return FightAnimator.GetPlaying(GetMesh() ? GetMesh()->GetAnimInstance() : nullptr);
}

// ---- The original's tick (world_simulation.cpp tick_combat, one NPC) ----

void AMarburgumFighter::Think()
{
    ++TickCount;
    const FVector Here = GetActorLocation();
    const double MovedCm = FVector::Dist2D(Here, LastThinkLocation);
    const bool bWasWalking = bWalking;
    LastThinkLocation = Here;
    const double Stride = MarburgumII::Gameplay::WalkSpeedCm * TickSeconds;
    const bool bStepFailed = bWasWalking && MovedCm < Stride * StuckStrideShare;
    bLastStepFailed = bStepFailed;

    if (State == EMarburgumFighterState::Incapacitated)
    {
        RecoverRemaining -= TickSeconds;
        if (RecoverRemaining <= 0.f)
        {
            Recover();
        }
        return;
    }

    // Who is about: the nearest standing player is company, and closer still, somebody to watch.
    AMarburgumCharacter* Company = nullptr;
    AMarburgumCharacter* Watched = nullptr;
    double CompanyDistance = CompanyRangeCm;
    double WatchDistance = WatchRangeCm;
    for (TActorIterator<AMarburgumCharacter> It(GetWorld()); It; ++It)
    {
        if (!IsStanding(**It))
        {
            continue;
        }
        const double Distance = FVector::Dist2D(Here, It->GetActorLocation());
        if (Distance < CompanyDistance)
        {
            CompanyDistance = Distance;
            Company = *It;
        }
        if (Distance < WatchDistance)
        {
            WatchDistance = Distance;
            Watched = *It;
        }
    }

    if (IsHostileRole() && Target.IsValid())
    {
        ThinkHostile(*Target);
        return;
    }
    if (IsHostileRole() && !Target.IsValid() && State != EMarburgumFighterState::Idle)
    {
        // The person left the world.
        GiveUp();
        return;
    }

    // A bystander does not join in. They step away from the body that is actually fighting.
    if (!IsHostileRole())
    {
        const AMarburgumFighter* Danger = nullptr;
        double Nearest = AvoidRangeCm;
        for (TActorIterator<AMarburgumFighter> It(GetWorld()); It; ++It)
        {
            if (*It == this || !It->IsHostileRole() || !It->GetTarget() || It->IsDown())
            {
                continue;
            }
            const double Distance = FVector::Dist2D(Here, It->GetActorLocation());
            if (Distance < Nearest)
            {
                Nearest = Distance;
                Danger = *It;
            }
        }
        if (Danger)
        {
            SetStateAndIntent(EMarburgumFighterState::Idle, EMarburgumFighterIntent::Avoid);
            FVector Away = Here - Danger->GetActorLocation();
            Away.Z = 0.0;
            if (Away.SizeSquared() < FMath::Square(20.0))
            {
                Away = FVector(1.0, 0.0, 0.0);
            }
            FVector Want = Here + Away.GetSafeNormal() * AvoidStepCm;
            FVector FromHome = Want - Home;
            FromHome.Z = 0.0;
            if (FromHome.Size() > AvoidHomeCm)
            {
                Want = Home + FromHome.GetSafeNormal() * AvoidHomeCm;
            }
            WalkTo(Want);
            return;
        }
    }

    if (Watched)
    {
        Stop();
        FaceToward(Watched->GetActorLocation());
        SetStateAndIntent(EMarburgumFighterState::Idle, EMarburgumFighterIntent::Watch);
        return;
    }
    if (!Company && FVector::Dist2D(Here, Home) > HomeArriveCm)
    {
        SetStateAndIntent(EMarburgumFighterState::Idle, EMarburgumFighterIntent::Return);
        WalkTo(Home);
        return;
    }

    // Nobody is near. Wait, then take a short walk around home, then wait again.
    if (Company)
    {
        Stop();
        SetStateAndIntent(EMarburgumFighterState::Idle, EMarburgumFighterIntent::Wait);
        return;
    }
    if (GoalUntilTick == 0)
    {
        GoalUntilTick = TickCount + FirstPauseTicks(IdHash);
        SetStateAndIntent(EMarburgumFighterState::Idle, EMarburgumFighterIntent::Wait);
    }
    if (Intent != EMarburgumFighterIntent::Wander)
    {
        if (TickCount < GoalUntilTick)
        {
            Stop();
            FacingYaw = GlanceYaw(HomeYaw, IdHash, TickCount);
            bHasFacing = true;
            SetStateAndIntent(EMarburgumFighterState::Idle, EMarburgumFighterIntent::Wait);
            return;
        }
        // The original keeps a stroll that the navigation mesh moves inside WanderRadius + 35 cm of home;
        // every offset it picks is already inside that.
        WanderGoal = Home + WanderOffsetCm(IdHash, TickCount);
        GoalUntilTick = TickCount + WanderTicks;
        Stuck = 0;
        SetStateAndIntent(EMarburgumFighterState::Idle, EMarburgumFighterIntent::Wander);
        WalkTo(WanderGoal);
        return;
    }
    if (FVector::Dist2D(Here, WanderGoal) < WanderArriveCm || TickCount >= GoalUntilTick)
    {
        Stop();
        GoalUntilTick = TickCount + PauseTicks;
        Stuck = 0;
        SetStateAndIntent(EMarburgumFighterState::Idle, EMarburgumFighterIntent::Wait);
        return;
    }
    if (bStepFailed && ++Stuck > WanderStuckTicks)
    {
        Stop();
        GoalUntilTick = TickCount + PauseTicks;
        Stuck = 0;
        SetStateAndIntent(EMarburgumFighterState::Idle, EMarburgumFighterIntent::Wait);
        return;
    }
    if (!bStepFailed)
    {
        Stuck = 0;
    }
    WalkTo(WanderGoal);
}

void AMarburgumFighter::ThinkHostile(AMarburgumCharacter& Person)
{
    const FVector Here = GetActorLocation();
    const FVector There = Person.GetActorLocation();
    const UMarburgumCharacterMovement* Movement = Cast<UMarburgumCharacterMovement>(Person.GetCharacterMovement());

    FHostileView View;
    // A body in a car is out of reach of a fist; the original's characters cannot be both seated and targeted.
    View.bTargetPresent = Movement && !Person.GetSeatedVehicle();
    View.bTargetDown = Movement && Movement->IsDown();
    View.TargetFromHomeCm = FVector::Dist2D(There, Home);
    View.PursuitRangeCm = PursuitRangeCm;
    View.bStaggered = TickCount < StaggerUntilTick;
    View.DistanceCm = FVector::Dist2D(Here, There);
    View.bWalled = View.DistanceCm > 5.0 && IsWalled(Person);
    View.ReachCm = ReachCm;
    View.Now = static_cast<double>(TickCount);
    View.SwingUntil = static_cast<double>(SwingUntilTick);
    View.NextAttackAt = static_cast<double>(NextAttackTick);

    switch (DecideHostile(View))
    {
    case EHostileStep::GiveUp:
        GiveUp();
        return;
    case EHostileStep::Staggered:
        Stop();
        SetStateAndIntent(EMarburgumFighterState::Staggered, EMarburgumFighterIntent::Wait);
        return;
    case EHostileStep::Close:
    {
        SetStateAndIntent(EMarburgumFighterState::Hostile, EMarburgumFighterIntent::Close);
        FVector Toward = There - Here;
        Toward.Z = 0.0;
        if (bLastStepFailed && Toward.SizeSquared() > 1.0)
        {
            // The straight line refused: try the side, and the other side every few refused steps.
            ++Stuck;
            const FVector Ahead = Toward.GetSafeNormal();
            FVector Side(-Ahead.Y, Ahead.X, 0.0);
            if ((Stuck / SideStepSwapSteps) % 2 == 1)
            {
                Side = -Side;
            }
            WalkTo(Here + Side * SideStepCm + Ahead * SideStepForwardCm);
        }
        else
        {
            Stuck = 0;
            WalkTo(There);
        }
        return;
    }
    case EHostileStep::BackOff:
    {
        Stuck = 0;
        SetStateAndIntent(EMarburgumFighterState::Hostile, EMarburgumFighterIntent::Close);
        FVector Away = Here - There;
        Away.Z = 0.0;
        if (Away.SizeSquared() < 1.0)
        {
            Away = FRotator(0.0, HomeYaw, 0.0).Vector();
        }
        WalkTo(Here + Away.GetSafeNormal() * BackOffStepCm);
        FaceToward(There);
        return;
    }
    case EHostileStep::Swinging:
        Stop();
        FaceToward(There);
        SetStateAndIntent(EMarburgumFighterState::Hostile, EMarburgumFighterIntent::Strike);
        return;
    case EHostileStep::Recover:
        // Hands stay up. Walking in place between punches reads as a nervous shuffle.
        Stop();
        FaceToward(There);
        SetStateAndIntent(EMarburgumFighterState::Hostile, EMarburgumFighterIntent::Wait);
        return;
    case EHostileStep::Strike:
        Stop();
        FaceToward(There);
        Strike(Person);
        return;
    }
}

void AMarburgumFighter::Strike(AMarburgumCharacter& Person)
{
    UMarburgumCharacterMovement* Movement = Cast<UMarburgumCharacterMovement>(Person.GetCharacterMovement());
    if (!Movement)
    {
        return;
    }
    const FName Outcome = Movement->ReceiveBlowAuthority(Damage, StaggerSeconds, false);
    NextAttackTick = TickCount + AttackIntervalTicks;
    SwingUntilTick = TickCount + TicksForSeconds(SwingSeconds);
    ++FightPose.SwingSeq;
    FightPose.bHeavy = false;
    SetStateAndIntent(EMarburgumFighterState::Hostile, EMarburgumFighterIntent::Strike);
    Person.MulticastImpact(Person.GetActorLocation() + FVector(0.0, 0.0, ChestCm), Outcome, false, Movement->IsDown());
    // The street sees a fight (ADR-044).
    if (UMarburgumStreetSubsystem* Street = GetWorld()->GetSubsystem<UMarburgumStreetSubsystem>())
    {
        Street->ReportFight(this, GetActorLocation(), false);
    }
    LogEvent(TEXT("npc_strike"), FString::Printf(TEXT("target=%s outcome=%s damage=%.1f target_vitality=%.1f target_down=%d"),
        *Person.GetName(), WireWord(Outcome), Damage, Movement->GetVitality(), Movement->IsDown() ? 1 : 0));
}

FName AMarburgumFighter::ReceiveBlowAuthority(AMarburgumCharacter* Attacker, float BlowDamage, float StaggerFor, bool bHeavy)
{
    if (State == EMarburgumFighterState::Incapacitated)
    {
        return OutcomeHit;
    }
    // NPCs do not guard: every blow that reaches one lands in full.
    const FBlowResult Blow = ResolveBlow(Vitality, BlowDamage, false, StaggerFor);
    Vitality = Blow.VitalityAfter;
    VitalityPoints = static_cast<uint8>(FMath::Clamp(FMath::CeilToInt(Vitality), 0, 255));
    if (Blow.bIncapacitated)
    {
        // Out of the fight, not dead: on the floor, no longer anybody's target and nobody's opponent.
        bKnockedDown = false;
        FallDown(NpcRecoverySeconds);
    }
    else
    {
        StaggerUntilTick = FMath::Max(StaggerUntilTick, TickCount + TicksForSeconds(Blow.StaggerSeconds));
    }
    OnBlowLanded(Attacker, Blow.bIncapacitated);
    MulticastImpact(GetActorLocation() + FVector(0.0, 0.0, ChestCm), Blow.Outcome, bHeavy, Blow.bIncapacitated);
    LogEvent(TEXT("npc_struck"), FString::Printf(TEXT("by=%s heavy=%d outcome=%s damage=%.1f vitality=%.1f down=%d"),
        Attacker ? *Attacker->GetName() : TEXT("-"), bHeavy ? 1 : 0, Blow.Outcome, Blow.Applied, Vitality, Blow.bIncapacitated ? 1 : 0));
    return Blow.Outcome;
}

void AMarburgumFighter::OnBlowLanded(AMarburgumCharacter* Attacker, bool bFloored)
{
    // Being hit is how somebody finds out about you. A hostile answers it; anyone else only suffers it.
    if (!bFloored && IsHostileRole() && Attacker)
    {
        Target = Attacker;
        SetStateAndIntent(EMarburgumFighterState::Hostile, Intent);
    }
}

void AMarburgumFighter::FallDown(float Seconds)
{
    Target.Reset();
    Stop();
    GetCharacterMovement()->StopMovementImmediately();
    // Nobody walks into a body on the ground, and a car rolls over it rather than stopping dead against it.
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Ignore);
    FightPose.bDown = true;
    FightPose.bGuarding = false;
    RecoverRemaining = Seconds;
    DownSinceSeconds = GetWorld()->GetTimeSeconds();
    SetStateAndIntent(EMarburgumFighterState::Incapacitated, EMarburgumFighterIntent::Wait);
}

void AMarburgumFighter::KnockDownAuthority(AActor* Vehicle, float CarDamage, float DownSeconds)
{
    if (State == EMarburgumFighterState::Incapacitated)
    {
        return;
    }
    // A target is remembered through the fall: the fighter gets up and carries on after whoever it was after.
    TWeakObjectPtr<AMarburgumCharacter> Kept = Target;
    Vitality = FMath::Max(0.f, Vitality - CarDamage);
    VitalityPoints = static_cast<uint8>(FMath::Clamp(FMath::CeilToInt(Vitality), 0, 255));
    const bool bOut = Vitality <= 0.f;
    bKnockedDown = !bOut;
    KnockedFrom = Vehicle ? Vehicle->GetActorLocation() : GetActorLocation();
    FallDown(bOut ? NpcRecoverySeconds : DownSeconds);
    if (!bOut)
    {
        Target = Kept;
    }
    MulticastImpact(GetActorLocation() + FVector(0.0, 0.0, ChestCm), OutcomeHit, true, true);
    LogEvent(TEXT("npc_run_over"), FString::Printf(TEXT("by=%s damage=%.1f vitality=%.1f out=%d"), Vehicle ? *Vehicle->GetName() : TEXT("-"), CarDamage,
        Vitality, bOut ? 1 : 0));
}

void AMarburgumFighter::MulticastImpact_Implementation(FVector_NetQuantize At, FName Outcome, bool bHeavy, bool bFloored)
{
    MarburgumFightSound::PlayImpact(GetWorld(), At, Outcome, bHeavy, bFloored);
}

void AMarburgumFighter::GiveUp()
{
    // Nothing else makes it hostile: an NPC only ever fights the person who hit it.
    Target.Reset();
    Stuck = 0;
    GoalUntilTick = TickCount + PauseTicks;
    Stop();
    SetStateAndIntent(EMarburgumFighterState::Idle, EMarburgumFighterIntent::Wait);
}

void AMarburgumFighter::Recover()
{
    const FNpcDefinition* Npc = GetDefinition();
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Block);
    if (bKnockedDown)
    {
        // Knocked down by a car, not beaten: up where it lies, with what it had left, after whoever it was after.
        bKnockedDown = false;
        FightPose.bDown = false;
        Stuck = 0;
        bHasFacing = false;
        LastThinkLocation = GetActorLocation();
        SetStateAndIntent(Target.IsValid() ? EMarburgumFighterState::Hostile : EMarburgumFighterState::Idle, EMarburgumFighterIntent::Wait);
        LogEvent(TEXT("npc_got_up"), FString::Printf(TEXT("vitality=%.0f"), Vitality));
        return;
    }
    // Marburgum II's addition: up again at home, whole, with nobody to fight.
    Vitality = MaxVitality;
    VitalityPoints = static_cast<uint8>(FMath::Clamp(FMath::CeilToInt(Vitality), 0, 255));
    TeleportTo(Home, FRotator(0.0, HomeYaw, 0.0), false, true);
    FightPose.bDown = false;
    StaggerUntilTick = SwingUntilTick = NextAttackTick = 0;
    GoalUntilTick = TickCount + PauseTicks;
    Stuck = 0;
    bHasFacing = false;
    LastThinkLocation = Home;
    SetStateAndIntent(EMarburgumFighterState::Idle, EMarburgumFighterIntent::Wait);
    LogEvent(TEXT("npc_recovered"), FString::Printf(TEXT("vitality=%.0f"), Npc ? Npc->Vitality : Vitality));
}

void AMarburgumFighter::SetStateAndIntent(EMarburgumFighterState NewState, EMarburgumFighterIntent NewIntent)
{
    if (NewState == State && NewIntent == Intent)
    {
        return;
    }
    const EMarburgumFighterState OldState = State;
    State = NewState;
    Intent = NewIntent;
    LogEvent(NewState != OldState ? TEXT("npc_state") : TEXT("npc_intent"), FString::Printf(TEXT("from=%s to=%s target=%s"),
        StateName(OldState), StateName(NewState), Target.IsValid() ? *Target->GetName() : TEXT("-")));
}

void AMarburgumFighter::WalkTo(const FVector& Goal)
{
    bWalking = true;
    WalkGoal = Goal;
    bHasFacing = false;
}

void AMarburgumFighter::Stop()
{
    bWalking = false;
}

void AMarburgumFighter::FaceToward(const FVector& Point)
{
    const FVector Toward(Point.X - GetActorLocation().X, Point.Y - GetActorLocation().Y, 0.0);
    if (Toward.SizeSquared() > 1.0)
    {
        FacingYaw = Toward.Rotation().Yaw;
        bHasFacing = true;
    }
}

bool AMarburgumFighter::IsWalled(const AActor& Other) const
{
    // Chest-high. A person on the other side of a wall is not in reach, however close they stand.
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumFighterWall), false, this);
    Params.AddIgnoredActor(&Other);
    const FVector Up(0.0, 0.0, ChestCm);
    return GetWorld()->LineTraceTestByChannel(GetActorLocation() + Up, Other.GetActorLocation() + Up, ECC_Visibility, Params);
}

void AMarburgumFighter::LogEvent(const TCHAR* Event, const FString& Detail) const
{
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=%s npc=%s state=%s intent=%s tick=%lld pos=%.0f,%.0f,%.0f %s"), CombatLogPrefix, Event,
        *NpcId.ToString(), StateName(State), IntentName(Intent), TickCount, GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z,
        *Detail);
}
