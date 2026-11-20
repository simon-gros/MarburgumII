#include "MarburgumChatWidget.h"

#include "MarburgumPlayerController.h"
#include "MarburgumPlayerState.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SafeZone.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "TimerManager.h"
#include "UI/MarburgumUIBuilder.h"
#include "UI/MarburgumUISettings.h"

#define LOCTEXT_NAMESPACE "MarburgumChat"

using namespace MarburgumUI;

namespace
{
    constexpr float PanelWidth = 460.f;
    constexpr float HistoryHeight = 260.f;
    constexpr int32 MaxHistory = 60;
    constexpr int32 MaxRecent = 5;
    /** Seconds the recent lines stay before fading, and the fade itself. */
    constexpr double LingerSeconds = 12.0;
    constexpr double FadeSeconds = 1.5;
    /** Clears the condition cluster below (two meters and their padding). */
    constexpr float BottomClearance = 104.f;

    FLinearColor ChannelColor(EMarburgumChatChannel Channel)
    {
        const FTokens& T = Tokens();
        return Channel == EMarburgumChatChannel::Private ? T.Color.Info : Channel == EMarburgumChatChannel::System ? T.Color.Warning : T.Color.Accent;
    }
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
    Unread = 0;
    if (UnreadChip)
    {
        UnreadChip->SetVisibility(ESlateVisibility::Collapsed);
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

    Body = WidgetTree->ConstructWidget<UWidgetSwitcher>();

    // Idle: the last lines and the Enter hint over a soft shade that fades after a while, and a marker for lines that
    // arrived and faded before the panel was opened.
    UVerticalBox* Idle = B.Column();
    UVerticalBox* RecentColumn = B.Column();
    RecentLines = B.Column();
    FBuilder::Stack(RecentColumn, RecentLines, T.Space.XS);
    FBuilder::Stack(RecentColumn, B.KeyHint(FText::FromString(TEXT("Enter")), LOCTEXT("ChatHint", "Chat")), 0.f);
    UOverlay* RecentStack = WidgetTree->ConstructWidget<UOverlay>();
    UOverlaySlot* ShadeSlot = RecentStack->AddChildToOverlay(B.Scrim(EMarburgumScrimShape::SoftFadeRight,
        T.Color.Backdrop.CopyWithNewOpacity(T.bHighContrast ? 0.88f : 0.62f)));
    ShadeSlot->SetHorizontalAlignment(HAlign_Fill);
    ShadeSlot->SetVerticalAlignment(VAlign_Fill);
    UBorder* RecentPad = B.Surface(ESurfaceKind::Base, FMargin(T.Space.M, T.Space.M, T.Space.XXXL, T.Space.M), RecentColumn, false);
    RecentPad->SetBrushColor(FLinearColor::Transparent);
    RecentStack->AddChildToOverlay(RecentPad);
    RecentFade = RecentStack;
    FBuilder::Stack(Idle, RecentStack, T.Space.XS);
    UTextBlock* UnreadText = nullptr;
    UnreadChip = B.Chip(EMarburgumGlyph::Chat, FText::GetEmpty(), ETone::Info, &UnreadText);
    UnreadLabel = UnreadText;
    UnreadChip->SetVisibility(ESlateVisibility::Collapsed);
    FBuilder::Stack(Idle, UnreadChip, 0.f);
    if (UVerticalBoxSlot* ChipSlot = Cast<UVerticalBoxSlot>(UnreadChip->Slot))
    {
        ChipSlot->SetHorizontalAlignment(HAlign_Left);
    }
    IdleView = Idle;
    Body->AddChild(Idle);

    // Open: the channel bar, history or who is online, and the composer.
    UVerticalBox* Open = B.Column();
    UHorizontalBox* Bar = B.Row();
    FBuilder::Inline(Bar, B.Glyph(EMarburgumGlyph::Chat, T.Color.Accent, T.Icon.Small), T.Space.S);
    FBuilder::Inline(Bar, B.Text(LOCTEXT("Overline", "CHAT"), ETypeRole::Overline, T.Color.Accent), T.Space.M, true);
    UTextBlock* OnlineText = nullptr;
    UHorizontalBox* OnlineContent = B.GlyphText(EMarburgumGlyph::Person, FText::GetEmpty(), ETypeRole::Overline, T.Color.TextSecondary, &OnlineText);
    OnlineLabel = OnlineText;
    UMarburgumSelectable* OnlineButton = B.Selectable(OnlineContent, FMargin(T.Space.S, T.Space.XS), true);
    OnlineButton->OnActivated.BindWeakLambda(this, [this]() { ToggleOnline(); });
    FBuilder::Inline(Bar, OnlineButton, 0.f);
    FBuilder::Stack(Open, Bar, T.Space.S);
    FBuilder::Stack(Open, B.Rule(T.Color.Border, 1.f), T.Space.S);

    UWidgetSwitcher* Views = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("ChatViews"));
    History = B.Scroll(nullptr);
    Views->AddChild(History);
    OnlineList = B.Column();
    Views->AddChild(B.Scroll(OnlineList));
    USizeBox* ViewHeight = WidgetTree->ConstructWidget<USizeBox>();
    ViewHeight->SetHeightOverride(HistoryHeight);
    ViewHeight->SetContent(Views);
    FBuilder::Stack(Open, ViewHeight, T.Space.M);

