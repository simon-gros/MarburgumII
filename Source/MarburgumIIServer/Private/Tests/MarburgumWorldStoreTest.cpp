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

    // A schema 3 commit round-trips each character's condition.
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
        TestEqual(TEXT("schema"), Store.GetLoadedSchema(), 3);
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

    // A schema this build does not know is refused rather than reinterpreted.
    FFileHelper::SaveStringToFile(TEXT("{\"format\":\"marburgumii.world_store\",\"schema_version\":4,\"revision\":1}"), *Path);
    {
        FMarburgumWorldStore Store(Path);
        TestFalse(TEXT("future schema refused"), Store.Load(Error));
    }

    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    return true;
}

#endif
