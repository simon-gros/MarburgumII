#include "MarburgumCharacterMovement.h"

#include "Components/BrushComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/PlayerStart.h"
#include "MarburgumCharacter.h"
#include "MarburgumCombatContract.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlayerState.h"

using namespace MarburgumII::Gameplay;
namespace Combat = MarburgumII::Combat;

namespace
{
    // Prefix for machine-parsed condition evidence lines (scripts/condition-smoke-linux.sh).
    constexpr const TCHAR* ConditionLogPrefix = TEXT("MB2COND");
}

/** One client move, with the run, guard and swing requests and the breath before and after it. */
class FMarburgumSavedMove : public FSavedMove_Character
{
public:
    using Super = FSavedMove_Character;

    bool bSavedWantsToRun = false;
    bool bSavedWantsToGuard = false;
    uint8 SavedSwingRequest = 0;
    bool bSavedSwingHeavy = false;
    uint8 StartSwingsHandled = 0;
    uint8 EndSwingsHandled = 0;
    float StartSwingCooldown = 0.f;
    float StartSwingRemaining = 0.f;
    float StartStamina = 0.f;
    bool bStartWinded = false;
    float StartFallTopFeetZ = 0.f;
    bool bStartDown = false;
    float EndStamina = 0.f;
    bool bEndWinded = false;
    bool bEndDown = false;

    virtual void Clear() override
    {
        Super::Clear();
        bSavedWantsToRun = bSavedWantsToGuard = false;
        SavedSwingRequest = StartSwingsHandled = EndSwingsHandled = 0;
        bSavedSwingHeavy = false;
        StartSwingCooldown = StartSwingRemaining = 0.f;
        StartStamina = EndStamina = StartFallTopFeetZ = 0.f;
        bStartWinded = bEndWinded = bStartDown = bEndDown = false;
    }

    virtual uint8 GetCompressedFlags() const override
    {
        // Custom_1 holds the guard up. Swings are not flags: a flag lives in one move, and the engine may skip a
        // move (one whose time comes out zero), so they travel as a request count in every move's data.
        return Super::GetCompressedFlags() | (bSavedWantsToRun ? FLAG_Custom_0 : 0) | (bSavedWantsToGuard ? FLAG_Custom_1 : 0);
    }

    virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override
    {
        // A move in which the body became winded or got its breath back stays a move of its own.
        const FMarburgumSavedMove* Other = static_cast<const FMarburgumSavedMove*>(NewMove.Get());
        // So does a move in which the body drowned, or that started down, and every swing.
        return bSavedWantsToRun == Other->bSavedWantsToRun && bSavedWantsToGuard == Other->bSavedWantsToGuard
            && SavedSwingRequest == Other->SavedSwingRequest && StartSwingsHandled == EndSwingsHandled
            && bStartWinded == bEndWinded && !bStartDown && !bEndDown
            && Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
    }

    virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override
    {
        Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);
        if (const UMarburgumCharacterMovement* Movement = Cast<UMarburgumCharacterMovement>(C->GetCharacterMovement()))
        {
            bSavedWantsToRun = Movement->WantsToRun();
            bSavedWantsToGuard = Movement->WantsToGuard();
            SavedSwingRequest = Movement->SwingRequest;
            bSavedSwingHeavy = Movement->bSwingRequestHeavy;
        }
    }

    virtual void SetInitialPosition(ACharacter* C) override
    {
        Super::SetInitialPosition(C);
        if (const UMarburgumCharacterMovement* Movement = Cast<UMarburgumCharacterMovement>(C->GetCharacterMovement()))
        {
            StartStamina = Movement->GetStamina();
            bStartWinded = Movement->IsWinded();
            bStartDown = Movement->IsDown();
            StartFallTopFeetZ = Movement->FallTopFeetZ;
            StartSwingCooldown = Movement->SwingCooldown;
            StartSwingRemaining = Movement->SwingRemaining;
            StartSwingsHandled = Movement->SwingsHandled;
        }
    }

    virtual void CombineWith(const FSavedMove_Character* OldMove, ACharacter* InCharacter, APlayerController* PC, const FVector& OldStartLocation) override
    {
        Super::CombineWith(OldMove, InCharacter, PC, OldStartLocation);
        // The combined move replays the pending one from its start, breath included; the engine then
        // records the new start with SetInitialPosition.
        const FMarburgumSavedMove* Old = static_cast<const FMarburgumSavedMove*>(OldMove);
        if (UMarburgumCharacterMovement* Movement = Cast<UMarburgumCharacterMovement>(InCharacter->GetCharacterMovement()))
        {
            Movement->RestoreStaminaForCombine(Old->StartStamina, Old->bStartWinded, Old->StartSwingCooldown, Old->StartSwingRemaining);
            Movement->SwingsHandled = Old->StartSwingsHandled;
            Movement->FallTopFeetZ = Old->StartFallTopFeetZ;
        }
    }

    virtual void PostUpdate(ACharacter* C, EPostUpdateMode PostUpdateMode) override
    {
        Super::PostUpdate(C, PostUpdateMode);
        if (const UMarburgumCharacterMovement* Movement = Cast<UMarburgumCharacterMovement>(C->GetCharacterMovement()))
        {
            // The test adversary claims a full bar whatever it predicted.
            EndStamina = Movement->IsIgnoringStaminaForTest() ? DefaultStamina : Movement->GetStamina();
            bEndWinded = Movement->IsIgnoringStaminaForTest() ? false : Movement->IsWinded();
            bEndDown = Movement->IsDown();
            EndSwingsHandled = Movement->SwingsHandled;
        }
    }

    virtual void PrepMoveFor(ACharacter* C) override
    {
        Super::PrepMoveFor(C);
        // A replayed move asks for what it asked for when it was made.
        if (UMarburgumCharacterMovement* Movement = Cast<UMarburgumCharacterMovement>(C->GetCharacterMovement()))
        {
            Movement->SwingRequest = SavedSwingRequest;
            Movement->bSwingRequestHeavy = bSavedSwingHeavy;
        }
    }
};

