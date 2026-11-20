#include "UI/MarburgumShopPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/SafeZone.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/WrapBox.h"
#include "GameFramework/Pawn.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumInteractable.h"
#include "MarburgumPlayerController.h"
#include "MarburgumPlayerState.h"
#include "MarburgumRetail.h"
#include "UI/MarburgumUIBuilder.h"

#define LOCTEXT_NAMESPACE "MarburgumShopPanel"

using namespace MarburgumUI;

namespace
{
    constexpr float PanelWidth = 660.f;

    const MarburgumII::Gameplay::FShopDefinition* ShopOf(const AMarburgumInteractable* Keeper)
    {
        return Keeper && !Keeper->GetShopId().IsNone() ? MarburgumII::Gameplay::FindShop(Keeper->GetShopId().ToString()) : nullptr;
    }
}

void UMarburgumShopPanel::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    // A counter card: the shop as an archival heading with its hours, the keeper's line, what you can spend, and one
    // numbered line per offer with its price, buy-back and shelf as chips. Every trade waits for the server's receipt.
    UVerticalBox* Column = B.Column();
    UHorizontalBox* Header = B.Row();
    FBuilder::Inline(Header, B.GlyphText(EMarburgumGlyph::Shop, LOCTEXT("Overline", "COUNTER"), ETypeRole::Overline, T.Color.Accent, nullptr, 14.f),
        T.Space.M, true);
    FBuilder::Inline(Header, B.KeyHint(FText::AsCultureInvariant(TEXT("B")), LOCTEXT("Close", "Leave the counter")), 0.f);
    FBuilder::Stack(Column, Header, T.Space.S);
    ShopLabel = B.Text(FText::GetEmpty(), ETypeRole::PlaceTitle);
    ShopLabel->SetAutoWrapText(true);
    FBuilder::Stack(Column, ShopLabel, T.Space.XXS);
    UTextBlock* Hours = nullptr;
    FBuilder::Stack(Column, B.GlyphText(EMarburgumGlyph::Clock, FText::GetEmpty(), ETypeRole::Caption, T.Color.TextSecondary, &Hours, 14.f), T.Space.M);
    HoursLabel = Hours;
    StatusLabel = B.Text(FText::GetEmpty(), ETypeRole::Quote);
    StatusLabel->SetAutoWrapText(true);
    FBuilder::Stack(Column, StatusLabel, T.Space.S);
    UTextBlock* Money = nullptr;
    FBuilder::Stack(Column, B.GlyphText(EMarburgumGlyph::Coin, FText::GetEmpty(), ETypeRole::Label, T.Color.TextPrimary, &Money, 16.f), T.Space.L);
    MoneyLabel = Money;
    FBuilder::Stack(Column, B.SectionTitle(LOCTEXT("Shelves", "ON THE SHELVES"), T.Color.Accent), T.Space.S);
    Rows = B.Column();
    FBuilder::Stack(Column, Rows, T.Space.M);
    UHorizontalBox* Keys = B.Row();
    FBuilder::Inline(Keys, B.KeyHint(FText::AsCultureInvariant(TEXT("1–6")), LOCTEXT("Buy", "Buy one")), T.Space.XL);
    FBuilder::Inline(Keys, B.KeyHint(FText::AsCultureInvariant(TEXT("Shift + 1–6")), LOCTEXT("Sell", "Sell one back")), 0.f);
    FBuilder::Stack(Column, Keys, T.Space.S);
    UTextBlock* Hint = B.Text(LOCTEXT("Hint", "The counter sets the prices. A trade counts once the server confirms it."),
        ETypeRole::Caption, T.Color.TextMuted);
    Hint->SetAutoWrapText(true);
    FBuilder::Stack(Column, Hint, 0.f);

    UBorder* Panel = B.Surface(ESurfaceKind::Panel, Pad(T.Space.XL), Column);
    UBorder* Anchor = B.Surface(ESurfaceKind::Base, Pad(T.Space.XL), B.Bounded(Panel, PanelWidth), false);
    Anchor->SetBrushColor(FLinearColor::Transparent);
    FBuilder::Align(Anchor, HAlign_Center, VAlign_Center);
    USafeZone* Safe = WidgetTree->ConstructWidget<USafeZone>();
    Safe->SetContent(Anchor);
    WidgetTree->RootWidget = Safe;
    SetVisibility(ESlateVisibility::Collapsed);
}

