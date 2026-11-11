#include "MarburgumPlayerController.h"

#include "MarburgumAuthorityService.h"
#include "MarburgumClientUiHooks.h"
#include "MarburgumChatPolicy.h"
#include "MarburgumCombatContract.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumCharacter.h"
#include "MarburgumInteractable.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumVehicle.h"
#include "MarburgumVehicleContract.h"
#include "MarburgumVehicleDynamics.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformTime.h"
#include "GameFramework/PlayerState.h"
#include "InputCoreTypes.h"

namespace
{
    constexpr int32 MaxLocalMessages = 80;
    constexpr double MinimumSpacingSeconds = 0.8;
    constexpr double WindowSeconds = 10.0;
    constexpr int32 MaxMessagesPerWindow = 8;

    const TCHAR* OutcomeName(EMarburgumRequestOutcome Outcome)
    {
        return MarburgumII::Gameplay::ToString(Outcome);
    }
}

void AMarburgumPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (IsLocalController())
    {
        FMarburgumClientUiHooks::OnLocalPlayerReady().Broadcast(this);
    }
}

void AMarburgumPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    if (InputComponent)
    {
        InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AMarburgumPlayerController::RequestChatFocus);
        InputComponent->BindKey(EKeys::E, IE_Pressed, this, &AMarburgumPlayerController::RequestInteract);
        InputComponent->BindKey(EKeys::I, IE_Pressed, this, &AMarburgumPlayerController::RequestToggleInventory);
        InputComponent->BindKey(EKeys::J, IE_Pressed, this, &AMarburgumPlayerController::RequestToggleJournal);
        InputComponent->BindKey(EKeys::C, IE_Pressed, this, &AMarburgumPlayerController::RequestToggleCharacter);
    }
}

void AMarburgumPlayerController::RequestToggleInventory()
{
    if (IsLocalController())
    {
        FMarburgumClientUiHooks::OnInventoryToggleRequested().Broadcast(this);
    }
}

void AMarburgumPlayerController::RequestToggleJournal()
{
    if (IsLocalController())
    {
        FMarburgumClientUiHooks::OnJournalToggleRequested().Broadcast(this);
    }
}

void AMarburgumPlayerController::RequestToggleCharacter()
{
    if (IsLocalController())
    {
        FMarburgumClientUiHooks::OnCharacterToggleRequested().Broadcast(this);
    }
}

AMarburgumInteractable* AMarburgumPlayerController::FindInteractionCandidate() const
{
    const APawn* Body = GetPawn();
    UWorld* World = GetWorld();
    if (!Body || !World)
    {
        return nullptr;
    }
    // A handful of placed objects per map; spatial queries arrive with the city (Stage 3).
    AMarburgumInteractable* Best = nullptr;
    double BestDistanceSquared = TNumericLimits<double>::Max();
    for (TActorIterator<AMarburgumInteractable> It(World); It; ++It)
    {
        const double DistanceSquared = FVector::DistSquared(Body->GetActorLocation(), It->GetActorLocation());
        if (DistanceSquared <= FMath::Square(It->InteractionRange) && DistanceSquared < BestDistanceSquared)
        {
            Best = *It;
            BestDistanceSquared = DistanceSquared;
        }
    }
    return Best;
}

void AMarburgumPlayerController::RequestInteract()
{
    FGuid Pending;
    TWeakObjectPtr<AMarburgumInteractable> PendingObject;
    if (!IsLocalController() || GetPendingInteraction(Pending, PendingObject) || IsVehicleRequestPending())
    {
        return; // One request in flight at a time; the prompt shows it as waiting.
    }
    // The same key gets in and out of cars, and a car in reach comes first (the original's
    // toggle_vehicle_or_interact).
    const AMarburgumCharacter* Body = Cast<AMarburgumCharacter>(GetPawn());
    if (Body && Body->GetSeatedVehicle())
    {
        RequestExitVehicle();
        return;
    }
    FName Seat;
    if (AMarburgumVehicle* Car = FindVehicleCandidate(Seat))
    {
        RequestEnterVehicle(Car, Seat);
        return;
    }
    if (AMarburgumInteractable* Target = FindInteractionCandidate())
    {
        SubmitInteraction(FGuid::NewGuid(), Target);
    }
}

void AMarburgumPlayerController::SubmitInteraction(const FGuid& OperationId, AMarburgumInteractable* Target)
{
    if (!IsLocalController())
    {
        return;
    }
    PendingOperationId = OperationId;
    PendingTarget = Target;
    PendingSinceSeconds = FPlatformTime::Seconds();
    ServerRequestInteraction(OperationId, Target);
}