class FMarburgumPredictionData : public FNetworkPredictionData_Client_Character
{
public:
    explicit FMarburgumPredictionData(const UCharacterMovementComponent& Movement) : FNetworkPredictionData_Client_Character(Movement) {}

    virtual FSavedMovePtr AllocateNewMove() override
    {
        return FSavedMovePtr(new FMarburgumSavedMove());
    }
};

void FMarburgumNetworkMoveData::ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType)
{
    FCharacterNetworkMoveData::ClientFillNetworkMoveData(ClientMove, MoveType);
    const FMarburgumSavedMove& Move = static_cast<const FMarburgumSavedMove&>(ClientMove);
    Stamina = Move.EndStamina;
    SwingRequest = Move.SavedSwingRequest;
    bSwingHeavy = Move.bSavedSwingHeavy;
}

bool FMarburgumNetworkMoveData::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType)
{
    if (!FCharacterNetworkMoveData::Serialize(CharacterMovement, Ar, PackageMap, MoveType))
    {
        return false;
    }
    Ar << Stamina;
    Ar << SwingRequest;
    Ar.SerializeBits(&bSwingHeavy, 1);
    return !Ar.IsError();
}

FMarburgumNetworkMoveDataContainer::FMarburgumNetworkMoveDataContainer()
{
    NewMoveData = &Moves[0];
    PendingMoveData = &Moves[1];
    OldMoveData = &Moves[2];
}

void FMarburgumMoveResponseDataContainer::ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment)
{
    FCharacterMoveResponseDataContainer::ServerFillResponseData(CharacterMovement, PendingAdjustment);
    const UMarburgumCharacterMovement& Movement = static_cast<const UMarburgumCharacterMovement&>(CharacterMovement);
    Stamina = Movement.Stamina;
    bWinded = Movement.bWinded;
    bDown = Movement.bDown;
    SwingCooldown = Movement.SwingCooldown;
    SwingsHandled = Movement.SwingsHandled;
}

bool FMarburgumMoveResponseDataContainer::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap)
{
    if (!FCharacterMoveResponseDataContainer::Serialize(CharacterMovement, Ar, PackageMap))
    {
        return false;
    }
    // Acknowledgements stay as small as the engine's; only corrections carry the condition.
    if (IsCorrection())
    {
        Ar << Stamina;
        Ar.SerializeBits(&bWinded, 1);
        Ar.SerializeBits(&bDown, 1);
        Ar << SwingCooldown;
        Ar << SwingsHandled;
    }
    return !Ar.IsError();
}

UMarburgumCharacterMovement::UMarburgumCharacterMovement(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    MaxWalkSpeed = WalkSpeedCm;
    MaxSwimSpeed = SwimSpeedCm;
    Stamina = DefaultStamina;
    Vitality = DefaultVitality;
    SetNetworkMoveDataContainer(MoveDataContainer);
    SetMoveResponseDataContainer(MoveResponseContainer);
}

FNetworkPredictionData_Client* UMarburgumCharacterMovement::GetPredictionData_Client() const
{
    if (!ClientPredictionData)
    {
        UMarburgumCharacterMovement* Mutable = const_cast<UMarburgumCharacterMovement*>(this);
        Mutable->ClientPredictionData = new FMarburgumPredictionData(*this);
    }
    return ClientPredictionData;
}

