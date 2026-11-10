#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/EngineBaseTypes.h"
#include "MarburgumSessionContract.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarburgumSessionSubsystem.generated.h"

class AMarburgumGameState;
class APlayerController;
class UNetDriver;

/** Immutable snapshot of the client connection lifecycle, consumed by the UI (signal `connection_phase`). */
struct FMarburgumSessionView
{
    MarburgumII::Session::EPhase Phase = MarburgumII::Session::EPhase::Starting;
    MarburgumII::Session::EFault Fault = MarburgumII::Session::EFault::None;
    FString Address;
    /** Engine/server-provided failure detail; diagnostic, may be empty. */
    FString Detail;
    int32 ReconnectAttempt = 0;
    double PhaseStartedSeconds = 0.0;
    /** Monotonic id incremented on every change, for deduplicating presentation work. */
    int32 Revision = 0;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FMarburgumSessionChanged, const FMarburgumSessionView&);

/**
 * Client connection lifecycle: connect, observe handshake/join/world load, verify admission, recover
 * from unexpected loss and report truthful failures.
 *
 * "Ready" is never inferred from time or a finished animation: it requires a live server connection,
 * a replicated AMarburgumGameState this process has no authority over, a matching network contract and
 * a replicated PlayerState. Disabled under -MB2NetSmoke, whose driver owns travel itself.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumSessionSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /** Start a connection attempt to host[:port]. Ignored while an attempt is in flight. */
    void Connect(const FString& Address);
    /** Cancel an in-flight attempt or recovery and return to the front-end. */
    void Cancel();
    /** Retry the last address after an error. */
    void Retry();
    /** Deliberate player disconnect from a ready session (no automatic recovery). */
    void Disconnect();

    const FMarburgumSessionView& GetView() const { return View; }
    FString GetLastAddress() const { return LastAddress; }
    FMarburgumSessionChanged& OnSessionChanged() { return SessionChanged; }

    /** The default address shown on the front-end: -MB2Server=, else 127.0.0.1:7777. */
    static FString DefaultAddress();

    /**
     * Development character key sent with every join from this client (ADR-014): the key in a
     * command-line server URL if the session started from one, else -MB2Character=, else a key generated
     * once and kept in Saved/MarburgumII/DevCharacter.txt. Not a credential.
     */
    const FString& GetCharacterKey() const { return CharacterKey; }

    /** True when the world is the client's verified, admitted server session. */
    static bool IsAdmitted(const UWorld* World);

private:
    bool Tick(float DeltaTime);
    void ObserveWorld(UWorld* World);
    void BeginAttempt(const FString& Address);
    void AbandonAttempt();
    void Fail(MarburgumII::Session::EFault Fault, const FString& Detail);
    void SetPhase(MarburgumII::Session::EPhase Phase, MarburgumII::Session::EFault Fault = MarburgumII::Session::EFault::None, const FString& Detail = FString());

    void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
    void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);

    FTSTicker::FDelegateHandle TickerHandle;
    FDelegateHandle NetworkFailureHandle;
    FDelegateHandle TravelFailureHandle;
    FMarburgumSessionChanged SessionChanged;

    static FString ResolveDefaultCharacterKey();

    FMarburgumSessionView View;
    FString LastAddress;
    FString CharacterKey;
    double AttemptStartedSeconds = 0.0;
    double NextReconnectSeconds = 0.0;
    bool bReconnectScheduled = false;
};
