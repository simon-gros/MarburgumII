#include "UI/MarburgumInventoryPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Engine/GameInstance.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumPlayerState.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "UI/MarburgumEvidenceText.h"
#include "UI/MarburgumUIBuilder.h"

#define LOCTEXT_NAMESPACE "MarburgumInventoryPanel"

using namespace MarburgumUI;
using MarburgumII::Gameplay::EItemKind;

namespace
{
    constexpr float TileWidth = 176.f;
    constexpr float TileHeight = 132.f;
    constexpr int32 FilterCount = 5;

    EMarburgumGlyph KindGlyph(EItemKind Kind)
    {
        switch (Kind)
        {
        case EItemKind::Tool: return EMarburgumGlyph::Use;
        case EItemKind::Fish: return EMarburgumGlyph::Fish;
        case EItemKind::Clue: return EMarburgumGlyph::Clue;
        case EItemKind::Goods: return EMarburgumGlyph::Item;
        case EItemKind::Photo: return EMarburgumGlyph::Clue;
        default: return EMarburgumGlyph::Item;
        }
    }

    FText KindName(EItemKind Kind)
    {
        switch (Kind)
        {
        case EItemKind::Tool: return LOCTEXT("Tool", "Tool");
        case EItemKind::Fish: return LOCTEXT("Fish", "Fish");
        case EItemKind::Clue: return LOCTEXT("Clue", "Evidence");
        case EItemKind::Goods: return LOCTEXT("Goods", "Goods");
        case EItemKind::Photo: return LOCTEXT("Photo", "Photograph");
        default: return LOCTEXT("Item", "Item");
        }
    }

    FText FilterName(int32 Filter)
    {
        return Filter == 0 ? LOCTEXT("All", "All") : KindName(static_cast<EItemKind>(Filter - 1));
    }

    bool Lists(const MarburgumII::Gameplay::FItemQuantity* List, int32 Count, const FString& Id)
    {
        for (int32 Index = 0; Index < Count; ++Index)
        {
            if (Id == List[Index].Id)
            {
                return true;
            }
        }
        return false;
    }

    /** Missions whose definition gives, asks for or takes the item: facts of the shared catalogue, not guesses. */
    TArray<const MarburgumII::Gameplay::FMissionDefinition*> MissionsFor(const FString& Id)
    {
        using namespace MarburgumII::Gameplay;
        TArray<const FMissionDefinition*> Out;
        for (const FMissionDefinition& Mission : Missions)
        {
            bool bListed = Lists(Mission.StartItems, Mission.StartItemCount, Id);
            for (int32 Stage = 0; !bListed && Stage < Mission.StageCount; ++Stage)
            {
                const FMissionStageDefinition& Def = Mission.Stages[Stage];
                bListed = Lists(Def.RequiredItems, Def.RequiredItemCount, Id) || Lists(Def.GrantItems, Def.GrantItemCount, Id);
                for (int32 Item = 0; !bListed && Item < Def.ObjectiveItemCount; ++Item)
                {
                    bListed = Id == Def.ObjectiveItems[Item];
                }
            }
            if (bListed)
            {
                Out.Add(&Mission);
            }
        }
        return Out;
    }
}

void UMarburgumInventoryPanel::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    UHorizontalBox* Columns = B.Row();

    UVerticalBox* ListColumn = B.Column();
    UHorizontalBox* Header = B.Row();
    UVerticalBox* Titles = B.Column();
    FBuilder::Stack(Titles, B.Text(LOCTEXT("Overline", "INVENTORY"), ETypeRole::Overline, T.Color.Accent), T.Space.XXS);
    FBuilder::Stack(Titles, B.Text(LOCTEXT("Title", "What you carry"), ETypeRole::Title), 0.f);
    FBuilder::Inline(Header, Titles, T.Space.L, true);
    SummaryLabel = B.Text(FText::GetEmpty(), ETypeRole::Caption, T.Color.TextSecondary);
    FBuilder::Inline(Header, SummaryLabel, 0.f);
    FBuilder::Stack(ListColumn, Header, T.Space.L);

    Filters = B.Row();
    FilterWidgets.Reset();
    for (int32 Index = 0; Index < FilterCount; ++Index)
    {
        UHorizontalBox* Label = B.Row();
        if (Index > 0)
        {
            FBuilder::Inline(Label, B.Glyph(KindGlyph(static_cast<EItemKind>(Index - 1)), T.Color.TextSecondary, T.Icon.Small), T.Space.XS);
        }
        FBuilder::Inline(Label, B.Text(Caps(FilterName(Index)), ETypeRole::Overline, T.Color.TextPrimary), 0.f);
        UMarburgumSelectable* Chip = B.Selectable(Label, FMargin(T.Space.M, T.Space.S), true);
        Chip->SetRadius(T.Radius.Pill);
        Chip->OnActivated.BindWeakLambda(this, [this, Index]() { SetFilter(Index); });
        FBuilder::Inline(Filters, Chip, T.Space.XS);
        FilterWidgets.Add(Chip);
    }
    FBuilder::Stack(ListColumn, Filters, T.Space.M);

    UVerticalBox* GridColumn = B.Column();
    StateBox = B.Column();
    FBuilder::Stack(GridColumn, StateBox, 0.f);
    Grid = WidgetTree->ConstructWidget<UWrapBox>();
    Grid->SetInnerSlotPadding(FVector2D(T.Space.S, T.Space.S));
    FBuilder::Stack(GridColumn, Grid, 0.f);
    Scroller = B.Scroll(GridColumn);
    FBuilder::Stack(ListColumn, Scroller, 0.f, true);
    FBuilder::Inline(Columns, B.Bounded(ListColumn, 900.f, 400.f), T.Space.XXL, true, false);

    DetailBox = B.Column();
    UBorder* DetailCard = B.Surface(ESurfaceKind::Panel, Pad(T.Space.XL), DetailBox);
    FBuilder::InlineTop(Columns, B.Bounded(DetailCard, 500.f, 320.f), 0.f);

    WidgetTree->RootWidget = Columns;
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

