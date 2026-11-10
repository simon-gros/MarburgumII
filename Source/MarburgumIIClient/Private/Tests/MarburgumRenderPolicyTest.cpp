#include "Render/MarburgumRenderPolicy.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

using namespace MarburgumII::Render;

namespace
{
    FRenderCapabilities Everything()
    {
        return FRenderCapabilities();
    }

    FRenderCapabilities NoPlugins()
    {
        FRenderCapabilities Caps;
        Caps.DlssUnavailable = TEXT("plugin_not_installed:DLSS");
        Caps.FrameGenerationUnavailable = TEXT("plugin_not_installed:StreamlineDLSSG");
        Caps.ReflexUnavailable = TEXT("plugin_not_installed:StreamlineReflex");
        return Caps;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumRenderPolicyTest,
    "Marburgum.Render.FeaturePolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarburgumRenderPolicyTest::RunTest(const FString& Parameters)
{
    FRenderRequest Defaults;

    FRenderResolution Out = Resolve(Defaults, Everything());
    TestTrue(TEXT("Auto picks DLSS where supported"), Out.Upscaler == EUpscaler::Dlss);
    TestTrue(TEXT("Auto DLSS starts at Quality"), Out.DlssMode == EDlssMode::Quality);
    TestTrue(TEXT("Auto Reflex is on where supported"), Out.Reflex == EReflex::On);
    TestTrue(TEXT("Frame generation is off by default"), Out.FrameGeneration == EFrameGeneration::Off);
    TestEqual(TEXT("Nothing to explain"), Out.Notes.Num(), 0);

    Out = Resolve(Defaults, NoPlugins());
    TestTrue(TEXT("Auto falls back to TSR without the plugin"), Out.Upscaler == EUpscaler::Tsr);
    TestTrue(TEXT("Reflex off without the plugin"), Out.Reflex == EReflex::Off);
    TestTrue(TEXT("Fallback reason recorded"), Out.Notes.Contains(TEXT("dlss:plugin_not_installed:DLSS")));

    FRenderRequest Tsr;
    Tsr.Upscaler = EUpscaler::Tsr;
    TestTrue(TEXT("Explicit TSR stays TSR on capable hardware"), Resolve(Tsr, Everything()).Upscaler == EUpscaler::Tsr);

    FRenderRequest Dlaa;
    Dlaa.Upscaler = EUpscaler::Dlss;
    Dlaa.DlssMode = EDlssMode::Dlaa;
    Out = Resolve(Dlaa, Everything());
    TestTrue(TEXT("DLAA is a DLSS mode"), Out.Upscaler == EUpscaler::Dlss && Out.DlssMode == EDlssMode::Dlaa);
    TestEqual(TEXT("DLAA renders at full resolution"), DlssScreenPercentage(EDlssMode::Dlaa), 100.f);
    TestTrue(TEXT("Performance renders at half resolution"), FMath::IsNearlyEqual(DlssScreenPercentage(EDlssMode::Performance), 50.f));

    FRenderRequest Safe = Dlaa;
    Safe.FrameGeneration = EFrameGeneration::Dlss;
    Safe.Reflex = EReflex::Boost;
    Safe.bSafeMode = true;
    Out = Resolve(Safe, Everything());
    TestTrue(TEXT("Safe mode forces TSR"), Out.Upscaler == EUpscaler::Tsr);
    TestTrue(TEXT("Safe mode forces frame generation off"), Out.FrameGeneration == EFrameGeneration::Off);
    TestTrue(TEXT("Safe mode forces Reflex off"), Out.Reflex == EReflex::Off);
    TestTrue(TEXT("Safe mode is recorded"), Out.Notes.Contains(TEXT("safe_mode")));

    FRenderRequest Generated;
    Generated.FrameGeneration = EFrameGeneration::Dlss;
    Generated.Reflex = EReflex::Off;
    Out = Resolve(Generated, Everything());
    TestTrue(TEXT("DLSS frame generation runs where supported"), Out.FrameGeneration == EFrameGeneration::Dlss);
    TestTrue(TEXT("Frame generation keeps Reflex on"), Out.Reflex == EReflex::On);
    TestTrue(TEXT("Forced Reflex is recorded"), Out.Notes.Contains(TEXT("reflex:required_by_frame_generation")));

    FRenderCapabilities SmoothMotion = Everything();
    SmoothMotion.bExternalFrameGeneration = true;
    Out = Resolve(Generated, SmoothMotion);
    TestTrue(TEXT("Smooth Motion blocks native frame generation"), Out.FrameGeneration == EFrameGeneration::Off);
    TestTrue(TEXT("Conflict recorded"), Out.Notes.Contains(TEXT("frame_generation:conflict_smooth_motion")));
    TestTrue(TEXT("Player's Reflex Off is honoured without frame generation"), Out.Reflex == EReflex::Off);

    FRenderCapabilities NoReflex = Everything();
    NoReflex.ReflexUnavailable = TEXT("unsupported");
    Out = Resolve(Generated, NoReflex);
    TestTrue(TEXT("Frame generation needs Reflex"), Out.FrameGeneration == EFrameGeneration::Off);
    TestTrue(TEXT("Missing Reflex recorded"), Out.Notes.Contains(TEXT("frame_generation:reflex_unsupported")));

    EDlssMode Mode = EDlssMode::Quality;
    TestTrue(TEXT("Tokens parse case-insensitively"), FromToken(TEXT(" Ultra_Performance "), Mode));
    TestTrue(TEXT("Parsed mode"), Mode == EDlssMode::UltraPerformance);
    EUpscaler Upscaler = EUpscaler::Tsr;
    TestFalse(TEXT("Unknown token rejected"), FromToken(TEXT("fsr"), Upscaler));
    TestTrue(TEXT("Rejected token leaves the value"), Upscaler == EUpscaler::Tsr);
    for (const EReflex Value : { EReflex::Auto, EReflex::Off, EReflex::On, EReflex::Boost })
    {
        EReflex Parsed = EReflex::Auto;
        TestTrue(TEXT("Reflex token round-trips"), FromToken(ToToken(Value), Parsed) && Parsed == Value);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
