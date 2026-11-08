#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/EngineBaseTypes.h"
#include "MarburgumGameState.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarburgumNetSmokeSubsystem.generated.h"

class AMarburgumPlayerController;

/**
 * Client-side network smoke driver, active only with -MB2NetSmoke.
 *
 * Joins a dedicated server, verifies replicated server-owned state, proves that client requests
 * are decided by the server (refused mutation, idempotent operation), optionally disconnects and
 * reconnects, then exits with status 0 (pass) or 1 (fail). Emits MB2SMOKE evidence lines.
 *
 * Options: -MB2SmokeTag=<name> -MB2SmokePeers=<n> -MB2SmokeReconnect -MB2SmokeTimeout=<seconds>
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumNetSmokeSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    enum class EPhase : uint8
    {
        WaitForSession,
        AwaitRequestResults,
        WaitForPeers,
        WaitForHeartbeat,
        WaitForStandalone,
        WaitForReconnect,
        AwaitReplayResult,
        Linger,
        Done
    };

    bool Tick(float DeltaTime);
    void TickPhase(UWorld* World, AMarburgumGameState* GameState, AMarburgumPlayerController* Controller);

    bool IsSessionReady(const UWorld* World, const AMarburgumGameState* GameState, const AMarburgumPlayerController* Controller) const;
    void BindController(AMarburgumPlayerController* Controller);
    void HandleRequestResult(const FGuid& OperationId, EMarburgumRequestOutcome Outcome);
    void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);

    void EnterPhase(EPhase NewPhase);
    void Pass(const AMarburgumGameState* GameState);
    void Fail(const FString& Reason);
    void Finish(uint8 ExitCode);

    FTSTicker::FDelegateHandle TickerHandle;
    FDelegateHandle NetworkFailureHandle;
    TWeakObjectPtr<AMarburgumPlayerController> BoundController;

    FString Tag = TEXT("client");
    int32 RequiredPeers = 1;
    bool bReconnectRequested = false;
    double TimeoutSeconds = 90.0;

    EPhase Phase = EPhase::WaitForSession;
    double StartTime = 0.0;
    double PhaseStartTime = 0.0;
    bool bReconnected = false;
    FString ServerAddress;

    FGuid OperationId;
    TArray<EMarburgumRequestOutcome> Results;
    int32 ExpectedResults = 0;
    int32 HeartbeatBaseline = 0;
    int32 LastObservedHeartbeat = 0;
    bool bFailed = false;
};