void UMarburgumInventoryPanel::SetOpen(bool bOpen)
{
    SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    if (bOpen)
    {
        Rebuild();
        if (Scroller)
        {
            Scroller->ScrollToStart();
        }
    }
}

bool UMarburgumInventoryPanel::IsOpen() const
{
    return GetVisibility() != ESlateVisibility::Collapsed;
}

void UMarburgumInventoryPanel::Scroll(float Lines)
{
    FBuilder::ScrollBy(Scroller, Lines);
}

UWidget* UMarburgumInventoryPanel::GetPreferredFocus() const
{
    const int32 Index = TileItems.IndexOfByKey(SelectedItem);
    if (TileWidgets.IsValidIndex(Index))
    {
        return TileWidgets[Index];
    }
    return TileWidgets.Num() > 0 ? TileWidgets[0].Get() : FilterWidgets.IsValidIndex(Filter) ? FilterWidgets[Filter].Get() : nullptr;
}

void UMarburgumInventoryPanel::HandleInventoryChanged(AMarburgumPlayerState* State)
{
    const APlayerController* Owner = GetOwningPlayer();
    if (IsOpen() && Owner && State == Owner->PlayerState)
    {
        const bool bHadFocus = HasFocusedDescendants();
        Rebuild();
        if (bHadFocus)
        {
            if (UWidget* Focus = GetPreferredFocus())
            {
                Focus->SetFocus();
            }
        }
    }
}

void UMarburgumInventoryPanel::SetFilter(int32 InFilter)
{
    Filter = FMath::Clamp(InFilter, 0, FilterCount - 1);
    Rebuild();
}

