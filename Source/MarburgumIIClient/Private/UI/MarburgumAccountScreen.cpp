#include "UI/MarburgumAccountScreen.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SafeZone.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "MarburgumAccountContract.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumSessionContract.h"
#include "UI/MarburgumCharacterPreview.h"
#include "UI/MarburgumUIWidgets.h"
#include "Components/HorizontalBoxSlot.h"
#include "UI/MarburgumUIBuilder.h"

#define LOCTEXT_NAMESPACE "MarburgumAccountScreen"

using namespace MarburgumUI;
using EStep = EMarburgumAccountStep;

namespace
{
    constexpr float CardWidth = 560.f;
    constexpr int32 StepCount = static_cast<int32>(EStep::Ready) + 1;

    FText TitleFor(EStep Step)
    {
        switch (Step)
        {
        case EStep::SignIn: return LOCTEXT("TitleSignIn", "Sign in");
        case EStep::Register: return LOCTEXT("TitleRegister", "Create an account");
        case EStep::Confirm: return LOCTEXT("TitleConfirm", "Confirm your email");
        case EStep::Credentials: return LOCTEXT("TitleCredentials", "Choose how you sign in");
        case EStep::Forgot: return LOCTEXT("TitleForgot", "Forgotten password");
        case EStep::Reset: return LOCTEXT("TitleReset", "Set a new password");
        case EStep::Character: return LOCTEXT("TitleCharacter", "Your character");
        default: return FText::GetEmpty();
        }
    }

    FText IntroFor(EStep Step)
    {
        switch (Step)
        {
        case EStep::SignIn: return LOCTEXT("IntroSignIn", "Sign in with your Marburgum II username and password.");
        case EStep::Register: return LOCTEXT("IntroRegister", "Start with an email address. We send it a code; the account exists once you confirm it.");
        case EStep::Confirm: return FText::GetEmpty();
        case EStep::Credentials: return FText::GetEmpty();
        case EStep::Forgot: return LOCTEXT("IntroForgot", "Type the email address of your account. We send it a reset code and your username.");
        case EStep::Reset: return FText::GetEmpty();
        case EStep::Character: return LOCTEXT("IntroCharacter",
            "Who you are in Maribor. Your first and last name are what other players see; they are not your username and need not be your own. You choose once.");
        default: return FText::GetEmpty();
        }
    }
}

UMarburgumAccountSubsystem* UMarburgumAccountScreen::GetAccounts() const
{
    const UGameInstance* GameInstance = GetGameInstance();
    return GameInstance ? GameInstance->GetSubsystem<UMarburgumAccountSubsystem>() : nullptr;
}

