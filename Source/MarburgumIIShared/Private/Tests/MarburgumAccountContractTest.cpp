#include "MarburgumAccountContract.h"
#include "MarburgumGameplayContract.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumAccountContractTest, "Marburgum.Shared.AccountContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext
        | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

bool FMarburgumAccountContractTest::RunTest(const FString& Parameters)
{
    using namespace MarburgumII::Accounts;

    // The development key (ADR-014) must not reach an account character's world-store id (QA 2026-11-19, A6).
    const FString AccountCharacter = TEXT("c_0123456789abcdef01234567");
    TestTrue(TEXT("an account id is a well-formed key"), MarburgumII::Gameplay::IsValidCharacterKey(AccountCharacter));
    TestTrue(TEXT("an account id is recognised"), IsAccountCharacterKey(AccountCharacter));
    for (const TCHAR* DevKey : { TEXT("simon"), TEXT("mb2p-day-t17950965379586"), TEXT("cc_old"), TEXT("ac_1"), TEXT("C_x1") })
    {
        TestFalse(*FString::Printf(TEXT("%s is a development key"), DevKey), IsAccountCharacterKey(DevKey));
    }

    // Only an https:// service address is used; anything else turns accounts on (the server refuses everyone)
    // without sending a password or the server key in the clear (A9).
    const FString Saved = FCommandLine::Get();
    const auto With = [](const TCHAR* Line)
    {
        FCommandLine::Set(Line);
    };
    With(TEXT("-MB2Accounts=https://127.0.0.1:7792/"));
    TestTrue(TEXT("https requested"), ServiceRequested());
    TestEqual(TEXT("https used, slash trimmed"), ServiceUrl(), FString(TEXT("https://127.0.0.1:7792")));
    With(TEXT("-MB2Accounts=HTTPS://accounts.example"));
    TestEqual(TEXT("scheme case ignored"), ServiceUrl(), FString(TEXT("HTTPS://accounts.example")));
    With(TEXT("-MB2Accounts=http://127.0.0.1:7792"));
    TestTrue(TEXT("http still requests accounts"), ServiceRequested());
    TestTrue(TEXT("http is never used"), ServiceUrl().IsEmpty());
    With(TEXT("-log"));
    TestFalse(TEXT("no switch, no accounts"), ServiceRequested());
    TestTrue(TEXT("no switch, no address"), ServiceUrl().IsEmpty());
    FCommandLine::Set(*Saved);
    return true;
}

#endif
