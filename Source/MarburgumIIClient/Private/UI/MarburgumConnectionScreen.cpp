#include "UI/MarburgumConnectionScreen.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SafeZone.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "GeneralProjectSettings.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "MarburgumNetworkContract.h"
#include "UI/MarburgumUIBuilder.h"

#define LOCTEXT_NAMESPACE "MarburgumConnection"

using namespace MarburgumUI;
using MarburgumII::Session::EFault;
using MarburgumII::Session::EPhase;

namespace
{
    constexpr float CardWidth = 560.f;
    constexpr int32 StepCount = 4;

    // Step reached by each in-flight phase; -1 when the phase is not part of an attempt.
    int32 StepForPhase(EPhase Phase)
    {
        switch (Phase)
        {
        case EPhase::Connecting: return 0;
        case EPhase::Joining: return 1;
        case EPhase::LoadingWorld: return 2;
        case EPhase::Ready: return 3;
        default: return -1;
        }
    }

    ETone ToneForPhase(EPhase Phase)
    {
        switch (Phase)
        {
        case EPhase::Ready: return ETone::Success;
        case EPhase::Reconnecting: return ETone::Warning;
        case EPhase::Error: return ETone::Danger;
        case EPhase::Connecting:
        case EPhase::Joining:
        case EPhase::LoadingWorld: return ETone::Info;
        default: return ETone::Neutral;
        }
    }

    FText PhaseLabelText(EPhase Phase)
    {
        switch (Phase)
        {
        case EPhase::Starting: return LOCTEXT("PhaseStarting", "STARTING");
        case EPhase::Idle: return LOCTEXT("PhaseIdle", "OFFLINE");
        case EPhase::Connecting: return LOCTEXT("PhaseConnecting", "CONNECTING");
        case EPhase::Joining: return LOCTEXT("PhaseJoining", "JOINING");
        case EPhase::LoadingWorld: return LOCTEXT("PhaseLoading", "LOADING WORLD");
        case EPhase::Ready: return LOCTEXT("PhaseReady", "CONNECTED");
        case EPhase::Reconnecting: return LOCTEXT("PhaseReconnecting", "RECONNECTING");
        case EPhase::Error: return LOCTEXT("PhaseError", "NOT CONNECTED");
        }
        return FText::GetEmpty();
    }

    FText FaultHeadline(EFault Fault)
    {
        switch (Fault)
        {
        case EFault::InvalidAddress: return LOCTEXT("FaultAddress", "That server address is not valid");
        case EFault::Timeout: return LOCTEXT("FaultTimeout", "The server did not respond in time");
        case EFault::Refused: return LOCTEXT("FaultRefused", "The server refused the connection");
        case EFault::ContractMismatch: return LOCTEXT("FaultContract", "Client and server versions do not match");
        case EFault::ConnectionLost: return LOCTEXT("FaultLost", "The connection to the server was lost");
        case EFault::TravelFailed: return LOCTEXT("FaultTravel", "Could not reach the server's world");
        case EFault::None: break;
        }
        return LOCTEXT("FaultUnknown", "Not connected");
    }

    // Engine failure strings can carry a full connection dump; the screen shows the leading sentence
    // (the complete text stays in the log).
    FString ShortDetail(const FString& Detail)
    {
        constexpr int32 MaxChars = 120;
        FString Short = Detail;
        int32 SentenceEnd = INDEX_NONE;
        if (Short.FindChar(TEXT('.'), SentenceEnd) && SentenceEnd > 0)
        {
            const int32 Second = Short.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromStart, SentenceEnd + 1);
            Short.LeftInline(Second != INDEX_NONE ? Second + 1 : Short.Len());
        }
        if (Short.Len() > MaxChars)
        {
            Short = Short.Left(MaxChars - 1) + TEXT("…");
        }
        return Short;
    }

    FText FaultGuidance(EFault Fault)
    {
        switch (Fault)
        {
        case EFault::InvalidAddress: return LOCTEXT("GuideAddress", "Use host:port, for example 127.0.0.1:7777.");
        case EFault::Timeout: return LOCTEXT("GuideTimeout", "Check that the server is running and reachable, then retry.");
        case EFault::Refused: return LOCTEXT("GuideRefused", "The server answered but did not admit this client.");
        case EFault::ContractMismatch: return LOCTEXT("GuideContract", "Update the client or connect to a server built from the same version.");
        case EFault::ConnectionLost: return LOCTEXT("GuideLost", "Automatic reconnection did not succeed. You can retry.");
        case EFault::TravelFailed: return LOCTEXT("GuideTravel", "The client could not load the world the server named.");
        case EFault::None: break;
        }
        return FText::GetEmpty();
    }
}

