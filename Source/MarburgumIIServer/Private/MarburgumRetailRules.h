#pragma once

#include "CoreMinimal.h"
#include "MarburgumInteractionTypes.h"
#include "MarburgumRetail.h"
#include "MarburgumWorldStore.h"

/** What a request at a counter means for one character, decided without side effects. */
struct FMarburgumTradeDecision
{
    /** Applied: OutNext holds the complete next record (revision already incremented). */
    bool bApplied = false;
    EMarburgumInteractionReason Reason = EMarburgumInteractionReason::None;
    EMarburgumInteractionEffect Effect = EMarburgumInteractionEffect::None;
    FString Shop;
    FString Item;
    int32 Quantity = 0;
    /** Negative for a purchase, positive for a sale. */
    int64 MoneyDelta = 0;
    /** Trading XP for a first purchase at this counter; 0 otherwise. */
    int64 TradingXp = 0;
    /** The item's shelf after the decision (before it, on a refusal); -1 when the item is not on the counter. */
    int32 Shelf = -1;
};

namespace MarburgumII::Retail
{
    /**
     * Pure counter rules (original project/native/retail_rules.hpp Apply, ADR-060), so they can be unit-tested.
     * In the original's order, a request is refused when the counter is closed, the quantity is not 1 to the
     * counter's limit, the counter does not sell (buy) or buy back (sell) the item, there are not enough copies on
     * the shelf, the character cannot pay, or the character does not hold enough copies to sell. Otherwise, in one
     * all-or-nothing record:
     *  - Buy: price x quantity leaves the character's money, the copies leave the shelf for the character's custody,
     *    and the character's first purchase at this counter also earns its Trading XP, once.
     *  - Sell: the copies leave custody, the counter pays its buy-back price, and the shelf fills again, never past
     *    its starting stock.
     *
     * Range, character, store and replay checks belong to the caller (UMarburgumWorldAuthority). Unlike the
     * original, a replay is answered from the world store's receipts, which are kept for good, and the shelf and the
     * character are one durable write, so a crash cannot leave a shelf short with the character uncharged.
     */
    FMarburgumTradeDecision DecideTrade(const FMarburgumWorldRecord& Current, const FString& Character, const FString& ShopId,
        EMarburgumTradeAction Action, const FString& Item, int32 Quantity, const FCivilTime& Now, FMarburgumWorldRecord& OutNext);
}
