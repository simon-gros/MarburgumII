#include "UI/MarburgumShopPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/SafeZone.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
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
    constexpr float PanelWidth = 620.f;

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

    UVerticalBox* Column = B.Column();
    UHorizontalBox* Header = B.Row();
    UVerticalBox* Titles = B.Column();
    FBuilder::Stack(Titles, B.Text(LOCTEXT("Overline", "COUNTER"), ETypeRole::Overline, T.Color.Accent), T.Space.XXS);
    ShopLabel = B.Text(FText::GetEmpty(), ETypeRole::Title);
    FBuilder::Stack(Titles, ShopLabel, T.Space.XXS);
    HoursLabel = B.Text(FText::GetEmpty(), ETypeRole::Caption, T.Color.TextMuted);
    FBuilder::Stack(Titles, HoursLabel, 0.f);
    FBuilder::Inline(Header, Titles, T.Space.M, true);
    FBuilder::Inline(Header, B.Text(LOCTEXT("Close", "[B] Leave the counter"), ETypeRole::Caption, T.Color.TextMuted), 0.f, false, false);
    FBuilder::Stack(Column, Header, T.Space.S);
    StatusLabel = B.Text(FText::GetEmpty(), ETypeRole::Body);
    StatusLabel->SetWrapTextAt(PanelWidth - 2.f * T.Space.XL);
    FBuilder::Stack(Column, StatusLabel, T.Space.XS);
    MoneyLabel = B.Text(FText::GetEmpty(), ETypeRole::Caption);
    FBuilder::Stack(Column, MoneyLabel, T.Space.M);
    FBuilder::Stack(Column, B.Rule(T.Color.Border, 1.f), T.Space.M);
    Rows = B.Column();
    FBuilder::Stack(Column, Rows, T.Space.S);
    UTextBlock* Hint = B.Text(LOCTEXT("Hint", "Press a number to buy one. Hold Shift with the number to sell one back. Petra sets the prices."),
        ETypeRole::Caption, T.Color.TextMuted);
    Hint->SetWrapTextAt(PanelWidth - 2.f * T.Space.XL);
    FBuilder::Stack(Column, Hint, 0.f);

    UBorder* Panel = B.Surface(ESurfaceKind::Panel, Pad(T.Space.XL), Column);
    USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
    Width->SetWidthOverride(PanelWidth);
    Width->SetContent(Panel);
    UBorder* Anchor = B.Surface(ESurfaceKind::Base, FMargin(0.f), Width, false);
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
    ShopLabel->SetText(FText::FromString(Definition->Name));
    HoursLabel->SetText(FText::FromString(MarburgumII::Retail::HoursLabel(*Definition)));
    StatusLabel->SetText(!Subject->IsShopKnown() ? LOCTEXT("Unknown", "Petra is still counting the shelves.")
        : bShownOpen ? LOCTEXT("Open", "Petra looks up from the counter.")
                     : FText::FromString(Definition->ClosedMessage));
    StatusLabel->SetColorAndOpacity(bShownOpen ? T.Color.TextPrimary : T.Color.TextMuted);
    MoneyLabel->SetText(FText::Format(LOCTEXT("Money", "You have {0} money."), FText::AsNumber(Money)));

    for (int32 Index = 0; Index < Definition->OfferCount; ++Index)
    {
        const FShopOffer& Offer = Definition->Offers[Index];
        const FItemDefinition* Item = FindItem(Offer.Item);
        const int32 Shelf = Shelves.IsValidIndex(Index) ? Shelves[Index] : -1;
        const int32 Held = State->CountItem(FName(Offer.Item));
        const bool bCanBuy = bShownOpen && Shelf > 0 && Money >= Offer.BuyPrice;
        const bool bCanSell = bShownOpen && Held > 0 && MarburgumII::Retail::BuysBack(Offer);
        ++LineCount;

        TArray<FString> Facts;
        Facts.Add(FString::Printf(TEXT("%d money"), Offer.BuyPrice));
        if (MarburgumII::Retail::BuysBack(Offer))
        {
            Facts.Add(FString::Printf(TEXT("takes back for %d"), Offer.SellPrice));
        }
        Facts.Add(Shelf < 0 ? FString(TEXT("shelf unknown")) : Shelf == 0 ? FString(TEXT("sold out")) : FString::Printf(TEXT("%d on the shelf"), Shelf));
        if (Held > 0)
        {
            Facts.Add(FString::Printf(TEXT("you hold %d"), Held));
        }

        UHorizontalBox* Line = B.Row();
        UBorder* Key = B.Surface(ESurfaceKind::Raised, Pad(T.Space.S, T.Space.XXS),
            B.Text(FText::AsNumber(Index + 1), ETypeRole::Heading, bCanBuy || bCanSell ? T.Color.Accent : T.Color.TextMuted));
        FBuilder::Inline(Line, Key, T.Space.M);
        UVerticalBox* Words = B.Column();
        FBuilder::Stack(Words, B.Text(FText::FromString(Item ? Item->DisplayName : Offer.Item), ETypeRole::Body,
            bCanBuy ? T.Color.TextPrimary : T.Color.TextMuted), T.Space.XXS);
        FBuilder::Stack(Words, B.Text(FText::FromString(FString::Join(Facts, TEXT("  ·  "))), ETypeRole::Caption), 0.f);
        FBuilder::Inline(Line, Words, 0.f, true);
        FBuilder::Stack(Rows, B.Surface(ESurfaceKind::Raised, Pad(T.Space.L, T.Space.S), Line), T.Space.XS);
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