bool AMarburgumPlayerController::GetPendingInteraction(FGuid& OutOperationId, TWeakObjectPtr<AMarburgumInteractable>& OutTarget) const
{
    if (!PendingOperationId.IsValid()
        || FPlatformTime::Seconds() - PendingSinceSeconds > MarburgumII::Gameplay::ReceiptTimeoutSeconds)
    {
        return false;
    }
    OutOperationId = PendingOperationId;
    OutTarget = PendingTarget;
    return true;
}

void AMarburgumPlayerController::ServerRequestInteraction_Implementation(FGuid OperationId, AMarburgumInteractable* Target)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }
    if (!AdmitRequest(TEXT("interaction")))
    {
        return;
    }

    FMarburgumInteractionReceipt Receipt;
    if (UMarburgumAuthorityService* Authority = UMarburgumAuthorityService::Get(World))
    {
        Receipt = Authority->HandleInteraction(*this, OperationId, Target);
    }
    else
    {
        Receipt.OperationId = OperationId;
        Receipt.Outcome = EMarburgumRequestOutcome::Rejected;
        Receipt.Reason = EMarburgumInteractionReason::AuthorityUnavailable;
    }
    ClientReceiveInteractionReceipt(Receipt);
}

bool AMarburgumPlayerController::AdmitRequest(const TCHAR* What)
{
    // Bounded request rate: floods are dropped without a reliable reply for each one.
    const UWorld* World = GetWorld();
    const double Now = World ? World->GetTimeSeconds() : 0.0;
    if (Now - RequestWindowStartSeconds >= 1.0)
    {
        RequestWindowStartSeconds = Now;
        RequestsInWindow = 0;
    }
    if (++RequestsInWindow > MarburgumII::Gameplay::MaxRequestsPerSecond)
    {
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=%s_dropped player=%d reason=rate"),
            MarburgumII::Gameplay::InteractLogPrefix, What, PlayerState ? PlayerState->GetPlayerId() : -1);
        return false;
    }
    return true;
}

// ---- Vehicles (ADR-039) ----

AMarburgumVehicle* AMarburgumPlayerController::FindVehicleCandidate(FName& OutSeat) const
{
    OutSeat = NAME_None;
    const AMarburgumCharacter* Body = Cast<AMarburgumCharacter>(GetPawn());
    if (!Body || Body->GetSeatedVehicle())
    {
        return nullptr;
    }
    TArray<AMarburgumVehicle*> Cars;
    AMarburgumVehicle::GetAll(GetWorld(), Cars);
    AMarburgumVehicle* Best = nullptr;
    double BestDistance = TNumericLimits<double>::Max();
    for (AMarburgumVehicle* Car : Cars)
    {
        const FName Seat = Car->FindReachableSeat(Body->GetActorLocation());
        if (Seat.IsNone())
        {
            continue;
        }
        const double Distance = FVector::DistSquared(Car->GetDoorLocation(Seat), Body->GetActorLocation());
        if (Distance < BestDistance)
        {
            BestDistance = Distance;
            Best = Car;
            OutSeat = Seat;
        }
    }
    return Best;
}

bool AMarburgumPlayerController::IsVehicleRequestPending() const
{
    return FPlatformTime::Seconds() - VehicleRequestAt <= MarburgumII::Gameplay::ReceiptTimeoutSeconds;
}

void AMarburgumPlayerController::RequestEnterVehicle(AMarburgumVehicle* Vehicle, FName SeatId)
{
    if (IsLocalController() && Vehicle)
    {
        VehicleRequestAt = FPlatformTime::Seconds();
        ServerEnterVehicle(Vehicle, SeatId);
    }
}

void AMarburgumPlayerController::RequestExitVehicle()
{
    if (IsLocalController())
    {
        VehicleRequestAt = FPlatformTime::Seconds();
        ServerExitVehicle();
    }
}

void AMarburgumPlayerController::ServerEnterVehicle_Implementation(AMarburgumVehicle* Vehicle, FName SeatId)
{
    if (!AdmitRequest(TEXT("vehicle")))
    {
        return;
    }
    AMarburgumCharacter* Body = Cast<AMarburgumCharacter>(GetPawn());
    FName Reason = TEXT("no_vehicle");
    const bool bOk = Body && Vehicle && Vehicle->TryEnterAuthority(*Body, SeatId, Reason);
    if (!Body || !Vehicle)
    {
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=enter_refused player=%d reason=%s"), MarburgumII::Vehicles::LogPrefix,
            PlayerState ? PlayerState->GetPlayerId() : -1, Body ? TEXT("no_vehicle") : TEXT("no_character"));
        Reason = Body ? Reason : FName(TEXT("no_character"));
    }
    ClientReceiveVehicleResult(bOk, TEXT("enter"), bOk ? NAME_None : Reason);
}

