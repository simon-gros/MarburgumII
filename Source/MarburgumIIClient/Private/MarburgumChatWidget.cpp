#include "MarburgumChatWidget.h"

#include "MarburgumPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/SafeZone.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "UI/MarburgumUIBuilder.h"

using namespace MarburgumUI;

namespace
{
    constexpr float PanelWidth = 440.f;
    constexpr float PanelHeight = 320.f;
    constexpr int32 MaxFeedEntries = 60;

    // CHAT.md §4: warm light text, subdued teal for private, amber for system notices.
    FLinearColor TextPrimary() { return Tokens().Color.TextPrimary; }
    FLinearColor TextMuted() { return Tokens().Color.TextSecondary; }
    FLinearColor TextPrivate() { return Tokens().Color.Info; }
    FLinearColor TextWarning() { return Tokens().Color.Warning; }
}

void UMarburgumChatWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    // The widget tree must exist before the first RebuildWidget; a root assigned later (for
    // example in NativeConstruct) never reaches Slate and the panel renders nothing.
    BuildPanel();
}

void UMarburgumChatWidget::NativeConstruct()
{
    Super::NativeConstruct();
    ChatController = Cast<AMarburgumPlayerController>(GetOwningPlayer());
    if (!ChatController)
    {
        return;
    }

    // Subscribe to server-approved delivery and replay bounded local history.
    ChatController->OnChatReceived.AddUniqueDynamic(this, &UMarburgumChatWidget::HandleMessage);
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
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    UVerticalBox* Column = B.Column();

    UHorizontalBox* Header = B.Row();
    UVerticalBox* Titles = B.Column();
    FBuilder::Stack(Titles, B.Text(FText::FromString(TEXT("CHAT")), ETypeRole::Overline, T.Color.Accent), T.Space.XXS);
    FBuilder::Stack(Titles, B.Text(FText::FromString(TEXT("General: this server  ·  Private: /w <id> text")), ETypeRole::Caption, T.Color.TextMuted), 0.f);
    FBuilder::Inline(Header, Titles, T.Space.S, true);
    UButton* PlayersButton = B.Button(FText::FromString(TEXT("ONLINE")), EButtonKind::Quiet);
    PlayersButton->OnClicked.AddDynamic(this, &UMarburgumChatWidget::ShowConnectedPlayers);
    FBuilder::Inline(Header, PlayersButton, 0.f);
    FBuilder::Stack(Column, Header, T.Space.S);
    FBuilder::Stack(Column, B.Rule(T.Color.Border, 1.f), T.Space.S);

    MessageFeed = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
    FBuilder::Stack(Column, MessageFeed, T.Space.S, true);

    UHorizontalBox* InputRow = B.Row();
    Composer = B.TextField(FText::GetEmpty(), FText::FromString(TEXT("Message  ·  Enter to chat")));
    Composer->OnTextCommitted.AddDynamic(this, &UMarburgumChatWidget::HandleTextCommitted);
    FBuilder::Inline(InputRow, Composer, T.Space.S, true, false);
    UButton* SendButton = B.Button(FText::FromString(TEXT("Send")), EButtonKind::Secondary);
    SendButton->OnClicked.AddDynamic(this, &UMarburgumChatWidget::HandleSendButton);
    FBuilder::Inline(InputRow, SendButton, 0.f, false, false);
    FBuilder::Stack(Column, InputRow, 0.f);

    USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
    Size->SetWidthOverride(PanelWidth);
    Size->SetHeightOverride(PanelHeight);
    Size->SetContent(B.Surface(ESurfaceKind::Overlay, Pad(T.Space.M), Column));

    // Lower-left, clear of the lower-centre interaction prompt region.
    UBorder* Anchor = B.Surface(ESurfaceKind::Base, FMargin(T.Space.XXL, 0.f, 0.f, T.Space.XXL), Size, false);
    Anchor->SetBrushColor(FLinearColor::Transparent);
    FBuilder::Align(Anchor, HAlign_Left, VAlign_Bottom);
    USafeZone* Safe = WidgetTree->ConstructWidget<USafeZone>();
    Safe->SetContent(Anchor);
    WidgetTree->RootWidget = Safe;
    // Only the panel itself takes clicks; the rest of the frame stays with the game.
    Anchor->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    Safe->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

    AddLine(TEXT("General reaches everyone on this server. Private: /w <id> text. ONLINE lists IDs."), TextMuted());
}

void UMarburgumChatWidget::AddEntry(const FString& Heading, const FString& Body, const FLinearColor& HeadingColor, const FLinearColor& BodyColor)
{
    if (!MessageFeed || !WidgetTree)
    {
        return;
    }
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    UVerticalBox* Entry = B.Column();
    if (!Heading.IsEmpty())
    {
        FBuilder::Stack(Entry, B.Text(FText::FromString(Heading), ETypeRole::Overline, HeadingColor), T.Space.XXS);
    }
    UTextBlock* BodyBlock = B.Text(FText::FromString(Body), ETypeRole::Body, BodyColor);
    BodyBlock->SetAutoWrapText(true);
    FBuilder::Stack(Entry, BodyBlock, T.Space.S);
    MessageFeed->AddChild(Entry);
    while (MessageFeed->GetChildrenCount() > MaxFeedEntries)
    {
        MessageFeed->RemoveChildAt(0);
    }
    MessageFeed->ScrollToEnd();
}

void UMarburgumChatWidget::AddLine(const FString& Line, const FLinearColor& Color)
{
    AddEntry(FString(), Line, Color, Color);
}

void UMarburgumChatWidget::HandleMessage(const FMarburgumChatMessage& Message)
{
    if (Message.Channel == EMarburgumChatChannel::System)
    {
        AddEntry(TEXT("SYSTEM"), Message.Text, TextWarning(), TextMuted());
        return;
    }

    if (Message.Channel == EMarburgumChatChannel::Private)
    {
        const bool IsSender = ChatController && ChatController->PlayerState &&
            ChatController->PlayerState->GetPlayerId() == Message.SenderPlayerId;
        const FString Address = IsSender
            ? FString::Printf(TEXT("PRIVATE  ·  You → #%d"), Message.RecipientPlayerId)
            : FString::Printf(TEXT("PRIVATE  ·  %s (#%d) → You"), *Message.SenderName, Message.SenderPlayerId);
        AddEntry(Address, Message.Text, TextPrivate(), TextPrimary());
        return;
    }

    AddEntry(FString::Printf(TEXT("%s  ·  #%d"), *Message.SenderName.ToUpper(), Message.SenderPlayerId),
        Message.Text, TextMuted(), TextPrimary());
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
            AddLine(TEXT("Usage: /w <online player ID> <message>"), TextWarning());
            return;
        }
        const int32 RecipientId = FCString::Atoi(*IdText);
        if (RecipientId <= 0)
        {
            AddLine(TEXT("Choose an online positive session player ID."), TextWarning());
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
        AddLine(TEXT("Player list is not available yet."), TextWarning());
        return;
    }

    AddLine(TEXT("ONLINE / session IDs (not persistent account identifiers):"), TextMuted());
    for (APlayerState* State : GetWorld()->GetGameState()->PlayerArray)
    {
        if (State)
        {
            AddLine(FString::Printf(TEXT("%s — #%d"),
                *State->GetPlayerName(), State->GetPlayerId()), TextMuted());
        }
    }
}