void UMarburgumConnectionScreen::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
    WidgetTree->RootWidget = Root;

    // Full-bleed backdrop: the front-end has no world to show, so it owns the whole frame.
    UBorder* Backdrop = B.Surface(ESurfaceKind::Base, FMargin(0.f));
    UOverlaySlot* BackdropSlot = Root->AddChildToOverlay(Backdrop);
    BackdropSlot->SetHorizontalAlignment(HAlign_Fill);
    BackdropSlot->SetVerticalAlignment(VAlign_Fill);

    // Documentary motif: Maribor's coordinates, large and quiet, top right, clear of the card.
    UTextBlock* Motif = B.Text(LOCTEXT("Coordinates", "46°33′N  15°39′E"), ETypeRole::Display, T.Color.TextPrimary.CopyWithNewOpacity(0.035f));
    FSlateFontInfo MotifFont = Motif->GetFont();
    MotifFont.Size = 88;
    MotifFont.LetterSpacing = 0;
    Motif->SetFont(MotifFont);
    UOverlaySlot* MotifSlot = Root->AddChildToOverlay(Motif);
    MotifSlot->SetHorizontalAlignment(HAlign_Right);
    MotifSlot->SetVerticalAlignment(VAlign_Top);
    MotifSlot->SetPadding(FMargin(0.f, T.Space.ScreenGutter * 0.8f - T.Space.L, T.Space.ScreenGutter, 0.f));

    UVerticalBox* Content = B.Column();
    FBuilder::Stack(Content, BuildBrand(), T.Space.XXXL);
    FBuilder::Stack(Content, B.Spacer(1.f, 1.f), 0.f, true);
    UWidget* Card = BuildConnectCard();
    FBuilder::Stack(Content, Card, T.Space.XXL);
    if (UVerticalBoxSlot* CardSlot = Cast<UVerticalBoxSlot>(Card->Slot))
    {
        CardSlot->SetHorizontalAlignment(HAlign_Left);
    }
    FBuilder::Stack(Content, BuildFooter(), 0.f);

    UBorder* Gutter = B.Surface(ESurfaceKind::Base, FMargin(T.Space.ScreenGutter, T.Space.ScreenGutter * 0.8f), Content, false);
    Gutter->SetBrushColor(FLinearColor::Transparent);
    USafeZone* Safe = WidgetTree->ConstructWidget<USafeZone>();
    Safe->SetContent(Gutter);
    UOverlaySlot* SafeSlot = Root->AddChildToOverlay(Safe);
    SafeSlot->SetHorizontalAlignment(HAlign_Fill);
    SafeSlot->SetVerticalAlignment(VAlign_Fill);

    SetIsFocusable(true);
}

UWidget* UMarburgumConnectionScreen::BuildBrand()
{
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    UVerticalBox* Brand = B.Column();
    FBuilder::Stack(Brand, B.Text(LOCTEXT("Series", "MAGNA RAEDARUM"), ETypeRole::Overline, T.Color.Accent), T.Space.M);
    FBuilder::Stack(Brand, B.Text(LOCTEXT("Title", "MARBURGUM II"), ETypeRole::Display), T.Space.L);
    UWidget* AccentRule = B.Rule(T.Color.Accent, 2.f, 96.f);
    FBuilder::Stack(Brand, AccentRule, T.Space.L);
    Cast<UVerticalBoxSlot>(AccentRule->Slot)->SetHorizontalAlignment(HAlign_Left);
    FBuilder::Stack(Brand, B.Text(LOCTEXT("Tagline", "A persistent urban role-playing game  ·  Maribor, the 1990s"), ETypeRole::Body, T.Color.TextSecondary), 0.f);
    return Brand;
}