void UMarburgumInventoryPanel::Rebuild()
{
    if (!Grid)
    {
        return;
    }
    using namespace MarburgumII::Gameplay;
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    Grid->ClearChildren();
    StateBox->ClearChildren();
    TileWidgets.Reset();
    TileItems.Reset();
    for (int32 Index = 0; Index < FilterWidgets.Num(); ++Index)
    {
        FilterWidgets[Index]->SetSelected(Index == Filter);
    }

    const APlayerController* Owner = GetOwningPlayer();
    const AMarburgumPlayerState* State = Owner ? Owner->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    auto ShowState = [&](UWidget* Card)
    {
        FBuilder::Stack(StateBox, Card, 0.f);
        SummaryLabel->SetText(FText::GetEmpty());
        DetailBox->ClearChildren();
    };
    if (!State || !State->IsInventoryKnown())
    {
        ShowState(B.StateCard(LOCTEXT("WaitingTitle", "Waiting for the server"),
            LOCTEXT("WaitingBody", "The inventory appears once the server has loaded this character's saved record."), ETone::Info));
        return;
    }
    if (!State->HasCharacter())
    {
        ShowState(B.StateCard(LOCTEXT("NoCharacterTitle", "No character selected"),
            LOCTEXT("NoCharacterBody", "Join with a character to hold items. Interactions are refused without one."), ETone::Warning));
        return;
    }
    const TArray<FMarburgumItemStack>& Stacks = State->GetInventory();
    if (Stacks.IsEmpty())
    {
        ShowState(B.StateCard(LOCTEXT("EmptyTitle", "Nothing carried yet"),
            LOCTEXT("EmptyBody", "Items appear here only after the server has confirmed and saved them."), ETone::Neutral));
        return;
    }

    int32 Total = 0;
    for (const FMarburgumItemStack& Stack : Stacks)
    {
        Total += Stack.Quantity;
        const FItemDefinition* Definition = FindItem(Stack.ItemId.ToString());
        if (Filter != 0 && (!Definition || static_cast<int32>(Definition->Kind) != Filter - 1))
        {
            continue;
        }
        const EItemKind Kind = Definition ? Definition->Kind : EItemKind::Goods;
        UVerticalBox* Tile = B.Column();
        UHorizontalBox* Top = B.Row();
        FBuilder::Inline(Top, B.Glyph(KindGlyph(Kind), Definition ? T.Color.Accent : T.Color.TextMuted, T.Icon.Large), T.Space.S, true);
        FBuilder::Inline(Top, B.Text(FText::Format(LOCTEXT("Quantity", "×{0}"), FText::AsNumber(Stack.Quantity)), ETypeRole::Numeric,
            T.Color.TextPrimary), 0.f);
        FBuilder::Stack(Tile, Top, T.Space.S);
        UTextBlock* Name = B.Text(Definition ? FText::FromString(MarburgumII::Gameplay::Localized(Definition->DisplayName)) : FText::FromName(Stack.ItemId), ETypeRole::Label);
        Name->SetAutoWrapText(true);
        FBuilder::Stack(Tile, Name, 0.f, true);
        FBuilder::Stack(Tile, B.Text(Caps(KindName(Kind)), ETypeRole::Overline, T.Color.TextMuted), 0.f);

        USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
        Size->SetWidthOverride(TileWidth);
        Size->SetHeightOverride(TileHeight);
        Size->SetContent(Tile);
        UMarburgumSelectable* Cell = B.Selectable(Size, Pad(T.Space.M));
        const FName ItemId = Stack.ItemId;
        Cell->OnFocused.BindWeakLambda(this, [this, ItemId]() { ShowDetail(ItemId); });
        Cell->OnActivated.BindWeakLambda(this, [this, ItemId]() { ShowDetail(ItemId); });
        Grid->AddChildToWrapBox(Cell);
        TileWidgets.Add(Cell);
        TileItems.Add(ItemId);
    }
    SummaryLabel->SetText(FText::Format(LOCTEXT("Summary", "{0} {0}|plural(one=item,other=items)  ·  {1} {1}|plural(one=kind,other=kinds)"),
        FText::AsNumber(Total), FText::AsNumber(Stacks.Num())));
    if (TileItems.IsEmpty())
    {
        FBuilder::Stack(StateBox, B.StateCard(LOCTEXT("FilterEmptyTitle", "Nothing of this kind"),
            LOCTEXT("FilterEmptyBody", "Choose All to see everything you carry."), ETone::Neutral), 0.f);
        DetailBox->ClearChildren();
        return;
    }
    ShowDetail(TileItems.Contains(SelectedItem) ? SelectedItem : TileItems[0]);
}

void UMarburgumInventoryPanel::Select(FName ItemId)
{
    SelectedItem = ItemId;
    Rebuild();
}

