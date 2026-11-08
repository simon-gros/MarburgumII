#include "Session/MarburgumSessionSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/PendingNetGame.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformTime.h"
#include "MarburgumGameState.h"
#include "MarburgumNetworkContract.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

using namespace MarburgumII::Session;

namespace
{
    constexpr float TickIntervalSeconds = 0.1f;

    // host or IPv4, optional :port. Options, map paths and whitespace are not accepted from the UI.
    bool IsValidServerAddress(const FString& Address)
    {
        if (Address.IsEmpty() || Address.Len() > 253)
        {
            return false;
        }
        FString Host = Address;
        FString PortText;
        if (Address.Split(TEXT(":"), &Host, &PortText, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
        {
            if (PortText.IsEmpty() || !PortText.IsNumeric())
            {
                return false;
            }
            const int32 Port = FCString::Atoi(*PortText);
            if (Port < 1 || Port > 65535)
            {
                return false;
            }
        }
        if (Host.IsEmpty())
        {
            return false;
        }
        for (const TCHAR Char : Host)
        {
            if (!FChar::IsAlnum(Char) && Char != TEXT('.') && Char != TEXT('-'))
            {
                return false;
            }
        }
        return true;
    }

    EFault FaultForNetworkFailure(ENetworkFailure::Type FailureType)
    {
        switch (FailureType)
        {
        case ENetworkFailure::PendingConnectionFailure: return EFault::Refused;
        case ENetworkFailure::OutdatedClient:
        case ENetworkFailure::OutdatedServer: return EFault::ContractMismatch;
        case ENetworkFailure::ConnectionTimeout: return EFault::Timeout;
        default: return EFault::ConnectionLost;
        }
    }
}

bool UMarburgumSessionSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
    if (!GameInstance || GameInstance->IsDedicatedServerInstance())
    {
        return false;
    }
    return !FParse::Param(FCommandLine::Get(), TEXT("MB2NetSmoke"));
}

void UMarburgumSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    View.PhaseStartedSeconds = FPlatformTime::Seconds();
    if (GEngine)
    {
        NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UMarburgumSessionSubsystem::HandleNetworkFailure);
        TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UMarburgumSessionSubsystem::HandleTravelFailure);
    }
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UMarburgumSessionSubsystem::Tick), TickIntervalSeconds);
}

void UMarburgumSessionSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    if (GEngine)
    {
        GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
        GEngine->OnTravelFailure().Remove(TravelFailureHandle);
    }
    Super::Deinitialize();
}

FString UMarburgumSessionSubsystem::DefaultAddress()
{
    FString Address;
    if (FParse::Value(FCommandLine::Get(), TEXT("MB2Server="), Address) && !Address.IsEmpty())
    {
        return Address;
    }
    return TEXT("127.0.0.1:7777");
}

bool UMarburgumSessionSubsystem::IsAdmitted(const UWorld* World)
{
    // A pending reconnect also reports NM_Client for the local standalone world, so require a live
    // server connection, a GameState this process has no authority over and a replicated PlayerState.
    if (!World || World->GetNetMode() != NM_Client)
    {
        return false;
    }
    const UNetDriver* NetDriver = World->GetNetDriver();
    if (!NetDriver || !NetDriver->ServerConnection || NetDriver->ServerConnection->GetConnectionState() != USOCK_Open)
    {
        return false;
    }
    const AMarburgumGameState* GameState = World->GetGameState<AMarburgumGameState>();
    const APlayerController* Controller = World->GetFirstPlayerController();
    return GameState
        && GameState->GetLocalRole() != ROLE_Authority
        && GameState->NetworkContractVersion != 0
        && GameState->AuthorityHeartbeat > 0
        && Controller
        && Controller->PlayerState;
}

void UMarburgumSessionSubsystem::Connect(const FString& Address)
{
    if (IsAttemptInFlight(View.Phase) || View.Phase == EPhase::Ready)
    {
        return;
    }
    const FString Trimmed = Address.TrimStartAndEnd();
    LastAddress = Trimmed;
    View.ReconnectAttempt = 0;
    if (!IsValidServerAddress(Trimmed))
    {
        Fail(EFault::InvalidAddress, Trimmed);
        return;
    }
    SetPhase(EPhase::Connecting);
    BeginAttempt(Trimmed);
}

