#pragma once

#include "CoreMinimal.h"
#include "MarburgumGameplayContract.h"

/**
 * Kiosk 67's clock and counter facts (original project/native/retail_rules.hpp, ADR-060), shared so the server's
 * rule and the client's panel say the same thing. The server decides every purchase; clients only show these.
 */
namespace MarburgumII::Retail
{
    /** A civil date and time; Weekday 0 is Monday, Minute counts from midnight. */
    struct FCivilTime
    {
        int32 Year = 1970;
        int32 Month = 1;
        int32 Day = 1;
        int32 Weekday = 3;
        int32 Minute = 0;
    };

    /**
     * Europe/Ljubljana from Unix seconds, with the EU summer-time rule the original used: CEST (UTC+2) from
     * 01:00 UTC on the last Sunday of March until 01:00 UTC on the last Sunday of October, otherwise CET (UTC+1).
     */
    MARBURGUMIISHARED_API FCivilTime LjubljanaFromUnix(int64 UnixSeconds);

    /** Whether the counter is open at Minute on Weekday. A window that ends past midnight is supported. */
    MARBURGUMIISHARED_API bool IsOpen(const Gameplay::FShopDefinition& Shop, int32 Weekday, int32 Minute);

    /** "Mon–Sat 07:00–19:00". */
    MARBURGUMIISHARED_API FString HoursLabel(const Gameplay::FShopDefinition& Shop);

    /** The counter takes this good back, and pays less than it sells it for. */
    inline bool BuysBack(const Gameplay::FShopOffer& Offer)
    {
        return Offer.SellPrice >= 0 && Offer.SellPrice < Offer.BuyPrice;
    }

    /** A shelf as the world store records it: remaining copies when known, else the starting stock. */
    inline int32 ShelfOf(const Gameplay::FShopOffer& Offer, const TMap<FString, int32>* Shelves)
    {
        const int32* Stored = Shelves ? Shelves->Find(Offer.Item) : nullptr;
        return FMath::Max(0, Stored ? *Stored : Offer.Stock);
    }
}
