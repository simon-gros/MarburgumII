#pragma once

#include "CoreMinimal.h"

// Client connection lifecycle vocabulary (ontology signal `connection_phase`, authority `client_observed`).
// Engine-neutral so the client UI, smoke drivers and tests share one definition. Server code never
// reads it: these phases describe what the client has observed, not what the server decided.
namespace MarburgumII::Session
{
    enum class EPhase : uint8
    {
        Starting,     // Process boot; no front-end presented yet.
        Idle,         // Front-end shown, no connection attempt in progress.
        Connecting,   // Pending net game exists; handshake not yet welcomed by the server.
        Joining,      // Server welcomed the client and named the map; map load not finished.
        LoadingWorld, // Server map loaded locally; replicated session state not yet complete.
        Ready,        // Verified admission: live server connection and replicated GameState/PlayerState.
        Reconnecting, // A ready session was lost unexpectedly; automatic recovery in progress.
        Error         // Terminal failure for this attempt; waits for the player to retry or edit.
    };

    enum class EFault : uint8
    {
        None,
        InvalidAddress,
        Timeout,
        Refused,
        ContractMismatch,
        ConnectionLost,
        TravelFailed
    };

    // A connection attempt that has not reached Ready in this time is abandoned as Timeout.
    inline constexpr double ConnectTimeoutSeconds = 20.0;

    // Automatic recovery after an unexpected loss of a Ready session; attempt n waits n * delay.
    inline constexpr int32 MaxReconnectAttempts = 3;
    inline constexpr double ReconnectDelaySeconds = 2.0;

    inline const TCHAR* ToString(EPhase Phase)
    {
        switch (Phase)
        {
        case EPhase::Starting: return TEXT("starting");
        case EPhase::Idle: return TEXT("idle");
        case EPhase::Connecting: return TEXT("connecting");
        case EPhase::Joining: return TEXT("joining");
        case EPhase::LoadingWorld: return TEXT("loading_world");
        case EPhase::Ready: return TEXT("ready");
        case EPhase::Reconnecting: return TEXT("reconnecting");
        case EPhase::Error: return TEXT("error");
        }
        return TEXT("unknown");
    }

    inline const TCHAR* ToString(EFault Fault)
    {
        switch (Fault)
        {
        case EFault::None: return TEXT("none");
        case EFault::InvalidAddress: return TEXT("invalid_address");
        case EFault::Timeout: return TEXT("timeout");
        case EFault::Refused: return TEXT("refused");
        case EFault::ContractMismatch: return TEXT("contract_mismatch");
        case EFault::ConnectionLost: return TEXT("connection_lost");
        case EFault::TravelFailed: return TEXT("travel_failed");
        }
        return TEXT("unknown");
    }

    // Phases in which a connection attempt is in flight and can be cancelled.
    inline bool IsAttemptInFlight(EPhase Phase)
    {
        return Phase == EPhase::Connecting || Phase == EPhase::Joining
            || Phase == EPhase::LoadingWorld || Phase == EPhase::Reconnecting;
    }

    // Prefix for machine-parsed client UI/session evidence lines.
    inline constexpr const TCHAR* UiLogPrefix = TEXT("MB2UI");
}
