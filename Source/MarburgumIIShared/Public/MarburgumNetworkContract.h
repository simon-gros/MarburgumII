#pragma once

#include "CoreMinimal.h"

// New MarburgumII semantic contract; NOT compatible with the legacy TCP JSON protocol v3.
// Gameplay replication is Unreal-native until an explicit, tested architecture decision changes it.
namespace MarburgumII::Network
{
    inline constexpr int32 ContractVersion = 1;
}
