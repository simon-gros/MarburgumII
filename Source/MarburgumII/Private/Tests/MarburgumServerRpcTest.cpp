#include "MarburgumPlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

// No client-to-server RPC of the game takes a name. The engine turns every FName a client sends into a permanent
// entry of the server's name table, so one FName parameter lets any client grow the server's memory for as long as
// it stays connected (QA 2026-11-19, S5). Ids travel as text and become names only once the catalogue knows them.

namespace
{
    bool CarriesName(const FProperty* Property, TSet<const UStruct*>& Visited)
    {
        if (CastField<FNameProperty>(Property))
        {
            return true;
        }
        if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
        {
            return CarriesName(Array->Inner, Visited);
        }
        if (const FSetProperty* Set = CastField<FSetProperty>(Property))
        {
            return CarriesName(Set->ElementProp, Visited);
        }
        if (const FMapProperty* Map = CastField<FMapProperty>(Property))
        {
            return CarriesName(Map->KeyProp, Visited) || CarriesName(Map->ValueProp, Visited);
        }
        if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
        {
            bool bAlreadyVisited = false;
            Visited.Add(Struct->Struct, &bAlreadyVisited);
            if (!bAlreadyVisited)
            {
                for (TFieldIterator<FProperty> Field(Struct->Struct); Field; ++Field)
                {
                    if (CarriesName(*Field, Visited))
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumServerRpcNameTest,
    "Marburgum.Net.ServerRpcCarriesNoName",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarburgumServerRpcNameTest::RunTest(const FString& Parameters)
{
    int32 Checked = 0;
    for (TObjectIterator<UClass> Class; Class; ++Class)
    {
        if (!Class->GetOutermost()->GetName().StartsWith(TEXT("/Script/MarburgumII")))
        {
            continue;
        }
        for (TFieldIterator<UFunction> Function(*Class, EFieldIteratorFlags::ExcludeSuper); Function; ++Function)
        {
            if (!Function->HasAnyFunctionFlags(FUNC_NetServer))
            {
                continue;
            }
            ++Checked;
            for (TFieldIterator<FProperty> Param(*Function); Param && Param->HasAnyPropertyFlags(CPF_Parm); ++Param)
            {
                TSet<const UStruct*> Visited;
                TestFalse(FString::Printf(TEXT("%s::%s parameter %s carries an FName"), *Class->GetName(), *Function->GetName(),
                    *Param->GetName()), CarriesName(*Param, Visited));
            }
        }
    }
    // Not vacuous: the game's server RPCs were found, including the three that used to take names.
    TestTrue(TEXT("Server RPCs found"), Checked >= 3);
    const UClass* Controller = AMarburgumPlayerController::StaticClass();
    for (const TCHAR* Name : { TEXT("ServerRequestMissionChoice"), TEXT("ServerRequestTrade"), TEXT("ServerEnterVehicle") })
    {
        const UFunction* Function = Controller->FindFunctionByName(Name);
        TestTrue(FString::Printf(TEXT("%s is a server RPC"), Name), Function && Function->HasAnyFunctionFlags(FUNC_NetServer));
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
