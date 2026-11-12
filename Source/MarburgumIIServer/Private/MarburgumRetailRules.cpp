#include "MarburgumRetailRules.h"

#include "MarburgumGameplayContract.h"

using namespace MarburgumII::Gameplay;

namespace MarburgumII::Retail
{
    namespace
    {
        // Money and XP stay well inside the range a JSON number holds exactly.
        constexpr int64 MaxAmount = 9007199254740991LL;
    }

    FMarburgumTradeDecision DecideTrade(const FMarburgumWorldRecord& Current, const FString& Character, const FString& ShopId,
        EMarburgumTradeAction Action, const FString& Item, int32 Quantity, const FCivilTime& Now, FMarburgumWorldRecord& OutNext)
    {
        FMarburgumTradeDecision Decision;
        Decision.Shop = ShopId;
        Decision.Item = Item;
        Decision.Quantity = Quantity;
        auto Refuse = [&Decision](EMarburgumInteractionReason Reason)
        {
            Decision.Reason = Reason;
            return Decision;
        };

        const FShopDefinition* Shop = FindShop(ShopId);
        if (!Shop)
        {
            return Refuse(EMarburgumInteractionReason::NoCounter);
        }
        const TMap<FString, int32>* Shelves = Current.Shelves.Find(ShopId);
        const FShopOffer* Offer = FindOffer(*Shop, Item);
        Decision.Shelf = Offer ? ShelfOf(*Offer, Shelves) : -1;
        if (!IsOpen(*Shop, Now.Weekday, Now.Minute))
        {
            return Refuse(EMarburgumInteractionReason::ShopClosed);
        }
        if (Quantity <= 0 || Quantity > Shop->MaxQuantity)
        {
            return Refuse(EMarburgumInteractionReason::InvalidQuantity);
        }
        const FItemDefinition* Goods = FindItem(Item);
        if (!Offer || !Goods || Goods->Kind != EItemKind::Goods)
        {
            return Refuse(Action == EMarburgumTradeAction::Sell ? EMarburgumInteractionReason::ItemNotAccepted
                                                                : EMarburgumInteractionReason::NotOnCounter);
        }
        if (Action == EMarburgumTradeAction::Sell && !BuysBack(*Offer))
        {
            return Refuse(EMarburgumInteractionReason::ItemNotAccepted);
        }

        const int64 Total = static_cast<int64>(Action == EMarburgumTradeAction::Buy ? Offer->BuyPrice : Offer->SellPrice) * Quantity;
        const int64 Money = Current.Money.FindRef(Character);
        const TMap<FString, int32>* Owned = Current.Inventories.Find(Character);
        const int32 Held = Owned ? Owned->FindRef(Item) : 0;
        const int32 Shelf = Decision.Shelf;
        if (Action == EMarburgumTradeAction::Buy)
        {
            if (Shelf < Quantity)
            {
                return Refuse(EMarburgumInteractionReason::OutOfStock);
            }
            if (Money < Total)
            {
                return Refuse(EMarburgumInteractionReason::InsufficientMoney);
            }
            if (Held > MAX_int32 - Quantity)
            {
                return Refuse(EMarburgumInteractionReason::InvalidQuantity);
            }
        }
        else
        {
            if (Held < Quantity)
            {
                return Refuse(EMarburgumInteractionReason::InsufficientItems);
            }
            if (Money > MaxAmount - Total)
            {
                return Refuse(EMarburgumInteractionReason::InvalidQuantity);
            }
        }

        OutNext = Current;
        ++OutNext.Revision;
        TMap<FString, int32>& Custody = OutNext.Inventories.FindOrAdd(Character);
        int64& Purse = OutNext.Money.FindOrAdd(Character);
        int32& Line = OutNext.Shelves.FindOrAdd(ShopId).FindOrAdd(Item);
        if (Action == EMarburgumTradeAction::Buy)
        {
            Purse -= Total;
            Custody.Add(Item, Held + Quantity);
            Line = Shelf - Quantity;
            Decision.MoneyDelta = -Total;
            Decision.Effect = EMarburgumInteractionEffect::ItemBought;
            TArray<FString>& FirstPurchases = OutNext.FirstPurchases.FindOrAdd(Character);
            if (Shop->IntroTradingXp > 0 && !FirstPurchases.Contains(ShopId))
            {
                FirstPurchases.Add(ShopId);
                int64& Xp = OutNext.Skills.FindOrAdd(Character).FindOrAdd(TradingSkill);
                Xp = FMath::Min(Xp + Shop->IntroTradingXp, MaxAmount);
                Decision.TradingXp = Shop->IntroTradingXp;
            }
            else if (FirstPurchases.IsEmpty())
            {
                OutNext.FirstPurchases.Remove(Character);
            }
        }
        else
        {
            Purse += Total;
            if (Held - Quantity > 0)
            {
                Custody.Add(Item, Held - Quantity);
            }
            else
            {
                Custody.Remove(Item);
            }
            // Copies come back onto the shelf, but never more than it started with.
            Line = FMath::Min(Offer->Stock, Shelf + Quantity);
            Decision.MoneyDelta = Total;
            Decision.Effect = EMarburgumInteractionEffect::ItemSold;
        }
        // Zero-money entries are not kept: a character with none is absent from the record.
        if (Purse == 0)
        {
            OutNext.Money.Remove(Character);
        }
        Decision.Shelf = Line;
        Decision.bApplied = true;
        return Decision;
    }
}
