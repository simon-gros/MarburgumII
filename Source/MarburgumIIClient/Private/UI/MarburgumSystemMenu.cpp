#include "UI/MarburgumSystemMenu.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SafeZone.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "MarburgumGameState.h"
#include "MarburgumWorldClock.h"
#include "UI/MarburgumUIBuilder.h"

#define LOCTEXT_NAMESPACE "MarburgumSystemMenu"

using namespace MarburgumUI;

namespace
{
    constexpr float MenuWidth = 420.f;

    UMarburgumSelectable* Entry(const FBuilder& B, EMarburgumGlyph Icon, const FText& Label, const FText& Detail, bool bDanger = false)
    {
        const FTokens& T = Tokens();
        UHorizontalBox* Line = B.Row();
        FBuilder::Inline(Line, B.Glyph(Icon, bDanger ? T.Color.Danger : T.Color.Accent, T.Icon.Medium), T.Space.L);
        UVerticalBox* Text = B.Column();
        FBuilder::Stack(Text, B.Text(Label, ETypeRole::Subheading, T.Color.TextPrimary), Detail.IsEmpty() ? 0.f : T.Space.XXS);
        if (!Detail.IsEmpty())
        {
            UTextBlock* DetailBlock = B.Text(Detail, ETypeRole::Caption, T.Color.TextSecondary);
            DetailBlock->SetAutoWrapText(true);
            FBuilder::Stack(Text, DetailBlock, 0.f);
        }
        FBuilder::Inline(Line, Text, 0.f, true);
        return B.Selectable(Line, FMargin(T.Space.L, T.Space.M), true);
    }
}

