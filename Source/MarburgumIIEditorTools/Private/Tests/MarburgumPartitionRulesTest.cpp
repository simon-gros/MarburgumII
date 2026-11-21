#include "MarburgumWholeLevel.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/DirectionalLight.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/PlayerStart.h"
#include "Landscape.h"
#include "LandscapeStreamingProxy.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumPartitionRulesTest,
    "Marburgum.Editor.PartitionRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarburgumPartitionRulesTest::RunTest(const FString& Parameters)
{
    using MarburgumPartition::ShouldStream;
    const UClass& Mesh = *AStaticMeshActor::StaticClass();
    TestTrue(TEXT("a building streams"), ShouldStream(Mesh, TEXT("w106063491"), TEXT("Buildings/avtobusna")));
    TestTrue(TEXT("paving streams"), ShouldStream(Mesh, TEXT("Lent_Paved_3_7"), TEXT("Ground/Paved")));
    TestTrue(TEXT("a bridge streams"), ShouldStream(Mesh, TEXT("greenwich_meljski"), TEXT("Bridges")));
    TestTrue(TEXT("a landscape streaming proxy streams"), ShouldStream(*ALandscapeStreamingProxy::StaticClass(), TEXT("LandscapeStreamingProxy_0_0"), TEXT("Ground")));
    TestFalse(TEXT("the distant ring is always loaded"), ShouldStream(Mesh, TEXT("Lent_Ring_12"), TEXT("Ground/Ring")));
    TestFalse(TEXT("a Drava sheet is always loaded"), ShouldStream(Mesh, TEXT("Drava_studenci"), TEXT("Water")));
    TestFalse(TEXT("the landscape actor is always loaded"), ShouldStream(*ALandscape::StaticClass(), TEXT("Lent_Landscape"), TEXT("Ground")));
    TestFalse(TEXT("a water volume is always loaded"), ShouldStream(*APhysicsVolume::StaticClass(), TEXT("Lent_Water_3"), TEXT("Water")));
    TestFalse(TEXT("a player start is always loaded"), ShouldStream(*APlayerStart::StaticClass(), TEXT("PlayerStart"), TEXT("")));
    TestFalse(TEXT("the sun is always loaded"), ShouldStream(*ADirectionalLight::StaticClass(), TEXT("Sun"), TEXT("Lighting")));
    return true;
}

#endif
