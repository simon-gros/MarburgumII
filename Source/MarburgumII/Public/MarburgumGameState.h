#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "MarburgumGameState.generated.h"

/** Outcome of a client request as decided by the authoritative server. */
UENUM(BlueprintType)
enum class EMarburgumRequestOutcome : uint8
{
    Applied,
    Duplicate,
    Rejected
};

/**
 * Server-authored, replicated world state shared by every connected client.
 * Clients only read these values; all mutation entry points are authority-only.
 */
UCLASS()
class MARBURGUMII_API AMarburgumGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    AMarburgumGameState();

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Marburgum|Network")
    int32 NetworkContractVersion = 0;

    /** Incremented by the server on a fixed interval; proves a live authority-to-client data path. */
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Marburgum|Network")
    int32 AuthorityHeartbeat = 0;

    /** Distinct client operations committed by the server (each operation id counts once). */
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Marburgum|Network")
    int32 AppliedRequestCount = 0;

    /** Client requests refused by server policy; refusals never change other state. */
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Marburgum|Network")
    int32 RejectedRequestCount = 0;

    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** Authority only. Commits an operation once; replays of the same id return Duplicate. */
    EMarburgumRequestOutcome CommitOperation(const FGuid& OperationId);

    /** Authority only. Records a refused request without applying it. */
    void RecordRejectedRequest();

private:
    void AdvanceHeartbeat();

    FTimerHandle HeartbeatTimer;

    /** Server-side idempotency ledger; process lifetime only until persistence exists. */
    TSet<FGuid> CommittedOperationIds;
};
