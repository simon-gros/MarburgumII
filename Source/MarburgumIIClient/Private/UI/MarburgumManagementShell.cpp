#include "UI/MarburgumManagementShell.h"

#include "Blueprint/WidgetTree.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SafeZone.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "MarburgumPlaces.h"
#include "MarburgumPlayerState.h"
#include "UI/MarburgumInputDevice.h"
#include "UI/MarburgumUIBuilder.h"

#define LOCTEXT_NAMESPACE "MarburgumManagementShell"

using namespace MarburgumUI;

namespace
{
    constexpr EMarburgumScreen TabOrder[] = { EMarburgumScreen::Character, EMarburgumScreen::Skills, EMarburgumScreen::Inventory,
        EMarburgumScreen::Journal, EMarburgumScreen::Settings };
    /** The page area never grows wider than this (ultrawide stays readable); it shrinks with the screen. */
    constexpr float MaxPageWidth = 1520.f;

    int32 TabIndex(EMarburgumScreen Screen)
    {
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(TabOrder); ++Index)
        {
            if (TabOrder[Index] == Screen)
            {
                return Index;
            }
        }
        return INDEX_NONE;
    }
}

EMarburgumScreen UMarburgumManagementShell::Step(EMarburgumScreen From, int32 Direction)
{
    const int32 Count = UE_ARRAY_COUNT(TabOrder);
    const int32 Index = TabIndex(From);
    if (Index == INDEX_NONE)
    {
        return TabOrder[0];
    }
    return TabOrder[((Index + Direction) % Count + Count) % Count];
}

FText UMarburgumManagementShell::ScreenName(EMarburgumScreen Screen)
{
    switch (Screen)
    {
    case EMarburgumScreen::Character: return LOCTEXT("Character", "Character");
    case EMarburgumScreen::Skills: return LOCTEXT("Skills", "Skills");
    case EMarburgumScreen::Inventory: return LOCTEXT("Inventory", "Inventory");
    case EMarburgumScreen::Journal: return LOCTEXT("Journal", "Journal");
    case EMarburgumScreen::Settings: return LOCTEXT("Settings", "Settings");
    default: return FText::GetEmpty();
    }
}

FText UMarburgumManagementShell::ScreenKey(EMarburgumScreen Screen)
{
    switch (Screen)
    {
    case EMarburgumScreen::Character: return FText::AsCultureInvariant(TEXT("C"));
    case EMarburgumScreen::Skills: return FText::AsCultureInvariant(TEXT("K"));
    case EMarburgumScreen::Inventory: return FText::AsCultureInvariant(TEXT("I"));
    case EMarburgumScreen::Journal: return FText::AsCultureInvariant(TEXT("J"));
    case EMarburgumScreen::Settings: return FText::AsCultureInvariant(TEXT("O"));
    default: return FText::GetEmpty();
    }
}

EMarburgumGlyph UMarburgumManagementShell::ScreenGlyph(EMarburgumScreen Screen)
{
    switch (Screen)
    {
    case EMarburgumScreen::Character: return EMarburgumGlyph::Character;
    case EMarburgumScreen::Skills: return EMarburgumGlyph::Skill;
    case EMarburgumScreen::Inventory: return EMarburgumGlyph::Inventory;
    case EMarburgumScreen::Journal: return EMarburgumGlyph::Journal;
    case EMarburgumScreen::Settings: return EMarburgumGlyph::Settings;
    default: return EMarburgumGlyph::None;
    }
}

