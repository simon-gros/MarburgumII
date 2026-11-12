#include "MarburgumWorldStore.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumWorldStoreConditionTest,
    "Marburgum.Server.WorldStoreCondition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarburgumWorldStoreConditionTest::RunTest(const FString& Parameters)
{
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), FGuid::NewGuid().ToString());
    const FString Path = FPaths::Combine(Directory, TEXT("store.json"));
    FString Error;

    // A current-schema commit round-trips each character's condition.
    {
        FMarburgumWorldStore Store(Path);
        TestTrue(TEXT("missing file is an empty world"), Store.Load(Error));
        FMarburgumWorldRecord Next;
        Next.Revision = 1;
        FMarburgumStoredCondition Winded;
        Winded.Stamina = 3;
        Winded.bWinded = true;
        Next.Conditions.Add(TEXT("runner"), Winded);
        TestTrue(TEXT("commit"), Store.Commit(Next, Error));
    }
    {
        FMarburgumWorldStore Store(Path);
        TestTrue(TEXT("reload"), Store.Load(Error));
        TestEqual(TEXT("schema"), Store.GetLoadedSchema(), 5);
        const FMarburgumStoredCondition* Loaded = Store.Get().Conditions.Find(TEXT("runner"));
        TestTrue(TEXT("condition saved"), Loaded != nullptr);
        if (Loaded)
        {
            TestEqual(TEXT("stamina"), Loaded->Stamina, 3);
            TestEqual(TEXT("vitality"), Loaded->Vitality, 100);
            TestTrue(TEXT("winded"), Loaded->bWinded);
        }
    }

    // A drowned body keeps the rest of its wait (ADR-032); a living one never writes the field.
    {
        FMarburgumWorldStore Store(Path);
        TestTrue(TEXT("load before down"), Store.Load(Error));
        FMarburgumWorldRecord Next = Store.Get();
        Next.Revision = 2;
        FMarburgumStoredCondition Down;
        Down.Vitality = 0;
        Down.Stamina = 40;
        Down.RecoverySeconds = 37;
        Next.Conditions.Add(TEXT("drowned"), Down);
        TestTrue(TEXT("commit down"), Store.Commit(Next, Error));
        FString Text;
        FFileHelper::LoadFileToString(Text, *Path);
        const int32 First = Text.Find(TEXT("recovery_s"));
        TestTrue(TEXT("only the down body has a wait"),
            First != INDEX_NONE && First == Text.Find(TEXT("recovery_s"), ESearchCase::CaseSensitive, ESearchDir::FromEnd));
    }
    {
        FMarburgumWorldStore Store(Path);
        TestTrue(TEXT("reload down"), Store.Load(Error));
        const FMarburgumStoredCondition Down = Store.Get().Conditions.FindRef(TEXT("drowned"));
        TestEqual(TEXT("down vitality"), Down.Vitality, 0);
        TestEqual(TEXT("wait kept"), Down.RecoverySeconds, 37);
        TestEqual(TEXT("living body has no wait"), Store.Get().Conditions.FindRef(TEXT("runner")).RecoverySeconds, 0);
    }
    // A wait on a living body is not trusted.
    FFileHelper::SaveStringToFile(TEXT("{\"format\":\"marburgumii.world_store\",\"schema_version\":3,\"revision\":1,")
        TEXT("\"conditions\":{\"alive\":{\"stamina\":50,\"vitality\":60,\"winded\":false,\"recovery_s\":30}}}"), *Path);
    {
        FMarburgumWorldStore Store(Path);
        TestTrue(TEXT("alive with a wait loads"), Store.Load(Error));
        TestEqual(TEXT("no wait while alive"), Store.Get().Conditions.FindRef(TEXT("alive")).RecoverySeconds, 0);
    }

    // A schema 2 document (before conditions) loads with every body rested.
    FFileHelper::SaveStringToFile(TEXT("{\"format\":\"marburgumii.world_store\",\"schema_version\":2,\"revision\":4,")
        TEXT("\"stocks\":{},\"inventories\":{\"simon\":{\"fishing_rod\":1}},\"missions\":{},\"receipts\":{}}"), *Path);
    {
        FMarburgumWorldStore Store(Path);
        TestTrue(TEXT("schema 2 loads"), Store.Load(Error));
        TestEqual(TEXT("schema 2 reported"), Store.GetLoadedSchema(), 2);
        TestTrue(TEXT("no conditions"), Store.Get().Conditions.IsEmpty());
        TestEqual(TEXT("custody kept"), Store.Get().Inventories.FindRef(TEXT("simon")).FindRef(TEXT("fishing_rod")), 1);
    }

    // Values outside the body's range are clamped, not trusted.
    FFileHelper::SaveStringToFile(TEXT("{\"format\":\"marburgumii.world_store\",\"schema_version\":3,\"revision\":1,")
        TEXT("\"conditions\":{\"odd\":{\"stamina\":250,\"vitality\":-7,\"winded\":true}}}"), *Path);
    {
        FMarburgumWorldStore Store(Path);
        TestTrue(TEXT("odd values load"), Store.Load(Error));
        const FMarburgumStoredCondition Odd = Store.Get().Conditions.FindRef(TEXT("odd"));
        TestEqual(TEXT("stamina clamped"), Odd.Stamina, 100);
        TestEqual(TEXT("vitality clamped"), Odd.Vitality, 0);
    }

    // Schema 4 (ADR-047, ADR-049): skill XP, money and mission outcomes round-trip; a schema 3 mission is under way.
    {
        FMarburgumWorldStore Store(Path);
        FMarburgumWorldRecord Next;
        Next.Revision = 9;
        Next.Skills.FindOrAdd(TEXT("simon")).Add(TEXT("fishing"), 44);
        Next.Money.Add(TEXT("simon"), 30);
        FMarburgumStoredMission& Done = Next.Missions.FindOrAdd(TEXT("simon")).Add(TEXT("kiosk_67_customer_stopped_coming"));
        Done.Stage = TEXT("resolution");
        Done.bCompleted = true;
        Done.Choice = TEXT("pay_tab");
        Done.Outcome = TEXT("tab_paid");
        Done.bRewardPaid = true;
        Done.Flags.Add(TEXT("ledger_initials_recognized"), true);
        FMarburgumStoredReceipt Receipt;
        Receipt.Character = TEXT("simon");
        Receipt.Kind = TEXT("mission_completed");
        Receipt.Granted.Add(TEXT("brass_key"), 1);
        Receipt.MoneyDelta = -20;
        Receipt.SkillXp.Add(TEXT("reputation"), 15);
        Next.Receipts.Add(TEXT("OP"), Receipt);
        TestTrue(TEXT("schema 4 commit"), Store.Commit(Next, Error));
    }
    {
        FMarburgumWorldStore Store(Path);
        TestTrue(TEXT("schema 4 reload"), Store.Load(Error));
        TestEqual(TEXT("xp kept"), Store.Get().Skills.FindRef(TEXT("simon")).FindRef(TEXT("fishing")), int64(44));
        TestEqual(TEXT("money kept"), Store.Get().Money.FindRef(TEXT("simon")), int64(30));
        const FMarburgumStoredMission Done = Store.Get().Missions.FindRef(TEXT("simon")).FindRef(TEXT("kiosk_67_customer_stopped_coming"));
        TestTrue(TEXT("completed kept"), Done.bCompleted && Done.bRewardPaid);
        TestEqual(TEXT("outcome kept"), Done.Outcome, FString(TEXT("tab_paid")));
        TestTrue(TEXT("flag kept"), Done.Flags.FindRef(TEXT("ledger_initials_recognized")));
        const FMarburgumStoredReceipt Receipt = Store.Get().Receipts.FindRef(TEXT("OP"));
        TestEqual(TEXT("receipt money"), Receipt.MoneyDelta, int64(-20));
        TestEqual(TEXT("receipt grant"), Receipt.Granted.FindRef(TEXT("brass_key")), 1);
        TestEqual(TEXT("receipt xp"), Receipt.SkillXp.FindRef(TEXT("reputation")), int64(15));
    }
    FFileHelper::SaveStringToFile(TEXT("{\"format\":\"marburgumii.world_store\",\"schema_version\":3,\"revision\":2,")
        TEXT("\"missions\":{\"simon\":{\"lent_first_catch\":{\"stage\":\"catch_first_fish\",\"started_revision\":1}}}}"), *Path);
    {
        FMarburgumWorldStore Store(Path);
        TestTrue(TEXT("schema 3 mission loads"), Store.Load(Error));
        TestFalse(TEXT("schema 3 mission under way"), Store.Get().Missions.FindRef(TEXT("simon")).FindRef(TEXT("lent_first_catch")).bCompleted);
        TestTrue(TEXT("schema 3 has no skills"), Store.Get().Skills.IsEmpty() && Store.Get().Money.IsEmpty());
    }

    // Schema 5 (ADR-060): shelves, first purchases and a counter receipt round-trip; a schema 4 world has none.
    {
        FMarburgumWorldStore Store(Path);
        FMarburgumWorldRecord Next;
        Next.Revision = 12;
        Next.Shelves.FindOrAdd(TEXT("kiosk_67")).Add(TEXT("mariborski_glas"), 11);
        Next.Shelves.FindOrAdd(TEXT("kiosk_67")).Add(TEXT("ballpoint_pen"), 0);
        Next.FirstPurchases.Add(TEXT("simon"), { TEXT("kiosk_67") });
        FMarburgumStoredReceipt Receipt;
        Receipt.Character = TEXT("simon");
        Receipt.Kind = TEXT("item_bought");
        Receipt.Interactable = TEXT("petra_kralj");
        Receipt.Shop = TEXT("kiosk_67");
        Receipt.Item = TEXT("mariborski_glas");
        Receipt.Quantity = 1;
        Receipt.MoneyDelta = -20;
        Receipt.SkillXp.Add(TEXT("trading"), 10);
        Next.Receipts.Add(TEXT("BUY"), Receipt);
        TestTrue(TEXT("schema 5 commit"), Store.Commit(Next, Error));
    }
    {
        FMarburgumWorldStore Store(Path);
        TestTrue(TEXT("schema 5 reload"), Store.Load(Error));
        TestEqual(TEXT("schema 5 reported"), Store.GetLoadedSchema(), 5);
        TestEqual(TEXT("shelf kept"), Store.Get().Shelves.FindRef(TEXT("kiosk_67")).FindRef(TEXT("mariborski_glas")), 11);
        const int32* Empty = Store.Get().Shelves.FindRef(TEXT("kiosk_67")).Find(TEXT("ballpoint_pen"));
        TestTrue(TEXT("an empty shelf is kept as empty, not as unknown"), Empty && *Empty == 0);
        TestTrue(TEXT("first purchase kept"), Store.Get().FirstPurchases.FindRef(TEXT("simon")).Contains(TEXT("kiosk_67")));
        const FMarburgumStoredReceipt Receipt = Store.Get().Receipts.FindRef(TEXT("BUY"));
        TestEqual(TEXT("receipt shop"), Receipt.Shop, FString(TEXT("kiosk_67")));
        TestEqual(TEXT("receipt price"), Receipt.MoneyDelta, int64(-20));
    }
    FFileHelper::SaveStringToFile(TEXT("{\"format\":\"marburgumii.world_store\",\"schema_version\":4,\"revision\":3,")
        TEXT("\"shelves\":{\"kiosk_67\":{\"mariborski_glas\":-4,\"maribor_postcard\":\"many\"}},\"money\":{\"simon\":30}}"), *Path);
    {
        FMarburgumWorldStore Store(Path);
        TestTrue(TEXT("schema 4 loads"), Store.Load(Error));
        TestTrue(TEXT("a negative or non-numeric shelf is not trusted"), Store.Get().Shelves.FindRef(TEXT("kiosk_67")).IsEmpty());
        TestTrue(TEXT("schema 4 has no first purchases"), Store.Get().FirstPurchases.IsEmpty());
    }

    // A schema this build does not know is refused rather than reinterpreted.
    FFileHelper::SaveStringToFile(TEXT("{\"format\":\"marburgumii.world_store\",\"schema_version\":6,\"revision\":1}"), *Path);
    {
        FMarburgumWorldStore Store(Path);
        TestFalse(TEXT("future schema refused"), Store.Load(Error));
    }

    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    return true;
}

#endif
