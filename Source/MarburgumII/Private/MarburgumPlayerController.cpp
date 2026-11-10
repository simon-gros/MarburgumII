#include "MarburgumPlayerController.h"

#include "MarburgumAuthorityService.h"
#include "MarburgumClientUiHooks.h"
#include "MarburgumChatPolicy.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumInteractable.h"
#include "MarburgumNetworkContract.h"
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
    if (!IsLocalController() || GetPendingInteraction(Pending, PendingObject))
    {
        return; // One request in flight at a time; the prompt shows it as waiting.
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
    // Bounded request rate: floods are dropped without a reliable reply for each one.
    const double Now = World->GetTimeSeconds();
    if (Now - RequestWindowStartSeconds >= 1.0)
    {
        RequestWindowStartSeconds = Now;
        RequestsInWindow = 0;
    }
    if (++RequestsInWindow > MarburgumII::Gameplay::MaxRequestsPerSecond)
    {
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=interaction_dropped player=%d reason=rate"),
            MarburgumII::Gameplay::InteractLogPrefix, PlayerState ? PlayerState->GetPlayerId() : -1);
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
