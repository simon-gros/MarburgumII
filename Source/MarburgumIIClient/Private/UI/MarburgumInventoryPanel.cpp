#include "UI/MarburgumInventoryPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/SafeZone.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/GameInstance.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumPlayerState.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "UI/MarburgumUIBuilder.h"

#define LOCTEXT_NAMESPACE "MarburgumInventoryPanel"

using namespace MarburgumUI;

namespace
{
    constexpr float PanelWidth = 520.f;
    // Right of centre: the notification stack sits at the right edge, the prompt below centre.
    constexpr float RightOffset = 440.f;
}

void UMarburgumInventoryPanel::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    UVerticalBox* Column = B.Column();
    UHorizontalBox* Header = B.Row();
    UVerticalBox* Titles = B.Column();
    FBuilder::Stack(Titles, B.Text(LOCTEXT("Overline", "INVENTORY"), ETypeRole::Overline, T.Color.Accent), T.Space.XXS);
    FBuilder::Stack(Titles, B.Text(LOCTEXT("Title", "Carried items"), ETypeRole::Title), 0.f);
    FBuilder::Inline(Header, Titles, T.Space.M, true);
    FBuilder::Inline(Header, B.Text(LOCTEXT("Close", "[I] Close"), ETypeRole::Caption, T.Color.TextMuted), 0.f);
    FBuilder::Stack(Column, Header, T.Space.S);

    CharacterLabel = B.Text(FText::GetEmpty(), ETypeRole::Caption);
    CharacterLabel->SetAutoWrapText(true);
    FBuilder::Stack(Column, CharacterLabel, T.Space.M);
    FBuilder::Stack(Column, B.Rule(T.Color.Border, 1.f), T.Space.M);

    Rows = B.Column();
    FBuilder::Stack(Column, Rows, T.Space.M);

    SummaryLabel = B.Text(FText::GetEmpty(), ETypeRole::Caption, T.Color.TextMuted);
    SummaryLabel->SetAutoWrapText(true);
    FBuilder::Stack(Column, SummaryLabel, 0.f);

    UBorder* Panel = B.Surface(ESurfaceKind::Panel, Pad(T.Space.XL), Column);
    USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
    Width->SetWidthOverride(PanelWidth);
    Width->SetContent(Panel);

    UBorder* Anchor = B.Surface(ESurfaceKind::Base, FMargin(0.f, 0.f, RightOffset, 0.f), Width, false);
    Anchor->SetBrushColor(FLinearColor::Transparent);
    FBuilder::Align(Anchor, HAlign_Right, VAlign_Center);

    USafeZone* Safe = WidgetTree->ConstructWidget<USafeZone>();
    Safe->SetContent(Anchor);
    WidgetTree->RootWidget = Safe;
    SetVisibility(ESlateVisibility::Collapsed);
}

void UMarburgumInventoryPanel::NativeConstruct()
{
    Super::NativeConstruct();
    InventoryHandle = AMarburgumPlayerState::OnInventoryChanged().AddUObject(this, &UMarburgumInventoryPanel::HandleInventoryChanged);
    Rebuild();
}

void UMarburgumInventoryPanel::NativeDestruct()
{
    AMarburgumPlayerState::OnInventoryChanged().Remove(InventoryHandle);
    Super::NativeDestruct();
}

void UMarburgumInventoryPanel::Toggle()
{
    SetVisibility(IsOpen() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    if (IsOpen())
    {
        Rebuild();
    }
}

bool UMarburgumInventoryPanel::IsOpen() const
{
    return GetVisibility() != ESlateVisibility::Collapsed;
}

void UMarburgumInventoryPanel::HandleInventoryChanged(AMarburgumPlayerState* State)
{
    const APlayerController* Owner = GetOwningPlayer();
    if (Owner && State == Owner->PlayerState)
    {
        Rebuild();
    }
}

void UMarburgumInventoryPanel::Rebuild()
{
    if (!Rows)
    {
        return;
    }
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    Rows->ClearChildren();

    const APlayerController* Owner = GetOwningPlayer();
    const AMarburgumPlayerState* State = Owner ? Owner->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    const UGameInstance* GameInstance = GetGameInstance();
    const UMarburgumSessionSubsystem* Session = GameInstance ? GameInstance->GetSubsystem<UMarburgumSessionSubsystem>() : nullptr;
    const FString Key = Session ? Session->GetCharacterKey() : FString();
    CharacterLabel->SetText(Key.IsEmpty()
        ? LOCTEXT("NoKey", "No development character on this connection")
        : FText::Format(LOCTEXT("Key", "Development character {0}  ·  a test identity, not an account"), FText::FromString(Key)));

    if (!State || !State->IsInventoryKnown())
    {
        FBuilder::Stack(Rows, B.StateCard(LOCTEXT("WaitingTitle", "Waiting for the server"),
            LOCTEXT("WaitingBody", "The inventory appears once the server has loaded this character's saved record."), ETone::Info), 0.f);
        SummaryLabel->SetText(FText::GetEmpty());
        return;
    }
    if (!State->HasCharacter())
    {
        FBuilder::Stack(Rows, B.StateCard(LOCTEXT("NoCharacterTitle", "No character selected"),
            LOCTEXT("NoCharacterBody", "Join with a development character key to hold items. Interactions are refused without one."), ETone::Warning), 0.f);
        SummaryLabel->SetText(FText::GetEmpty());
        return;
    }
    const TArray<FMarburgumItemStack>& Items = State->GetInventory();
    if (Items.IsEmpty())
    {
        FBuilder::Stack(Rows, B.StateCard(LOCTEXT("EmptyTitle", "Nothing carried yet"),
            LOCTEXT("EmptyBody", "Items appear here only after the server has confirmed and saved them."), ETone::Neutral), 0.f);
        SummaryLabel->SetText(FText::GetEmpty());
        return;
    }

    int32 Total = 0;
    for (const FMarburgumItemStack& Stack : Items)
    {
        const MarburgumII::Gameplay::FItemDefinition* Definition = MarburgumII::Gameplay::FindItem(Stack.ItemId.ToString());
        UHorizontalBox* Line = B.Row();
        UVerticalBox* Names = B.Column();
        FBuilder::Stack(Names, B.Text(Definition ? FText::FromString(Definition->DisplayName) : FText::FromName(Stack.ItemId),
            ETypeRole::BodyStrong), T.Space.XXS);
        FBuilder::Stack(Names, B.Text(Definition ? LOCTEXT("Registered", "Registered item  ·  server-confirmed")
            : LOCTEXT("Unregistered", "Unknown to this client version"), ETypeRole::Caption, T.Color.TextMuted), 0.f);
        FBuilder::Inline(Line, Names, T.Space.M, true);
        FBuilder::Inline(Line, B.Text(FText::Format(LOCTEXT("Quantity", "×{0}"), FText::AsNumber(Stack.Quantity)),
            ETypeRole::Numeric, T.Color.TextPrimary), 0.f);
        FBuilder::Stack(Rows, B.Surface(ESurfaceKind::Raised, Pad(T.Space.L, T.Space.M), Line), T.Space.S);
        Total += Stack.Quantity;
    }
    SummaryLabel->SetText(FText::Format(LOCTEXT("Summary", "{0} {0}|plural(one=item,other=items) in {1} {1}|plural(one=stack,other=stacks). Saved by the server; survives a restart."),
        FText::AsNumber(Total), FText::AsNumber(Items.Num())));
}

#undef LOCTEXT_NAMESPACE