AMarburgumPlayerState* UMarburgumCharacterMovement::GetMarburgumPlayerState() const
{
    return CharacterOwner ? CharacterOwner->GetPlayerState<AMarburgumPlayerState>() : nullptr;
}

float UMarburgumCharacterMovement::GetMaxSpeed() const
{
    if (bRunning && (MovementMode == MOVE_Walking || MovementMode == MOVE_NavWalking || MovementMode == MOVE_Falling))
    {
        return RunSpeedCm;
    }
    return Super::GetMaxSpeed();
}

FVector UMarburgumCharacterMovement::ConstrainInputAcceleration(const FVector& InputAcceleration) const
{
    // A body with no vitality does not move itself, wherever the input comes from.
    return bDown ? FVector::ZeroVector : Super::ConstrainInputAcceleration(InputAcceleration);
}

float UMarburgumCharacterMovement::GetWaterSurfaceZ() const
{
    const APhysicsVolume* Volume = GetPhysicsVolume();
    if (!Volume || !Volume->bWaterVolume || !Volume->GetBrushComponent())
    {
        return 0.f;
    }
    // Water volumes are boxes whose top is the drawn surface (MarburgumSwimContent -Steps=water).
    return static_cast<float>(Volume->GetBrushComponent()->Bounds.GetBox().Max.Z);
}

float UMarburgumCharacterMovement::FeetZ() const
{
    const UCapsuleComponent* Capsule = CharacterOwner ? CharacterOwner->GetCapsuleComponent() : nullptr;
    const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.f;
    return static_cast<float>(UpdatedComponent->GetComponentLocation().Z) - HalfHeight;
}

void UMarburgumCharacterMovement::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
    Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
    if (!UpdatedComponent)
    {
        return;
    }
    if (CharacterOwner && CharacterOwner->HasAuthority() && !bDown
        && (MovementMode == MOVE_Swimming) != (PreviousMovementMode == MOVE_Swimming))
    {
        LogTransition(MovementMode == MOVE_Swimming ? TEXT("in_water") : TEXT("left_water"));
    }
    if (MovementMode == MOVE_Falling)
    {
        FallTopFeetZ = FeetZ();
    }
    else if (MovementMode == MOVE_Swimming && PreviousMovementMode == MOVE_Falling)
    {
        // A fall into the river puts the mouth under (applied at the start of the first swim step), and
        // from high enough the water is as hard as the ground.
        bPlunge = true;
        const float Drop = FallTopFeetZ - GetWaterSurfaceZ();
        if (Drop >= LethalWaterImpactCm && !bDown)
        {
            GoDown(TEXT("water_impact"));
        }
    }
}

void UMarburgumCharacterMovement::PhysSwimming(float DeltaTime, int32 Iterations)
{
    // The original's tick_water, in place of the engine's buoyancy: the swimmer decides the depth.
    if (DeltaTime < MIN_TICK_TIME)
    {
        return;
    }
    const APhysicsVolume* Volume = GetPhysicsVolume();
    if (!Volume || !Volume->bWaterVolume)
    {
        SetMovementMode(MOVE_Falling);
        StartNewPhysics(DeltaTime, Iterations);
        return;
    }
    const float Surface = GetWaterSurfaceZ();
    const FVector OldLocation = UpdatedComponent->GetComponentLocation();
    const FQuat Rotation = UpdatedComponent->GetComponentQuat();
    float Feet = FeetZ();
    if (bPlunge)
    {
        bPlunge = false;
        const float Deepest = Surface - MouthHeightCm - FallInMouthBelowSurfaceCm;
        if (Feet > Deepest)
        {
            FHitResult Hit;
            SafeMoveUpdatedComponent(FVector(0.0, 0.0, Deepest - Feet), Rotation, true, Hit);
            Feet = FeetZ();
        }
    }

    // Holding a direction keeps the mouth in the air. Letting go, the body slips under.
    const bool bSteering = !bDown && !Acceleration.IsNearlyZero();
    const FVector Stroke = bSteering ? Acceleration.GetSafeNormal2D() : FVector::ZeroVector;
    const float Target = Surface - MouthHeightCm + (bSteering ? SwimMouthAboveSurfaceCm : -IdleMouthBelowSurfaceCm);
    const float Rate = (bSteering ? SwimRiseCmPerSecond : SinkCmPerSecond) * DeltaTime;
    float Rise = FMath::Clamp(Target - Feet, -Rate, Rate);
    Rise = FMath::Max(Rise, Surface - MaxSinkBelowSurfaceCm - Feet);
    const FVector Delta = Stroke * SwimSpeedCm * DeltaTime + FVector(0.0, 0.0, Rise);

    FHitResult Hit(1.f);
    SafeMoveUpdatedComponent(Delta, Rotation, true, Hit);
    if (Hit.Time < 1.f && IsSwimming())
    {
        HandleImpact(Hit, DeltaTime, Delta);
        SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, true);
    }
    if (!IsSwimming())
    {
        // The stroke left the water (the engine's volume change already made the body fall).
        return;
    }

    // The bank: steering near the surface, pull up onto standable ground just ahead.
    if (bSteering && FeetZ() + MouthHeightCm > Surface - BankReachBelowSurfaceCm && TryClimbOut(Surface, Stroke))
    {
        return;
    }
    // The velocity is what the body did. When the engine pushed it out of a penetration (that sets
    // bJustTeleported) the push is not motion, so the velocity is the move that was asked for. Keeping
    // the old value instead left a swimmer that had stopped reporting its last stroke (ADR-036).
    Velocity = bJustTeleported ? Delta / DeltaTime : (UpdatedComponent->GetComponentLocation() - OldLocation) / DeltaTime;
}