void UMarburgumAccountScreen::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
    WidgetTree->RootWidget = Root;
    UBorder* Backdrop = B.Surface(ESurfaceKind::Base, FMargin(0.f));
    UOverlaySlot* BackdropSlot = Root->AddChildToOverlay(Backdrop);
    BackdropSlot->SetHorizontalAlignment(HAlign_Fill);
    BackdropSlot->SetVerticalAlignment(VAlign_Fill);

    // The title screens' survey plate of the Drava at Lent behind the forms (hidden while the creator's stage is up),
    // with a warm glow and a shade on the left where the card stands.
    UOverlay* MotifLayer = WidgetTree->ConstructWidget<UOverlay>();
    const auto Fill = [](UOverlaySlot* Slot) { Slot->SetHorizontalAlignment(HAlign_Fill); Slot->SetVerticalAlignment(VAlign_Fill); };
    Fill(MotifLayer->AddChildToOverlay(B.Scrim(EMarburgumScrimShape::SoftBand, T.Color.Accent.CopyWithNewOpacity(0.05f))));
    Fill(MotifLayer->AddChildToOverlay(WidgetTree->ConstructWidget<UMarburgumMapPlate>()));
    UHorizontalBox* ShadeRow = B.Row();
    FBuilder::Inline(ShadeRow, B.Scrim(EMarburgumScrimShape::FadeRight, T.Color.Backdrop.CopyWithNewOpacity(0.92f)), 0.f, true);
    FBuilder::Inline(ShadeRow, B.Spacer(1.f, 1.f), 0.f, true);
    if (UHorizontalBoxSlot* Left = Cast<UHorizontalBoxSlot>(ShadeRow->GetChildAt(0)->Slot))
    {
        Left->SetVerticalAlignment(VAlign_Fill);
    }
    Fill(MotifLayer->AddChildToOverlay(ShadeRow));
    MotifLayer->SetVisibility(ESlateVisibility::HitTestInvisible);
    Motif = MotifLayer;
    Fill(Root->AddChildToOverlay(MotifLayer));

    // One screen, two halves that never overlap (QA U9): the brand and the card on the left, scrolling when a high
    // interface scale makes them taller than the window; on the character step, the body on its stage on the right.
    UVerticalBox* Content = B.Column();
    FBuilder::Stack(Content, BuildBrand(), T.Space.XXXL);
    UWidget* Card = BuildCard();
    FBuilder::Stack(Content, Card, T.Space.XL);
    if (UVerticalBoxSlot* CardSlot = Cast<UVerticalBoxSlot>(Card->Slot))
    {
        // Every step's card takes the column's width, so the card does not change size from step to step.
        CardSlot->SetHorizontalAlignment(HAlign_Fill);
    }
    if (ShowsDiagnostics())
    {
        // Developer detail (QA U14): only with -MB2UIDiagnostics, never on a player's screen.
        UTextBlock* Footer = B.Text(LOCTEXT("Footer",
            "Development preview  ·  account service: email codes are written to the service's outbox folder until a mail sender is chosen"),
            ETypeRole::Caption, T.Color.TextMuted);
        Footer->SetAutoWrapText(true);
        FBuilder::Stack(Content, Footer, 0.f);
    }
    UScrollBox* ContentScroll = B.Scroll(Content);
    CardWidthBox = WidgetTree->ConstructWidget<USizeBox>();
    CardWidthBox->SetWidthOverride(CardWidth);
    CardWidthBox->SetContent(ContentScroll);

    UHorizontalBox* Split = B.Row();
    FBuilder::Inline(Split, CardWidthBox, T.Space.XXL, false, false);

    // The creator's body, in 3D (ADR-057), on a framed stage with what it is and how to turn it.
    Preview = CreateWidget<UMarburgumCharacterPreview>(this, UMarburgumCharacterPreview::StaticClass());
    UVerticalBox* Stage = B.Column();
    FBuilder::Stack(Stage, Preview, T.Space.M, true);
    UHorizontalBox* StageFoot = B.Row();
    PreviewCaption = B.Text(FText::GetEmpty(), ETypeRole::Subheading, T.Color.TextPrimary);
    // How to turn the body is written on the stage itself (UMarburgumCharacterPreview); the foot names it.
    FBuilder::Inline(StageFoot, PreviewCaption, 0.f, true);
    FBuilder::Stack(Stage, StageFoot, 0.f);
    UBorder* Frame = B.Surface(ESurfaceKind::Sunken, Pad(T.Space.L), Stage, false);
    Frame->SetBrush(RoundedBrush(T.Color.Sunken.CopyWithNewOpacity(0.6f), T.Radius.Large, T.Color.Hairline, T.BorderWidth));
    PreviewFrame = Frame;
    FBuilder::Inline(Split, Frame, 0.f, true, false);
    PreviewFrame->SetVisibility(ESlateVisibility::Collapsed);

    UBorder* Gutter = B.Surface(ESurfaceKind::Base, FMargin(T.Space.ScreenGutter, T.Space.ScreenGutter * 0.8f), Split, false);
    Gutter->SetBrushColor(FLinearColor::Transparent);
    USafeZone* Safe = WidgetTree->ConstructWidget<USafeZone>();
    Safe->SetContent(Gutter);
    UOverlaySlot* SafeSlot = Root->AddChildToOverlay(Safe);
    SafeSlot->SetHorizontalAlignment(HAlign_Fill);
    SafeSlot->SetVerticalAlignment(VAlign_Fill);
    ShowModel();
    SetIsFocusable(true);
}

