#include "MarburgumGameState.h"

#include "MarburgumNetworkContract.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

AMarburgumGameState::AMarburgumGameState()
{
    bReplicates = true;
    NetworkContractVersion = MarburgumII::Network::ContractVersion;
}

void AMarburgumGameState::BeginPlay()
{
    Super::BeginPlay();

    if (HasAuthority())
    {
        GetWorldTimerManager().SetTimer(
            HeartbeatTimer, this, &AMarburgumGameState::AdvanceHeartbeat,
            MarburgumII::Network::AuthorityHeartbeatIntervalSeconds, true);
    }
}

void AMarburgumGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMarburgumGameState, NetworkContractVersion);
    DOREPLIFETIME(AMarburgumGameState, AuthorityHeartbeat);
    DOREPLIFETIME(AMarburgumGameState, AppliedRequestCount);
    DOREPLIFETIME(AMarburgumGameState, RejectedRequestCount);
}

EMarburgumRequestOutcome AMarburgumGameState::CommitOperation(const FGuid& OperationId)
{
    check(HasAuthority());

    if (!OperationId.IsValid())
    {
        RecordRejectedRequest();
        return EMarburgumRequestOutcome::Rejected;
    }

    bool bAlreadyCommitted = false;
    CommittedOperationIds.Add(OperationId, &bAlreadyCommitted);
    if (bAlreadyCommitted)
    {
        return EMarburgumRequestOutcome::Duplicate;
    }

    ++AppliedRequestCount;
    return EMarburgumRequestOutcome::Applied;
}

void AMarburgumGameState::RecordRejectedRequest()
{
    check(HasAuthority());
    ++RejectedRequestCount;
}

void AMarburgumGameState::AdvanceHeartbeat()
{
    ++AuthorityHeartbeat;
}