void UMarburgumManagementShell::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(true);
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
    WidgetTree->RootWidget = Root;

    // The street stays faintly visible behind the screens (the world keeps running), darker at the top and left where
    // the header and lists sit.
    // A soft blur keeps the world's colour and light behind the screens without its detail competing with the text.
    UBackgroundBlur* Blur = WidgetTree->ConstructWidget<UBackgroundBlur>();
    Blur->SetBlurStrength(T.bHighContrast ? 14.f : 9.f);
    UOverlaySlot* BlurSlot = Root->AddChildToOverlay(Blur);
    BlurSlot->SetHorizontalAlignment(HAlign_Fill);
    BlurSlot->SetVerticalAlignment(VAlign_Fill);
    UImage* Dim = WidgetTree->ConstructWidget<UImage>();
    Dim->SetBrush(SolidBrush(T.Color.Scrim));
    UOverlaySlot* DimSlot = Root->AddChildToOverlay(Dim);
    DimSlot->SetHorizontalAlignment(HAlign_Fill);
    DimSlot->SetVerticalAlignment(VAlign_Fill);
    UMarburgumScrim* TopShade = B.Scrim(EMarburgumScrimShape::FadeDown, T.Color.Backdrop.CopyWithNewOpacity(0.7f));
    UOverlaySlot* ShadeSlot = Root->AddChildToOverlay(TopShade);
    ShadeSlot->SetHorizontalAlignment(HAlign_Fill);
    ShadeSlot->SetVerticalAlignment(VAlign_Fill);

    UVerticalBox* Frame = B.Column();

    // Header: who you are on the left, the screens in the middle, how to leave on the right.
    UHorizontalBox* Header = B.Row();
    UVerticalBox* Identity = B.Column();
    NameLabel = B.Text(FText::GetEmpty(), ETypeRole::PlaceTitle, T.Color.TextPrimary);
    PlaceLabel = B.Text(FText::GetEmpty(), ETypeRole::Caption, T.Color.TextSecondary);
    FBuilder::Stack(Identity, NameLabel, T.Space.XXS);
    FBuilder::Stack(Identity, PlaceLabel, 0.f);
    USizeBox* IdentityWidth = B.Bounded(Identity, 360.f, 220.f);
    FBuilder::Inline(Header, IdentityWidth, T.Space.XL, false);

    UHorizontalBox* TabRow = B.Row();
    Tabs.Reset();
    TabUnderlines.Reset();
    for (const EMarburgumScreen Screen : TabOrder)
    {
        UVerticalBox* TabColumn = B.Column();
        UHorizontalBox* TabLine = B.Row();
        FBuilder::Inline(TabLine, B.Glyph(ScreenGlyph(Screen), T.Color.TextSecondary, T.Icon.Small), T.Space.S);
        FBuilder::Inline(TabLine, B.Text(Caps(ScreenName(Screen)), ETypeRole::Overline, T.Color.TextPrimary), T.Space.S);
        FBuilder::Inline(TabLine, B.KeyCap(ScreenKey(Screen), false), 0.f);
        FBuilder::Stack(TabColumn, TabLine, T.Space.XS);
        UImage* Underline = B.Rule(T.Color.Accent, 2.f);
        Underline->SetVisibility(ESlateVisibility::Hidden);
        FBuilder::Stack(TabColumn, Underline, 0.f);
        UMarburgumSelectable* Tab = B.Selectable(TabColumn, FMargin(T.Space.M, T.Space.S, T.Space.M, T.Space.XS), true);
        Tab->OnActivated.BindWeakLambda(this, [this, Screen]()
        {
            if (Screen != Shown)
            {
                OnScreenRequested.ExecuteIfBound(Screen);
            }
        });
        FBuilder::Inline(TabRow, Tab, T.Space.XS);
        Tabs.Add(Tab);
        TabUnderlines.Add(Underline);
    }
    UBorder* TabHolder = B.Surface(ESurfaceKind::Base, FMargin(0.f), TabRow, false);
    TabHolder->SetBrushColor(FLinearColor::Transparent);
    FBuilder::Align(TabHolder, HAlign_Center, VAlign_Center);
    FBuilder::Inline(Header, TabHolder, T.Space.XL, true);

    UTextBlock* CloseKey = nullptr;
    UHorizontalBox* CloseHint = B.KeyHint(FText::AsCultureInvariant(TEXT("Esc")), LOCTEXT("Close", "Close"), &CloseKey);
    HintKeys.Add(CloseKey);
    FBuilder::Inline(Header, CloseHint, 0.f);
    FBuilder::Stack(Frame, Header, T.Space.M);
    FBuilder::Stack(Frame, B.Rule(T.Color.Hairline, 1.f), T.Space.XL);

    Pages = WidgetTree->ConstructWidget<UOverlay>();
    PageWidth = WidgetTree->ConstructWidget<USizeBox>();
    PageWidth->SetContent(Pages);
    UBorder* PageHolder = B.Surface(ESurfaceKind::Base, FMargin(0.f), PageWidth, false);
    PageHolder->SetBrushColor(FLinearColor::Transparent);
    FBuilder::Align(PageHolder, HAlign_Center, VAlign_Fill);
    FBuilder::Stack(Frame, PageHolder, T.Space.L, true);

    // Footer: the keys that work on every screen.
    UHorizontalBox* Footer = B.Row();
    FBuilder::Inline(Footer, B.Spacer(1.f, 1.f), 0.f, true);
    // The keys of the device in use (RefreshHintKeys): Close, Switch screen, Move, Select, Back.
    UTextBlock* Keys[4] = {};
    FBuilder::Inline(Footer, B.KeyHint(FText::AsCultureInvariant(TEXT("Q / E")), LOCTEXT("SwitchScreen", "Switch screen"), &Keys[0]), T.Space.XL);
    FBuilder::Inline(Footer, B.KeyHint(FText::AsCultureInvariant(TEXT("↑ ↓")), LOCTEXT("Move", "Move"), &Keys[1]), T.Space.XL);
    FBuilder::Inline(Footer, B.KeyHint(FText::AsCultureInvariant(TEXT("Enter")), LOCTEXT("Select", "Select"), &Keys[2]), T.Space.XL);
    FBuilder::Inline(Footer, B.KeyHint(FText::AsCultureInvariant(TEXT("Esc")), LOCTEXT("Back", "Back to the street"), &Keys[3]), 0.f);
    HintKeys.Append(Keys, UE_ARRAY_COUNT(Keys));
    RefreshHintKeys();
    FBuilder::Stack(Frame, Footer, 0.f);

    UBorder* Gutter = B.Surface(ESurfaceKind::Base, FMargin(T.Space.XXXL, T.Space.XL, T.Space.XXXL, T.Space.L), Frame, false);
    Gutter->SetBrushColor(FLinearColor::Transparent);
    FBuilder::Align(Gutter, HAlign_Fill, VAlign_Fill);
    USafeZone* Safe = WidgetTree->ConstructWidget<USafeZone>();
    Safe->SetContent(Gutter);
    UOverlaySlot* SafeSlot = Root->AddChildToOverlay(Safe);
    SafeSlot->SetHorizontalAlignment(HAlign_Fill);
    SafeSlot->SetVerticalAlignment(VAlign_Fill);

    SetVisibility(ESlateVisibility::Collapsed);
}