void UMarburgumAccountScreen::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    if (!CardWidthBox || GetVisibility() == ESlateVisibility::Collapsed)
    {
        return;
    }
    const FTokens& T = Tokens();
    const float Available = MyGeometry.GetLocalSize().X - 2.f * T.Space.ScreenGutter;
    constexpr float MinCard = 340.f;
    constexpr float MinPreview = 280.f;
    // The card keeps its full width while the preview has room beside it, then narrows; below that the preview yields.
    const float Card = bCreatorShown
        ? FMath::Clamp(Available - MinPreview - T.Space.XXL, MinCard, CardWidth)
        : FMath::Clamp(Available, MinCard, CardWidth);
    if (!FMath::IsNearlyEqual(Card, CardWidthBox->GetWidthOverride(), 1.f))
    {
        CardWidthBox->SetWidthOverride(Card);
    }
    const bool bFits = Available - Card - T.Space.XXL >= MinPreview;
    if (bFits != bPreviewFits)
    {
        bPreviewFits = bFits;
        const bool bShowPreview = bCreatorShown && bPreviewFits;
        PreviewFrame->SetVisibility(bShowPreview ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        Preview->SetActive(bShowPreview);
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=creator_layout card=%.0f available=%.0f preview=%d"),
            MarburgumII::Session::UiLogPrefix, Card, Available, bShowPreview ? 1 : 0);
    }
}

UWidget* UMarburgumAccountScreen::BuildBrand()
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

UEditableTextBox* UMarburgumAccountScreen::AddField(UVerticalBox* Panel, const FText& Label, const FText& Hint, bool bPassword)
{
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    FBuilder::Stack(Panel, B.Text(Label, ETypeRole::Overline), T.Space.XS);
    UEditableTextBox* Field = B.TextField(FText::GetEmpty(), Hint);
    Field->SetIsPassword(bPassword);
    Field->OnTextCommitted.AddDynamic(this, &UMarburgumAccountScreen::HandleCommitted);
    FBuilder::Stack(Panel, Field, T.Space.M);
    return Field;
}

UButton* UMarburgumAccountScreen::AddButton(UHorizontalBox* Row, const FText& Label, int32 Kind, float GapAfter)
{
    FBuilder B(*WidgetTree);
    UButton* Button = B.Button(Label, static_cast<EButtonKind>(Kind));
    FBuilder::Inline(Row, Button, GapAfter, false, false);
    Actions.Add(Button);
    return Button;
}

