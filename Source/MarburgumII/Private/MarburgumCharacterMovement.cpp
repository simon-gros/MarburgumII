#include "MarburgumCharacterMovement.h"

#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlayerState.h"

using namespace MarburgumII::Gameplay;

namespace
{
    // Prefix for machine-parsed condition evidence lines (scripts/condition-smoke-linux.sh).
    constexpr const TCHAR* ConditionLogPrefix = TEXT("MB2COND");
}

/** One client move, with the run request and the breath before and after it. */
class FMarburgumSavedMove : public FSavedMove_Character
{
public:
    using Super = FSavedMove_Character;

    bool bSavedWantsToRun = false;
    float StartStamina = 0.f;
    bool bStartWinded = false;
    float EndStamina = 0.f;
    bool bEndWinded = false;

    virtual void Clear() override
    {
        Super::Clear();
        bSavedWantsToRun = false;
        StartStamina = EndStamina = 0.f;
        bStartWinded = bEndWinded = false;
    }

    virtual uint8 GetCompressedFlags() const override
    {
        return Super::GetCompressedFlags() | (bSavedWantsToRun ? FLAG_Custom_0 : 0);
    }

    virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override
    {
        // A move in which the body became winded or got its breath back stays a move of its own.
        const FMarburgumSavedMove* Other = static_cast<const FMarburgumSavedMove*>(NewMove.Get());
        return bSavedWantsToRun == Other->bSavedWantsToRun && bStartWinded == bEndWinded
            && Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
    }

    virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override
    {
        Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);
        if (const UMarburgumCharacterMovement* Movement = Cast<UMarburgumCharacterMovement>(C->GetCharacterMovement()))
        {
            bSavedWantsToRun = Movement->WantsToRun();
        }
    }

    virtual void SetInitialPosition(ACharacter* C) override
    {
        Super::SetInitialPosition(C);
        if (const UMarburgumCharacterMovement* Movement = Cast<UMarburgumCharacterMovement>(C->GetCharacterMovement()))
        {
            StartStamina = Movement->GetStamina();
            bStartWinded = Movement->IsWinded();
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
            Movement->RestoreStaminaForCombine(Old->StartStamina, Old->bStartWinded);
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
    Stamina = static_cast<const FMarburgumSavedMove&>(ClientMove).EndStamina;
}

bool FMarburgumNetworkMoveData::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType)
{
    if (!FCharacterNetworkMoveData::Serialize(CharacterMovement, Ar, PackageMap, MoveType))
    {
        return false;
    }
    Ar << Stamina;
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
    }
    return !Ar.IsError();
}

UMarburgumCharacterMovement::UMarburgumCharacterMovement(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    MaxWalkSpeed = WalkSpeedCm;
    Stamina = DefaultStamina;
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

void UMarburgumCharacterMovement::UpdateFromCompressedFlags(uint8 Flags)
{
    Super::UpdateFromCompressedFlags(Flags);
    bWantsToRun = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
}

bool UMarburgumCharacterMovement::ClientUpdatePositionAfterServerUpdate()
{
    // Replaying saved moves sets the run request from each move's flags; the key the player is holding
    // now is what counts afterwards (the engine does the same for crouch).
    const bool bRealWantsToRun = bWantsToRun;
    const bool bResult = Super::ClientUpdatePositionAfterServerUpdate();
    bWantsToRun = bRealWantsToRun;
    return bResult;
}

void UMarburgumCharacterMovement::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    SyncConditionFromPlayerState();
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
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
        // After the first value, the owning client follows the server through move corrections.
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
    bConditionSynced = true;
}

void UMarburgumCharacterMovement::RestoreStaminaForCombine(float StartStamina, bool bStartWinded)
{
    Stamina = StartStamina;
    bWinded = bStartWinded;
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
}

void UMarburgumCharacterMovement::UpdateCharacterStateAfterMovement(float DeltaSeconds)
{
    Super::UpdateCharacterStateAfterMovement(DeltaSeconds);
    if (bIgnoreStaminaForTest && CharacterOwner && !CharacterOwner->HasAuthority())
    {
        return;
    }
    const bool bWasWinded = bWinded;
    if (bRunning)
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
    else
    {
        Stamina = FMath::Min(DefaultStamina, Stamina + StaminaRecoveryPerSecond * DeltaSeconds);
        if (bWinded && Stamina >= RunResumeStamina)
        {
            bWinded = false;
        }
    }

    if (!CharacterOwner || !CharacterOwner->HasAuthority())
    {
        return;
    }
    if (bRunning)
    {
        ServerRunSeconds += DeltaSeconds;
    }
    if (bWinded != bWasWinded)
    {
        LogTransition(bWinded ? TEXT("winded") : TEXT("breath_back"));
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
        }
    }
    Super::ClientHandleMoveResponse(MoveResponse);
}

void UMarburgumCharacterMovement::LogTransition(const TCHAR* Event) const
{
    const AMarburgumPlayerState* State = GetMarburgumPlayerState();
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=%s player=%d character=%s stamina=%.1f run_s=%.2f"),
        ConditionLogPrefix, Event, State ? State->GetPlayerId() : -1,
        State && !State->GetCharacterKey().IsEmpty() ? *State->GetCharacterKey() : TEXT("-"), Stamina, ServerRunSeconds);
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
