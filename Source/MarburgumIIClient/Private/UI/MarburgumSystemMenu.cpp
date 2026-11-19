#include "UI/MarburgumSystemMenu.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/SafeZone.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "UI/MarburgumUIBuilder.h"

#define LOCTEXT_NAMESPACE "MarburgumSystemMenu"

using namespace MarburgumUI;

namespace
{
    constexpr float MenuWidth = 360.f;
}

void UMarburgumSystemMenu::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    UVerticalBox* Column = B.Column();
    FBuilder::Stack(Column, B.Text(LOCTEXT("Overline", "MARBURGUM II"), ETypeRole::Overline, T.Color.Accent), T.Space.XXS);
    FBuilder::Stack(Column, B.Text(LOCTEXT("Title", "Menu"), ETypeRole::Title), T.Space.XS);
    UTextBlock* Note = B.Text(LOCTEXT("Note", "The city goes on while this is open."), ETypeRole::Caption, T.Color.TextMuted);
    Note->SetAutoWrapText(true);
    FBuilder::Stack(Column, Note, T.Space.L);

    ResumeButton = B.Button(LOCTEXT("Resume", "Back to the street"), EButtonKind::Primary);
    ResumeButton->OnClicked.AddDynamic(this, &UMarburgumSystemMenu::HandleResume);
    FBuilder::Stack(Column, ResumeButton, T.Space.S);
    UButton* LeaveButton = B.Button(LOCTEXT("Leave", "Leave the world"), EButtonKind::Secondary);
    LeaveButton->OnClicked.AddDynamic(this, &UMarburgumSystemMenu::HandleLeave);
    FBuilder::Stack(Column, LeaveButton, T.Space.S);
    UButton* QuitButton = B.Button(LOCTEXT("Quit", "Quit the game"), EButtonKind::Quiet);
    QuitButton->OnClicked.AddDynamic(this, &UMarburgumSystemMenu::HandleQuit);
    FBuilder::Stack(Column, QuitButton, T.Space.M);
    FBuilder::Stack(Column, B.Text(LOCTEXT("Hint", "[Esc] Back"), ETypeRole::Caption, T.Color.TextMuted), 0.f);

    UBorder* Panel = B.Surface(ESurfaceKind::Panel, Pad(T.Space.XL), Column);
    USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
    Width->SetWidthOverride(MenuWidth);
    Width->SetContent(Panel);

    // A dimmed street behind it says the menu, not the world, has the keys.
    UBorder* Scrim = B.Surface(ESurfaceKind::Base, Pad(0.f), Width, false);
    Scrim->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.45f));
    FBuilder::Align(Scrim, HAlign_Center, VAlign_Center);

    USafeZone* Safe = WidgetTree->ConstructWidget<USafeZone>();
    Safe->SetContent(Scrim);
    WidgetTree->RootWidget = Safe;
    SetIsFocusable(true);
    SetVisibility(ESlateVisibility::Collapsed);
}

bool UMarburgumSystemMenu::IsOpen() const
{
    return GetVisibility() != ESlateVisibility::Collapsed;
}

void UMarburgumSystemMenu::Open()
{
    SetVisibility(ESlateVisibility::Visible);
}

void UMarburgumSystemMenu::Close()
{
    SetVisibility(ESlateVisibility::Collapsed);
}

UWidget* UMarburgumSystemMenu::GetPreferredFocus() const
{
    return ResumeButton;
}

FReply UMarburgumSystemMenu::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    if (InKeyEvent.GetKey() == EKeys::Escape)
    {
        HandleResume();
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UMarburgumSystemMenu::HandleResume()
{
    OnResume.ExecuteIfBound();
}

void UMarburgumSystemMenu::HandleLeave()
{
    OnLeave.ExecuteIfBound();
}

void UMarburgumSystemMenu::HandleQuit()
{
    OnQuit.ExecuteIfBound();
}

#undef LOCTEXT_NAMESPACE
