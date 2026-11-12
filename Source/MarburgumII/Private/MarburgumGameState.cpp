#include "MarburgumGameState.h"

#include "MarburgumNetworkContract.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
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
    DOREPLIFETIME(AMarburgumGameState, Weather);
}

const FMarburgumWeather* AMarburgumGameState::WeatherOf(const UWorld* World)
{
    const AMarburgumGameState* State = World ? World->GetGameState<AMarburgumGameState>() : nullptr;
    return State ? &State->Weather : nullptr;
}

void AMarburgumGameState::SetWeather(const FMarburgumWeather& Next)
{
    check(HasAuthority());
    if (Next == Weather)
    {
        return;
    }
    Weather = Next;
    OnWeatherChanged().Broadcast(Weather);
}

AMarburgumGameState::FWeatherChanged& AMarburgumGameState::OnWeatherChanged()
{
    static FWeatherChanged Event;
    return Event;
}

void AMarburgumGameState::OnRep_Weather()
{
    OnWeatherChanged().Broadcast(Weather);
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
