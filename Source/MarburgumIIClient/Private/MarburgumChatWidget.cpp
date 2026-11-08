#include "MarburgumChatWidget.h"

#include "MarburgumPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"

namespace
{
    const FLinearColor TextPrimary(0.93f, 0.91f, 0.84f);
    const FLinearColor TextMuted(0.63f, 0.70f, 0.73f);
    const FLinearColor TextPrivate(0.65f, 0.87f, 0.86f);
    const FLinearColor TextWarning(0.99f, 0.70f, 0.47f);
}

void UMarburgumChatWidget::NativeConstruct()
{
    Super::NativeConstruct();
    ChatController = Cast<AMarburgumPlayerController>(GetOwningPlayer());
    if (!WidgetTree || !ChatController)
    {
        return;
    }

    BuildPanel();

    // Subscribe to server-approved delivery and replay bounded local history.
    ChatController->OnChatReceived.AddDynamic(this, &UMarburgumChatWidget::HandleMessage);
    for (const FMarburgumChatMessage& Message : ChatController->RecentChat)
    {
        HandleMessage(Message);
    }
}

void UMarburgumChatWidget::NativeDestruct()
{
    if (ChatController)
    {
        ChatController->OnChatReceived.RemoveDynamic(this, &UMarburgumChatWidget::HandleMessage);
    }
    Super::NativeDestruct();
}

void UMarburgumChatWidget::BuildPanel()
{
    UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
    WidgetTree->RootWidget = Root;

    UBorder* Frame = WidgetTree->ConstructWidget<UBorder>();
    Frame->SetBrushColor(FLinearColor(0.045f, 0.078f, 0.095f, 0.91f));
    Frame->SetPadding(FMargin(13.0f, 10.0f));
    UCanvasPanelSlot* FrameSlot = Root->AddChildToCanvas(Frame);
    FrameSlot->SetAnchors(FAnchors(0.0f, 1.0f));
    FrameSlot->SetAlignment(FVector2D(0.0f, 1.0f));
    FrameSlot->SetPosition(FVector2D(20.0f, -20.0f));
    FrameSlot->SetSize(FVector2D(450.0f, 268.0f));

    UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    Frame->SetContent(Column);

    UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
    Column->AddChildToVerticalBox(Header);
    UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>();
    Title->SetText(FText::FromString(TEXT("MARBURGUM  /  GENERAL & PRIVATE")));
    Title->SetColorAndOpacity(FSlateColor(TextPrimary));
    UHorizontalBoxSlot* TitleSlot = Header->AddChildToHorizontalBox(Title);
    TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

    UButton* PlayersButton = WidgetTree->ConstructWidget<UButton>();
    UTextBlock* PlayersLabel = WidgetTree->ConstructWidget<UTextBlock>();
    PlayersLabel->SetText(FText::FromString(TEXT("ONLINE")));
    PlayersLabel->SetColorAndOpacity(FSlateColor(TextMuted));
    PlayersButton->AddChild(PlayersLabel);
    PlayersButton->OnClicked.AddDynamic(this, &UMarburgumChatWidget::ShowConnectedPlayers);
    Header->AddChildToHorizontalBox(PlayersButton);

    MessageFeed = WidgetTree->ConstructWidget<UScrollBox>();
    UVerticalBoxSlot* FeedSlot = Column->AddChildToVerticalBox(MessageFeed);
    FeedSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    FeedSlot->SetPadding(FMargin(0.0f, 8.0f));

    UHorizontalBox* InputRow = WidgetTree->ConstructWidget<UHorizontalBox>();
    Column->AddChildToVerticalBox(InputRow);
    Composer = WidgetTree->ConstructWidget<UEditableTextBox>();
    Composer->SetHintText(FText::FromString(TEXT("Message   |   /w <player id> <private message>")));
    Composer->OnTextCommitted.AddDynamic(this, &UMarburgumChatWidget::HandleTextCommitted);
    UHorizontalBoxSlot* InputSlot = InputRow->AddChildToHorizontalBox(Composer);
    InputSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

    UButton* SendButton = WidgetTree->ConstructWidget<UButton>();
    UTextBlock* SendText = WidgetTree->ConstructWidget<UTextBlock>();
    SendText->SetText(FText::FromString(TEXT(" SEND ")));
    SendButton->AddChild(SendText);
    SendButton->OnClicked.AddDynamic(this, &UMarburgumChatWidget::HandleSendButton);
    InputRow->AddChildToHorizontalBox(SendButton);

    AddLine(TEXT("General is visible to this server. Private: /w 2 Hello. ONLINE lists IDs."),
        TextMuted);
}

