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
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "MarburgumAccountContract.h"
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
    UWidget* Card = BuildCard();
    FBuilder::Stack(Content, Card, T.Space.XXL);
    if (UVerticalBoxSlot* CardSlot = Cast<UVerticalBoxSlot>(Card->Slot))
    {
        CardSlot->SetHorizontalAlignment(HAlign_Left);
    }
    FBuilder::Stack(Content, B.Text(LOCTEXT("Footer",
        "Development preview  ·  account service: email codes are written to the service's outbox folder until a mail sender is chosen"),
        ETypeRole::Caption, T.Color.TextMuted), 0.f);

    UBorder* Gutter = B.Surface(ESurfaceKind::Base, FMargin(T.Space.ScreenGutter, T.Space.ScreenGutter * 0.8f), Content, false);
    Gutter->SetBrushColor(FLinearColor::Transparent);
    USafeZone* Safe = WidgetTree->ConstructWidget<USafeZone>();
    Safe->SetContent(Gutter);
    UOverlaySlot* SafeSlot = Root->AddChildToOverlay(Safe);
    SafeSlot->SetHorizontalAlignment(HAlign_Fill);
    SafeSlot->SetVerticalAlignment(VAlign_Fill);
    SetIsFocusable(true);
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
        Fields(EStep::Character).Add(AddField(Panel, LOCTEXT("FirstName", "FIRST NAME"), LOCTEXT("FirstNameHint", "e.g. Špela")));
        Fields(EStep::Character).Add(AddField(Panel, LOCTEXT("LastName", "LAST NAME"), LOCTEXT("LastNameHint", "e.g. Novak")));
        FBuilder::Stack(Panel, B.Text(LOCTEXT("Body", "BODY"), ETypeRole::Overline), T.Space.XS);
        UHorizontalBox* Chooser = B.Row();
        AddButton(Chooser, LOCTEXT("PreviousModel", "‹"), Secondary, T.Space.M)->OnClicked.AddDynamic(this, &UMarburgumAccountScreen::HandlePreviousModel);
        ModelName = B.Text(FText::GetEmpty(), ETypeRole::Heading);
        FBuilder::Inline(Chooser, ModelName, T.Space.M, true);
        ModelCount = B.Text(FText::GetEmpty(), ETypeRole::Numeric, T.Color.TextMuted);
        FBuilder::Inline(Chooser, ModelCount, T.Space.M);
        AddButton(Chooser, LOCTEXT("NextModel", "›"), Secondary, 0.f)->OnClicked.AddDynamic(this, &UMarburgumAccountScreen::HandleNextModel);
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
    USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
    Width->SetWidthOverride(CardWidth);
    Width->SetContent(Card);
    return Width;
}

void UMarburgumAccountScreen::ShowModel()
{
    using namespace MarburgumII::Accounts;
    constexpr int32 Count = UE_ARRAY_COUNT(CharacterModels);
    ModelIndex = (ModelIndex % Count + Count) % Count;
    ModelName->SetText(FText::FromString(CharacterModels[ModelIndex].NameEn));
    ModelCount->SetText(FText::Format(LOCTEXT("ModelCount", "{0} / {1}"), FText::AsNumber(ModelIndex + 1), FText::AsNumber(Count)));
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

void UMarburgumAccountScreen::HandlePreviousModel()
{
    --ModelIndex;
    ShowModel();
}

void UMarburgumAccountScreen::HandleNextModel()
{
    ++ModelIndex;
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