void AMarburgumPlayerController::ServerExitVehicle_Implementation()
{
    if (!AdmitRequest(TEXT("vehicle")))
    {
        return;
    }
    AMarburgumCharacter* Body = Cast<AMarburgumCharacter>(GetPawn());
    AMarburgumVehicle* Vehicle = Body ? Body->GetSeatedVehicle() : nullptr;
    FName Reason = TEXT("not_seated");
    const bool bOk = Vehicle && Vehicle->TryExitAuthority(*Body, Reason);
    ClientReceiveVehicleResult(bOk, TEXT("exit"), bOk ? NAME_None : Reason);
}

void AMarburgumPlayerController::ServerSetVehicleInput_Implementation(float Throttle, float Brake, float Steer, bool bHandbrake)
{
    // Identity comes from this connection's pawn, never the payload; only the driver's input counts.
    const AMarburgumCharacter* Body = Cast<AMarburgumCharacter>(GetPawn());
    if (AMarburgumVehicle* Vehicle = Body ? Body->GetSeatedVehicle() : nullptr)
    {
        Vehicle->SetDriverInputAuthority(PlayerState, Throttle, Brake, Steer, bHandbrake);
    }
}

void AMarburgumPlayerController::ClientReceiveVehicleResult_Implementation(bool bOk, FName Action, FName Reason)
{
    VehicleRequestAt = -1000.0;
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=%s ok=%d reason=%s"), MarburgumII::Vehicles::LogPrefix, *Action.ToString(),
        bOk ? 1 : 0, Reason.IsNone() ? TEXT("-") : *Reason.ToString());
    OnVehicleResult.Broadcast(bOk, Action, Reason);
}

void AMarburgumPlayerController::ClientReceiveCombatResult_Implementation(const FMarburgumCombatResult& Result)
{
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=result heavy=%d ok=%d reason=%s outcome=%s target=%s damage=%.1f target_vitality=%d"),
        MarburgumII::Combat::CombatLogPrefix, Result.bHeavy ? 1 : 0, Result.bOk ? 1 : 0, Result.Reason.IsNone() ? TEXT("-") : *Result.Reason.ToString(),
        Result.Outcome.IsNone() ? TEXT("-") : *Result.Outcome.ToString(), Result.TargetId.IsNone() ? TEXT("-") : *Result.TargetId.ToString(),
        Result.Damage, Result.TargetVitality);
    OnCombatResult.Broadcast(Result);
}

void AMarburgumPlayerController::SetDrivingOverride(float Throttle, float Brake, float Steer, bool bHandbrake)
{
    bDrivingOverride = true;
    OverrideThrottle = Throttle;
    OverrideBrake = Brake;
    OverrideSteer = Steer;
    bOverrideHandbrake = bHandbrake;
}

void AMarburgumPlayerController::ClearDrivingOverride()
{
    bDrivingOverride = false;
}

void AMarburgumPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);
    if (IsLocalController())
    {
        UpdateDriving();
    }
}

