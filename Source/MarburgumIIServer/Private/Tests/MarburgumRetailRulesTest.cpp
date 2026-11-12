#include "MarburgumRetailRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MarburgumGameplayContract.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumRetailRulesTest,
    "Marburgum.Server.RetailRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumRetailClockTest,
    "Marburgum.Server.RetailClock",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    using MarburgumII::Retail::DecideTrade;
    using MarburgumII::Retail::FCivilTime;

    const TCHAR* Kiosk = TEXT("kiosk_67");
    const TCHAR* Paper = TEXT("mariborski_glas");
    const TCHAR* Postcard = TEXT("maribor_postcard");
    const TCHAR* Pen = TEXT("ballpoint_pen");

    /** Thursday 10:00, inside Kiosk 67's hours. */
    FCivilTime Open()
    {
        FCivilTime Now;
        Now.Weekday = 3;
        Now.Minute = 10 * 60;
        return Now;
    }

    FCivilTime At(int32 Weekday, int32 Minute)
    {
        FCivilTime Now;
        Now.Weekday = Weekday;
        Now.Minute = Minute;
        return Now;
    }

    FMarburgumWorldRecord WithMoney(const FString& Character, int64 Money)
    {
        FMarburgumWorldRecord Record;
        Record.Revision = 7;
        if (Money > 0)
        {
            Record.Money.Add(Character, Money);
        }
        return Record;
    }

    /** Apply one trade to Record in place; the decision is returned for its fields. */
    FMarburgumTradeDecision Trade(FMarburgumWorldRecord& Record, const FString& Character, EMarburgumTradeAction Action,
        const FString& Item, int32 Quantity, const FCivilTime& Now = Open())
    {
        FMarburgumWorldRecord Next;
        const FMarburgumTradeDecision Decision = DecideTrade(Record, Character, Kiosk, Action, Item, Quantity, Now, Next);
        if (Decision.bApplied)
        {
            Record = Next;
        }
        return Decision;
    }

    int32 Held(const FMarburgumWorldRecord& Record, const FString& Character, const FString& Item)
    {
        const TMap<FString, int32>* Owned = Record.Inventories.Find(Character);
        return Owned ? Owned->FindRef(Item) : 0;
    }

    int32 Shelf(const FMarburgumWorldRecord& Record, const FString& Item)
    {
        const MarburgumII::Gameplay::FShopDefinition* Shop = MarburgumII::Gameplay::FindShop(Kiosk);
        const MarburgumII::Gameplay::FShopOffer* Offer = Shop ? MarburgumII::Gameplay::FindOffer(*Shop, Item) : nullptr;
        return Offer ? MarburgumII::Retail::ShelfOf(*Offer, Record.Shelves.Find(Kiosk)) : -1;
    }
}

