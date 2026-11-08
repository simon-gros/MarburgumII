#include "MarburgumNetSmokeSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#define MB2_SMOKE_LOG(Format, ...) \
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client tag=%s " Format), MarburgumII::Network::SmokeLogPrefix, *Tag, ##__VA_ARGS__)

namespace
{
    // A value no client may ever impose; the server must refuse it.
    constexpr int32 UnauthorizedContractVersion = 999;
    constexpr double StandaloneSettleSeconds = 1.0;
    constexpr double LingerSeconds = 3.0;
    constexpr int32 HeartbeatAdvanceRequired = 2;

    const TCHAR* OutcomeName(EMarburgumRequestOutcome Outcome)
    {
        switch (Outcome)
        {
        case EMarburgumRequestOutcome::Applied: return TEXT("applied");
        case EMarburgumRequestOutcome::Duplicate: return TEXT("duplicate");
        case EMarburgumRequestOutcome::Rejected: return TEXT("rejected");
        }
        return TEXT("unknown");
    }
}

bool UMarburgumNetSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    return FParse::Param(FCommandLine::Get(), TEXT("MB2NetSmoke"));
}

void UMarburgumNetSmokeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    const TCHAR* CommandLine = FCommandLine::Get();
    FParse::Value(CommandLine, TEXT("MB2SmokeTag="), Tag);
    FParse::Value(CommandLine, TEXT("MB2SmokePeers="), RequiredPeers);
    FParse::Value(CommandLine, TEXT("MB2SmokeTimeout="), TimeoutSeconds);
    bReconnectRequested = FParse::Param(CommandLine, TEXT("MB2SmokeReconnect"));
    RequiredPeers = FMath::Max(RequiredPeers, 1);

    StartTime = FPlatformTime::Seconds();
    PhaseStartTime = StartTime;

    if (GEngine)
    {
        NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UMarburgumNetSmokeSubsystem::HandleNetworkFailure);
    }
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UMarburgumNetSmokeSubsystem::Tick), 0.1f);

    MB2_SMOKE_LOG("event=start peers=%d reconnect=%d timeout=%.0f", RequiredPeers, bReconnectRequested ? 1 : 0, TimeoutSeconds);
}

void UMarburgumNetSmokeSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    if (GEngine)
    {
        GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
    }
    if (AMarburgumPlayerController* Controller = BoundController.Get())
    {
        Controller->OnRequestResult.RemoveAll(this);
    }
    Super::Deinitialize();
}

bool UMarburgumNetSmokeSubsystem::Tick(float DeltaTime)
{
    if (Phase == EPhase::Done)
    {
        return true;
    }

    if (FPlatformTime::Seconds() - StartTime > TimeoutSeconds)
    {
        Fail(FString::Printf(TEXT("timeout phase=%d"), static_cast<int32>(Phase)));
        return true;
    }

    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    AMarburgumGameState* GameState = World ? World->GetGameState<AMarburgumGameState>() : nullptr;
    AMarburgumPlayerController* Controller = World ? Cast<AMarburgumPlayerController>(World->GetFirstPlayerController()) : nullptr;
    TickPhase(World, GameState, Controller);
    return true;
}

