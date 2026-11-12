#include "MarburgumBuildingMesher.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumBuildingMesherTest,
    "Marburgum.Editor.BuildingMesher",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    namespace MB = MarburgumBuildings;

    MB::FFootprint Plan(const char* Id, std::vector<MB::FVec2> Outline, MB::ERoof Roof)
    {
        MB::FFootprint F;
        F.Id = Id;
        F.Outline = std::move(Outline);
        F.Ground = 20.0;
        F.Base = 19.8;
        F.Eaves = 29.0;
        F.RoofRise = 4.5;
        F.Roof = Roof;
        return F;
    }
}

bool FMarburgumBuildingMesherTest::RunTest(const FString& Parameters)
{
    std::vector<MB::FFootprint> Plans;
    // A gabled rectangle, a hipped L, a gabled U, a flat block with a parapet, a skewed and clockwise
    // outline, and a row of two houses sharing a wall.
    Plans.push_back(Plan("rectangle", { { 0, 0 }, { 18, 0 }, { 18, 9 }, { 0, 9 } }, MB::ERoof::Gabled));
    Plans.push_back(Plan("ell", { { 0, 0 }, { 20, 0 }, { 20, 8 }, { 9, 8 }, { 9, 22 }, { 0, 22 } }, MB::ERoof::Hipped));
    Plans.push_back(Plan("you", { { 0, 0 }, { 30, 0 }, { 30, 20 }, { 21, 20 }, { 21, 8 }, { 9, 8 }, { 9, 20 }, { 0, 20 } }, MB::ERoof::Gabled));
    Plans.push_back(Plan("flat", { { 0, 0 }, { 25, 0 }, { 25, 12 }, { 12, 12 }, { 12, 18 }, { 0, 18 } }, MB::ERoof::Flat));
    Plans.push_back(Plan("skewed", { { 0, 0 }, { -3, 11 }, { 14, 13.5 }, { 16, 1 } }, MB::ERoof::Gabled));
    Plans.push_back(Plan("row_west", { { 100, 0 }, { 110, 0 }, { 110, 12 }, { 100, 12 } }, MB::ERoof::Gabled));
    Plans.push_back(Plan("row_east", { { 110, 0 }, { 121, 0 }, { 121, 12 }, { 110, 12 } }, MB::ERoof::Gabled));
    for (MB::FFootprint& F : Plans)
    {
        TestTrue(FString::Printf(TEXT("%hs outline kept"), F.Id.c_str()), MB::CleanOutline(F.Outline));
    }
    MB::FindPartyWalls(Plans);
    TestTrue(TEXT("Row houses share their middle wall"), Plans[5].Party[1] && Plans[6].Party[3]);
    TestFalse(TEXT("A free-standing wall is not a party wall"), Plans[0].Party[0]);

    for (const MB::FFootprint& F : Plans)
    {
        MB::FBuildingMesh Mesh;
        std::string Error;
        const bool bBuilt = MB::BuildBuilding(F, Mesh, Error);
        TestTrue(FString::Printf(TEXT("%hs builds (%hs)"), F.Id.c_str(), Error.c_str()), bBuilt);
        if (!bBuilt)
        {
            continue;
        }
        const MB::FCheck Check = MB::Check(Mesh, F);
        TestTrue(FString::Printf(TEXT("%hs is closed with no T-junctions (%d open edges)"), F.Id.c_str(), Check.StrictOpenEdges), Check.bStrict);
        TestTrue(FString::Printf(TEXT("%hs faces outward"), F.Id.c_str()), Check.Volume > 0.0);
        // Seen from above, the roof covers the whole footprint and the eave overhang beyond it.
        TestTrue(FString::Printf(TEXT("%hs roof covers the footprint"), F.Id.c_str()), Check.RoofCover >= Check.FootprintArea * 0.999);
        double Low = 1.0e9;
        double High = -1.0e9;
        for (const MB::FVec3& P : Mesh.Positions)
        {
            Low = FMath::Min(Low, P.Z);
            High = FMath::Max(High, P.Z);
        }
        TestTrue(FString::Printf(TEXT("%hs starts at its base"), F.Id.c_str()), FMath::IsNearlyEqual(Low, F.Base, 1.0e-6));
        TestTrue(FString::Printf(TEXT("%hs stays within its measured rise"), F.Id.c_str()), High <= F.Eaves + F.RoofRise + 1.0e-4);
    }
    MB::FBuildingMesh Rectangle;
    std::string Error;
    MB::BuildBuilding(Plans[0], Rectangle, Error);
    TestEqual(TEXT("The gabled rectangle has two gable ends"), Rectangle.Gables, 2);
    TestEqual(TEXT("The gabled rectangle keeps its gabled roof"), FString(Rectangle.RoofMethod.c_str()), FString(TEXT("gabled")));
    TestEqual(TEXT("The three-storey rectangle has a plinth, a string course and a cornice"), Rectangle.Bands, 3);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