void UMarburgumSystemMenu::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
    WidgetTree->RootWidget = Root;
    // The street stays visible on the right: the world is not paused. The left falls into shadow for the menu.
    UImage* Dim = WidgetTree->ConstructWidget<UImage>();
    Dim->SetBrush(SolidBrush(T.Color.Scrim.CopyWithNewOpacity(T.Color.Scrim.A * 0.55f)));
    UOverlaySlot* DimSlot = Root->AddChildToOverlay(Dim);
    DimSlot->SetHorizontalAlignment(HAlign_Fill);
    DimSlot->SetVerticalAlignment(VAlign_Fill);
    UMarburgumScrim* Shade = B.Scrim(EMarburgumScrimShape::FadeRight, T.Color.Backdrop.CopyWithNewOpacity(0.92f));
    USizeBox* ShadeWidth = WidgetTree->ConstructWidget<USizeBox>();
    ShadeWidth->SetWidthOverride(MenuWidth * 2.2f);
    ShadeWidth->SetContent(Shade);
    UOverlaySlot* ShadeSlot = Root->AddChildToOverlay(ShadeWidth);
    ShadeSlot->SetHorizontalAlignment(HAlign_Left);
    ShadeSlot->SetVerticalAlignment(VAlign_Fill);

    UVerticalBox* Column = B.Column();
    FBuilder::Stack(Column, B.Rule(T.Color.Accent, 2.f, 40.f), T.Space.L);
    if (UPanelSlot* RuleSlot = Column->GetSlots().Last())
    {
        Cast<UVerticalBoxSlot>(RuleSlot)->SetHorizontalAlignment(HAlign_Left);
    }
    FBuilder::Stack(Column, B.Text(LOCTEXT("Wordmark", "Marburgum"), ETypeRole::Display, T.Color.TextPrimary), T.Space.XXS);
    ClockLabel = B.Text(FText::GetEmpty(), ETypeRole::Overline, T.Color.TextSecondary);
    FBuilder::Stack(Column, ClockLabel, T.Space.XXL);

    Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>();

    // Page 0: the entries.
    UVerticalBox* Entries = B.Column();
    ResumeEntry = Entry(B, EMarburgumGlyph::ChevronLeft, LOCTEXT("Resume", "Back to the street"), FText::GetEmpty());
    ResumeEntry->OnActivated.BindWeakLambda(this, [this]() { OnResume.ExecuteIfBound(); });
    FBuilder::Stack(Entries, ResumeEntry, T.Space.XS);
    UMarburgumSelectable* SettingsEntry = Entry(B, EMarburgumGlyph::Settings, LOCTEXT("Settings", "Settings"),
        LOCTEXT("SettingsDetail", "Interface, sound, display and keys"));
    SettingsEntry->OnActivated.BindWeakLambda(this, [this]() { OnSettings.ExecuteIfBound(); });
    FBuilder::Stack(Entries, SettingsEntry, T.Space.XS);
    UMarburgumSelectable* LeaveEntry = Entry(B, EMarburgumGlyph::Exit, LOCTEXT("Leave", "Leave the world"),
        LOCTEXT("LeaveDetail", "Back to the title screen. Your character is saved."));
    LeaveEntry->OnActivated.BindWeakLambda(this, [this]() { AskToConfirm(false); });
    FBuilder::Stack(Entries, LeaveEntry, T.Space.XS);
    UMarburgumSelectable* QuitEntry = Entry(B, EMarburgumGlyph::Close, LOCTEXT("Quit", "Quit the game"), FText::GetEmpty(), true);
    QuitEntry->OnActivated.BindWeakLambda(this, [this]() { AskToConfirm(true); });
    FBuilder::Stack(Entries, QuitEntry, 0.f);
    Pages->AddChild(Entries);

    // Page 1: are you sure.
    UVerticalBox* Confirm = B.Column();
    ConfirmTitle = B.Text(FText::GetEmpty(), ETypeRole::Heading);
    FBuilder::Stack(Confirm, ConfirmTitle, T.Space.S);
    ConfirmBody = B.Text(FText::GetEmpty(), ETypeRole::Body, T.Color.TextSecondary);
    ConfirmBody->SetAutoWrapText(true);
    FBuilder::Stack(Confirm, ConfirmBody, T.Space.L);
    ConfirmEntry = Entry(B, EMarburgumGlyph::Exit, FText::GetEmpty(), FText::GetEmpty(), true);
    ConfirmEntry->OnActivated.BindWeakLambda(this, [this]()
    {
        (bConfirmQuit ? OnQuit : OnLeave).ExecuteIfBound();
    });
    FBuilder::Stack(Confirm, ConfirmEntry, T.Space.XS);
    StayEntry = Entry(B, EMarburgumGlyph::ChevronLeft, LOCTEXT("Stay", "Stay"), FText::GetEmpty());
    StayEntry->OnActivated.BindWeakLambda(this, [this]() { CancelConfirm(); });
    FBuilder::Stack(Confirm, StayEntry, 0.f);
    UBorder* ConfirmCard = B.Surface(ESurfaceKind::Panel, Pad(T.Space.XL), Confirm);
    ConfirmCard->SetBrush(RoundedBrush(T.Color.Panel, T.Radius.Large, T.Color.Danger.CopyWithNewOpacity(0.6f), T.BorderWidth));
    Pages->AddChild(ConfirmCard);
    FBuilder::Stack(Column, Pages, T.Space.XXL);

    UTextBlock* Note = B.Text(LOCTEXT("Note", "The city goes on while this is open: Marburgum is one shared world and never pauses."),
        ETypeRole::Caption, T.Color.TextMuted);
    Note->SetAutoWrapText(true);
    FBuilder::Stack(Column, Note, T.Space.L);
    UHorizontalBox* Hints = B.Row();
    FBuilder::Inline(Hints, B.KeyHint(FText::AsCultureInvariant(TEXT("Enter")), LOCTEXT("Select", "Select")), T.Space.XL);
    FBuilder::Inline(Hints, B.KeyHint(FText::AsCultureInvariant(TEXT("Esc")), LOCTEXT("Back", "Back")), 0.f);
    FBuilder::Stack(Column, Hints, 0.f);

    USizeBox* Width = B.Bounded(Column, MenuWidth, 300.f);
    UBorder* Gutter = B.Surface(ESurfaceKind::Base, FMargin(T.Space.XXXL * 2.f, T.Space.XXXL), Width, false);
    Gutter->SetBrushColor(FLinearColor::Transparent);
    FBuilder::Align(Gutter, HAlign_Left, VAlign_Center);
    USafeZone* Safe = WidgetTree->ConstructWidget<USafeZone>();
    Safe->SetContent(Gutter);
    UOverlaySlot* SafeSlot = Root->AddChildToOverlay(Safe);
    SafeSlot->SetHorizontalAlignment(HAlign_Fill);
    SafeSlot->SetVerticalAlignment(VAlign_Fill);

    SetIsFocusable(true);
    SetVisibility(ESlateVisibility::Collapsed);
}

