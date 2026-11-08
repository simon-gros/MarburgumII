#include "MarburgumChatPolicy.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumChatPolicyTest,
    "Marburgum.Chat.TextPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarburgumChatPolicyTest::RunTest(const FString& Parameters)
{
    FString Plain;
    TestTrue(TEXT("Ordinary message accepted"),
        MarburgumII::Chat::SanitizeMessage(TEXT("  Hello, Maribor  "), Plain));
    TestEqual(TEXT("Whitespace trimmed"), Plain, FString(TEXT("Hello, Maribor")));
    TestTrue(TEXT("Slovene diacritics accepted"),
        MarburgumII::Chat::SanitizeMessage(TEXT("Živjo, Maribor!"), Plain));
    TestFalse(TEXT("Empty message rejected"),
        MarburgumII::Chat::SanitizeMessage(TEXT("  "), Plain));
    TestFalse(TEXT("Newlines rejected"),
        MarburgumII::Chat::SanitizeMessage(TEXT("first\nsecond"), Plain));
    TestFalse(TEXT("Overlong message rejected"),
        MarburgumII::Chat::SanitizeMessage(FString::ChrN(241, TEXT('x')), Plain));
    TestFalse(TEXT("Bidi control rejected"),
        MarburgumII::Chat::SanitizeMessage(FString(TEXT("hello")) + TCHAR(0x202E), Plain));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