UWidget* UMarburgumConnectionScreen::BuildConnectCard()
{
    const FTokens& T = Tokens();
    const FColors& C = T.Color;
    FBuilder B(*WidgetTree);

    UVerticalBox* Body = B.Column();
    FBuilder::Stack(Body, B.Text(LOCTEXT("ServerHeading", "SERVER"), ETypeRole::Overline), T.Space.S);

    UHorizontalBox* AddressRow = B.Row();
    AddressField = B.TextField(FText::FromString(UMarburgumSessionSubsystem::DefaultAddress()), LOCTEXT("AddressHint", "host:port"));
    AddressField->OnTextCommitted.AddDynamic(this, &UMarburgumConnectionScreen::HandleAddressCommitted);
    FBuilder::Inline(AddressRow, AddressField, T.Space.M, true, false);
    ConnectButton = B.Button(LOCTEXT("Connect", "Connect"), EButtonKind::Primary);
    ConnectButton->OnClicked.AddDynamic(this, &UMarburgumConnectionScreen::HandleConnectClicked);
    FBuilder::Inline(AddressRow, ConnectButton, 0.f, false, false);
    FBuilder::Stack(Body, AddressRow, T.Space.XL);

    FBuilder::Stack(Body, B.Rule(C.Border, 1.f), T.Space.L);

    UHorizontalBox* StatusRow = B.Row();
    UTextBlock* PillLabel = nullptr;
    PhasePill = B.Pill(PhaseLabelText(EPhase::Starting), ETone::Neutral, &PillLabel);
    PhaseLabel = PillLabel;
    FBuilder::Inline(StatusRow, PhasePill, T.Space.M);
    Headline = B.Text(FText::GetEmpty(), ETypeRole::BodyStrong);
    FBuilder::Inline(StatusRow, Headline, T.Space.M, true);
    Elapsed = B.Text(FText::GetEmpty(), ETypeRole::Numeric, C.TextMuted);
    FBuilder::Inline(StatusRow, Elapsed, 0.f);
    FBuilder::Stack(Body, StatusRow, T.Space.S);

    Detail = B.Text(FText::GetEmpty(), ETypeRole::Caption);
    Detail->SetAutoWrapText(true);
    FBuilder::Stack(Body, Detail, T.Space.L);

    // Truthful step indicator: each step lights only when the session subsystem observed it.
    const FText StepNames[StepCount] = {
        LOCTEXT("StepConnect", "HANDSHAKE"), LOCTEXT("StepJoin", "JOIN"),
        LOCTEXT("StepLoad", "WORLD"), LOCTEXT("StepAdmit", "ADMITTED") };
    UHorizontalBox* Steps = B.Row();
    for (int32 Index = 0; Index < StepCount; ++Index)
    {
        UVerticalBox* Step = B.Column();
        // White brush: SetBrushColor tints it, so the step color is exactly the token color.
        UBorder* Bar = B.Surface(ESurfaceKind::Raised, FMargin(0.f), B.Spacer(1.f, 4.f), false);
        Bar->SetBrush(RoundedBrush(FLinearColor::White, T.Radius.Small));
        FBuilder::Stack(Step, Bar, T.Space.XS);
        UTextBlock* Label = B.Text(StepNames[Index], ETypeRole::Overline, C.TextMuted);
        FBuilder::Stack(Step, Label, 0.f);
        FBuilder::Inline(Steps, Step, Index + 1 < StepCount ? T.Space.S : 0.f, true, false);
        StepBars.Add(Bar);
        StepLabels.Add(Label);
    }
    FBuilder::Stack(Body, Steps, T.Space.XL);

    UHorizontalBox* Actions = B.Row();
    RetryButton = B.Button(LOCTEXT("Retry", "Retry"), EButtonKind::Secondary);
    RetryButton->OnClicked.AddDynamic(this, &UMarburgumConnectionScreen::HandleRetryClicked);
    FBuilder::Inline(Actions, RetryButton, T.Space.S);
    CancelButton = B.Button(LOCTEXT("Cancel", "Cancel"), EButtonKind::Quiet);
    CancelButton->OnClicked.AddDynamic(this, &UMarburgumConnectionScreen::HandleCancelClicked);
    FBuilder::Inline(Actions, CancelButton, 0.f);
    FBuilder::Stack(Body, Actions, 0.f);
    if (UVerticalBoxSlot* ActionsSlot = Cast<UVerticalBoxSlot>(Actions->Slot))
    {
        ActionsSlot->SetHorizontalAlignment(HAlign_Left);
    }

    UBorder* Card = B.Surface(ESurfaceKind::Panel, Pad(T.Space.XL), Body);
    USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
    Width->SetWidthOverride(CardWidth);
    Width->SetContent(Card);
    return Width;
}