bool UMarburgumCharacterMovement::TryClimbOut(float SurfaceZ, const FVector& Stroke)
{
    UWorld* World = GetWorld();
    const UCapsuleComponent* Capsule = CharacterOwner ? CharacterOwner->GetCapsuleComponent() : nullptr;
    if (!World || !Capsule)
    {
        return false;
    }
    const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
    const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), HalfHeight);
    const ECollisionChannel Channel = UpdatedComponent->GetCollisionObjectType();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumClimbOut), false, CharacterOwner);
    FCollisionResponseParams Response;
    InitCollisionParams(Params, Response);
    const FVector Here = UpdatedComponent->GetComponentLocation();

    // Nearest first, along the stroke: ground no lower than ShoreMinBelowSurfaceCm under the surface and
    // no higher than a swimmer can pull up onto, with room to stand and nothing in between.
    for (int32 Step = 1; Step <= BankAheadSteps; ++Step)
    {
        const FVector Ahead = FVector(Here.X, Here.Y, SurfaceZ) + Stroke * (BankSearchCm * Step / BankAheadSteps);
        FHitResult Ground;
        if (!World->LineTraceSingleByChannel(Ground, Ahead + FVector(0.0, 0.0, BankMaxRiseCm + 5.0),
                Ahead - FVector(0.0, 0.0, ShoreMinBelowSurfaceCm + 5.0), Channel, Params, Response)
            || !IsWalkable(Ground) || Ground.ImpactPoint.Z < SurfaceZ - ShoreMinBelowSurfaceCm
            || Ground.ImpactPoint.Z > SurfaceZ + BankMaxRiseCm)
        {
            continue;
        }
        const FVector Stand(Ahead.X, Ahead.Y, Ground.ImpactPoint.Z + HalfHeight + 2.0);
        if (World->LineTraceTestByChannel(FVector(Here.X, Here.Y, Stand.Z), Stand, Channel, Params, Response)
            || World->OverlapBlockingTestByChannel(Stand, UpdatedComponent->GetComponentQuat(), Channel, Shape, Params, Response))
        {
            continue;
        }
        UpdatedComponent->SetWorldLocation(Stand, false, nullptr, ETeleportType::TeleportPhysics);
        bJustTeleported = true;
        Velocity = FVector::ZeroVector;
        bSubmerged = false;
        SetMovementMode(MOVE_Walking);
        if (CharacterOwner->HasAuthority())
        {
            LogTransition(TEXT("climbed_out"));
        }
        return true;
    }
    return false;
}

void UMarburgumCharacterMovement::GoDown(const TCHAR* Why)
{
    bDown = true;
    Vitality = 0.f;
    bRunning = false;
    bGuarding = false;
    SwingRemaining = 0.f;
    StaggerRemaining = 0.f;
    if (CharacterOwner && CharacterOwner->HasAuthority())
    {
        bAtRecoveryPoint = false;
        RecoveryRemaining = RecoverySeconds;
        LogTransition(Why);
    }
}

void UMarburgumCharacterMovement::TickRecoveryAuthority(float DeltaTime)
{
    if (!bDown || !CharacterOwner || !CharacterOwner->HasAuthority() || !bConditionSynced)
    {
        return;
    }
    if (!bAtRecoveryPoint)
    {
        // As the original takes a dead player to the graveyard: here the start tagged "recovery", else
        // the arrival start, else where the body is.
        APlayerStart* Recovery = nullptr;
        for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
        {
            if (It->PlayerStartTag == RecoveryStartTag || (!Recovery && It->PlayerStartTag == DefaultStartTag))
            {
                Recovery = *It;
            }
        }
        if (Recovery)
        {
            CharacterOwner->TeleportTo(Recovery->GetActorLocation(), Recovery->GetActorRotation(), false, true);
        }
        SetMovementMode(MOVE_Falling);
        Velocity = FVector::ZeroVector;
        bAtRecoveryPoint = true;
        LogTransition(TEXT("recovery_point"));
    }
    else
    {
        RecoveryRemaining = FMath::Max(0.f, RecoveryRemaining - DeltaTime);
        if (RecoveryRemaining <= 0.f)
        {
            bDown = false;
            bAtRecoveryPoint = false;
            Vitality = DefaultVitality;
            Stamina = DefaultStamina;
            bWinded = false;
            LogTransition(TEXT("recovered"));
        }
    }
    if (AMarburgumPlayerState* State = GetMarburgumPlayerState())
    {
        State->UpdateConditionAuthority(MakeCondition(*State));
    }
}