bool FMarburgumRetailRulesTest::RunTest(const FString& Parameters)
{
    using namespace MarburgumII::Gameplay;
    using namespace MarburgumII::Retail;

    // The counter is the original's (shops.json): three goods, hours and prices.
    const FShopDefinition* Shop = FindShop(Kiosk);
    TestTrue(TEXT("Kiosk 67 is registered"), Shop != nullptr);
    if (!Shop)
    {
        return false;
    }
    TestEqual(TEXT("Petra keeps it"), FString(Shop->Npc), FString(TEXT("petra_kralj")));
    TestTrue(TEXT("found by its keeper"), FindShopKeptBy(TEXT("petra_kralj")) == Shop);
    TestEqual(TEXT("three goods"), Shop->OfferCount, 3);
    TestEqual(TEXT("newspaper price"), FindOffer(*Shop, Paper)->BuyPrice, 20);
    TestEqual(TEXT("newspaper shelf"), FindOffer(*Shop, Paper)->Stock, 12);
    TestEqual(TEXT("hours label"), HoursLabel(*Shop), FString(TEXT("Mon–Sat 07:00–19:00")));
    for (int32 Index = 0; Index < Shop->OfferCount; ++Index)
    {
        TestTrue(TEXT("buy-back is below the price"), BuysBack(Shop->Offers[Index]));
        const FItemDefinition* Item = FindItem(Shop->Offers[Index].Item);
        TestTrue(TEXT("every offer is registered goods"), Item && Item->Kind == EItemKind::Goods);
    }

    // Hours: 07:00 inclusive to 19:00 exclusive, Monday to Saturday.
    TestTrue(TEXT("Monday 07:00 open"), IsOpen(*Shop, 0, 7 * 60));
    TestTrue(TEXT("Monday 18:59 open"), IsOpen(*Shop, 0, 19 * 60 - 1));
    TestFalse(TEXT("Monday 19:00 closed"), IsOpen(*Shop, 0, 19 * 60));
    TestFalse(TEXT("Monday 06:59 closed"), IsOpen(*Shop, 0, 7 * 60 - 1));
    TestFalse(TEXT("midnight closed"), IsOpen(*Shop, 0, 0));
    TestTrue(TEXT("Saturday noon open"), IsOpen(*Shop, 5, 12 * 60));
    TestFalse(TEXT("Sunday noon closed"), IsOpen(*Shop, 6, 12 * 60));
    TestFalse(TEXT("a weekday out of range is closed"), IsOpen(*Shop, 7, 12 * 60));
    FShopDefinition Overnight = *Shop;
    Overnight.OpenMinute = 22 * 60;
    Overnight.CloseMinute = 2 * 60;
    TestTrue(TEXT("overnight window after 22:00"), IsOpen(Overnight, 0, 23 * 60));
    TestTrue(TEXT("overnight window before 02:00"), IsOpen(Overnight, 0, 60));
    TestFalse(TEXT("overnight window shut at noon"), IsOpen(Overnight, 0, 12 * 60));

    const FString Ana = TEXT("ana");
    const FString Borut = TEXT("borut");

    // A purchase moves price, goods, shelf and the first-purchase Trading XP together.
    FMarburgumWorldRecord Record = WithMoney(Ana, 80);
    FMarburgumTradeDecision Bought = Trade(Record, Ana, EMarburgumTradeAction::Buy, Paper, 1);
    TestTrue(TEXT("buy applied"), Bought.bApplied && Bought.Effect == EMarburgumInteractionEffect::ItemBought);
    TestEqual(TEXT("buy costs the price"), Bought.MoneyDelta, int64(-20));
    TestEqual(TEXT("money after buy"), Record.Money.FindRef(Ana), int64(60));
    TestEqual(TEXT("newspaper held"), Held(Record, Ana, Paper), 1);
    TestEqual(TEXT("shelf falls by one"), Shelf(Record, Paper), 11);
    TestEqual(TEXT("decision reports the shelf"), Bought.Shelf, 11);
    TestEqual(TEXT("first purchase XP"), Bought.TradingXp, int64(10));
    TestEqual(TEXT("Trading XP recorded"), Record.Skills.FindRef(Ana).FindRef(TEXT("trading")), int64(10));
    TestTrue(TEXT("first purchase recorded"), Record.FirstPurchases.FindRef(Ana).Contains(Kiosk));
    TestEqual(TEXT("revision advances"), Record.Revision, int64(8));

    FMarburgumTradeDecision Again = Trade(Record, Ana, EMarburgumTradeAction::Buy, Paper, 2);
    TestTrue(TEXT("second buy applied"), Again.bApplied);
    TestEqual(TEXT("second buy pays no XP"), Again.TradingXp, int64(0));
    TestEqual(TEXT("XP unchanged"), Record.Skills.FindRef(Ana).FindRef(TEXT("trading")), int64(10));
    TestEqual(TEXT("two copies cost 40"), Record.Money.FindRef(Ana), int64(20));
    TestEqual(TEXT("three held"), Held(Record, Ana, Paper), 3);

    // A sale pays the buy-back price and never fills the shelf past its starting stock.
    FMarburgumTradeDecision Sold = Trade(Record, Ana, EMarburgumTradeAction::Sell, Paper, 1);
    TestTrue(TEXT("sale applied"), Sold.bApplied && Sold.Effect == EMarburgumInteractionEffect::ItemSold);
    TestEqual(TEXT("sale pays 8"), Sold.MoneyDelta, int64(8));
    TestEqual(TEXT("a sale gives no XP"), Sold.TradingXp, int64(0));
    TestEqual(TEXT("shelf back up"), Shelf(Record, Paper), 10);
    FMarburgumWorldRecord Full = WithMoney(Borut, 0);
    Full.Inventories.FindOrAdd(Borut).Add(Pen, 2);
    TestTrue(TEXT("selling to a full shelf"), Trade(Full, Borut, EMarburgumTradeAction::Sell, Pen, 2).bApplied);
    TestEqual(TEXT("a full shelf stays at its starting stock"), Shelf(Full, Pen), 6);
    TestEqual(TEXT("seller paid 10"), Full.Money.FindRef(Borut), int64(10));
    TestFalse(TEXT("the last copy sold leaves no entry"), Full.Inventories.FindRef(Borut).Contains(Pen));

    // Buying and selling back loses the spread.
    FMarburgumWorldRecord Spread = WithMoney(Borut, 100);
    Trade(Spread, Borut, EMarburgumTradeAction::Buy, Paper, 1);
    Trade(Spread, Borut, EMarburgumTradeAction::Sell, Paper, 1);
    TestEqual(TEXT("buy then sell loses the spread"), Spread.Money.FindRef(Borut), int64(88));
    TestEqual(TEXT("nothing left in hand"), Held(Spread, Borut, Paper), 0);

    // Refusals change nothing and say why.
    auto Refused = [this](const FMarburgumWorldRecord& Before, const FString& Character, EMarburgumTradeAction Action, const FString& Item,
        int32 Quantity, const FCivilTime& Now, EMarburgumInteractionReason Expected, const TCHAR* What)
    {
        FMarburgumWorldRecord Next;
        Next.Revision = -1;
        const FMarburgumTradeDecision Decision = DecideTrade(Before, Character, Kiosk, Action, Item, Quantity, Now, Next);
        TestFalse(FString::Printf(TEXT("%s refused"), What), Decision.bApplied);
        TestEqual(FString::Printf(TEXT("%s reason"), What), FString(MarburgumII::Gameplay::ToString(Decision.Reason)),
            FString(MarburgumII::Gameplay::ToString(Expected)));
        TestEqual(FString::Printf(TEXT("%s leaves no next record"), What), Next.Revision, int64(-1));
    };
    const FMarburgumWorldRecord Poor = WithMoney(Ana, 10);
    Refused(Poor, Ana, EMarburgumTradeAction::Buy, Paper, 1, Open(), EMarburgumInteractionReason::InsufficientMoney, TEXT("too poor"));
    Refused(WithMoney(Ana, 100), Ana, EMarburgumTradeAction::Sell, Pen, 1, Open(), EMarburgumInteractionReason::InsufficientItems,
        TEXT("selling what is not held"));
    FMarburgumWorldRecord Rich = WithMoney(Ana, 500);
    Rich.Inventories.FindOrAdd(Ana).Add(TEXT("brass_key"), 1);
    Refused(Rich, Ana, EMarburgumTradeAction::Sell, TEXT("brass_key"), 1, Open(), EMarburgumInteractionReason::ItemNotAccepted,
        TEXT("a mission item is not taken"));
    Refused(Rich, Ana, EMarburgumTradeAction::Buy, TEXT("fishing_rod"), 1, Open(), EMarburgumInteractionReason::NotOnCounter,
        TEXT("goods not on the counter"));
    Refused(Rich, Ana, EMarburgumTradeAction::Buy, TEXT("no_such_item"), 1, Open(), EMarburgumInteractionReason::NotOnCounter,
        TEXT("an unknown item"));
    Refused(Rich, Ana, EMarburgumTradeAction::Buy, Paper, 0, Open(), EMarburgumInteractionReason::InvalidQuantity, TEXT("zero"));
    Refused(Rich, Ana, EMarburgumTradeAction::Buy, Paper, -3, Open(), EMarburgumInteractionReason::InvalidQuantity, TEXT("negative"));
    Refused(Rich, Ana, EMarburgumTradeAction::Buy, Paper, 6, Open(), EMarburgumInteractionReason::InvalidQuantity, TEXT("above the cap"));
    TestTrue(TEXT("five at once is allowed"), Trade(Rich, Ana, EMarburgumTradeAction::Buy, Paper, 5).bApplied);
    Refused(Rich, Ana, EMarburgumTradeAction::Buy, Paper, 1, At(6, 12 * 60), EMarburgumInteractionReason::ShopClosed, TEXT("Sunday"));
    Refused(Rich, Ana, EMarburgumTradeAction::Buy, Paper, 1, At(3, 19 * 60), EMarburgumInteractionReason::ShopClosed, TEXT("19:00"));
    Refused(Rich, Ana, EMarburgumTradeAction::Sell, Paper, 1, At(3, 6 * 60 + 59), EMarburgumInteractionReason::ShopClosed,
        TEXT("selling before opening"));
    {
        FMarburgumWorldRecord Next;
        const FMarburgumTradeDecision Elsewhere = DecideTrade(Rich, Ana, TEXT("no_counter"), EMarburgumTradeAction::Buy, Paper, 1, Open(), Next);
        TestTrue(TEXT("an unknown counter"), !Elsewhere.bApplied && Elsewhere.Reason == EMarburgumInteractionReason::NoCounter);
    }

    // Two buyers, one copy: the first takes it, the second is told it has gone and keeps the money.
    FMarburgumWorldRecord Race = WithMoney(Ana, 35);
    Race.Money.Add(Borut, 35);
    Race.Shelves.FindOrAdd(Kiosk).Add(Postcard, 1);
    TestTrue(TEXT("first buyer gets the last postcard"), Trade(Race, Ana, EMarburgumTradeAction::Buy, Postcard, 1).bApplied);
    const FMarburgumTradeDecision Late = Trade(Race, Borut, EMarburgumTradeAction::Buy, Postcard, 1);
    TestTrue(TEXT("second buyer finds it gone"), !Late.bApplied && Late.Reason == EMarburgumInteractionReason::OutOfStock);
    TestEqual(TEXT("the shelf does not go below none"), Shelf(Race, Postcard), 0);
    TestEqual(TEXT("the late buyer keeps the money"), Race.Money.FindRef(Borut), int64(35));
    TestFalse(TEXT("spending everything leaves no money entry"), Race.Money.Contains(Ana));
    return true;
}