void UMarburgumSessionSubsystem::Retry()
{
    if (View.Phase == EPhase::Error && !LastAddress.IsEmpty())
    {
        Connect(LastAddress);
    }
}

void UMarburgumSessionSubsystem::Cancel()
{
    if (!IsAttemptInFlight(View.Phase) && View.Phase != EPhase::Error)
    {
        return;
    }
    bReconnectScheduled = false;
    View.ReconnectAttempt = 0;
    AbandonAttempt();
    SetPhase(EPhase::Idle);
}

void UMarburgumSessionSubsystem::Disconnect()
{
    if (View.Phase != EPhase::Ready)
    {
        return;
    }
    // Leave Ready first so the session loss below is not treated as an unexpected drop.
    SetPhase(EPhase::Idle);
    AbandonAttempt();
}

void UMarburgumSessionSubsystem::BeginAttempt(const FString& Address)
{
    UGameInstance* GameInstance = GetGameInstance();
    UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
    if (!World || !GEngine)
    {
        Fail(EFault::TravelFailed, TEXT("no world"));
        return;
    }
    AttemptStartedSeconds = FPlatformTime::Seconds();
    bReconnectScheduled = false;
    GEngine->SetClientTravel(World, *Address, TRAVEL_Absolute);
}

void UMarburgumSessionSubsystem::AbandonAttempt()
{
    UGameInstance* GameInstance = GetGameInstance();
    UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
    if (!World || !GEngine)
    {
        return;
    }
    if (FWorldContext* Context = GameInstance->GetWorldContext(); Context && Context->PendingNetGame)
    {
        GEngine->CancelPending(World);
    }
    // Any world that is not the local front-end goes back to the default (front-end) map.
    if (World->GetNetMode() == NM_Client)
    {
        GEngine->SetClientTravel(World, TEXT("?closed"), TRAVEL_Absolute);
    }
}

void UMarburgumSessionSubsystem::Fail(EFault Fault, const FString& Detail)
{
    bReconnectScheduled = false;
    SetPhase(EPhase::Error, Fault, Detail);
}

bool UMarburgumSessionSubsystem::Tick(float DeltaTime)
{
    UGameInstance* GameInstance = GetGameInstance();
    ObserveWorld(GameInstance ? GameInstance->GetWorld() : nullptr);
    return true;
}

void UMarburgumSessionSubsystem::ObserveWorld(UWorld* World)
{
    const double Now = FPlatformTime::Seconds();
    const FWorldContext* Context = GetGameInstance() ? GetGameInstance()->GetWorldContext() : nullptr;
    const UPendingNetGame* Pending = Context ? Context->PendingNetGame.Get() : nullptr;

    switch (View.Phase)
    {
    case EPhase::Starting:
        // A server address on the command line makes the engine connect before any UI exists.
        if (Pending)
        {
            LastAddress = Pending->URL.GetHostPortString();
            AttemptStartedSeconds = Now;
            SetPhase(EPhase::Connecting);
        }
        else if (World && World->GetNetMode() == NM_Client)
        {
            LastAddress = World->URL.GetHostPortString();
            AttemptStartedSeconds = Now;
            SetPhase(EPhase::LoadingWorld);
        }
        else if (World && World->HasBegunPlay())
        {
            SetPhase(EPhase::Idle);
        }
        return;

    case EPhase::Idle:
    case EPhase::Error:
        return;

    case EPhase::Ready:
        if (!IsAdmitted(World))
        {
            // Lost without a network-failure callback (e.g. the server closed the connection cleanly).
            HandleNetworkFailure(World, nullptr, ENetworkFailure::ConnectionLost, TEXT("session no longer admitted"));
        }
        return;

    case EPhase::Connecting:
    case EPhase::Joining:
    case EPhase::LoadingWorld:
    case EPhase::Reconnecting:
        break;
    }

    if (bReconnectScheduled)
    {
        // Wait on the front-end until the backoff elapses and no old travel is still pending.
        if (Now >= NextReconnectSeconds && !Pending && (!World || World->GetNetMode() != NM_Client))
        {
            BeginAttempt(LastAddress);
        }
        return;
    }

    if (IsAdmitted(World))
    {
        const AMarburgumGameState* GameState = World->GetGameState<AMarburgumGameState>();
        if (GameState->NetworkContractVersion != MarburgumII::Network::ContractVersion)
        {
            AbandonAttempt();
            Fail(EFault::ContractMismatch, FString::Printf(TEXT("server contract %d, client contract %d"),
                GameState->NetworkContractVersion, MarburgumII::Network::ContractVersion));
            return;
        }
        View.ReconnectAttempt = 0;
        SetPhase(EPhase::Ready);
        return;
    }

    if (Now - AttemptStartedSeconds > ConnectTimeoutSeconds)
    {
        AbandonAttempt();
        if (View.Phase == EPhase::Reconnecting)
        {
            HandleNetworkFailure(World, nullptr, ENetworkFailure::ConnectionTimeout, TEXT("reconnect attempt timed out"));
        }
        else
        {
            Fail(EFault::Timeout, FString::Printf(TEXT("no admission within %.0f s"), ConnectTimeoutSeconds));
        }
        return;
    }

    if (View.Phase == EPhase::Reconnecting)
    {
        return; // Sub-phases of a recovery attempt are not surfaced separately.
    }
    if (Pending)
    {
        SetPhase(Pending->bSuccessfullyConnected ? EPhase::Joining : EPhase::Connecting);
    }
    else if (World && World->GetNetMode() == NM_Client)
    {
        SetPhase(EPhase::LoadingWorld);
    }
}

void UMarburgumSessionSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=network_failure phase=%s type=%s error=\"%s\""),
        UiLogPrefix, ToString(View.Phase), ENetworkFailure::ToString(FailureType), *ErrorString);

    switch (View.Phase)
    {
    case EPhase::Ready:
    case EPhase::Reconnecting:
        if (View.Phase == EPhase::Reconnecting && bReconnectScheduled)
        {
            return; // Already waiting to retry; late callbacks from the dropped connection add nothing.
        }
        if (View.ReconnectAttempt >= MaxReconnectAttempts)
        {
            Fail(EFault::ConnectionLost, ErrorString);
            return;
        }
        ++View.ReconnectAttempt;
        bReconnectScheduled = true;
        // Linear backoff gives a restarting server a few seconds before attempts run out.
        NextReconnectSeconds = FPlatformTime::Seconds() + ReconnectDelaySeconds * View.ReconnectAttempt;
        SetPhase(EPhase::Reconnecting, EFault::ConnectionLost, ErrorString);
        return;

    case EPhase::Connecting:
    case EPhase::Joining:
    case EPhase::LoadingWorld:
        Fail(FaultForNetworkFailure(FailureType), ErrorString);
        return;

    default:
        return;
    }
}

void UMarburgumSessionSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=travel_failure phase=%s type=%s error=\"%s\""),
        UiLogPrefix, ToString(View.Phase), ETravelFailure::ToString(FailureType), *ErrorString);

    if (View.Phase == EPhase::Reconnecting)
    {
        HandleNetworkFailure(World, nullptr, ENetworkFailure::ConnectionLost, ErrorString);
    }
    else if (View.Phase == EPhase::Connecting || View.Phase == EPhase::Joining || View.Phase == EPhase::LoadingWorld)
    {
        Fail(EFault::TravelFailed, ErrorString);
    }
}

void UMarburgumSessionSubsystem::SetPhase(EPhase Phase, EFault Fault, const FString& Detail)
{
    const bool bChanged = View.Phase != Phase || View.Fault != Fault || View.Detail != Detail;
    if (!bChanged)
    {
        return;
    }
    if (View.Phase != Phase)
    {
        View.PhaseStartedSeconds = FPlatformTime::Seconds();
    }
    View.Phase = Phase;
    View.Fault = Fault;
    View.Detail = Detail;
    View.Address = LastAddress;
    ++View.Revision;

    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=session phase=%s fault=%s address=%s attempt=%d detail=\"%s\""),
        UiLogPrefix, ToString(Phase), ToString(Fault), *LastAddress, View.ReconnectAttempt, *Detail);
    SessionChanged.Broadcast(View);
}