void UMarburgumChatWidget::AddLine(const FString& Line, const FLinearColor& Color)
{
    if (!MessageFeed || !WidgetTree)
    {
        return;
    }
    UTextBlock* Entry = WidgetTree->ConstructWidget<UTextBlock>();
    Entry->SetAutoWrapText(true);
    Entry->SetText(FText::FromString(Line));
    Entry->SetColorAndOpacity(FSlateColor(Color));
    MessageFeed->AddChild(Entry);
    while (MessageFeed->GetChildrenCount() > 60)
    {
        MessageFeed->RemoveChildAt(0);
    }
    MessageFeed->ScrollToEnd();
}

void UMarburgumChatWidget::HandleMessage(const FMarburgumChatMessage& Message)
{
    if (Message.Channel == EMarburgumChatChannel::System)
    {
        AddLine(FString::Printf(TEXT("SYSTEM  %s"), *Message.Text), TextWarning);
        return;
    }

    if (Message.Channel == EMarburgumChatChannel::Private)
    {
        const bool IsSender = ChatController && ChatController->PlayerState &&
            ChatController->PlayerState->GetPlayerId() == Message.SenderPlayerId;
        const FString Address = IsSender
            ? FString::Printf(TEXT("PRIVATE  You -> #%d"), Message.RecipientPlayerId)
            : FString::Printf(TEXT("PRIVATE  %s (#%d) -> You"),
                              *Message.SenderName, Message.SenderPlayerId);
        AddLine(Address + TEXT(": ") + Message.Text, TextPrivate);
        return;
    }

    AddLine(FString::Printf(TEXT("%s (#%d): %s"),
        *Message.SenderName, Message.SenderPlayerId, *Message.Text), TextPrimary);
}

void UMarburgumChatWidget::HandleTextCommitted(const FText& Text, ETextCommit::Type Method)
{
    if (Method == ETextCommit::OnEnter)
    {
        SubmitComposer();
        ReturnFocusToWorld();
    }
}

void UMarburgumChatWidget::HandleSendButton()
{
    SubmitComposer();
}

void UMarburgumChatWidget::SubmitComposer()
{
    if (!Composer || !ChatController)
    {
        return;
    }

    const FString Line = Composer->GetText().ToString().TrimStartAndEnd();
    if (Line.IsEmpty())
    {
        return;
    }

    if (Line.StartsWith(TEXT("/w ")) || Line.StartsWith(TEXT("/whisper ")))
    {
        const FString Rest = Line.RightChop(Line.StartsWith(TEXT("/w ")) ? 3 : 9);
        FString IdText;
        FString Body;
        if (!Rest.Split(TEXT(" "), &IdText, &Body) || !IdText.IsNumeric() ||
            Body.TrimStartAndEnd().IsEmpty())
        {
            AddLine(TEXT("Usage: /w <online player ID> <message>"), TextWarning);
            return;
        }
        const int32 RecipientId = FCString::Atoi(*IdText);
        if (RecipientId <= 0)
        {
            AddLine(TEXT("Choose an online positive session player ID."), TextWarning);
            return;
        }
        ChatController->SendPrivateChat(RecipientId, Body);
    }
    else if (Line.StartsWith(TEXT("/g ")))
    {
        ChatController->SendGeneralChat(Line.RightChop(3));
    }
    else
    {
        ChatController->SendGeneralChat(Line);
    }
    Composer->SetText(FText::GetEmpty());
}

void UMarburgumChatWidget::FocusComposer()
{
    if (!Composer || !ChatController)
    {
        return;
    }
    FInputModeGameAndUI Mode;
    Mode.SetWidgetToFocus(Composer->TakeWidget());
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    ChatController->SetInputMode(Mode);
    ChatController->bShowMouseCursor = true;
    Composer->SetKeyboardFocus();
}

void UMarburgumChatWidget::ReturnFocusToWorld()
{
    if (ChatController)
    {
        ChatController->SetInputMode(FInputModeGameOnly());
        ChatController->bShowMouseCursor = false;
    }
}

void UMarburgumChatWidget::ShowConnectedPlayers()
{
    if (!GetWorld() || !GetWorld()->GetGameState())
    {
        AddLine(TEXT("Player list is not available yet."), TextWarning);
        return;
    }

    AddLine(TEXT("ONLINE / session IDs (not persistent account identifiers):"), TextMuted);
    for (APlayerState* State : GetWorld()->GetGameState()->PlayerArray)
    {
        if (State)
        {
            AddLine(FString::Printf(TEXT("%s — #%d"),
                *State->GetPlayerName(), State->GetPlayerId()), TextMuted);
        }
    }
}