void UMarburgumCharacterMovement::MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel)
{
    // The server takes the swing request from the move it is running; a replaying client set it in PrepMoveFor.
    if (CharacterOwner && CharacterOwner->HasAuthority())
    {
        if (const FMarburgumNetworkMoveData* Move = static_cast<const FMarburgumNetworkMoveData*>(GetCurrentNetworkMoveData()))
        {
            SwingRequest = Move->SwingRequest;
            bSwingRequestHeavy = Move->bSwingHeavy;
        }
    }
    Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAccel);
}

void UMarburgumCharacterMovement::UpdateFromCompressedFlags(uint8 Flags)
{
    Super::UpdateFromCompressedFlags(Flags);
    bWantsToRun = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
    bWantsToGuard = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
}

bool UMarburgumCharacterMovement::ClientUpdatePositionAfterServerUpdate()
{
    // Replaying saved moves sets the run, guard and swing requests from each move; the keys the player is
    // holding now, and the latest swing asked for, are what count afterwards (the engine does the same for
    // crouch).
    const bool bRealWantsToRun = bWantsToRun;
    const bool bRealWantsToGuard = bWantsToGuard;
    const uint8 RealSwingRequest = SwingRequest;
    const bool bRealSwingHeavy = bSwingRequestHeavy;
    const bool bResult = Super::ClientUpdatePositionAfterServerUpdate();
    bWantsToRun = bRealWantsToRun;
    bWantsToGuard = bRealWantsToGuard;
    SwingRequest = RealSwingRequest;
    bSwingRequestHeavy = bRealSwingHeavy;
    return bResult;
}

void UMarburgumCharacterMovement::RequestSwing(EMarburgumSwing Swing)
{
    // One swing at a time: a second press before the first has been simulated does not queue a combination.
    if (Swing != EMarburgumSwing::None && SwingRequest == SwingsHandled)
    {
        ++SwingRequest;
        bSwingRequestHeavy = Swing == EMarburgumSwing::Heavy;
    }
}

void UMarburgumCharacterMovement::UpdateSwing(float DeltaSeconds)
{
    // Every move carries the latest request; the first move simulated with a new one throws it, once.
    if (SwingRequest == SwingsHandled)
    {
        return;
    }
    SwingsHandled = SwingRequest;
    const bool bHeavy = bSwingRequestHeavy;

    Combat::FSwingAsk Ask;
    Ask.bHasCharacter = bConditionSynced;
    Ask.bDown = bDown;
    // Timers here count down to zero, so "now" is always zero and "not yet" is anything still left.
    Ask.Now = 0.0;
    Ask.NextSwingAt = SwingCooldown;
    Ask.StaggeredUntil = StaggerRemaining;
    Ask.Stamina = Stamina;
    Ask.bHeavy = bHeavy;
    const TCHAR* Refusal = Combat::SwingRefusal(Ask);
    if (!Refusal)
    {
        // The swing happens whether or not it finds anybody, and costs the same either way. You cannot
        // punch from behind your own guard.
        Stamina -= Combat::SwingStaminaCost(bHeavy);
        SwingCooldown = Combat::SwingCooldownSeconds(bHeavy);
        SwingRemaining = bHeavy ? Combat::HeavySwingSeconds : Combat::SwingSeconds;
        bGuarding = false;
        bRunning = false;
        if (!CharacterOwner || !CharacterOwner->bClientUpdating)
        {
            // A replayed move re-spends the breath but is not a new swing to draw.
            ++SwingSeq;
            bLastSwingHeavy = bHeavy;
        }
    }
    if (CharacterOwner && CharacterOwner->HasAuthority())
    {
        if (AMarburgumCharacter* Body = Cast<AMarburgumCharacter>(CharacterOwner))
        {
            Body->ResolveSwingAuthority(bHeavy, Refusal);
        }
    }
}