void AMarburgumPlayerController::UpdateDriving()
{
    using namespace MarburgumII::Vehicles;
    const AMarburgumCharacter* Body = Cast<AMarburgumCharacter>(GetPawn());
    const AMarburgumVehicle* Car = Body && Body->IsDriving() ? Body->GetSeatedVehicle() : nullptr;
    if (!Car)
    {
        return;
    }
    // The walking keys read as pedals (player_controller.cpp update_driving): forward is the throttle, back
    // brakes while rolling forward and reverses once stopped, left and right are the wheel, and the run key
    // is the handbrake. The car's speed is the one the server last reported.
    float Throttle = 0.f;
    float Brake = 0.f;
    float Steer = 0.f;
    bool bHandbrake = false;
    if (bDrivingOverride)
    {
        Throttle = OverrideThrottle;
        Brake = OverrideBrake;
        Steer = OverrideSteer;
        bHandbrake = bOverrideHandbrake;
    }
    else
    {
        const float Forward = (IsInputKeyDown(EKeys::W) ? 1.f : 0.f) - (IsInputKeyDown(EKeys::S) ? 1.f : 0.f);
        Steer = (IsInputKeyDown(EKeys::D) ? 1.f : 0.f) - (IsInputKeyDown(EKeys::A) ? 1.f : 0.f);
        bHandbrake = IsInputKeyDown(EKeys::LeftShift);
        double PedalThrottle = 0.0;
        double PedalBrake = 0.0;
        Dynamics::Pedals(Forward, Car->GetForwardSpeed(), PedalThrottle, PedalBrake);
        Throttle = static_cast<float>(PedalThrottle);
        Brake = static_cast<float>(PedalBrake);
    }

    // Send on change, and resend while anything is held so the server's 0.4 s expiry never lapses.
    const double Now = FPlatformTime::Seconds();
    const bool bChanged = bHandbrake != bSentHandbrake || FMath::Abs(Steer - SentSteer) > InputEpsilon
        || FMath::Abs(Throttle - SentThrottle) > InputEpsilon || FMath::Abs(Brake - SentBrake) > InputEpsilon;
    const bool bIdle = Throttle == 0.f && Brake == 0.f && Steer == 0.f && !bHandbrake;
    const bool bKeepalive = !bIdle && Now - SentAt >= InputKeepaliveSeconds;
    if (!bChanged && !bKeepalive)
    {
        return;
    }
    SentThrottle = Throttle;
    SentBrake = Brake;
    SentSteer = Steer;
    bSentHandbrake = bHandbrake;
    SentAt = Now;
    ServerSetVehicleInput(Throttle, Brake, Steer, bHandbrake);
}

void AMarburgumPlayerController::ClientReceiveInteractionReceipt_Implementation(const FMarburgumInteractionReceipt& Receipt)
{
    if (Receipt.OperationId == PendingOperationId)
    {
        PendingOperationId.Invalidate();
        PendingTarget.Reset();
    }
    OnInteractionReceipt.Broadcast(Receipt);
}

void AMarburgumPlayerController::RequestChatFocus()
{
    if (IsLocalController())
    {
        FMarburgumClientUiHooks::OnChatFocusRequested().Broadcast(this);
    }
}

void AMarburgumPlayerController::SendGeneralChat(const FString& Text)
{
    if (IsLocalController())
    {
        ServerSendChat(EMarburgumChatChannel::General, INDEX_NONE, Text);
    }
}

void AMarburgumPlayerController::SendPrivateChat(int32 RecipientPlayerId, const FString& Text)
{
    if (IsLocalController())
    {
        ServerSendChat(EMarburgumChatChannel::Private, RecipientPlayerId, Text);
    }
}

void AMarburgumPlayerController::SendSystemNotice(const FString& Notice)
{
    // Throttle negative acknowledgements too; malformed RPC floods must not
    // amplify into a reliable server-to-client reply for every bad request.
    const UWorld* World = GetWorld();
    if (World && World->GetTimeSeconds() - LastSystemNoticeAt < 1.0)
    {
        return;
    }
    LastSystemNoticeAt = World ? World->GetTimeSeconds() : LastSystemNoticeAt;
    FMarburgumChatMessage Message;
    Message.Channel = EMarburgumChatChannel::System;
    Message.Text = Notice;
    ClientReceiveChat(Message);
}