void UMarburgumNetSmokeSubsystem::TickPhase(UWorld* World, AMarburgumGameState* GameState, AMarburgumPlayerController* Controller)
{
    const double PhaseElapsed = FPlatformTime::Seconds() - PhaseStartTime;

    switch (Phase)
    {
    case EPhase::WaitForSession:
        if (!IsSessionReady(World, GameState, Controller))
        {
            return;
        }
        BindController(Controller);
        MB2_SMOKE_LOG("event=observed session=%d server=%s:%d contract=%d heartbeat=%d players=%d player=%d",
            bReconnected ? 2 : 1, *World->URL.Host, World->URL.Port, GameState->NetworkContractVersion,
            GameState->AuthorityHeartbeat, GameState->PlayerArray.Num(), Controller->PlayerState->GetPlayerId());

        if (GameState->NetworkContractVersion != MarburgumII::Network::ContractVersion)
        {
            Fail(FString::Printf(TEXT("contract_mismatch observed=%d expected=%d"),
                GameState->NetworkContractVersion, MarburgumII::Network::ContractVersion));
            return;
        }

        Results.Reset();
        if (!bReconnected)
        {
            ServerAddress = FString::Printf(TEXT("%s:%d"), *World->URL.Host, World->URL.Port);
            OperationId = FGuid::NewGuid();
            ExpectedResults = 3;
            // Reliable RPCs on one actor arrive in order: refused mutation, first submit, replayed submit.
            Controller->ServerRequestContractVersion(UnauthorizedContractVersion);
            Controller->ServerSubmitOperation(OperationId);
            Controller->ServerSubmitOperation(OperationId);
            MB2_SMOKE_LOG("event=requests_sent op=%s", *OperationId.ToString(EGuidFormats::Digits));
            EnterPhase(EPhase::AwaitRequestResults);
        }
        else
        {
            // The authoritative world must have continued, not restarted, while this client was away.
            if (GameState->AuthorityHeartbeat < LastObservedHeartbeat)
            {
                Fail(FString::Printf(TEXT("heartbeat_regressed observed=%d previous=%d"),
                    GameState->AuthorityHeartbeat, LastObservedHeartbeat));
                return;
            }
            ExpectedResults = 1;
            Controller->ServerSubmitOperation(OperationId);
            MB2_SMOKE_LOG("event=replay_sent op=%s", *OperationId.ToString(EGuidFormats::Digits));
            EnterPhase(EPhase::AwaitReplayResult);
        }
        return;

    case EPhase::AwaitRequestResults:
        if (Results.Num() < ExpectedResults)
        {
            return;
        }
        MB2_SMOKE_LOG("event=request_results contract_request=%s first_submit=%s replayed_submit=%s contract=%d",
            OutcomeName(Results[0]), OutcomeName(Results[1]), OutcomeName(Results[2]),
            GameState ? GameState->NetworkContractVersion : -1);
        if (Results[0] != EMarburgumRequestOutcome::Rejected
            || Results[1] != EMarburgumRequestOutcome::Applied
            || Results[2] != EMarburgumRequestOutcome::Duplicate)
        {
            Fail(TEXT("unexpected_request_outcomes"));
            return;
        }
        if (!GameState || GameState->NetworkContractVersion != MarburgumII::Network::ContractVersion)
        {
            Fail(TEXT("contract_changed_by_client_request"));
            return;
        }
        EnterPhase(EPhase::WaitForPeers);
        return;

    case EPhase::WaitForPeers:
        if (!GameState || GameState->PlayerArray.Num() < RequiredPeers)
        {
            return;
        }
        HeartbeatBaseline = GameState->AuthorityHeartbeat;
        MB2_SMOKE_LOG("event=peers players=%d required=%d heartbeat=%d", GameState->PlayerArray.Num(), RequiredPeers, HeartbeatBaseline);
        EnterPhase(EPhase::WaitForHeartbeat);
        return;

    case EPhase::WaitForHeartbeat:
        if (!GameState || GameState->AuthorityHeartbeat < HeartbeatBaseline + HeartbeatAdvanceRequired)
        {
            return;
        }
        LastObservedHeartbeat = GameState->AuthorityHeartbeat;
        MB2_SMOKE_LOG("event=replicated_state heartbeat=%d baseline=%d applied=%d rejected=%d",
            GameState->AuthorityHeartbeat, HeartbeatBaseline, GameState->AppliedRequestCount, GameState->RejectedRequestCount);
        // Replicated counters show the server, not the client, committed the outcomes.
        if (GameState->AppliedRequestCount < 1 || GameState->RejectedRequestCount < 1)
        {
            Fail(TEXT("server_counters_not_replicated"));
            return;
        }
        if (bReconnectRequested && !bReconnected)
        {
            MB2_SMOKE_LOG("event=disconnect heartbeat=%d", LastObservedHeartbeat);
            GEngine->Exec(World, TEXT("disconnect"));
            EnterPhase(EPhase::WaitForStandalone);
            return;
        }
        Pass(GameState);
        return;

    case EPhase::WaitForStandalone:
        if (!World || World->GetNetMode() == NM_Client || PhaseElapsed < StandaloneSettleSeconds)
        {
            return;
        }
        bReconnected = true;
        MB2_SMOKE_LOG("event=reconnect server=%s", *ServerAddress);
        GEngine->SetClientTravel(World, *ServerAddress, TRAVEL_Absolute);
        EnterPhase(EPhase::WaitForSession);
        return;

    case EPhase::AwaitReplayResult:
        if (Results.Num() < ExpectedResults)
        {
            return;
        }
        MB2_SMOKE_LOG("event=replay_result outcome=%s", OutcomeName(Results[0]));
        if (Results[0] != EMarburgumRequestOutcome::Duplicate)
        {
            Fail(TEXT("operation_reapplied_after_reconnect"));
            return;
        }
        Pass(GameState);
        return;

    case EPhase::Linger:
        // Stay connected briefly so concurrent peers can observe this client's PlayerState.
        if (PhaseElapsed >= LingerSeconds)
        {
            Finish(0);
        }
        return;

    case EPhase::Done:
        return;
    }
}