const TCHAR* UMarburgumCharacterMovement::ReceiveBlowAuthority(float Damage, float StaggerSeconds, bool bHeavy)
{
    if (bDown)
    {
        return Combat::OutcomeHit;
    }
    // A guard is paid for in stamina. One that cannot be paid for breaks, and the blow it was meant to stop
    // arrives in full.
    const bool bWasGuarding = bGuarding;
    const Combat::FGuardPayment Guard =
        Combat::PayForGuard(bGuarding, Stamina, bHeavy ? Combat::HeavyGuardStaminaCost : Combat::GuardStaminaCost);
    Stamina = Guard.StaminaAfter;
    bGuarding = Guard.bGuarded;
    const Combat::FBlowResult Blow = Combat::ResolveBlow(Vitality, Damage, Guard.bGuarded, StaggerSeconds);
    Vitality = Blow.VitalityAfter;
    StaggerRemaining = FMath::Max(StaggerRemaining, Blow.StaggerSeconds);
    LogCombat(TEXT("struck"), FString::Printf(TEXT("outcome=%s heavy=%d damage=%.1f guard_broke=%d stagger_s=%.2f vitality=%.1f stamina=%.1f"),
        Blow.Outcome, bHeavy ? 1 : 0, Blow.Applied, bWasGuarding && !Guard.bGuarded ? 1 : 0, Blow.StaggerSeconds, Vitality, Stamina));
    if (Blow.bIncapacitated)
    {
        GoDown(TEXT("beaten"));
    }
    if (AMarburgumPlayerState* State = GetMarburgumPlayerState(); State && bConditionSynced)
    {
        State->UpdateConditionAuthority(MakeCondition(*State));
    }
    return Blow.Outcome;
}

void UMarburgumCharacterMovement::LogCombat(const TCHAR* Event, const FString& Detail) const
{
    const AMarburgumPlayerState* State = GetMarburgumPlayerState();
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=%s player=%d character=%s %s"), Combat::CombatLogPrefix, Event,
        State ? State->GetPlayerId() : -1, State && !State->GetCharacterKey().IsEmpty() ? *State->GetCharacterKey() : TEXT("-"), *Detail);
}

void UMarburgumCharacterMovement::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    SyncConditionFromPlayerState();
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    TickRecoveryAuthority(DeltaTime);
}

void UMarburgumCharacterMovement::SyncConditionFromPlayerState()
{
    AMarburgumPlayerState* State = GetMarburgumPlayerState();
    if (!bConditionSynced && GetNetMode() == NM_Standalone)
    {
        // An offline development world has no authority to restore a condition: a rested body.
        bConditionSynced = true;
        return;
    }
    if (!State || !State->IsConditionKnown() || !CharacterOwner)
    {
        return;
    }
    if (CharacterOwner->HasAuthority())
    {
        // The world authority restored a saved condition: the simulation starts from it.
        if (bConditionSynced && AdoptedRestoreCount == State->GetConditionRestoreCount())
        {
            return;
        }
        AdoptedRestoreCount = State->GetConditionRestoreCount();
    }
    else if (bConditionSynced || !CharacterOwner->IsLocallyControlled())
    {
        // After the first value, the owning client follows the server through move corrections, except
        // for recovering: a body that is down sends no input, so no correction would ever tell it.
        if (bConditionSynced && CharacterOwner->IsLocallyControlled() && bDown && !State->GetCondition().IsDown())
        {
            bDown = false;
            Vitality = State->GetCondition().Vitality;
            Stamina = FMath::Max(Stamina, static_cast<float>(State->GetCondition().Stamina));
            bWinded = State->GetCondition().bWinded;
        }
        // Nor for being beaten down while standing still: nothing this client predicted disagrees with it.
        else if (bConditionSynced && CharacterOwner->IsLocallyControlled() && !bDown && State->GetCondition().IsDown())
        {
            bDown = true;
            Vitality = 0.f;
            bRunning = false;
            bGuarding = false;
        }
        return;
    }
    else if (bIgnoreStaminaForTest)
    {
        bConditionSynced = true;
        return;
    }
    Stamina = State->GetCondition().Stamina;
    bWinded = State->GetCondition().bWinded;
    bRunning = false;
    Vitality = State->GetCondition().Vitality;
    bDown = State->GetCondition().IsDown();
    if (bDown && CharacterOwner->HasAuthority())
    {
        // Saved while down: the body goes back to the recovery point for the time it had left.
        bAtRecoveryPoint = false;
        RecoveryRemaining = State->GetCondition().RecoverySeconds > 0 ? State->GetCondition().RecoverySeconds : RecoverySeconds;
    }
    bConditionSynced = true;
}

void UMarburgumCharacterMovement::RestoreStaminaForCombine(float StartStamina, bool bStartWinded, float StartSwingCooldown,
    float StartSwingRemaining)
{
    Stamina = StartStamina;
    bWinded = bStartWinded;
    SwingCooldown = StartSwingCooldown;
    SwingRemaining = StartSwingRemaining;
}