void UMarburgumShopPanel::Open(AMarburgumInteractable* InKeeper)
{
    Keeper = InKeeper;
    Shown.Reset();
    Rebuild();
    SetVisibility(LineCount > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UMarburgumShopPanel::Close()
{
    SetVisibility(ESlateVisibility::Collapsed);
    Keeper.Reset();
    Shop = NAME_None;
    LineCount = 0;
    bShownOpen = false;
    Shown.Reset();
}

bool UMarburgumShopPanel::IsOpen() const
{
    return GetVisibility() != ESlateVisibility::Collapsed;
}

FString UMarburgumShopPanel::Snapshot() const
{
    const AMarburgumInteractable* Subject = Keeper.Get();
    const APlayerController* Owner = GetOwningPlayer();
    const AMarburgumPlayerState* State = Owner ? Owner->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    const MarburgumII::Gameplay::FShopDefinition* Definition = ShopOf(Subject);
    if (!Subject || !State || !Definition)
    {
        return FString();
    }
    FString Out = FString::Printf(TEXT("%d|%lld"), Subject->IsShopOpen() ? 1 : 0, State->GetMoney());
    for (const int32 Copies : Subject->GetShelves())
    {
        Out += FString::Printf(TEXT("|%d"), Copies);
    }
    for (int32 Index = 0; Index < Definition->OfferCount; ++Index)
    {
        Out += FString::Printf(TEXT("|%d"), State->CountItem(FName(Definition->Offers[Index].Item)));
    }
    return Out;
}

void UMarburgumShopPanel::Rebuild()
{
    using namespace MarburgumII::Gameplay;
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    Rows->ClearChildren();
    LineCount = 0;

    const AMarburgumInteractable* Subject = Keeper.Get();
    const FShopDefinition* Definition = ShopOf(Subject);
    const APlayerController* Owner = GetOwningPlayer();
    const AMarburgumPlayerState* State = Owner ? Owner->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    if (!Definition || !State)
    {
        Shop = NAME_None;
        return;
    }
    Shop = FName(Definition->Id);
    bShownOpen = Subject->IsShopOpen();
    const TArray<int32>& Shelves = Subject->GetShelves();
    const int64 Money = State->GetMoney();
    ShopLabel->SetText(FText::FromString(MarburgumII::Gameplay::Localized(Definition->Name)));
    HoursLabel->SetText(FText::FromString(MarburgumII::Retail::HoursLabel(*Definition)));
    StatusLabel->SetText(!Subject->IsShopKnown() ? LOCTEXT("Unknown", "Petra is still counting the shelves.")
        : bShownOpen ? LOCTEXT("Open", "Petra looks up from the counter.")
                     : FText::FromString(MarburgumII::Gameplay::Localized(Definition->ClosedMessage)));
    StatusLabel->SetColorAndOpacity(bShownOpen ? T.Color.TextSecondary : T.Color.TextMuted);
    MoneyLabel->SetText(FText::Format(LOCTEXT("Money", "You have {0} money"), FText::AsNumber(Money)));

    for (int32 Index = 0; Index < Definition->OfferCount; ++Index)
    {
        const FShopOffer& Offer = Definition->Offers[Index];
        const FItemDefinition* Item = FindItem(Offer.Item);
        const int32 Shelf = Shelves.IsValidIndex(Index) ? Shelves[Index] : -1;
        const int32 Held = State->CountItem(FName(Offer.Item));
        const bool bCanBuy = bShownOpen && Shelf > 0 && Money >= Offer.BuyPrice;
        const bool bCanSell = bShownOpen && Held > 0 && MarburgumII::Retail::BuysBack(Offer);
        ++LineCount;

        UHorizontalBox* Line = B.Row();
        FBuilder::Inline(Line, B.KeyCap(FText::AsNumber(Index + 1), bCanBuy || bCanSell), T.Space.M, false, false);
        UVerticalBox* Words = B.Column();
        UTextBlock* Name = B.Text(FText::FromString(MarburgumII::Gameplay::Localized(Item ? Item->DisplayName : Offer.Item)), ETypeRole::BodyStrong,
            bCanBuy || bCanSell ? T.Color.TextPrimary : T.Color.TextMuted);
        Name->SetAutoWrapText(true);
        FBuilder::Stack(Words, Name, T.Space.XS);
        UWrapBox* Chips = WidgetTree->ConstructWidget<UWrapBox>();
        Chips->SetInnerSlotPadding(FVector2D(T.Space.XS, T.Space.XS));
        Chips->AddChildToWrapBox(B.Chip(EMarburgumGlyph::Coin, FText::Format(LOCTEXT("Price", "{0} money"), FText::AsNumber(Offer.BuyPrice)),
            bShownOpen && Money >= Offer.BuyPrice ? ETone::Accent : ETone::Neutral));
        if (MarburgumII::Retail::BuysBack(Offer))
        {
            Chips->AddChildToWrapBox(B.Chip(EMarburgumGlyph::Trade, FText::Format(LOCTEXT("TakesBack", "Takes back for {0}"),
                FText::AsNumber(Offer.SellPrice)), ETone::Info));
        }
        Chips->AddChildToWrapBox(Shelf < 0 ? B.Chip(EMarburgumGlyph::Info, LOCTEXT("ShelfUnknown", "Shelf unknown"), ETone::Neutral)
            : Shelf == 0 ? B.Chip(EMarburgumGlyph::Close, LOCTEXT("SoldOut", "Sold out"), ETone::Warning)
                         : B.Chip(EMarburgumGlyph::Inventory, FText::Format(LOCTEXT("OnShelf", "{0} on the shelf"), FText::AsNumber(Shelf)), ETone::Neutral));
        if (Held > 0)
        {
            Chips->AddChildToWrapBox(B.Chip(EMarburgumGlyph::Check, FText::Format(LOCTEXT("Held", "You hold {0}"), FText::AsNumber(Held)), ETone::Success));
        }
        if (bShownOpen && Shelf > 0 && Money < Offer.BuyPrice)
        {
            Chips->AddChildToWrapBox(B.Chip(EMarburgumGlyph::Lock, LOCTEXT("Short", "Not enough money"), ETone::Danger));
        }
        FBuilder::Stack(Words, Chips, 0.f);
        FBuilder::Inline(Line, Words, 0.f, true, false);
        FBuilder::Stack(Rows, B.Surface(bCanBuy || bCanSell ? ESurfaceKind::Raised : ESurfaceKind::Sunken, Pad(T.Space.L, T.Space.M), Line), T.Space.XS);
    }
    Shown = Snapshot();
}

bool UMarburgumShopPanel::Trade(int32 Number, bool bSell)
{
    AMarburgumPlayerController* Controller = Cast<AMarburgumPlayerController>(GetOwningPlayer());
    const MarburgumII::Gameplay::FShopDefinition* Definition = ShopOf(Keeper.Get());
    const int32 Index = Number - 1;
    if (!IsOpen() || !Controller || !Definition || Index < 0 || Index >= Definition->OfferCount)
    {
        return false;
    }
    // One request at a time, as with E; the receipt arrives in the notification feed.
    FGuid Pending;
    TWeakObjectPtr<AMarburgumInteractable> PendingTarget;
    if (Controller->GetPendingInteraction(Pending, PendingTarget))
    {
        return false;
    }
    Controller->SubmitTrade(FGuid::NewGuid(), Keeper.Get(), bSell ? EMarburgumTradeAction::Sell : EMarburgumTradeAction::Buy,
        FName(Definition->Offers[Index].Item), 1);
    return true;
}

void UMarburgumShopPanel::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    if (!IsOpen())
    {
        return;
    }
    const APlayerController* Owner = GetOwningPlayer();
    const APawn* Body = Owner ? Owner->GetPawn() : nullptr;
    const AMarburgumInteractable* Subject = Keeper.Get();
    if (!Body || !Subject || FVector::Dist(Body->GetActorLocation(), Subject->GetActorLocation()) > Subject->InteractionRange + 100.f)
    {
        Close();
        return;
    }
    if (Snapshot() != Shown)
    {
        Rebuild();
    }
}

#undef LOCTEXT_NAMESPACE
