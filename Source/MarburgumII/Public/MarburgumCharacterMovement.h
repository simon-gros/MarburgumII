#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/CharacterMovementReplication.h"
#include "MarburgumInteractionTypes.h"
#include "MarburgumCharacterMovement.generated.h"

class AMarburgumPlayerState;

/** Client -> server move data: the engine's, plus the stamina the client predicted at the end of the move. */
struct FMarburgumNetworkMoveData : public FCharacterNetworkMoveData
{
    float Stamina = 0.f;

    virtual void ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType) override;
    virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType) override;
};

struct FMarburgumNetworkMoveDataContainer : public FCharacterNetworkMoveDataContainer
{
    FMarburgumNetworkMoveDataContainer();

private:
    FMarburgumNetworkMoveData Moves[3];
};

/** Server -> client move response: a correction also carries the server's stamina, winded and down state. */
struct FMarburgumMoveResponseDataContainer : public FCharacterMoveResponseDataContainer
{
    float Stamina = 0.f;
    bool bWinded = false;
    bool bDown = false;

    virtual void ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment) override;
    virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap) override;
};

/**
 * The engine's networked character movement, plus walking/running with the original game's breath rules
 * (ADR-020) and server evidence when a client's move timestamps run ahead of server time (ADR-019).
 *
 * Running: the client asks with a compressed move flag (held Shift); the move runs only if the body has
 * breath: at most RunSpeedCm, draining stamina while on the ground, and a run that empties the bar
 * leaves the body winded until stamina is back to RunResumeStamina. Every machine that simulates a move
 * applies the same rule, so the owning client predicts its own stamina. Each move carries that
 * prediction to the server, which corrects the client (position, stamina, winded) whenever its own
 * simulation disagrees by more than StaminaCorrectionTolerance. The server's simulation decides how fast
 * the pawn moves: a client that claims breath it does not have still walks.
 *
 * Swimming (ADR-032, the original's tick_water): water is any water physics volume; its top is the surface.
 * Holding a direction swims at SwimSpeedCm and raises the mouth above the surface; letting go, the body
 * slips under. A fall into the river puts the mouth under. Steering at the surface onto a low enough bank
 * climbs out. While the mouth is under, stamina is the breath being held; none left is drowning, which
 * takes all vitality. A body with none is down: the server takes it to the recovery point, it cannot
 * move, and after RecoverySeconds it is alive again with full vitality and breath.
 *
 * Condition: the server mirrors the simulation into AMarburgumPlayerState (whole points, owner-only)
 * and adopts the saved condition the world authority restores there. Time-discrepancy detection and
 * resolution are the engine's, configured in [/Script/Engine.GameNetworkManager]; this class reports
 * them at most once a second per character as `MB2INTERACT role=server event=time_discrepancy`.
 */
UCLASS()
class MARBURGUMII_API UMarburgumCharacterMovement : public UCharacterMovementComponent
{
    GENERATED_BODY()

public:
    explicit UMarburgumCharacterMovement(const FObjectInitializer& ObjectInitializer);

    /** Owning client: hold to run. Sent to the server as a move flag; the simulation decides if it runs. */
    void SetWantsToRun(bool bWants) { bWantsToRun = bWants; }
    bool WantsToRun() const { return bWantsToRun; }

    /** The simulation's view: running in the last move, exact stamina, winded. Predicted on the owner. */
    bool IsRunning() const { return bRunning; }
    float GetStamina() const { return Stamina; }
    bool IsWinded() const { return bWinded; }

    /** In the water (the engine's swimming mode), and whether the mouth is under the surface. */
    bool IsInWaterBody() const { return MovementMode == MOVE_Swimming; }
    bool IsSubmerged() const { return bSubmerged; }

    /** No vitality: drowned. The body does not move until it recovers. */
    bool IsDown() const { return bDown; }

    /** The surface of the water the body is in, or 0 when it is not in water. */
    float GetWaterSurfaceZ() const;

    /** False until this machine has adopted the server's condition (restored or replicated). */
    bool IsConditionSynced() const { return bConditionSynced; }

    /** Server: seconds this pawn has spent running, as the server simulated it (ADR-019 movement bound). */
    double GetServerRunSeconds() const { return ServerRunSeconds; }

    /**
     * Test adversary only (-MB2ConditionCheat via the condition smoke): this client ignores its breath,
     * predicts running whenever it asks to and reports full stamina. It changes nothing on the server.
     */
    void SetIgnoreStaminaForTest(bool bIgnore) { bIgnoreStaminaForTest = bIgnore; }
    bool IsIgnoringStaminaForTest() const { return bIgnoreStaminaForTest; }

    /** Number of server corrections this client applied that changed its stamina or winded state. */
    int32 GetConditionCorrections() const { return ConditionCorrections; }

    virtual float GetMaxSpeed() const override;
    virtual FVector ConstrainInputAcceleration(const FVector& InputAcceleration) const override;
    virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void OnTimeDiscrepancyDetected(float CurrentTimeDiscrepancy, float LifetimeRawTimeDiscrepancy, float Lifetime,
        float CurrentMoveError) override;

    /** Saved-move hooks: the start of a move (to undo it when moves combine) and adopting a correction. */
    void RestoreStaminaForCombine(float StartStamina, bool bStartWinded);

protected:
    virtual void UpdateFromCompressedFlags(uint8 Flags) override;
    virtual bool ClientUpdatePositionAfterServerUpdate() override;
    virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
    virtual void UpdateCharacterStateAfterMovement(float DeltaSeconds) override;
    virtual bool ServerCheckClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& ClientWorldLocation,
        const FVector& RelativeClientLocation, FMovementBaseInterfaceData* ClientMovementBaseInterfaceData, FName ClientBaseBoneName,
        uint8 ClientMovementMode) override;
    virtual void ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse) override;
    virtual void PhysSwimming(float DeltaTime, int32 Iterations) override;
    virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;

private:
    friend class FMarburgumSavedMove;
    friend struct FMarburgumMoveResponseDataContainer;

    AMarburgumPlayerState* GetMarburgumPlayerState() const;
    void SyncConditionFromPlayerState();
    FMarburgumCondition MakeCondition(const AMarburgumPlayerState& State) const;
    void LogTransition(const TCHAR* Event) const;
    float FeetZ() const;
    bool TryClimbOut(float SurfaceZ, const FVector& Stroke);
    void GoDown(const TCHAR* Why);
    void TickRecoveryAuthority(float DeltaTime);

    FMarburgumNetworkMoveDataContainer MoveDataContainer;
    FMarburgumMoveResponseDataContainer MoveResponseContainer;

    bool bWantsToRun = false;
    bool bRunning = false;
    bool bWinded = false;
    float Stamina = 100.f;

    // Water and vitality (ADR-032). Simulated on every machine that runs a move, except the recovery
    // countdown and the trip to the recovery point, which only the server does.
    bool bSubmerged = false;
    bool bDown = false;
    bool bPlunge = false;
    float FallTopFeetZ = 0.f;
    float Vitality = 100.f;
    bool bAtRecoveryPoint = false;
    float RecoveryRemaining = 0.f;

    bool bConditionSynced = false;
    int32 AdoptedRestoreCount = 0;
    bool bIgnoreStaminaForTest = false;
    int32 ConditionCorrections = 0;
    double ServerRunSeconds = 0.0;

    int32 Detections = 0;
    int32 UnreportedDetections = 0;
    double LastReportSeconds = -1.0;
};