void UMarburgumCharacterMovement::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
    Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
    const bool bMoving = !Acceleration.IsNearlyZero() && (IsMovingOnGround() || IsFalling());
    if (bIgnoreStaminaForTest && CharacterOwner && !CharacterOwner->HasAuthority())
    {
        bRunning = bWantsToRun && bMoving;
        return;
    }
    // No breath, or winded and not yet back to RunResumeStamina (winded clears there): the body walks.
    bRunning = bConditionSynced && bWantsToRun && bMoving && Stamina > 0.f && !bWinded;
    UpdateSwing(DeltaSeconds);
    // Nothing to raise while on the floor, in the water or throwing a blow, or with nothing left to raise it with.
    bGuarding = bConditionSynced && bWantsToGuard && !bDown && !IsSwimming() && SwingRemaining <= 0.f && Stamina > 0.f;
}

void UMarburgumCharacterMovement::UpdateCharacterStateAfterMovement(float DeltaSeconds)
{
    Super::UpdateCharacterStateAfterMovement(DeltaSeconds);
    if (bIgnoreStaminaForTest && CharacterOwner && !CharacterOwner->HasAuthority())
    {
        return;
    }
    const bool bWasWinded = bWinded;
    const bool bWasSubmerged = bSubmerged;
    const bool bWasDown = bDown;
    bSubmerged = IsSwimming() && FeetZ() + MouthHeightCm < GetWaterSurfaceZ() - SubmergedMarginCm;
    if (IsFalling())
    {
        FallTopFeetZ = FMath::Max(FallTopFeetZ, FeetZ());
    }
    if (bSubmerged && !bDown)
    {
        // Holding a breath spends the same stamina a run does, more slowly: a full bar is about a minute
        // under. None left, and still under, is drowning.
        Stamina = FMath::Max(0.f, Stamina - BreathDrainPerSecond * DeltaSeconds);
        if (Stamina <= 0.f)
        {
            Stamina = 0.f;
            bWinded = true;
            GoDown(TEXT("drowned"));
        }
    }
    else if (bRunning)
    {
        // Breath is spent on the ground; a running jump neither spends nor recovers it.
        if (IsMovingOnGround())
        {
            Stamina = FMath::Max(0.f, Stamina - RunStaminaDrainPerSecond * DeltaSeconds);
            if (Stamina <= 0.f)
            {
                Stamina = 0.f;
                bWinded = true;
            }
        }
    }
    else if (bGuarding)
    {
        // Holding a guard up costs a trickle and rests nothing; with none left the hands come down.
        Stamina = FMath::Max(0.f, Stamina - Combat::GuardDrainPerSecond * DeltaSeconds);
        if (Stamina <= 0.f)
        {
            bGuarding = false;
        }
    }
    else
    {
        Stamina = FMath::Min(DefaultStamina, Stamina + StaminaRecoveryPerSecond * DeltaSeconds);
        if (bWinded && Stamina >= RunResumeStamina)
        {
            bWinded = false;
        }
    }

    // The swing timers run on the move clock, the same on every machine that simulates the move.
    SwingCooldown = FMath::Max(0.f, SwingCooldown - DeltaSeconds);
    SwingRemaining = FMath::Max(0.f, SwingRemaining - DeltaSeconds);

    if (!CharacterOwner || !CharacterOwner->HasAuthority())
    {
        return;
    }
    StaggerRemaining = FMath::Max(0.f, StaggerRemaining - DeltaSeconds);
    if (bRunning)
    {
        ServerRunSeconds += DeltaSeconds;
    }
    if (bWinded != bWasWinded && !bDown && !bWasDown)
    {
        LogTransition(bWinded ? TEXT("winded") : TEXT("breath_back"));
    }
    if (bSubmerged != bWasSubmerged && !bDown && !bWasDown)
    {
        LogTransition(bSubmerged ? TEXT("under") : TEXT("surfaced"));
    }
    AMarburgumPlayerState* State = GetMarburgumPlayerState();
    if (State && bConditionSynced)
    {
        State->UpdateConditionAuthority(MakeCondition(*State));
    }
}

FMarburgumCondition UMarburgumCharacterMovement::MakeCondition(const AMarburgumPlayerState& State) const
{
    FMarburgumCondition Condition = State.GetCondition();
    // Whole points, rounded down: the bar never shows breath the body does not have.
    Condition.Stamina = static_cast<uint8>(FMath::Clamp(FMath::FloorToInt(Stamina), 0, 255));
    Condition.bWinded = bWinded;
    Condition.bRunning = bRunning;
    Condition.bSwimming = IsSwimming();
    Condition.bSubmerged = bSubmerged;
    Condition.Vitality = static_cast<uint8>(FMath::Clamp(FMath::CeilToInt(Vitality), 0, 255));
    Condition.RecoverySeconds = bDown ? static_cast<uint8>(FMath::Clamp(FMath::CeilToInt(RecoveryRemaining), 1, 255)) : 0;
    return Condition;
}