UWidget* UMarburgumAccountScreen::BuildCard()
{
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    const int32 Primary = static_cast<int32>(EButtonKind::Primary);
    const int32 Secondary = static_cast<int32>(EButtonKind::Secondary);
    const int32 Quiet = static_cast<int32>(EButtonKind::Quiet);

    UVerticalBox* Body = B.Column();
    StepTitle = B.Text(FText::GetEmpty(), ETypeRole::Title);
    FBuilder::Stack(Body, StepTitle, T.Space.S);
    StepIntro = B.Text(FText::GetEmpty(), ETypeRole::Body, T.Color.TextSecondary);
    StepIntro->SetAutoWrapText(true);
    FBuilder::Stack(Body, StepIntro, T.Space.L);

    Panels.SetNum(StepCount);
    StepFields.SetNum(StepCount);
    const auto MakePanel = [&](EStep Step) -> UVerticalBox*
    {
        UVerticalBox* Panel = B.Column();
        Panels[static_cast<int32>(Step)] = Panel;
        FBuilder::Stack(Body, Panel, 0.f);
        return Panel;
    };
    const auto Fields = [&](EStep Step) -> TArray<TObjectPtr<UEditableTextBox>>& { return StepFields[static_cast<int32>(Step)]; };

    {
        UVerticalBox* Panel = MakePanel(EStep::SignIn);
        Fields(EStep::SignIn).Add(AddField(Panel, LOCTEXT("Username", "USERNAME"), LOCTEXT("UsernameHint", "your username")));
        Fields(EStep::SignIn).Add(AddField(Panel, LOCTEXT("Password", "PASSWORD"), FText::GetEmpty(), true));
        UHorizontalBox* Row = B.Row();
        AddButton(Row, LOCTEXT("SignIn", "Sign in"), Primary, T.Space.S)->OnClicked.AddDynamic(this, &UMarburgumAccountScreen::HandleSignIn);
        AddButton(Row, LOCTEXT("CreateAccount", "Create account"), Secondary, T.Space.S)->OnClicked.AddDynamic(this, &UMarburgumAccountScreen::HandleShowRegister);
        AddButton(Row, LOCTEXT("Forgot", "Forgot password"), Quiet, 0.f)->OnClicked.AddDynamic(this, &UMarburgumAccountScreen::HandleShowForgot);
        FBuilder::Stack(Panel, Row, 0.f);
    }
    {
        UVerticalBox* Panel = MakePanel(EStep::Register);
        Fields(EStep::Register).Add(AddField(Panel, LOCTEXT("Email", "EMAIL"), LOCTEXT("EmailHint", "you@example.com")));
        UHorizontalBox* Row = B.Row();
        AddButton(Row, LOCTEXT("SendCode", "Send code"), Primary, T.Space.S)->OnClicked.AddDynamic(this, &UMarburgumAccountScreen::HandleSendCode);
        AddButton(Row, LOCTEXT("Back", "Back"), Quiet, 0.f)->OnClicked.AddDynamic(this, &UMarburgumAccountScreen::HandleBack);
        FBuilder::Stack(Panel, Row, 0.f);
    }
    {
        UVerticalBox* Panel = MakePanel(EStep::Confirm);
        Fields(EStep::Confirm).Add(AddField(Panel, LOCTEXT("Code", "CODE"), LOCTEXT("CodeHint", "eight characters from the email")));
        UHorizontalBox* Row = B.Row();
        AddButton(Row, LOCTEXT("Confirm", "Confirm"), Primary, T.Space.S)->OnClicked.AddDynamic(this, &UMarburgumAccountScreen::HandleConfirm);
        AddButton(Row, LOCTEXT("Resend", "Send a new code"), Secondary, T.Space.S)->OnClicked.AddDynamic(this, &UMarburgumAccountScreen::HandleResend);
        AddButton(Row, LOCTEXT("ChangeEmail", "Change email"), Quiet, 0.f)->OnClicked.AddDynamic(this, &UMarburgumAccountScreen::HandleShowRegister);
        FBuilder::Stack(Panel, Row, 0.f);
    }
    {
        UVerticalBox* Panel = MakePanel(EStep::Credentials);
        Fields(EStep::Credentials).Add(AddField(Panel, LOCTEXT("ChooseUsername", "USERNAME"), LOCTEXT("ChooseUsernameHint", "3–20: a–z, 0–9, . _ -")));
        Fields(EStep::Credentials).Add(AddField(Panel, LOCTEXT("ChoosePassword", "PASSWORD"), LOCTEXT("ChoosePasswordHint", "at least 10 characters"), true));
        Fields(EStep::Credentials).Add(AddField(Panel, LOCTEXT("RepeatPassword", "PASSWORD AGAIN"), FText::GetEmpty(), true));
        UHorizontalBox* Row = B.Row();
        AddButton(Row, LOCTEXT("Finish", "Create account"), Primary, 0.f)->OnClicked.AddDynamic(this, &UMarburgumAccountScreen::HandleCredentials);
        FBuilder::Stack(Panel, Row, 0.f);
    }
    {
        UVerticalBox* Panel = MakePanel(EStep::Forgot);
        Fields(EStep::Forgot).Add(AddField(Panel, LOCTEXT("ForgotEmail", "EMAIL"), LOCTEXT("ForgotEmailHint", "the address of your account")));
        UHorizontalBox* Row = B.Row();
        AddButton(Row, LOCTEXT("SendReset", "Send reset code"), Primary, T.Space.S)->OnClicked.AddDynamic(this, &UMarburgumAccountScreen::HandleSendReset);
        AddButton(Row, LOCTEXT("Back2", "Back"), Quiet, 0.f)->OnClicked.AddDynamic(this, &UMarburgumAccountScreen::HandleBack);
        FBuilder::Stack(Panel, Row, 0.f);
    }
    {
        UVerticalBox* Panel = MakePanel(EStep::Reset);
        Fields(EStep::Reset).Add(AddField(Panel, LOCTEXT("ResetCode", "CODE"), LOCTEXT("ResetCodeHint", "eight characters from the email")));
        Fields(EStep::Reset).Add(AddField(Panel, LOCTEXT("NewPassword", "NEW PASSWORD"), LOCTEXT("NewPasswordHint", "at least 10 characters"), true));
        Fields(EStep::Reset).Add(AddField(Panel, LOCTEXT("NewPasswordAgain", "NEW PASSWORD AGAIN"), FText::GetEmpty(), true));
        UHorizontalBox* Row = B.Row();
        AddButton(Row, LOCTEXT("SetPassword", "Set password"), Primary, T.Space.S)->OnClicked.AddDynamic(this, &UMarburgumAccountScreen::HandleReset);
        AddButton(Row, LOCTEXT("Back3", "Back"), Quiet, 0.f)->OnClicked.AddDynamic(this, &UMarburgumAccountScreen::HandleBack);
        FBuilder::Stack(Panel, Row, 0.f);
    }
    {
        UVerticalBox* Panel = MakePanel(EStep::Character);
        // First and last name side by side, the same width each.
        UHorizontalBox* Names = B.Row();
        UVerticalBox* FirstColumn = B.Column();
        UVerticalBox* LastColumn = B.Column();
        Fields(EStep::Character).Add(AddField(FirstColumn, LOCTEXT("FirstName", "FIRST NAME"), LOCTEXT("FirstNameHint", "e.g. Špela")));
        Fields(EStep::Character).Add(AddField(LastColumn, LOCTEXT("LastName", "LAST NAME"), LOCTEXT("LastNameHint", "e.g. Novak")));
        FBuilder::Inline(Names, FirstColumn, T.Space.M, true, false);
        FBuilder::Inline(Names, LastColumn, 0.f, true, false);
        FBuilder::Stack(Panel, Names, 0.f);
        FBuilder::Stack(Panel, B.Text(LOCTEXT("Body", "BODY"), ETypeRole::Overline), T.Space.XS);
        // One card per approved body (ADR-057); the preview beside the card shows the chosen one.
        UHorizontalBox* Chooser = B.Row();
        constexpr int32 Count = UE_ARRAY_COUNT(MarburgumII::Accounts::CharacterModels);
        for (int32 Index = 0; Index < Count; ++Index)
        {
            UMarburgumModelCard* Card = CreateWidget<UMarburgumModelCard>(this, UMarburgumModelCard::StaticClass());
            Card->Setup(Index, FText::FromString(MarburgumII::Accounts::CharacterModels[Index].NameEn));
            Card->OnPicked.AddUObject(this, &UMarburgumAccountScreen::HandleModelPicked);
            FBuilder::Inline(Chooser, Card, Index + 1 < Count ? T.Space.S : 0.f, true, false);
            ModelCards.Add(Card);
            Actions.Add(Card->GetButton());
        }
        FBuilder::Stack(Panel, Chooser, T.Space.L);
        UHorizontalBox* Row = B.Row();
        AddButton(Row, LOCTEXT("CreateCharacter", "Create character"), Primary, T.Space.S)->OnClicked.AddDynamic(this, &UMarburgumAccountScreen::HandleCreateCharacter);
        AddButton(Row, LOCTEXT("SignOut", "Sign out"), Quiet, 0.f)->OnClicked.AddDynamic(this, &UMarburgumAccountScreen::HandleSignOut);
        FBuilder::Stack(Panel, Row, 0.f);
        ShowModel();
    }

    FBuilder::Stack(Body, B.Rule(T.Color.Border, 1.f), T.Space.M);
    Busy = B.Text(LOCTEXT("Busy", "Working…"), ETypeRole::Caption, T.Color.Info);
    FBuilder::Stack(Body, Busy, T.Space.XS);
    Message = B.Text(FText::GetEmpty(), ETypeRole::Body);
    Message->SetAutoWrapText(true);
    FBuilder::Stack(Body, Message, 0.f);

    UBorder* Card = B.Surface(ESurfaceKind::Panel, Pad(T.Space.XL), Body);
    Card->SetBrush(RoundedBrush(T.Color.Panel, T.Radius.Large, T.Color.Border, T.BorderWidth));
    return Card;
}

