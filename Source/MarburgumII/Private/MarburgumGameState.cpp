#include "MarburgumGameState.h"

#include "MarburgumNetworkContract.h"
#include "Net/UnrealNetwork.h"

AMarburgumGameState::AMarburgumGameState()
{
    bReplicates = true;
    NetworkContractVersion = MarburgumII::Network::ContractVersion;
}

void AMarburgumGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMarburgumGameState, NetworkContractVersion);
}
