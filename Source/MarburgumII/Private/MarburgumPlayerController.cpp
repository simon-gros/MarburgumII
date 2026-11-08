#include "MarburgumPlayerController.h"

#include "MarburgumClientUiHooks.h"
#include "MarburgumChatPolicy.h"
#include "MarburgumNetworkContract.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
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
        switch (Outcome)
        {
        case EMarburgumRequestOutcome::Applied: return TEXT("applied");
        case EMarburgumRequestOutcome::Duplicate: return TEXT("duplicate");
        case EMarburgumRequestOutcome::Rejected: return TEXT("rejected");
        }
        return TEXT("unknown");
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
    }
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