bool UMarburgumSystemMenu::IsOpen() const
{
    return GetVisibility() != ESlateVisibility::Collapsed;
}

void UMarburgumSystemMenu::Open()
{
    Pages->SetActiveWidgetIndex(0);
    FDateTime Utc;
    if (AMarburgumGameState::WorldUtc(GetWorld(), Utc))
    {
        const MarburgumII::Clock::FCivil Now = MarburgumII::Clock::MarburgumAt(Utc);
        ClockLabel->SetText(Caps(FString::Printf(TEXT("Maribor  ·  %s  ·  %s"), *MarburgumII::Clock::DateLabel(Now),
            *MarburgumII::Clock::TimeLabel(Now))));
    }
    else
    {
        ClockLabel->SetText(Caps(FString(TEXT("Maribor"))));
    }
    SetVisibility(ESlateVisibility::Visible);
}

void UMarburgumSystemMenu::Close()
{
    SetVisibility(ESlateVisibility::Collapsed);
}

UWidget* UMarburgumSystemMenu::GetPreferredFocus() const
{
    return Pages && Pages->GetActiveWidgetIndex() == 1 ? StayEntry.Get() : ResumeEntry.Get();
}

bool UMarburgumSystemMenu::IsConfirming() const
{
    return IsOpen() && Pages && Pages->GetActiveWidgetIndex() == 1;
}

void UMarburgumSystemMenu::AskToConfirm(bool bQuit)
{
    bConfirmQuit = bQuit;
    ConfirmTitle->SetText(bQuit ? LOCTEXT("QuitTitle", "Quit Marburgum?") : LOCTEXT("LeaveTitle", "Leave the world?"));
    ConfirmBody->SetText(bQuit
        ? LOCTEXT("QuitBody", "The game closes. The server saves your character as you go; the city carries on without you.")
        : LOCTEXT("LeaveBody", "You return to the title screen. The server saves your character as you go, and you can come back."));
    if (ConfirmEntry)
    {
        // The entry's label is the first text in its content.
        TArray<UWidget*> Children;
        WidgetTree->GetChildWidgets(ConfirmEntry, Children);
        for (UWidget* Child : Children)
        {
            if (UTextBlock* Text = Cast<UTextBlock>(Child))
            {
                Text->SetText(bQuit ? LOCTEXT("QuitConfirm", "Quit the game") : LOCTEXT("LeaveConfirm", "Leave the world"));
                break;
            }
        }
    }
    Pages->SetActiveWidgetIndex(1);
    StayEntry->Focus();
}

void UMarburgumSystemMenu::CancelConfirm()
{
    Pages->SetActiveWidgetIndex(0);
    ResumeEntry->Focus();
}

FReply UMarburgumSystemMenu::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    const FKey Key = InKeyEvent.GetKey();
    if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Right)
    {
        if (IsConfirming())
        {
            CancelConfirm();
        }
        else
        {
            OnResume.ExecuteIfBound();
        }
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

#undef LOCTEXT_NAMESPACE
