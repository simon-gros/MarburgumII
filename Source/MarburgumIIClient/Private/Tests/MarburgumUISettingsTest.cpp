#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/TextLocalizationResource.h"
#include "Misc/Paths.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "UI/MarburgumUISettings.h"
#include "UI/MarburgumUITheme.h"

#if WITH_DEV_AUTOMATION_TESTS

// The Settings screen's preferences (UI-06 settings_screen): a hand-edited ini cannot push a value out of range, and
// Persist writes every preference to this machine's GameUserSettings.ini, where the next start reads it.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumUISettingsTest, "Marburgum.Client.UISettings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::CommandletContext
        | EAutomationTestFlags::ProductFilter)

bool FMarburgumUISettingsTest::RunTest(const FString& Parameters)
{
    UMarburgumUISettings& Settings = UMarburgumUISettings::Get();
    // Restored at the end: the test must not change the player's preferences.
    const float SavedScale = Settings.InterfaceScale;
    const int32 SavedTextSize = Settings.TextSize;
    const bool bSavedContrast = Settings.bHighContrast;
    const bool bSavedMotion = Settings.bReducedMotion;
    const float SavedHud = Settings.HudOpacity;
    const int32 SavedCondition = Settings.ConditionDisplay;
    const int32 SavedNotices = Settings.NotificationTime;
    const bool bSavedStamps = Settings.bChatTimestamps;
    const float SavedVolume = Settings.MasterVolume;
    const FString SavedLanguage = Settings.Language;

    Settings.InterfaceScale = 9.f;
    Settings.TextSize = -3;
    Settings.HudOpacity = 0.f;
    Settings.NotificationTime = 7;
    Settings.MasterVolume = NAN;
    Settings.Language = TEXT("klingon");
    Settings.Sanitize();
    TestEqual(TEXT("scale clamped"), Settings.InterfaceScale, UMarburgumUISettings::MaxInterfaceScale);
    TestEqual(TEXT("text size clamped"), Settings.TextSize, 0);
    TestEqual(TEXT("HUD opacity clamped"), Settings.HudOpacity, 0.4f);
    TestEqual(TEXT("notification time clamped"), Settings.NotificationTime, 2);
    TestEqual(TEXT("volume repaired"), Settings.MasterVolume, 1.f);
    TestTrue(TEXT("unknown language dropped"), Settings.Language.IsEmpty());

    Settings.InterfaceScale = 1.25f;
    Settings.TextSize = 2;
    Settings.bHighContrast = true;
    Settings.bReducedMotion = true;
    Settings.HudOpacity = 0.7f;
    Settings.Language = TEXT("sl");
    Settings.Persist();
    const FString Section = UMarburgumUISettings::StaticClass()->GetPathName();
    float Scale = 0.f;
    int32 TextSize = 0;
    bool bContrast = false;
    bool bMotion = false;
    FString Language;
    TestTrue(TEXT("scale saved"), GConfig->GetFloat(*Section, TEXT("InterfaceScale"), Scale, GGameUserSettingsIni) && FMath::IsNearlyEqual(Scale, 1.25f));
    TestTrue(TEXT("text size saved"), GConfig->GetInt(*Section, TEXT("TextSize"), TextSize, GGameUserSettingsIni) && TextSize == 2);
    TestTrue(TEXT("contrast saved"), GConfig->GetBool(*Section, TEXT("bHighContrast"), bContrast, GGameUserSettingsIni) && bContrast);
    TestTrue(TEXT("motion saved"), GConfig->GetBool(*Section, TEXT("bReducedMotion"), bMotion, GGameUserSettingsIni) && bMotion);
    TestTrue(TEXT("language saved"), GConfig->GetString(*Section, TEXT("Language"), Language, GGameUserSettingsIni) && Language == TEXT("sl"));

    Settings.ResetToDefaults();
    TestEqual(TEXT("reset scale"), Settings.InterfaceScale, 1.f);
    TestFalse(TEXT("reset contrast"), Settings.bHighContrast);
    TestTrue(TEXT("reset language follows the system"), Settings.Language.IsEmpty());

    Settings.InterfaceScale = SavedScale;
    Settings.TextSize = SavedTextSize;
    Settings.bHighContrast = bSavedContrast;
    Settings.bReducedMotion = bSavedMotion;
    Settings.HudOpacity = SavedHud;
    Settings.ConditionDisplay = SavedCondition;
    Settings.NotificationTime = SavedNotices;
    Settings.bChatTimestamps = bSavedStamps;
    Settings.MasterVolume = SavedVolume;
    Settings.Language = SavedLanguage;
    Settings.Persist();
    return true;
}

// QA U11: the Slovene translation is compiled (Content/Localization/Game/sl/Game.locres) and carries the interface's
// strings, and a game date takes the language's own calendar names. (An editor process does not apply game text to
// the running culture; the -culture=sl client captures show the translation live.)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumLocalizationTest, "Marburgum.Client.Localization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::CommandletContext
        | EAutomationTestFlags::ProductFilter)

bool FMarburgumLocalizationTest::RunTest(const FString& Parameters)
{
    FTextLocalizationResource Slovene;
    const FString Path = FPaths::ProjectContentDir() / TEXT("Localization/Game/sl/Game.locres");
    if (!TestTrue(TEXT("Slovene resource loads"), Slovene.LoadFromFile(Path, 0)))
    {
        return false;
    }
    const auto Expect = [this, &Slovene](const TCHAR* Namespace, const TCHAR* Key, const TCHAR* Wanted)
    {
        const FTextLocalizationResource::FEntry* Entry = Slovene.Entries.Find(FTextId(Namespace, Key));
        const FString Found = Entry && Entry->LocalizedString ? *Entry->LocalizedString : FString(TEXT("<missing>"));
        TestEqual(FString::Printf(TEXT("%s,%s"), Namespace, Key), Found, FString(Wanted));
    };
    Expect(TEXT("MarburgumChat"), TEXT("Overline"), TEXT("KLEPET"));
    Expect(TEXT("MarburgumSystemMenu"), TEXT("Quit"), TEXT("Zapri igro"));
    TestTrue(TEXT("every gathered string translated"), Slovene.Entries.Num() >= 600);

    FInternationalization& I18N = FInternationalization::Get();
    const FString Language = I18N.GetCurrentLanguage()->GetName();
    const FString Locale = I18N.GetCurrentLocale()->GetName();
    TestTrue(TEXT("Slovene culture available"), I18N.SetCurrentLanguageAndLocale(TEXT("sl")));
    const FString Date = MarburgumUI::GameDate(1996, 6, 18, 1, true).ToString();
    TestTrue(FString::Printf(TEXT("Slovene date (%s)"), *Date), Date.Contains(TEXT("junij")) && Date.Contains(TEXT("1996")));
    I18N.SetCurrentLanguageAndLocale(TEXT("en"));
    TestEqual(TEXT("English date"), MarburgumUI::GameDate(1996, 6, 18, 1, true).ToString(), FString(TEXT("Tuesday 18 June 1996")));
    I18N.SetCurrentLanguage(Language);
    I18N.SetCurrentLocale(Locale);
    return true;
}

#endif