bool FMarburgumRetailClockTest::RunTest(const FString& Parameters)
{
    using MarburgumII::Retail::LjubljanaFromUnix;
    auto Expect = [this](int64 Unix, int32 Year, int32 Month, int32 Day, int32 Weekday, int32 Minute, const TCHAR* What)
    {
        const FCivilTime Civil = LjubljanaFromUnix(Unix);
        TestEqual(FString::Printf(TEXT("%s date"), What), FString::Printf(TEXT("%04d-%02d-%02d"), Civil.Year, Civil.Month, Civil.Day),
            FString::Printf(TEXT("%04d-%02d-%02d"), Year, Month, Day));
        TestEqual(FString::Printf(TEXT("%s weekday"), What), Civil.Weekday, Weekday);
        TestEqual(FString::Printf(TEXT("%s minute"), What), Civil.Minute, Minute);
    };
    // The original's own checks, then the autumn change and a leap day.
    Expect(1768478400, 2026, 1, 15, 3, 13 * 60, TEXT("January noon UTC is 13:00 CET"));
    Expect(1784116800, 2026, 7, 15, 2, 14 * 60, TEXT("July noon UTC is 14:00 CEST"));
    Expect(1774744200, 2026, 3, 29, 6, 90, TEXT("just before the spring change is CET"));
    Expect(1774746000, 2026, 3, 29, 6, 3 * 60, TEXT("01:00 UTC on the spring change is 03:00 CEST"));
    Expect(1792889940, 2026, 10, 25, 6, 2 * 60 + 59, TEXT("00:59 UTC on the autumn change is 02:59 CEST"));
    Expect(1792890000, 2026, 10, 25, 6, 2 * 60, TEXT("01:00 UTC on the autumn change is 02:00 CET"));
    Expect(1709249400, 2024, 3, 1, 4, 30, TEXT("a leap day's last half hour UTC is the next Friday in Ljubljana"));

    // The kiosk shuts at 19:00 local: 17:59 UTC on a November Thursday is open, 18:00 is not.
    const MarburgumII::Gameplay::FShopDefinition* Shop = MarburgumII::Gameplay::FindShop(Kiosk);
    if (Shop)
    {
        const FCivilTime Before = LjubljanaFromUnix(1794506340);
        const FCivilTime After = LjubljanaFromUnix(1794506400);
        TestTrue(TEXT("18:59 CET on Thursday is open"), MarburgumII::Retail::IsOpen(*Shop, Before.Weekday, Before.Minute));
        TestFalse(TEXT("19:00 CET on Thursday is closed"), MarburgumII::Retail::IsOpen(*Shop, After.Weekday, After.Minute));
        const FCivilTime Sunday = LjubljanaFromUnix(1794736800);
        TestFalse(TEXT("Sunday 11:00 is closed"), MarburgumII::Retail::IsOpen(*Shop, Sunday.Weekday, Sunday.Minute));
    }
    return true;
}

#endif