UWidget* UMarburgumConnectionScreen::BuildFooter()
{
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    const FString Version = GetDefault<UGeneralProjectSettings>()->ProjectVersion;
    const FText FooterText = FText::Format(
        LOCTEXT("Footer", "Development preview  ·  client {0}  ·  network contract v{1}  ·  UI tokens v{2}  ·  no account service"),
        FText::FromString(Version), FText::AsNumber(MarburgumII::Network::ContractVersion), FText::AsNumber(TokenVersion));
    return B.Text(FooterText, ETypeRole::Caption, T.Color.TextMuted);
}

void UMarburgumConnectionScreen::ApplyView(const FMarburgumSessionView& View)
{
    if (!PhaseLabel)
    {
        return;
    }
    ShownPhase = View.Phase;
    ShownPhaseStarted = View.PhaseStartedSeconds;
    LastElapsedSecond = -1;

    const ETone Tone = ToneForPhase(View.Phase);
    PhaseLabel->SetText(PhaseLabelText(View.Phase));
    FBuilder::SetPillTone(PhasePill, PhaseLabel, Tone);

    const FText Address = FText::FromString(View.Address);
    FText HeadlineText;
    FText DetailText;
    switch (View.Phase)
    {
    case EPhase::Starting:
        HeadlineText = LOCTEXT("HeadStarting", "Starting");
        break;
    case EPhase::Idle:
        HeadlineText = LOCTEXT("HeadIdle", "Not connected");
        DetailText = LOCTEXT("DetailIdle", "Enter the address of an authoritative server. While offline, this client runs no gameplay.");
        break;
    case EPhase::Connecting:
        HeadlineText = LOCTEXT("HeadConnecting", "Contacting the server");
        DetailText = FText::Format(LOCTEXT("DetailConnecting", "Network handshake with {0}."), Address);
        break;
    case EPhase::Joining:
        HeadlineText = LOCTEXT("HeadJoining", "Server accepted the handshake");
        DetailText = LOCTEXT("DetailJoining", "Receiving the world assignment.");
        break;
    case EPhase::LoadingWorld:
        HeadlineText = LOCTEXT("HeadLoading", "Loading the server's world");
        DetailText = LOCTEXT("DetailLoading", "Admission is confirmed when the replicated world state arrives.");
        break;
    case EPhase::Ready:
        HeadlineText = FText::Format(LOCTEXT("HeadReady", "Admitted to {0}"), Address);
        break;
    case EPhase::Reconnecting:
        HeadlineText = FText::Format(LOCTEXT("HeadReconnecting", "Connection lost  ·  attempt {0} of {1}"),
            FText::AsNumber(View.ReconnectAttempt), FText::AsNumber(MarburgumII::Session::MaxReconnectAttempts));
        DetailText = FText::Format(LOCTEXT("DetailReconnecting", "Reconnecting to {0}. No game rules run on this client while it waits."), Address);
        break;
    case EPhase::Error:
        HeadlineText = FaultHeadline(View.Fault);
        DetailText = View.Detail.IsEmpty()
            ? FaultGuidance(View.Fault)
            : FText::Format(LOCTEXT("DetailError", "{0}\nDetail: {1}"), FaultGuidance(View.Fault), FText::FromString(ShortDetail(View.Detail)));
        break;
    }
    Headline->SetText(HeadlineText);
    Detail->SetText(DetailText);
    Detail->SetVisibility(DetailText.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);

    const bool bInFlight = MarburgumII::Session::IsAttemptInFlight(View.Phase);
    const bool bCanConnect = View.Phase == EPhase::Idle || View.Phase == EPhase::Error;
    ConnectButton->SetIsEnabled(bCanConnect);
    AddressField->SetIsReadOnly(!bCanConnect);
    RetryButton->SetVisibility(View.Phase == EPhase::Error && View.Fault != EFault::InvalidAddress
        ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    CancelButton->SetVisibility(bInFlight ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

    UpdateSteps(View.Phase);
    UpdateElapsed();
}

void UMarburgumConnectionScreen::UpdateSteps(EPhase Phase)
{
    const FTokens& T = Tokens();
    const int32 Step = StepForPhase(Phase);
    if (Step >= 0)
    {
        ReachedStep = Step;
    }
    else if (Phase == EPhase::Idle)
    {
        ReachedStep = -1;
    }

    for (int32 Index = 0; Index < StepBars.Num(); ++Index)
    {
        FLinearColor BarColor = T.Color.Border;
        FLinearColor LabelColor = T.Color.TextMuted;
        if (Phase == EPhase::Error && Index == FMath::Max(ReachedStep, 0))
        {
            BarColor = T.Color.Danger;
            LabelColor = T.Color.Danger;
        }
        else if (Phase == EPhase::Reconnecting)
        {
            BarColor = Index == 0 ? T.Color.Warning : T.Color.Border;
            LabelColor = Index == 0 ? T.Color.Warning : T.Color.TextMuted;
        }
        else if (Index < ReachedStep || (Phase == EPhase::Ready && Index == ReachedStep))
        {
            BarColor = T.Color.Accent;
            LabelColor = T.Color.TextSecondary;
        }
        else if (Index == ReachedStep)
        {
            BarColor = T.Color.Info;
            LabelColor = T.Color.TextPrimary;
        }
        StepBars[Index]->SetBrushColor(BarColor);
        StepLabels[Index]->SetColorAndOpacity(FSlateColor(LabelColor));
    }
}

void UMarburgumConnectionScreen::UpdateElapsed()
{
    if (!MarburgumII::Session::IsAttemptInFlight(ShownPhase))
    {
        if (LastElapsedSecond != -2)
        {
            Elapsed->SetText(FText::GetEmpty());
            LastElapsedSecond = -2;
        }
        return;
    }
    const int32 Seconds = FMath::FloorToInt32(FPlatformTime::Seconds() - ShownPhaseStarted);
    if (Seconds != LastElapsedSecond)
    {
        LastElapsedSecond = Seconds;
        Elapsed->SetText(FText::Format(LOCTEXT("Elapsed", "{0} s"), FText::AsNumber(Seconds)));
    }
}

void UMarburgumConnectionScreen::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    UpdateElapsed();
}

FReply UMarburgumConnectionScreen::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    if (InKeyEvent.GetKey() == EKeys::Escape && MarburgumII::Session::IsAttemptInFlight(ShownPhase))
    {
        HandleCancelClicked();
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

UWidget* UMarburgumConnectionScreen::GetPreferredFocus() const
{
    return ConnectButton;
}

UMarburgumSessionSubsystem* UMarburgumConnectionScreen::GetSession() const
{
    const UGameInstance* GameInstance = GetGameInstance();
    return GameInstance ? GameInstance->GetSubsystem<UMarburgumSessionSubsystem>() : nullptr;
}

void UMarburgumConnectionScreen::HandleConnectClicked()
{
    if (UMarburgumSessionSubsystem* Session = GetSession())
    {
        Session->Connect(AddressField->GetText().ToString());
    }
}

void UMarburgumConnectionScreen::HandleRetryClicked()
{
    if (UMarburgumSessionSubsystem* Session = GetSession())
    {
        Session->Retry();
    }
}

void UMarburgumConnectionScreen::HandleCancelClicked()
{
    if (UMarburgumSessionSubsystem* Session = GetSession())
    {
        Session->Cancel();
    }
}

void UMarburgumConnectionScreen::HandleAddressCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
    if (CommitMethod == ETextCommit::OnEnter)
    {
        HandleConnectClicked();
    }
}

#undef LOCTEXT_NAMESPACE