    UHorizontalBox* InputRow = B.Row();
    UTextBlock* ChannelText = nullptr;
    UBorder* ChannelChip = B.Chip(EMarburgumGlyph::None, FText::GetEmpty(), ETone::Accent, &ChannelText);
    ChannelLabel = ChannelText;
    FBuilder::Inline(InputRow, ChannelChip, T.Space.S);
    Composer = B.TextField(FText::GetEmpty(), LOCTEXT("ComposerHint", "Say something"));
    Composer->OnTextCommitted.AddDynamic(this, &UMarburgumChatWidget::HandleTextCommitted);
    FBuilder::Inline(InputRow, Composer, 0.f, true, false);
    FBuilder::Stack(Open, InputRow, T.Space.S);
    UHorizontalBox* Hints = B.Row();
    FBuilder::Inline(Hints, B.KeyHint(FText::FromString(TEXT("Enter")), LOCTEXT("Send", "Send")), T.Space.L);
    FBuilder::Inline(Hints, B.KeyHint(FText::FromString(TEXT("Tab")), LOCTEXT("Channel", "General / Private")), T.Space.L);
    FBuilder::Inline(Hints, B.KeyHint(FText::FromString(TEXT("Esc")), LOCTEXT("Leave", "Back to the street")), 0.f);
    FBuilder::Stack(Open, Hints, 0.f);
    OpenView = B.Surface(ESurfaceKind::Overlay, Pad(T.Space.M), Open);
    Body->AddChild(OpenView);

    USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
    Size->SetWidthOverride(PanelWidth);
    Size->SetContent(Body);

    // Lower-left, above the condition cluster and clear of the lower-centre subtitles and prompt.
    UBorder* Anchor = B.Surface(ESurfaceKind::Base, FMargin(T.Space.XL, 0.f, 0.f, T.Space.XL + BottomClearance), Size, false);
    Anchor->SetBrushColor(FLinearColor::Transparent);
    FBuilder::Align(Anchor, HAlign_Left, VAlign_Bottom);
    USafeZone* Safe = WidgetTree->ConstructWidget<USafeZone>();
    Safe->SetContent(Anchor);
    WidgetTree->RootWidget = Safe;
    // Only the open panel takes clicks; the recent lines and the rest of the frame stay with the game.
    Anchor->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    Safe->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    Idle->SetVisibility(ESlateVisibility::HitTestInvisible);
    RefreshChannel();
}

FString UMarburgumChatWidget::DisplayName(int32 PlayerId, const FString& Fallback) const
{
    const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
    if (GameState)
    {
        for (const APlayerState* State : GameState->PlayerArray)
        {
            const AMarburgumPlayerState* Marburgum = Cast<AMarburgumPlayerState>(State);
            if (Marburgum && Marburgum->GetPlayerId() == PlayerId && Marburgum->HasIdentity())
            {
                return FString::Printf(TEXT("%s %s"), *Marburgum->GetFirstName(), *Marburgum->GetLastName());
            }
        }
    }
    // Without a character name the engine's player name is a host-derived token, never shown: a session number is.
    return FText::Format(LOCTEXT("PlayerNumber", "Player {0}"), FText::AsNumber(PlayerId)).ToString();
}

