#include "MarburgumSurface.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumSurfaceTest, "Marburgum.Shared.Surface",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext
        | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

bool FMarburgumSurfaceTest::RunTest(const FString& Parameters)
{
    using namespace MarburgumII::Surface;

    // Physical materials name the surface; anything else is unknown.
    TestEqual(TEXT("PM_asphalt"), FromPhysicalMaterialName(TEXT("PM_asphalt")), FString(TEXT("asphalt")));
    TestEqual(TEXT("PM_Cobblestone"), FromPhysicalMaterialName(TEXT("PM_Cobblestone")), FString(TEXT("cobblestone")));
    TestEqual(TEXT("engine default"), FromPhysicalMaterialName(TEXT("DefaultPhysicalMaterial")), FString(Unknown));
    TestEqual(TEXT("bare prefix"), FromPhysicalMaterialName(TEXT("PM_")), FString(Unknown));
    TestEqual(TEXT("roads"), FString(PavedSlotSurface(TEXT("asphalt"))), FString(TEXT("asphalt")));
    TestEqual(TEXT("paint lies on asphalt"), FString(PavedSlotSurface(TEXT("paint"))), FString(TEXT("asphalt")));
    TestEqual(TEXT("setts"), FString(PavedSlotSurface(TEXT("setts"))), FString(TEXT("cobblestone")));
    TestEqual(TEXT("pavers"), FString(PavedSlotSurface(TEXT("pavers"))), FString(TEXT("paving")));
    TestEqual(TEXT("kerb"), FString(PavedSlotSurface(TEXT("kerb"))), FString(TEXT("concrete")));

    // A 2 x 2 map over ±2 m: north-west grass, north-east hard standing, south-west earth, south-east river stones.
    TArray<uint8> Bytes = { 'M', 'B', 'S', 'K', 1, 0, 0, 0, 2, 0, 0, 0 };
    const float Half = 2.f;
    Bytes.Append(reinterpret_cast<const uint8*>(&Half), 4);
    Bytes.Append({ 1, 0, 2, 4 });
    FLandscapeMap Map;
    TestTrue(TEXT("map parses"), Map.Parse(Bytes));
    // Unreal +X is north and +Y east.
    TestEqual(TEXT("north-west"), Map.SurfaceAt(100.0, -100.0), FString(TEXT("grass")));
    TestEqual(TEXT("north-east"), Map.SurfaceAt(100.0, 100.0), FString(TEXT("concrete")));
    TestEqual(TEXT("south-west"), Map.SurfaceAt(-100.0, -100.0), FString(TEXT("soil")));
    TestEqual(TEXT("south-east"), Map.SurfaceAt(-100.0, 100.0), FString(TEXT("gravel")));
    TestEqual(TEXT("outside"), Map.SurfaceAt(500.0, 0.0), FString(Unknown));
    TArray<uint8> Bad = Bytes;
    Bad.Last() = 9;
    TestFalse(TEXT("an unknown kind is refused"), FLandscapeMap().Parse(Bad));
    Bad = Bytes;
    Bad.Pop();
    TestFalse(TEXT("a short map is refused"), FLandscapeMap().Parse(Bad));
    TestEqual(TEXT("unloaded map is unknown"), FLandscapeMap().SurfaceAt(0.0, 0.0), FString(Unknown));

    // The committed map: Lent's 1,536 m square in 2 m cells.
    TArray<uint8> Lent;
    TestTrue(TEXT("lent_surface.bin readable"), FFileHelper::LoadFileToArray(Lent, *FPaths::Combine(FPaths::ProjectContentDir(), LandscapeMapFile)));
    FLandscapeMap LentMap;
    TestTrue(TEXT("lent_surface.bin parses"), LentMap.Parse(Lent));
    TestEqual(TEXT("768 cells a side"), LentMap.GetCells(), 768);

    // Footstep sets.
    TestEqual(TEXT("dry asphalt is hard"), FootstepSetFor(TEXT("asphalt"), 0.0, 0.0, 0.0), FootstepHard);
    TestEqual(TEXT("unknown ground sounds hard"), FootstepSetFor(Unknown, 0.0, 0.0, 0.5), FootstepHard);
    TestEqual(TEXT("soaked asphalt splashes"), FootstepSetFor(TEXT("asphalt"), 1.0, 0.0, 0.999), FootstepHardWet);
    TestEqual(TEXT("a damp street does not"), FootstepSetFor(TEXT("cobblestone"), 0.1, 0.0, 0.0), FootstepHard);
    TestEqual(TEXT("grass is soft, wet or not"), FootstepSetFor(TEXT("grass"), 1.0, 0.0, 0.0), FootstepSoft);
    TestEqual(TEXT("gravel crunches"), FootstepSetFor(TEXT("gravel"), 0.0, 0.0, 0.0), FootstepCrunch);
    TestEqual(TEXT("lying snow crunches everywhere"), FootstepSetFor(TEXT("asphalt"), 1.0, 0.05, 0.0), FootstepCrunch);
    TestEqual(TEXT("wet share at 0"), WetStepShare(0.0), 0.0);
    TestEqual(TEXT("wet share at 1"), WetStepShare(1.0), 1.0);
    TestTrue(TEXT("wet share rises"), WetStepShare(0.3) > 0.0 && WetStepShare(0.3) < WetStepShare(0.5));
    TestEqual(TEXT("asset path"), FootstepAsset(FootstepHardWet, 3), FString(TEXT("/Game/Audio/Footsteps/footstep_concrete_wet_003.footstep_concrete_wet_003")));
    TestTrue(TEXT("walking is quieter than running"), StepVolume(220.0) < StepVolume(440.0) && StepVolume(440.0) < StepVolume(500.0));

    // A foot that lifts and comes down is one footfall; a foot that only wobbles is none.
    FFootfallDetector Foot;
    int32 Falls = 0;
    for (int32 Frame = 0; Frame < 600; ++Frame)
    {
        // Two strides a second at 60 fps: 10 cm ankle lifts.
        const double Phase = FMath::Fmod(Frame / 60.0 * 1.0, 1.0);
        const double Height = 10.0 + (Phase < 0.4 ? 0.0 : 12.0 * FMath::Sin(UE_PI * (Phase - 0.4) / 0.6));
        Falls += Foot.Update(Height, 1.0 / 60.0) ? 1 : 0;
    }
    TestEqual(TEXT("ten strides, ten footfalls"), Falls, 10);
    FFootfallDetector Still;
    Falls = 0;
    for (int32 Frame = 0; Frame < 600; ++Frame)
    {
        Falls += Still.Update(10.0 + 2.0 * FMath::Sin(Frame * 0.3), 1.0 / 60.0) ? 1 : 0;
    }
    TestEqual(TEXT("a wobble is no step"), Falls, 0);
    return true;
}

#endif