void AMarburgumPlayerController::ServerSendChat_Implementation(
    EMarburgumChatChannel Channel, int32 RecipientPlayerId, const FString& RawText)
{
    if (!HasAuthority() || !PlayerState || !GetWorld())
    {
        return;
    }

    if (Channel != EMarburgumChatChannel::General && Channel != EMarburgumChatChannel::Private)
    {
        SendSystemNotice(TEXT("Unknown chat channel."));
        return;
    }

    FString Text;
    if (!MarburgumII::Chat::SanitizeMessage(RawText, Text))
    {
        SendSystemNotice(TEXT("Messages must contain 1–240 characters, without line breaks."));
        return;
    }

    const double Now = GetWorld()->GetTimeSeconds();
    // Sliding window: do not reward frequent sender bursts at a fixed-window boundary.
    while (!AcceptedTimestamps.IsEmpty() && Now - AcceptedTimestamps[0] >= WindowSeconds)
    {
        AcceptedTimestamps.RemoveAt(0);
    }
    if (Now - LastAcceptedAt < MinimumSpacingSeconds ||
        AcceptedTimestamps.Num() >= MaxMessagesPerWindow)
    {
        SendSystemNotice(TEXT("You are sending messages too quickly."));
        return;
    }

    AMarburgumPlayerController* PrivateRecipient = nullptr;
    if (Channel == EMarburgumChatChannel::Private)
    {
        if (RecipientPlayerId == INDEX_NONE)
        {
            SendSystemNotice(TEXT("Choose a connected player's session ID."));
            return;
        }
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            AMarburgumPlayerController* Candidate = Cast<AMarburgumPlayerController>(It->Get());
            if (Candidate && Candidate->PlayerState &&
                Candidate->PlayerState->GetPlayerId() == RecipientPlayerId)
            {
                PrivateRecipient = Candidate;
                break;
            }
        }
        if (!PrivateRecipient)
        {
            SendSystemNotice(TEXT("That player is not connected to this server."));
            return;
        }
    }

    LastAcceptedAt = Now;
    AcceptedTimestamps.Add(Now);
    FMarburgumChatMessage Message;
    Message.Channel = Channel;
    Message.SenderPlayerId = PlayerState->GetPlayerId();
    Message.SenderName = PlayerState->GetPlayerName();
    if (Message.SenderName.IsEmpty())
    {
        Message.SenderName = FString::Printf(TEXT("Player %d"), Message.SenderPlayerId);
    }
    Message.RecipientPlayerId = Channel == EMarburgumChatChannel::Private
        ? RecipientPlayerId : INDEX_NONE;
    Message.Text = MoveTemp(Text);
    Message.SenderSequence = ++NextMessageSequence;

    if (Channel == EMarburgumChatChannel::Private)
    {
        // Unicast only: sender's echo and authorized recipient. No GameState history.
        ClientReceiveChat(Message);
        if (PrivateRecipient != this)
        {
            PrivateRecipient->ClientReceiveChat(Message);
        }
    }
    else
    {
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            if (AMarburgumPlayerController* Receiver = Cast<AMarburgumPlayerController>(It->Get()))
            {
                Receiver->ClientReceiveChat(Message);
            }
        }
    }

    // Avoid logging message bodies or private recipients' private content.
    UE_LOG(LogTemp, Log, TEXT("Marburgum chat accepted: channel=%d sender=%d sequence=%d"),
        static_cast<int32>(Channel), Message.SenderPlayerId, Message.SenderSequence);
}

void AMarburgumPlayerController::ClientReceiveChat_Implementation(const FMarburgumChatMessage& Message)
{
    RecentChat.Add(Message);
    if (RecentChat.Num() > MaxLocalMessages)
    {
        RecentChat.RemoveAt(0, RecentChat.Num() - MaxLocalMessages);
    }
    OnChatReceived.Broadcast(Message);
}

AMarburgumGameState* AMarburgumPlayerController::GetMarburgumGameState() const
{
    const UWorld* World = GetWorld();
    return World ? World->GetGameState<AMarburgumGameState>() : nullptr;
}

void AMarburgumPlayerController::ServerSubmitOperation_Implementation(FGuid OperationId)
{
    AMarburgumGameState* GameState = GetMarburgumGameState();
    const EMarburgumRequestOutcome Outcome = GameState
        ? GameState->CommitOperation(OperationId)
        : EMarburgumRequestOutcome::Rejected;

    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=operation player=%d op=%s outcome=%s applied_total=%d"),
        MarburgumII::Network::SmokeLogPrefix,
        PlayerState ? PlayerState->GetPlayerId() : -1,
        *OperationId.ToString(EGuidFormats::Digits),
        OutcomeName(Outcome),
        GameState ? GameState->AppliedRequestCount : -1);

    ClientReceiveRequestResult(OperationId, Outcome);
}

void AMarburgumPlayerController::ServerRequestContractVersion_Implementation(int32 RequestedVersion)
{
    // The contract version is server-defined; any client request to change it is refused.
    AMarburgumGameState* GameState = GetMarburgumGameState();
    if (GameState)
    {
        GameState->RecordRejectedRequest();
    }

    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=contract_change_refused player=%d requested=%d contract=%d rejected_total=%d"),
        MarburgumII::Network::SmokeLogPrefix,
        PlayerState ? PlayerState->GetPlayerId() : -1,
        RequestedVersion,
        GameState ? GameState->NetworkContractVersion : -1,
        GameState ? GameState->RejectedRequestCount : -1);

    ClientReceiveRequestResult(FGuid(), EMarburgumRequestOutcome::Rejected);
}

void AMarburgumPlayerController::ClientReceiveRequestResult_Implementation(FGuid OperationId, EMarburgumRequestOutcome Outcome)
{
    OnRequestResult.Broadcast(OperationId, Outcome);
}