UWidget* UMarburgumChatWidget::MakeLine(const FEntry& Entry, bool bCompact)
{
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    // Who and when on a small line, what they said under it at full width (names never squeeze the text).
    UVerticalBox* Line = B.Column();
    UHorizontalBox* Head = B.Row();
    if (!Entry.Heading.IsEmpty())
    {
        UTextBlock* Heading = B.Text(Entry.Heading, ETypeRole::Label, ChannelColor(Entry.Channel));
        if (bCompact)
        {
            Heading->SetShadowOffset(FVector2D(1.f, 1.f));
            Heading->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.6f));
        }
        FBuilder::Inline(Head, Heading, T.Space.S);
    }
    if (UMarburgumUISettings::Get().bChatTimestamps)
    {
        FBuilder::Inline(Head, B.Text(FText::FromString(Entry.Received.ToString(TEXT("%H:%M"))), ETypeRole::Caption,
            bCompact ? T.Color.TextSecondary : T.Color.TextMuted), 0.f);
    }
    if (Head->GetChildrenCount() > 0)
    {
        FBuilder::Stack(Line, Head, 0.f);
    }
    UTextBlock* Text = B.Text(Entry.Body, ETypeRole::Body, Entry.Channel == EMarburgumChatChannel::System ? T.Color.TextSecondary : T.Color.TextPrimary);
    Text->SetAutoWrapText(true);
    if (bCompact)
    {
        // Over the street the text carries its own shadow for legibility.
        Text->SetShadowOffset(FVector2D(1.f, 1.f));
        Text->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.65f));
    }
    FBuilder::Stack(Line, Text, 0.f);
    return Line;
}

void UMarburgumChatWidget::AddEntry(const FEntry& Entry)
{
    if (!History || !RecentLines)
    {
        return;
    }
    const FTokens& T = Tokens();
    UWidget* Full = MakeLine(Entry, false);
    History->AddChild(Full);
    if (UScrollBoxSlot* Slot = Cast<UScrollBoxSlot>(Full->Slot))
    {
        Slot->SetPadding(FMargin(0.f, 0.f, 0.f, T.Space.XS));
    }
    while (History->GetChildrenCount() > MaxHistory)
    {
        History->RemoveChildAt(0);
    }
    History->ScrollToEnd();

    FBuilder::Stack(RecentLines, MakeLine(Entry, true), T.Space.XXS);
    while (RecentLines->GetChildrenCount() > MaxRecent)
    {
        RecentLines->RemoveChildAt(0);
    }
    // Others' lines count as new until the panel is opened; the marker shows once the recent lines have faded.
    if (!bComposing && !Entry.bOwn)
    {
        ++Unread;
        UnreadLabel->SetText(Caps(FText::Format(LOCTEXT("Unread", "{0} new  ·  Enter"), FText::AsNumber(Unread))));
    }
    LastActivity = FPlatformTime::Seconds();
}

void UMarburgumChatWidget::AddNotice(const FText& Text, bool bWarning)
{
    FEntry Entry;
    Entry.Channel = EMarburgumChatChannel::System;
    Entry.Heading = bWarning ? LOCTEXT("NoticeHeading", "Chat") : FText::GetEmpty();
    Entry.Body = Text;
    Entry.Received = FDateTime::Now();
    Entry.bOwn = true;
    AddEntry(Entry);
}

void UMarburgumChatWidget::HandleMessage(const FMarburgumChatMessage& Message)
{
    FEntry Entry;
    Entry.Channel = Message.Channel;
    Entry.Body = FText::FromString(Message.Text);
    Entry.Received = FDateTime::Now();
    const bool bSender = ChatController && ChatController->PlayerState && ChatController->PlayerState->GetPlayerId() == Message.SenderPlayerId;
    Entry.bOwn = bSender;
    if (Message.Channel == EMarburgumChatChannel::System)
    {
        Entry.Heading = LOCTEXT("Server", "Server");
    }
    else if (Message.Channel == EMarburgumChatChannel::Private)
    {
        // The other end of a private line, which a reply goes back to.
        const int32 Other = bSender ? Message.RecipientPlayerId : Message.SenderPlayerId;
        Entry.Heading = bSender
            ? FText::Format(LOCTEXT("PrivateTo", "To {0}"), FText::FromString(DisplayName(Other, FString())))
            : FText::Format(LOCTEXT("PrivateFrom", "From {0}"), FText::FromString(DisplayName(Other, Message.SenderName)));
        PrivateTarget = Other;
        RefreshChannel();
    }
    else
    {
        Entry.Heading = FText::FromString(DisplayName(Message.SenderPlayerId, Message.SenderName));
    }
    AddEntry(Entry);
}

void UMarburgumChatWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    if (bComposing || !RecentLines)
    {
        return;
    }
    const double Idle = FPlatformTime::Seconds() - LastActivity;
    const float Target = Idle < LingerSeconds ? 1.f
        : Tokens().bReducedMotion ? 0.f
        : 1.f - FMath::Clamp(static_cast<float>((Idle - LingerSeconds) / FadeSeconds), 0.f, 1.f);
    if (!FMath::IsNearlyEqual(Target, RecentOpacity, 0.01f) || (Target == 0.f && RecentOpacity != 0.f))
    {
        RecentOpacity = Target;
        RecentFade->SetRenderOpacity(RecentOpacity);
        const bool bMarker = Unread > 0 && RecentOpacity <= 0.f;
        UnreadChip->SetVisibility(bMarker ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    }
}

void UMarburgumChatWidget::HandleTextCommitted(const FText& Text, ETextCommit::Type Method)
{
    if (Method == ETextCommit::OnEnter)
    {
        SubmitComposer();
        ReturnFocusToWorld();
    }
    else if (Method == ETextCommit::OnCleared)
    {
        ReturnFocusToWorld();
    }
    else if (Method == ETextCommit::OnUserMovedFocus && GetWorld())
    {
        // Clicking back into the world keeps the draft but gives the mouse and keys back to the game. Focus has not
        // settled yet; a click inside the panel keeps typing mode.
        GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
        {
            if (bComposing && !HasFocusedDescendants())
            {
                ReturnFocusToWorld();
            }
        }));
    }
}

FReply UMarburgumChatWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    const FKey Key = InKeyEvent.GetKey();
    if (bComposing && (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right))
    {
        ReturnFocusToWorld();
        return FReply::Handled();
    }
    if (bComposing && Key == EKeys::Tab)
    {
        CycleChannel();
        if (Composer)
        {
            Composer->SetKeyboardFocus();
        }
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
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
        FString Text;
        if (!Rest.Split(TEXT(" "), &IdText, &Text) || !IdText.IsNumeric() || Text.TrimStartAndEnd().IsEmpty())
        {
            AddNotice(LOCTEXT("Usage", "Private lines: /w <player number> <message>, or choose someone under Online."), true);
            return;
        }
        const int32 RecipientId = FCString::Atoi(*IdText);
        if (RecipientId <= 0)
        {
            AddNotice(LOCTEXT("BadId", "Choose the number of a player who is online now."), true);
            return;
        }
        ChatController->SendPrivateChat(RecipientId, Text);
    }
    else if (Line.StartsWith(TEXT("/g ")))
    {
        ChatController->SendGeneralChat(Line.RightChop(3));
    }
    else if (bPrivate)
    {
        if (PrivateTarget == INDEX_NONE)
        {
            AddNotice(LOCTEXT("NoTarget", "Choose someone under Online first, or write /w <player number> <message>."), true);
            return;
        }
        ChatController->SendPrivateChat(PrivateTarget, Line);
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
    SetComposing(true);
    FInputModeGameAndUI Mode;
    Mode.SetWidgetToFocus(Composer->TakeWidget());
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    ChatController->SetInputMode(Mode);
    ChatController->bShowMouseCursor = true;
    ChatController->SetTyping(true);
    Composer->SetKeyboardFocus();
}

void UMarburgumChatWidget::LeaveComposer()
{
    if (bComposing && ChatController)
    {
        ChatController->SetTyping(false);
    }
    SetComposing(false);
}

void UMarburgumChatWidget::ReturnFocusToWorld()
{
    SetComposing(false);
    if (ChatController)
    {
        ChatController->SetTyping(false);
        ChatController->SetInputMode(FInputModeGameOnly());
        ChatController->bShowMouseCursor = false;
    }
    if (FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().SetAllUserFocusToGameViewport();
    }
}

