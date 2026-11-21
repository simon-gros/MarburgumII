#include "MarburgumVegetation.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// The trees and bushes (GFX-01): the file tools/lent-ground writes, how far each species is drawn and its trunk.

namespace
{
    using namespace MarburgumII;

    constexpr EAutomationTestFlags VegetationTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
        | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter;

    const TCHAR* Species = TEXT(R"("species":[)"
        R"({"id":"lime","tree":true,"natural_height_m":16.0,"natural_radius_m":6.0,"trunk_radius_m":0.35,"trunk_height_m":4.0},)"
        R"({"id":"privet","tree":false,"natural_height_m":2.0,"natural_radius_m":1.6,"trunk_radius_m":0.035,"trunk_height_m":0.22}])");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumVegetationFileTest, "Marburgum.Shared.Vegetation.File", VegetationTestFlags)
bool FMarburgumVegetationFileTest::RunTest(const FString& Parameters)
{
    using namespace Vegetation;

    TArray<FSpecies> Kinds;
    TArray<FPlant> Plants;
    FString Error;
    const FString Two = FString::Printf(TEXT(R"({"format":"marburgumii.vegetation","version":1,%s,"plants":[[10.5,-20.25,3.1,0,45.0,0.8,1.1],[0,0,0,1,-30,1,1]]})"), Species);
    TestTrue(TEXT("a small file reads"), Parse(Two, Kinds, Plants, Error));
    if (TestEqual(TEXT("two species"), Kinds.Num(), 2) && TestEqual(TEXT("two plants"), Plants.Num(), 2))
    {
        TestEqual(TEXT("plan east is Unreal Y"), Plants[0].Location.Y, 1050.0);
        TestEqual(TEXT("plan south is Unreal -X"), Plants[0].Location.X, 2025.0);
        TestEqual(TEXT("the ground is Z"), Plants[0].Location.Z, 310.0);
        TestEqual(TEXT("yaw"), Plants[0].Yaw, 45.0f);
        TestEqual(TEXT("spread"), Plants[0].Spread, 0.8f);
        TestEqual(TEXT("grow"), Plants[0].Grow, 1.1f);
        TestEqual(TEXT("a lime"), Kinds[Plants[0].Species].Id, FString(TEXT("lime")));
        TestTrue(TEXT("is a tree"), Kinds[0].bTree);
        TestFalse(TEXT("privet is not"), Kinds[1].bTree);
        TestEqual(TEXT("natural height in centimetres"), Kinds[0].NaturalHeightCm, 1600.0f);
        TestEqual(TEXT("yaw normalised"), Plants[1].Yaw, -30.0f);

        // A lime is drawn for 480 m, privet for the 70 m floor; the cap is 700 m.
        TestEqual(TEXT("a lime's reach"), CullDistanceCm(Kinds[0]), 48000.0f);
        TestEqual(TEXT("privet's"), CullDistanceCm(Kinds[1]), 7000.0f);
        FSpecies Tall = Kinds[0];
        Tall.NaturalHeightCm = 4000.0f;
        TestEqual(TEXT("capped"), CullDistanceCm(Tall), 70000.0f);

        // The lime's trunk: 0.35 m scaled by its 0.8 crown is 28 cm round, 4 m to the crown grown by 1.1 is 4.4 m tall,
        // standing on the ground.
        const FTransform Trunk = TrunkTransform(Kinds[0], Plants[0]);
        TestTrue(TEXT("the trunk is 56 cm across"), FMath::IsNearlyEqual(Trunk.GetScale3D().X, 0.56, 1e-4));
        TestTrue(TEXT("and 4.4 m tall"), FMath::IsNearlyEqual(Trunk.GetScale3D().Z, 4.4, 1e-4));
        TestTrue(TEXT("from the ground up"), FMath::IsNearlyEqual(Trunk.GetLocation().Z, 310.0 + 220.0, 1e-3));
        // A thin, short trunk still blocks a walker.
        FPlant Small = Plants[0];
        Small.Spread = 0.1f;
        Small.Grow = 0.2f;
        const FTransform Thin = TrunkTransform(Kinds[0], Small);
        TestTrue(TEXT("never thinner than 20 cm"), FMath::IsNearlyEqual(Thin.GetScale3D().X, 0.2, 1e-4));
        TestTrue(TEXT("never shorter than 2.5 m"), FMath::IsNearlyEqual(Thin.GetScale3D().Z, 2.5, 1e-4));

        TestEqual(TEXT("the mesh path"), MeshPath(TEXT("lime")), FString(TEXT("/Game/Vegetation/Species/lime/SM_lime.SM_lime")));
    }
    TestFalse(TEXT("a wrong format is refused"), Parse(TEXT(R"({"format":"marburgumii.street_lights","version":1,"species":[],"plants":[]})"), Kinds, Plants, Error));
    TestFalse(TEXT("an unknown species is refused"),
        Parse(FString::Printf(TEXT(R"({"format":"marburgumii.vegetation","version":1,%s,"plants":[[0,0,0,2,0,1,1]]})"), Species), Kinds, Plants, Error));
    TestFalse(TEXT("a zero scale is refused"),
        Parse(FString::Printf(TEXT(R"({"format":"marburgumii.vegetation","version":1,%s,"plants":[[0,0,0,0,0,0,1]]})"), Species), Kinds, Plants, Error));
    TestFalse(TEXT("a short row is refused"),
        Parse(FString::Printf(TEXT(R"({"format":"marburgumii.vegetation","version":1,%s,"plants":[[0,0,0,0,0,1]]})"), Species), Kinds, Plants, Error));

    // The shipped file.
    FString Text;
    const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), PlantsFile);
    if (!TestTrue(TEXT("Content/Marburgum/Vegetation/lent_plants.json exists"), FFileHelper::LoadFileToString(Text, *Path)))
    {
        return false;
    }
    TestTrue(TEXT("and reads"), Parse(Text, Kinds, Plants, Error));
    TestTrue(TEXT("thousands of plants on the square's grass"), Plants.Num() >= 1000);
    int32 Trees = 0;
    for (const FPlant& Plant : Plants)
    {
        Trees += Kinds[Plant.Species].bTree ? 1 : 0;
        if (FMath::Abs(Plant.Location.X) > 76800.0 || FMath::Abs(Plant.Location.Y) > 76800.0)
        {
            AddError(FString::Printf(TEXT("a plant outside the square at %s"), *Plant.Location.ToString()));
            break;
        }
    }
    TestTrue(TEXT("trees and bushes both"), Trees > 0 && Trees < Plants.Num());
    return true;
}

#endif