void UMarburgumAccountScreen::ShowModel()
{
    using namespace MarburgumII::Accounts;
    constexpr int32 Count = UE_ARRAY_COUNT(CharacterModels);
    ModelIndex = (ModelIndex % Count + Count) % Count;
    for (int32 Index = 0; Index < ModelCards.Num(); ++Index)
    {
        ModelCards[Index]->SetSelected(Index == ModelIndex);
    }
    if (Preview)
    {
        Preview->ShowModel(ModelIndex);
    }
    if (PreviewCaption)
    {
        PreviewCaption->SetText(FText::FromString(CharacterModels[ModelIndex].NameEn));
    }
}

void UMarburgumAccountScreen::ApplyView(const FMarburgumAccountView& View)
{
    if (!StepTitle)
    {
        return;
    }
    const FTokens& T = Tokens();
    const bool bStepChanged = View.Step != ShownStep;
    ShownStep = View.Step;
    StepTitle->SetText(TitleFor(View.Step));
    const FText Intro = View.Step == EStep::Character && !View.Username.IsEmpty()
        ? FText::Format(LOCTEXT("IntroCharacterNamed", "Signed in as {0}. {1}"), FText::FromString(View.Username), IntroFor(View.Step))
        : IntroFor(View.Step);
    StepIntro->SetText(Intro);
    StepIntro->SetVisibility(Intro.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    for (int32 Index = 0; Index < Panels.Num(); ++Index)
    {
        if (Panels[Index])
        {
            Panels[Index]->SetVisibility(Index == static_cast<int32>(View.Step) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        }
    }
    if (bStepChanged)
    {
        // Nothing typed on one screen, a password least of all, waits on another.
        for (TArray<TObjectPtr<UEditableTextBox>>& Fields : StepFields)
        {
            for (UEditableTextBox* Field : Fields)
            {
                if (Field && Field->GetIsPassword())
                {
                    Field->SetText(FText::GetEmpty());
                }
            }
        }
        if (View.Step == EStep::Forgot && !View.Email.IsEmpty() && StepFields[static_cast<int32>(EStep::Forgot)][0]->GetText().IsEmpty())
        {
            StepFields[static_cast<int32>(EStep::Forgot)][0]->SetText(FText::FromString(View.Email));
        }
    }
    Busy->SetVisibility(View.bBusy ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
    for (UButton* Button : Actions)
    {
        Button->SetIsEnabled(!View.bBusy);
    }
    Message->SetText(View.Message);
    Message->SetColorAndOpacity(FSlateColor(View.bError ? T.Color.Danger : T.Color.TextSecondary));
    Message->SetVisibility(View.Message.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    if (Preview)
    {
        bCreatorShown = View.Step == EStep::Character;
        const bool bShowPreview = bCreatorShown && bPreviewFits;
        PreviewFrame->SetVisibility(bShowPreview ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        Preview->SetActive(bShowPreview);
        // The coordinates would show half-covered behind the preview.
        Motif->SetVisibility(bCreatorShown ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    }
}

void UMarburgumAccountScreen::ShowLocalError(const FText& Text)
{
    Message->SetText(Text);
    Message->SetColorAndOpacity(FSlateColor(Tokens().Color.Danger));
    Message->SetVisibility(ESlateVisibility::HitTestInvisible);
}

UWidget* UMarburgumAccountScreen::GetPreferredFocus() const
{
    const int32 Step = static_cast<int32>(ShownStep);
    return StepFields.IsValidIndex(Step) && !StepFields[Step].IsEmpty() ? StepFields[Step][0].Get() : nullptr;
}

void UMarburgumAccountScreen::Primary()
{
    switch (ShownStep)
    {
    case EStep::SignIn: HandleSignIn(); break;
    case EStep::Register: HandleSendCode(); break;
    case EStep::Confirm: HandleConfirm(); break;
    case EStep::Credentials: HandleCredentials(); break;
    case EStep::Forgot: HandleSendReset(); break;
    case EStep::Reset: HandleReset(); break;
    case EStep::Character: HandleCreateCharacter(); break;
    default: break;
    }
}

void UMarburgumAccountScreen::HandleCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
    if (CommitMethod == ETextCommit::OnEnter)
    {
        Primary();
    }
}

void UMarburgumAccountScreen::SubmitForTest(const TArray<FString>& Values)
{
    const int32 Step = static_cast<int32>(ShownStep);
    if (!StepFields.IsValidIndex(Step))
    {
        return;
    }
    for (int32 Index = 0; Index < Values.Num() && Index < StepFields[Step].Num(); ++Index)
    {
        StepFields[Step][Index]->SetText(FText::FromString(Values[Index]));
    }
    Primary();
}

void UMarburgumAccountScreen::SelectModelForTest(FName Model)
{
    int32 Index = 0;
    for (const MarburgumII::Accounts::FCharacterModel& Entry : MarburgumII::Accounts::CharacterModels)
    {
        if (Model == FName(Entry.Id))
        {
            ModelIndex = Index;
            ShowModel();
            return;
        }
        ++Index;
    }
}

namespace
{
    FString FieldText(const TArray<TArray<TObjectPtr<UEditableTextBox>>>& Fields, EStep Step, int32 Index)
    {
        return Fields[static_cast<int32>(Step)][Index]->GetText().ToString();
    }
}

void UMarburgumAccountScreen::HandleSignIn()
{
    if (UMarburgumAccountSubsystem* Accounts = GetAccounts())
    {
        Accounts->SignIn(FieldText(StepFields, EStep::SignIn, 0), FieldText(StepFields, EStep::SignIn, 1));
    }
}

void UMarburgumAccountScreen::HandleShowRegister()
{
    if (UMarburgumAccountSubsystem* Accounts = GetAccounts())
    {
        Accounts->Show(EStep::Register);
    }
}

void UMarburgumAccountScreen::HandleShowForgot()
{
    if (UMarburgumAccountSubsystem* Accounts = GetAccounts())
    {
        Accounts->Show(EStep::Forgot);
    }
}

void UMarburgumAccountScreen::HandleBack()
{
    if (UMarburgumAccountSubsystem* Accounts = GetAccounts())
    {
        Accounts->Show(EStep::SignIn);
    }
}

void UMarburgumAccountScreen::HandleSendCode()
{
    if (UMarburgumAccountSubsystem* Accounts = GetAccounts())
    {
        Accounts->Register(FieldText(StepFields, EStep::Register, 0));
    }
}

void UMarburgumAccountScreen::HandleConfirm()
{
    if (UMarburgumAccountSubsystem* Accounts = GetAccounts())
    {
        Accounts->Confirm(FieldText(StepFields, EStep::Confirm, 0));
    }
}

void UMarburgumAccountScreen::HandleResend()
{
    if (UMarburgumAccountSubsystem* Accounts = GetAccounts())
    {
        Accounts->ResendCode();
    }
}

void UMarburgumAccountScreen::HandleCredentials()
{
    const FString Password = FieldText(StepFields, EStep::Credentials, 1);
    if (Password != FieldText(StepFields, EStep::Credentials, 2))
    {
        ShowLocalError(LOCTEXT("PasswordsDiffer", "The two passwords are not the same."));
        return;
    }
    if (UMarburgumAccountSubsystem* Accounts = GetAccounts())
    {
        Accounts->SetCredentials(FieldText(StepFields, EStep::Credentials, 0), Password);
    }
}

void UMarburgumAccountScreen::HandleSendReset()
{
    if (UMarburgumAccountSubsystem* Accounts = GetAccounts())
    {
        Accounts->RequestReset(FieldText(StepFields, EStep::Forgot, 0));
    }
}

void UMarburgumAccountScreen::HandleReset()
{
    const FString Password = FieldText(StepFields, EStep::Reset, 1);
    if (Password != FieldText(StepFields, EStep::Reset, 2))
    {
        ShowLocalError(LOCTEXT("PasswordsDiffer2", "The two passwords are not the same."));
        return;
    }
    if (UMarburgumAccountSubsystem* Accounts = GetAccounts())
    {
        Accounts->Reset(FieldText(StepFields, EStep::Reset, 0), Password);
    }
}

void UMarburgumAccountScreen::HandleModelPicked(int32 Index)
{
    ModelIndex = Index;
    ShowModel();
}

void UMarburgumAccountScreen::HandleCreateCharacter()
{
    if (UMarburgumAccountSubsystem* Accounts = GetAccounts())
    {
        Accounts->CreateCharacter(FieldText(StepFields, EStep::Character, 0), FieldText(StepFields, EStep::Character, 1),
            FName(MarburgumII::Accounts::CharacterModels[ModelIndex].Id));
    }
}

void UMarburgumAccountScreen::HandleSignOut()
{
    if (UMarburgumAccountSubsystem* Accounts = GetAccounts())
    {
        Accounts->SignOut();
    }
}

#undef LOCTEXT_NAMESPACE
