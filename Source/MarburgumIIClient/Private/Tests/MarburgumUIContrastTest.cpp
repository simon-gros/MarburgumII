#include "Misc/AutomationTest.h"
#include "UI/MarburgumUISettings.h"
#include "UI/MarburgumUITheme.h"

#if WITH_DEV_AUTOMATION_TESTS

// QA U13: every text colour the theme hands out reads at WCAG 4.5:1 or better on every opaque surface it is drawn on,
// in the standard and the high-contrast theme.

namespace
{
    double RelativeLuminance(const FLinearColor& Linear)
    {
        // Theme colours are linear already (FLinearColor::FromSRGBColor), so no transfer function here.
        return 0.2126 * Linear.R + 0.7152 * Linear.G + 0.0722 * Linear.B;
    }

    double ContrastRatio(const FLinearColor& A, const FLinearColor& B)
    {
        const double LA = RelativeLuminance(A);
        const double LB = RelativeLuminance(B);
        return (FMath::Max(LA, LB) + 0.05) / (FMath::Min(LA, LB) + 0.05);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumUIContrastTest, "Marburgum.Client.UIContrast",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::CommandletContext
        | EAutomationTestFlags::ProductFilter)

bool FMarburgumUIContrastTest::RunTest(const FString& Parameters)
{
    for (const bool bHighContrast : { false, true })
    {
        MarburgumUI::FPresentationOptions Options;
        Options.bHighContrast = bHighContrast;
        MarburgumUI::SetPresentationOptions(Options);
        const MarburgumUI::FColors& C = MarburgumUI::Tokens().Color;
        const TPair<const TCHAR*, FLinearColor> Surfaces[] = {
            { TEXT("Backdrop"), C.Backdrop }, { TEXT("Panel"), C.Panel }, { TEXT("Raised"), C.Raised }, { TEXT("Sunken"), C.Sunken },
            { TEXT("Paper"), C.Paper } };
        const TPair<const TCHAR*, FLinearColor> Inks[] = {
            { TEXT("TextPrimary"), C.TextPrimary }, { TEXT("TextSecondary"), C.TextSecondary }, { TEXT("TextMuted"), C.TextMuted },
            { TEXT("Accent"), C.Accent }, { TEXT("Info"), C.Info }, { TEXT("Success"), C.Success }, { TEXT("Warning"), C.Warning },
            { TEXT("Danger"), C.Danger }, { TEXT("PaperText"), C.PaperText } };
        for (const auto& Surface : Surfaces)
        {
            for (const auto& Ink : Inks)
            {
                const double Ratio = ContrastRatio(Ink.Value, Surface.Value);
                TestTrue(FString::Printf(TEXT("%s on %s%s: %.2f:1"), Ink.Key, Surface.Key, bHighContrast ? TEXT(" (high contrast)") : TEXT(""), Ratio),
                    Ratio >= 4.5);
            }
        }
        TestTrue(TEXT("TextOnAccent on Accent"), ContrastRatio(C.TextOnAccent, C.Accent) >= 4.5);
    }
    // Back to the player's own theme.
    UMarburgumUISettings::Get().ApplyToRuntime(nullptr);
    return true;
}

#endif