bool UMarburgumCharacterMovement::ServerCheckClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel,
    const FVector& ClientWorldLocation, const FVector& RelativeClientLocation, FMovementBaseInterfaceData* ClientMovementBaseInterfaceData,
    FName ClientBaseBoneName, uint8 ClientMovementMode)
{
    if (Super::ServerCheckClientError(ClientTimeStamp, DeltaTime, Accel, ClientWorldLocation, RelativeClientLocation,
            ClientMovementBaseInterfaceData, ClientBaseBoneName, ClientMovementMode))
    {
        return true;
    }
    const FMarburgumNetworkMoveData* Move = static_cast<const FMarburgumNetworkMoveData*>(GetCurrentNetworkMoveData());
    return bConditionSynced && Move && FMath::Abs(Move->Stamina - Stamina) > StaminaCorrectionTolerance;
}

void UMarburgumCharacterMovement::ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse)
{
    if (MoveResponse.IsCorrection())
    {
        const FMarburgumMoveResponseDataContainer& Response = static_cast<const FMarburgumMoveResponseDataContainer&>(MoveResponse);
        // Count corrections against what this client predicted for that very move.
        if (const FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character())
        {
            const int32 Index = ClientData->GetSavedMoveIndex(Response.ClientAdjustment.TimeStamp);
            const FMarburgumSavedMove* Predicted = Index != INDEX_NONE
                ? static_cast<const FMarburgumSavedMove*>(ClientData->SavedMoves[Index].Get()) : nullptr;
            if (Predicted && (FMath::Abs(Predicted->EndStamina - Response.Stamina) > StaminaCorrectionTolerance
                    || Predicted->bEndWinded != Response.bWinded))
            {
                ++ConditionCorrections;
            }
        }
        if (!bIgnoreStaminaForTest)
        {
            // The saved moves after this one are replayed from the server's breath.
            Stamina = Response.Stamina;
            bWinded = Response.bWinded;
            bDown = Response.bDown;
            SwingCooldown = Response.SwingCooldown;
            SwingsHandled = Response.SwingsHandled;
            Vitality = bDown ? 0.f : FMath::Max(Vitality, 1.f);
        }
    }
    Super::ClientHandleMoveResponse(MoveResponse);
}

void UMarburgumCharacterMovement::LogTransition(const TCHAR* Event) const
{
    const AMarburgumPlayerState* State = GetMarburgumPlayerState();
    const FVector At = UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=%s player=%d character=%s stamina=%.1f run_s=%.2f vitality=%.0f pos=%.0f,%.0f,%.0f mode=%d"),
        ConditionLogPrefix, Event, State ? State->GetPlayerId() : -1,
        State && !State->GetCharacterKey().IsEmpty() ? *State->GetCharacterKey() : TEXT("-"), Stamina, ServerRunSeconds, Vitality,
        At.X, At.Y, At.Z, static_cast<int32>(MovementMode));
}

void UMarburgumCharacterMovement::OnTimeDiscrepancyDetected(float CurrentTimeDiscrepancy, float LifetimeRawTimeDiscrepancy,
    float Lifetime, float CurrentMoveError)
{
    Super::OnTimeDiscrepancyDetected(CurrentTimeDiscrepancy, LifetimeRawTimeDiscrepancy, Lifetime, CurrentMoveError);
    ++Detections;
    ++UnreportedDetections;
    // A client that keeps running fast is detected several times a second; one line a second is enough.
    const double Now = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
    if (LastReportSeconds >= 0.0 && Now - LastReportSeconds < 1.0)
    {
        return;
    }
    const AMarburgumPlayerState* State = GetMarburgumPlayerState();
    UE_LOG(LogMarburgumNet, Log,
        TEXT("%s role=server event=time_discrepancy player=%d character=%s discrepancy_s=%.3f lifetime_raw_s=%.3f lifetime_s=%.1f detections=%d since_last=%d"),
        InteractLogPrefix, State ? State->GetPlayerId() : -1,
        State && !State->GetCharacterKey().IsEmpty() ? *State->GetCharacterKey() : TEXT("-"),
        CurrentTimeDiscrepancy, LifetimeRawTimeDiscrepancy, Lifetime, Detections, UnreportedDetections);
    UnreportedDetections = 0;
    LastReportSeconds = Now;
}