void UMarburgumManagementShell::AddPage(EMarburgumScreen Screen, UWidget* Page)
{
    if (!Pages || !Page || PageWidgets.Contains(Screen))
    {
        return;
    }
    UOverlaySlot* PageSlot = Pages->AddChildToOverlay(Page);
    PageSlot->SetHorizontalAlignment(HAlign_Fill);
    PageSlot->SetVerticalAlignment(VAlign_Fill);
    PageWidgets.Add(Screen, Page);
}

void UMarburgumManagementShell::Show(EMarburgumScreen Screen)
{
    Shown = Screen;
    const int32 Selected = TabIndex(Screen);
    for (int32 Index = 0; Index < Tabs.Num(); ++Index)
    {
        Tabs[Index]->SetSelected(Index == Selected);
        TabUnderlines[Index]->SetVisibility(Index == Selected ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
    }
    RefreshIdentity();
    SetVisibility(ESlateVisibility::Visible);
}

void UMarburgumManagementShell::Hide()
{
    Shown = EMarburgumScreen::None;
    SetVisibility(ESlateVisibility::Collapsed);
}

void UMarburgumManagementShell::RefreshIdentity()
{
    const APlayerController* Owner = GetOwningPlayer();
    const AMarburgumPlayerState* State = Owner ? Owner->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    if (State && State->HasIdentity())
    {
        NameLabel->SetText(FText::Format(LOCTEXT("FullName", "{0} {1}"), FText::FromString(State->GetFirstName()),
            FText::FromString(State->GetLastName())));
    }
    else
    {
        NameLabel->SetText(LOCTEXT("Unnamed", "Development character"));
    }
    FText Place = LOCTEXT("Maribor", "Maribor");
    if (State && State->IsLocationKnown())
    {
        const MarburgumII::Places::FDistrict* District = State->GetPlace().District.IsEmpty() ? nullptr
            : MarburgumII::Places::Get().FindDistrictById(State->GetPlace().District);
        const FString& Street = State->GetPlace().Street;
        if (District && !Street.IsEmpty())
        {
            Place = FText::Format(LOCTEXT("DistrictStreet", "{0}  ·  {1}"), FText::FromString(District->DisplayName), FText::FromString(Street));
        }
        else if (District)
        {
            Place = FText::FromString(District->DisplayName);
        }
        else if (!Street.IsEmpty())
        {
            Place = FText::FromString(Street);
        }
    }
    PlaceLabel->SetText(Place);
}

FReply UMarburgumManagementShell::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    const FKey Key = InKeyEvent.GetKey();
    if (InKeyEvent.IsRepeat() && Key != EKeys::PageUp && Key != EKeys::PageDown)
    {
        return FReply::Handled();
    }
    if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Right)
    {
        OnCloseRequested.ExecuteIfBound();
        return FReply::Handled();
    }
    const TPair<FKey, EMarburgumScreen> Direct[] = { { EKeys::C, EMarburgumScreen::Character }, { EKeys::K, EMarburgumScreen::Skills },
        { EKeys::I, EMarburgumScreen::Inventory }, { EKeys::J, EMarburgumScreen::Journal }, { EKeys::O, EMarburgumScreen::Settings },
        // The pad's Journal (View) and Inventory (Y) buttons close what they opened, as J and I do.
        { EKeys::Gamepad_Special_Left, EMarburgumScreen::Journal }, { EKeys::Gamepad_FaceButton_Top, EMarburgumScreen::Inventory } };
    for (const TPair<FKey, EMarburgumScreen>& Entry : Direct)
    {
        if (Key == Entry.Key)
        {
            OnScreenRequested.ExecuteIfBound(Entry.Value);
            return FReply::Handled();
        }
    }
    int32 Direction = 0;
    if (Key == EKeys::Tab)
    {
        Direction = InKeyEvent.IsShiftDown() ? -1 : 1;
    }
    else if (Key == EKeys::Q || Key == EKeys::Gamepad_LeftShoulder)
    {
        Direction = -1;
    }
    else if (Key == EKeys::E || Key == EKeys::Gamepad_RightShoulder)
    {
        Direction = 1;
    }
    if (Direction != 0)
    {
        OnScreenRequested.ExecuteIfBound(Step(Shown, Direction));
        return FReply::Handled();
    }
    if (Key == EKeys::PageUp || Key == EKeys::PageDown)
    {
        OnScrollRequested.ExecuteIfBound(Key == EKeys::PageUp ? -12.f : 12.f);
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UMarburgumManagementShell::RefreshHintKeys()
{
    HintDevice = ActiveInputDevice();
    const bool bPad = HintDevice == EMarburgumInputDevice::Gamepad;
    const FText Labels[] = { InputKeyLabel(EMarburgumInputAction::Back, HintDevice),
        FText::AsCultureInvariant(bPad ? TEXT("LB / RB") : TEXT("Q / E")), InputKeyLabel(EMarburgumInputAction::Select, HintDevice),
        InputKeyLabel(EMarburgumInputAction::Confirm, HintDevice), InputKeyLabel(EMarburgumInputAction::Back, HintDevice) };
    for (int32 Index = 0; Index < HintKeys.Num() && Index < UE_ARRAY_COUNT(Labels); ++Index)
    {
        if (HintKeys[Index])
        {
            HintKeys[Index]->SetText(Labels[Index]);
        }
    }
}

void UMarburgumManagementShell::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    if (HintDevice != ActiveInputDevice())
    {
        RefreshHintKeys();
    }
    if (!IsOpen() || !PageWidth)
    {
        return;
    }
    const float Gutters = 2.f * Tokens().Space.XXXL;
    const float Width = FMath::Max(320.f, FMath::Min(MaxPageWidth, MyGeometry.GetLocalSize().X - Gutters));
    if (!FMath::IsNearlyEqual(Width, PageWidth->GetWidthOverride(), 1.f))
    {
        PageWidth->SetWidthOverride(Width);
    }
}

FReply UMarburgumManagementShell::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    // A scroll region under the pointer consumes the wheel first; elsewhere it scrolls the open page.
    OnScrollRequested.ExecuteIfBound(InMouseEvent.GetWheelDelta() > 0.f ? -3.f : 3.f);
    return FReply::Handled();
}

FReply UMarburgumManagementShell::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    // Clicks on the dimmed street stay with the screen; the world does not take them while it is open.
    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