void UMarburgumChatWidget::SetComposing(bool bInComposing)
{
    bComposing = bInComposing;
    if (!Body)
    {
        return;
    }
    Body->SetActiveWidget(bComposing ? OpenView.Get() : IdleView.Get());
    if (bComposing)
    {
        Unread = 0;
        UnreadChip->SetVisibility(ESlateVisibility::Collapsed);
        History->ScrollToEnd();
        if (OnlineLabel && GetWorld() && GetWorld()->GetGameState())
        {
            OnlineLabel->SetText(Caps(FText::Format(LOCTEXT("Online", "{0} online"), FText::AsNumber(GetWorld()->GetGameState()->PlayerArray.Num()))));
        }
    }
    else
    {
        // Coming back from the panel, the recent lines show again for a while.
        LastActivity = FPlatformTime::Seconds();
        RecentOpacity = 1.f;
        if (RecentFade)
        {
            RecentFade->SetRenderOpacity(1.f);
        }
        if (bShowingOnline)
        {
            ToggleOnline();
        }
    }
}

void UMarburgumChatWidget::CycleChannel()
{
    bPrivate = !bPrivate;
    RefreshChannel();
}

void UMarburgumChatWidget::RefreshChannel()
{
    if (!ChannelLabel)
    {
        return;
    }
    if (bPrivate)
    {
        ChannelLabel->SetText(PrivateTarget == INDEX_NONE ? Caps(LOCTEXT("PrivateNone", "Private"))
            : Caps(FText::Format(LOCTEXT("PrivateTarget", "To {0}"), FText::FromString(DisplayName(PrivateTarget, FString())))));
        ChannelLabel->SetColorAndOpacity(FSlateColor(Tokens().Color.Info));
    }
    else
    {
        ChannelLabel->SetText(Caps(LOCTEXT("General", "General")));
        ChannelLabel->SetColorAndOpacity(FSlateColor(Tokens().Color.Accent));
    }
}

void UMarburgumChatWidget::ToggleOnline()
{
    bShowingOnline = !bShowingOnline;
    if (UWidgetSwitcher* Views = Cast<UWidgetSwitcher>(History ? History->GetParent() : nullptr))
    {
        Views->SetActiveWidgetIndex(bShowingOnline ? 1 : 0);
    }
    if (bShowingOnline)
    {
        RebuildOnline();
    }
}

void UMarburgumChatWidget::RebuildOnline()
{
    if (!OnlineList)
    {
        return;
    }
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    OnlineList->ClearChildren();
    const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
    if (!GameState)
    {
        FBuilder::Stack(OnlineList, B.Text(LOCTEXT("NoList", "Who is online is not known yet."), ETypeRole::Caption, T.Color.TextMuted), 0.f);
        return;
    }
    FBuilder::Stack(OnlineList, B.Text(LOCTEXT("OnlineNote", "Numbers belong to this session only, not to accounts."), ETypeRole::Caption,
        T.Color.TextMuted), T.Space.S);
    const int32 Self = ChatController && ChatController->PlayerState ? ChatController->PlayerState->GetPlayerId() : INDEX_NONE;
    for (const APlayerState* State : GameState->PlayerArray)
    {
        if (!State)
        {
            continue;
        }
        const int32 Id = State->GetPlayerId();
        UHorizontalBox* Line = B.Row();
        FBuilder::Inline(Line, B.Glyph(EMarburgumGlyph::Person, Id == Self ? T.Color.Accent : T.Color.TextSecondary, T.Icon.Small), T.Space.S);
        FBuilder::Inline(Line, B.Text(FText::FromString(DisplayName(Id, State->GetPlayerName())), ETypeRole::Label, T.Color.TextPrimary), T.Space.S, true);
        FBuilder::Inline(Line, B.Text(Id == Self ? LOCTEXT("You", "you") : FText::Format(LOCTEXT("Number", "#{0}"), FText::AsNumber(Id)),
            ETypeRole::Caption, T.Color.TextMuted), 0.f);
        UMarburgumSelectable* Row = B.Selectable(Line, FMargin(T.Space.S, T.Space.XS), true);
        if (Id != Self)
        {
            Row->OnActivated.BindWeakLambda(this, [this, Id]()
            {
                PrivateTarget = Id;
                bPrivate = true;
                RefreshChannel();
                ToggleOnline();
                if (Composer)
                {
                    Composer->SetKeyboardFocus();
                }
            });
        }
        FBuilder::Stack(OnlineList, Row, T.Space.XXS);
    }
}

#undef LOCTEXT_NAMESPACE