bool UMarburgumNetSmokeSubsystem::IsSessionReady(const UWorld* World, const AMarburgumGameState* GameState, const AMarburgumPlayerController* Controller) const
{
    return World
        && World->GetNetMode() == NM_Client
        && GameState
        && GameState->NetworkContractVersion != 0
        && GameState->AuthorityHeartbeat > 0
        && Controller
        && Controller->PlayerState;
}

void UMarburgumNetSmokeSubsystem::BindController(AMarburgumPlayerController* Controller)
{
    if (BoundController.Get() == Controller)
    {
        return;
    }
    if (AMarburgumPlayerController* Previous = BoundController.Get())
    {
        Previous->OnRequestResult.RemoveAll(this);
    }
    Controller->OnRequestResult.AddUObject(this, &UMarburgumNetSmokeSubsystem::HandleRequestResult);
    BoundController = Controller;
}

void UMarburgumNetSmokeSubsystem::HandleRequestResult(const FGuid& ResultOperationId, EMarburgumRequestOutcome Outcome)
{
    MB2_SMOKE_LOG("event=result op=%s outcome=%s", *ResultOperationId.ToString(EGuidFormats::Digits), OutcomeName(Outcome));
    Results.Add(Outcome);
}

void UMarburgumNetSmokeSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
    MB2_SMOKE_LOG("event=network_failure type=%s error=\"%s\"", ENetworkFailure::ToString(FailureType), *ErrorString);
    if (Phase != EPhase::WaitForStandalone && Phase != EPhase::Linger && Phase != EPhase::Done)
    {
        Fail(TEXT("network_failure"));
    }
}

void UMarburgumNetSmokeSubsystem::EnterPhase(EPhase NewPhase)
{
    Phase = NewPhase;
    PhaseStartTime = FPlatformTime::Seconds();
}

void UMarburgumNetSmokeSubsystem::Pass(const AMarburgumGameState* GameState)
{
    MB2_SMOKE_LOG("event=pass reconnected=%d heartbeat=%d players=%d applied=%d rejected=%d elapsed=%.1f",
        bReconnected ? 1 : 0,
        GameState ? GameState->AuthorityHeartbeat : -1,
        GameState ? GameState->PlayerArray.Num() : -1,
        GameState ? GameState->AppliedRequestCount : -1,
        GameState ? GameState->RejectedRequestCount : -1,
        FPlatformTime::Seconds() - StartTime);
    EnterPhase(EPhase::Linger);
}

void UMarburgumNetSmokeSubsystem::Fail(const FString& Reason)
{
    if (bFailed)
    {
        return;
    }
    bFailed = true;
    MB2_SMOKE_LOG("event=fail reason=%s", *Reason);
    Finish(1);
}

void UMarburgumNetSmokeSubsystem::Finish(uint8 ExitCode)
{
    Phase = EPhase::Done;
    FPlatformMisc::RequestExitWithStatus(false, ExitCode);
}

#undef MB2_SMOKE_LOG
