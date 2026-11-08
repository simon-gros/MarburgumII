#include "MarburgumPlayerController.h"

#include "MarburgumClientUiHooks.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "InputCoreTypes.h"

namespace
{
    constexpr int32 MaxMessageCharacters = 240;
    constexpr int32 MaxLocalMessages = 80;
    constexpr double MinimumSpacingSeconds = 0.8;
    constexpr double WindowSeconds = 10.0;
    constexpr int32 MaxMessagesPerWindow = 8;

    bool SanitizeMessage(const FString& Input, FString& Output)
    {
        Output = Input.TrimStartAndEnd();
        if (Output.IsEmpty() || Output.Len() > MaxMessageCharacters)
        {
            return false;
        }

        // Chat is plain text, not parsed markup: reject controls and line breaks.
        for (const TCHAR Character : Output)
        {
            if (Character < TEXT(' ') || Character == 0x7F)
            {
                return false;
            }
        }

        return true;
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
    if (!SanitizeMessage(RawText, Text))
    {
        SendSystemNotice(TEXT("Messages must contain 1–240 characters, without line breaks."));
        return;
    }

    const double Now = GetWorld()->GetTimeSeconds();
    if (Now - RateWindowStart >= WindowSeconds)
    {
        RateWindowStart = Now;
        RateWindowMessages = 0;
    }
    if (Now - LastAcceptedAt < MinimumSpacingSeconds || RateWindowMessages >= MaxMessagesPerWindow)
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
    ++RateWindowMessages;
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