void UMarburgumInventoryPanel::ShowDetail(FName ItemId)
{
    using namespace MarburgumII::Gameplay;
    SelectedItem = ItemId;
    for (int32 Index = 0; Index < TileWidgets.Num(); ++Index)
    {
        TileWidgets[Index]->SetSelected(TileItems[Index] == ItemId);
    }
    const APlayerController* Owner = GetOwningPlayer();
    const AMarburgumPlayerState* State = Owner ? Owner->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    if (!State || !DetailBox)
    {
        return;
    }
    int32 Quantity = 0;
    for (const FMarburgumItemStack& Stack : State->GetInventory())
    {
        if (Stack.ItemId == ItemId)
        {
            Quantity = Stack.Quantity;
        }
    }
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    DetailBox->ClearChildren();
    const FString Id = ItemId.ToString();
    const FItemDefinition* Definition = FindItem(Id);
    const EItemKind Kind = Definition ? Definition->Kind : EItemKind::Goods;

    UBorder* Plate = B.Surface(ESurfaceKind::Sunken, Pad(T.Space.L), B.Glyph(KindGlyph(Kind), T.Color.Accent, 48.f), false);
    Plate->SetBrush(RoundedBrush(T.Color.Sunken, T.Radius.Large, T.Color.Border, T.BorderWidth));
    FBuilder::Align(Plate, HAlign_Center, VAlign_Center);
    USizeBox* PlateSize = WidgetTree->ConstructWidget<USizeBox>();
    PlateSize->SetHeightOverride(112.f);
    PlateSize->SetContent(Plate);
    FBuilder::Stack(DetailBox, PlateSize, T.Space.L);

    FBuilder::Stack(DetailBox, B.Chip(KindGlyph(Kind), Caps(KindName(Kind)), Kind == EItemKind::Clue ? ETone::Info : ETone::Neutral), T.Space.S);
    if (UVerticalBoxSlot* ChipSlot = Cast<UVerticalBoxSlot>(DetailBox->GetSlots().Last()))
    {
        ChipSlot->SetHorizontalAlignment(HAlign_Left);
    }
    UTextBlock* Name = B.Text(Definition ? FText::FromString(MarburgumII::Gameplay::Localized(Definition->DisplayName)) : FText::FromName(ItemId), ETypeRole::Title);
    Name->SetAutoWrapText(true);
    FBuilder::Stack(DetailBox, Name, T.Space.M);
    // What kind of evidence it is and what it says (ADR-084): an original, a print, a copy, testimony, information or an
    // inference are never shown as the same thing.
    if (Definition && Definition->Evidence != MarburgumII::Gameplay::EEvidenceClass::None)
    {
        FBuilder::Stack(DetailBox, B.Field(LOCTEXT("EvidenceClass", "Evidence"), MarburgumUI::EvidenceClassName(Definition->Evidence)), T.Space.XS);
        UTextBlock* Meaning = B.Text(MarburgumUI::EvidenceClassMeaning(Definition->Evidence), ETypeRole::Caption, T.Color.TextMuted);
        Meaning->SetAutoWrapText(true);
        FBuilder::Stack(DetailBox, Meaning, T.Space.M);
    }
    if (Definition && *Definition->Description)
    {
        UTextBlock* Description = B.Text(FText::FromString(MarburgumII::Gameplay::Localized(Definition->Description)), ETypeRole::Quote, T.Color.TextSecondary);
        Description->SetAutoWrapText(true);
        FBuilder::Stack(DetailBox, Description, T.Space.L);
    }
    FBuilder::Stack(DetailBox, B.Field(LOCTEXT("Carried", "Carried"), FText::AsNumber(Quantity)), T.Space.XS);
    FBuilder::Stack(DetailBox, B.Field(LOCTEXT("Custody", "Custody"), LOCTEXT("Confirmed", "Confirmed and saved by the server")), T.Space.L);

    const TArray<const FMissionDefinition*> Related = MissionsFor(Id);
    if (!Related.IsEmpty())
    {
        FBuilder::Stack(DetailBox, B.SectionTitle(LOCTEXT("Missions", "MISSIONS"), T.Color.Accent), T.Space.S);
        for (const FMissionDefinition* Mission : Related)
        {
            FBuilder::Stack(DetailBox, B.GlyphText(EMarburgumGlyph::Mission, FText::FromString(MarburgumII::Gameplay::Localized(Mission->Name)), ETypeRole::Body, T.Color.TextPrimary),
                T.Space.XS);
        }
        FBuilder::Stack(DetailBox, B.Spacer(1.f, T.Space.M), 0.f);
    }

    FBuilder::Stack(DetailBox, B.SectionTitle(LOCTEXT("Actions", "WHAT YOU CAN DO"), T.Color.Accent), T.Space.S);
    bool bAnyCounter = false;
    for (const FShopDefinition& Shop : Shops)
    {
        const FShopOffer* Offer = FindOffer(Shop, Id);
        if (Offer && Offer->SellPrice > 0)
        {
            bAnyCounter = true;
            UHorizontalBox* Line = B.Row();
            FBuilder::Inline(Line, B.Glyph(EMarburgumGlyph::Shop, T.Color.TextSecondary, T.Icon.Small), T.Space.S);
            UTextBlock* Sell = B.Text(FText::Format(LOCTEXT("Sell", "Sell at {0} for {1} each"), FText::FromString(MarburgumII::Gameplay::Localized(Shop.Name)),
                FText::AsNumber(Offer->SellPrice)), ETypeRole::Body, T.Color.TextSecondary);
            Sell->SetAutoWrapText(true);
            FBuilder::Inline(Line, Sell, T.Space.S, true);
            FBuilder::Inline(Line, B.KeyCap(FText::AsCultureInvariant(TEXT("B")), false), 0.f);
            FBuilder::Stack(DetailBox, Line, T.Space.XS);
        }
    }
    UTextBlock* Note = B.Text(bAnyCounter
        ? LOCTEXT("CounterNote", "Selling happens at the counter itself: stand at its keeper and press B. The server confirms every sale.")
        : Kind == EItemKind::Clue
            ? LOCTEXT("ClueNote", "Evidence is handed over when a mission asks for it; talk to the person who needs it.")
            : Kind == EItemKind::Photo
            ? LOCTEXT("PhotoNote", "A print of your own photograph. Somebody who asked for it will want to see it; the subject stays documented either way.")
            : LOCTEXT("NoAction", "Nothing to do with it from here. Missions and counters that want it will ask."),
        ETypeRole::Caption, T.Color.TextMuted);
    Note->SetAutoWrapText(true);
    FBuilder::Stack(DetailBox, Note, 0.f);
}

#undef LOCTEXT_NAMESPACE
