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
        TestEqual(TEXT("schema"), Store.GetLoadedSchema(), 7);
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
        TestEqual(TEXT("current schema reported"), Store.GetLoadedSchema(), 7);
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

    // Schema 6 (ADR-081): each character's documented photo subjects round-trip; a schema 5 world has none.
    {
        FMarburgumWorldStore Store(Path);
        FMarburgumWorldRecord Next;
        Next.Revision = 20;
        Next.Discoveries.Add(TEXT("simon"), { TEXT("vodni_stolp"), TEXT("stari_most") });
        Next.Skills.FindOrAdd(TEXT("simon")).Add(TEXT("photography"), 50);
        TestTrue(TEXT("schema 6 commit"), Store.Commit(Next, Error));
    }
    {
        FMarburgumWorldStore Store(Path);
        TestTrue(TEXT("schema 6 reload"), Store.Load(Error));
        TestEqual(TEXT("current schema reported after a schema 6 commit"), Store.GetLoadedSchema(), 7);
        const TArray<FString> Documented = Store.Get().Discoveries.FindRef(TEXT("simon"));
        TestTrue(TEXT("discoveries kept"), Documented.Num() == 2 && Documented.Contains(TEXT("vodni_stolp")) && Documented.Contains(TEXT("stari_most")));
        TestEqual(TEXT("photography XP kept"), Store.Get().Skills.FindRef(TEXT("simon")).FindRef(TEXT("photography")), int64(50));
    }
    FFileHelper::SaveStringToFile(TEXT("{\"format\":\"marburgumii.world_store\",\"schema_version\":5,\"revision\":3,\"first_purchases\":{}}"), *Path);
    {
        FMarburgumWorldStore Store(Path);
        TestTrue(TEXT("schema 5 loads"), Store.Load(Error));
        TestTrue(TEXT("schema 5 has no discoveries"), Store.Get().Discoveries.IsEmpty());
    }

    // Schema 7 (ADR-085): attributes and equipment round-trip with canonical ids; a schema 6 world has neither.
    {
        FMarburgumWorldStore Store(Path);
        TestTrue(TEXT("schema 5 store reopens"), Store.Load(Error));
        FMarburgumWorldRecord Next;
        Next.Revision = 30;
        MarburgumII::CharacterSheet::FScores Scores;
        Scores[0] = 13;
        Scores[1] = 15;
        Next.Attributes.Add(TEXT("simon"), Scores);
        Next.Inventories.FindOrAdd(TEXT("simon")).Add(TEXT("fishing_rod"), 1);
        Next.Equipment.FindOrAdd(TEXT("simon")).Add(TEXT("right_hand"), TEXT("fishing_rod"));
        FMarburgumStoredReceipt Receipt;
        Receipt.Character = TEXT("simon");
        Receipt.Kind = TEXT("item_equipped");
        Receipt.Item = TEXT("fishing_rod");
        Receipt.Quantity = 1;
        Receipt.Slot = TEXT("right_hand");
        Next.Receipts.Add(TEXT("EQUIP"), Receipt);
        TestTrue(TEXT("schema 7 commit"), Store.Commit(Next, Error));
        FString Written;
        FFileHelper::LoadFileToString(Written, *Path);
        TestTrue(TEXT("endurance is written"), Written.Contains(TEXT("\"endurance\"")));
        TestFalse(TEXT("wind is never written"), Written.Contains(TEXT("wind")));
    }
    {
        FMarburgumWorldStore Store(Path);
        TestTrue(TEXT("schema 7 reload"), Store.Load(Error));
        TestEqual(TEXT("schema 7 reported"), Store.GetLoadedSchema(), 7);
        TestTrue(TEXT("a clean document needs no migration"), Store.GetMigration().IsEmpty());
        const MarburgumII::CharacterSheet::FScores Loaded = Store.Get().Attributes.FindRef(TEXT("simon"));
        TestEqual(TEXT("might kept"), Loaded[0], 13);
        TestEqual(TEXT("endurance kept"), Loaded[1], 15);
        TestEqual(TEXT("eye default"), Loaded[2], 10);
        TestEqual(TEXT("equipment kept"), Store.Get().Equipment.FindRef(TEXT("simon")).FindRef(TEXT("right_hand")), FString(TEXT("fishing_rod")));
        TestEqual(TEXT("receipt slot kept"), Store.Get().Receipts.FindRef(TEXT("EQUIP")).Slot, FString(TEXT("right_hand")));
    }
    // A legacy record: `wind` read as Endurance, a mixed record keeps `endurance`, damage is repaired and reported.
    FFileHelper::SaveStringToFile(TEXT("{\"format\":\"marburgumii.world_store\",\"schema_version\":6,\"revision\":4,")
        TEXT("\"attributes\":{\"old\":{\"might\":12,\"wind\":14,\"eye\":9,\"poise\":10},")
        TEXT("\"mixed\":{\"might\":10,\"wind\":7,\"endurance\":16,\"eye\":10,\"poise\":10},")
        TEXT("\"broken\":{\"might\":40,\"endurance\":\"lots\",\"eye\":10,\"poise\":10},\"plain\":{\"might\":10,\"endurance\":10,\"eye\":10,\"poise\":10}},")
        TEXT("\"equipment\":{\"old\":{\"right_hand\":\"fishing_rod\",\"head\":\"fishing_rod\",\"pocket\":\"camera\"}}}"), *Path);
    {
        FMarburgumWorldStore Store(Path);
        TestTrue(TEXT("legacy attributes load"), Store.Load(Error));
        const FMarburgumStoreMigration& Migration = Store.GetMigration();
        TestTrue(TEXT("old came from wind"), Migration.LegacyAliases.Contains(TEXT("old")));
        TestTrue(TEXT("mixed conflict audited"), Migration.Conflicts.Contains(TEXT("mixed")));
        TestTrue(TEXT("broken repaired"), Migration.Repaired.Contains(TEXT("broken")));
        TestTrue(TEXT("misfit equipment repaired"), Migration.Repaired.Contains(TEXT("old")));
        TestEqual(TEXT("wind became endurance"), Store.Get().Attributes.FindRef(TEXT("old"))[1], 14);
        TestEqual(TEXT("other scores survive the rename"), Store.Get().Attributes.FindRef(TEXT("old"))[0], 12);
        TestEqual(TEXT("canonical endurance wins"), Store.Get().Attributes.FindRef(TEXT("mixed"))[1], 16);
        TestEqual(TEXT("might clamped"), Store.Get().Attributes.FindRef(TEXT("broken"))[0], 20);
        TestEqual(TEXT("unusable endurance defaulted"), Store.Get().Attributes.FindRef(TEXT("broken"))[1], 10);
        TestFalse(TEXT("a default character is not stored"), Store.Get().Attributes.Contains(TEXT("plain")));
        const TMap<FString, FString> Slots = Store.Get().Equipment.FindRef(TEXT("old"));
        TestTrue(TEXT("only the fitting slot survives"), Slots.Num() == 1 && Slots.FindRef(TEXT("right_hand")) == TEXT("fishing_rod"));
        // The next commit writes the canonical record; loading it again migrates nothing.
        FMarburgumWorldRecord Next = Store.Get();
        ++Next.Revision;
        TestTrue(TEXT("migrated commit"), Store.Commit(Next, Error));
    }
    {
        FMarburgumWorldStore Store(Path);
        TestTrue(TEXT("migrated reload"), Store.Load(Error));
        TestTrue(TEXT("migration is one-time"), Store.GetMigration().IsEmpty());
        TestEqual(TEXT("endurance still 14"), Store.Get().Attributes.FindRef(TEXT("old"))[1], 14);
        FString Written;
        FFileHelper::LoadFileToString(Written, *Path);
        TestFalse(TEXT("no wind left on disk"), Written.Contains(TEXT("wind")));
    }
    FFileHelper::SaveStringToFile(TEXT("{\"format\":\"marburgumii.world_store\",\"schema_version\":6,\"revision\":3}"), *Path);
    {
        FMarburgumWorldStore Store(Path);
        TestTrue(TEXT("schema 6 loads"), Store.Load(Error));
        TestTrue(TEXT("schema 6 has no attributes"), Store.Get().Attributes.IsEmpty());
        TestTrue(TEXT("schema 6 has no equipment"), Store.Get().Equipment.IsEmpty());
        TestTrue(TEXT("nothing to migrate"), Store.GetMigration().IsEmpty());
    }

    // A schema this build does not know is refused rather than reinterpreted.
    FFileHelper::SaveStringToFile(TEXT("{\"format\":\"marburgumii.world_store\",\"schema_version\":8,\"revision\":1}"), *Path);
    {
        FMarburgumWorldStore Store(Path);
        TestFalse(TEXT("future schema refused"), Store.Load(Error));
    }

    // One server per store: a second one on the same path is refused until the first lets go (QA 2026-11-19).
    const FString Shared = FPaths::Combine(Directory, TEXT("shared.json"));
    {
        FMarburgumWorldStore First(Shared);
        TestTrue(TEXT("first server takes the store"), First.Load(Error));
        {
            FMarburgumWorldStore Second(Shared);
            FString SecondError;
            TestFalse(TEXT("second server on the same store is refused"), Second.Load(SecondError));
            TestTrue(TEXT("the refusal names the other server"), SecondError.Contains(TEXT("another server")));
        }
        FMarburgumWorldRecord Next;
        Next.Revision = 1;
        TestTrue(TEXT("the first still commits"), First.Commit(Next, Error));
    }
    {
        FMarburgumWorldStore Later(Shared);
        TestTrue(TEXT("the store is free once the first is gone"), Later.Load(Error));
        TestEqual(TEXT("and holds the first's commit"), Later.Get().Revision, static_cast<int64>(1));
    }

    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    return true;
}

#endif
