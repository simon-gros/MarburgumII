#pragma once

#include "CoreMinimal.h"

// New MarburgumII semantic contract; NOT compatible with the legacy TCP JSON protocol v3.
// Gameplay replication is Unreal-native until an explicit, tested architecture decision changes it.
namespace MarburgumII::Network
{
    inline constexpr int32 ContractVersion = 1;

    // Seconds between authoritative heartbeat increments on the server-owned GameState.
    inline constexpr float AuthorityHeartbeatIntervalSeconds = 1.0f;

    // Prefix for machine-parsed network smoke evidence lines (scripts/net-smoke-linux.sh).
    inline constexpr const TCHAR* SmokeLogPrefix = TEXT("MB2SMOKE");
}

// Shared by core, client and server modules so smoke evidence comes from one log category.
MARBURGUMIISHARED_API DECLARE_LOG_CATEGORY_EXTERN(LogMarburgumNet, Log, All);
