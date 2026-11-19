#include "Misc/AutomationTest.h"
#include "UI/MarburgumUITheme.h"

#if WITH_DEV_AUTOMATION_TESTS

// Capitals for headings and name labels keep Slovene and neighbouring letters (QA 2026-11-19): FString::ToUpper
// changes ASCII only, so a sender called Kočar showed as "KOčAR" in chat.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumUICapsTest, "Marburgum.Client.UICaps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::CommandletContext
        | EAutomationTestFlags::ProductFilter)

bool FMarburgumUICapsTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("ASCII"), MarburgumUI::Caps(FString(TEXT("Petra Kralj"))).ToString(), FString(TEXT("PETRA KRALJ")));
    TestEqual(TEXT("Slovene"), MarburgumUI::Caps(FString(TEXT("Ana Kočar, Špela Žagar"))).ToString(), FString(TEXT("ANA KOČAR, ŠPELA ŽAGAR")));
    TestEqual(TEXT("Croatian"), MarburgumUI::Caps(FString(TEXT("Đurđa Kranjčević"))).ToString(), FString(TEXT("ĐURĐA KRANJČEVIĆ")));
    TestEqual(TEXT("German"), MarburgumUI::Caps(FString(TEXT("Müller Öztürk"))).ToString(), FString(TEXT("MÜLLER ÖZTÜRK")));
    TestEqual(TEXT("from FText"), MarburgumUI::Caps(FText::FromString(TEXT("ribič"))).ToString(), FString(TEXT("RIBIČ")));
    return true;
}

#endif
