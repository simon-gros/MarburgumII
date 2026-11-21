#include "MarburgumGroundFile.h"
#include "MarburgumStreetFurniture.h"
#include "MarburgumStreetLights.h"
#include "MarburgumVegetation.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// The ground under the lamps, plants and street furniture, worked out on the whole level (ADR-091) for clients that hold
// only the cells round their player.

namespace
{
    constexpr EAutomationTestFlags GroundFileTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
        | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumGroundFileTest, "Marburgum.Shared.GroundFile", GroundFileTestFlags)
bool FMarburgumGroundFileTest::RunTest(const FString& Parameters)
{
    using namespace MarburgumII;

    TestEqual(TEXT("the ground file sits beside its placement file"), GroundFile::PathFor(TEXT("Marburgum/Lights/lent_street_lights.json")),
        FString(TEXT("Marburgum/Lights/lent_street_lights_ground.json")));

    const FString Placements = TEXT(R"({"format":"marburgumii.street_lights","version":1,"lamps":[[0,0,3,0,0,1],[1,0,3,1,0,2]]})");
    const FString For = GroundFile::Fingerprint(Placements);
    TestEqual(TEXT("the fingerprint is SHA-1 in hex"), For.Len(), 40);
    TestNotEqual(TEXT("and changes with the placement file"), GroundFile::Fingerprint(Placements + TEXT(" ")), For);

    TArray<TOptional<double>> Ground;
    FString Error;
    const FString Written = GroundFile::Write(For, { 312.34, TOptional<double>() });
    TestTrue(TEXT("the written file reads back"), GroundFile::Parse(Written, For, 2, Ground, Error));
    if (TestEqual(TEXT("a height for each item"), Ground.Num(), 2))
    {
        TestTrue(TEXT("the first stands"), Ground[0].IsSet() && FMath::IsNearlyEqual(*Ground[0], 312.3));
        TestFalse(TEXT("the second has no ground"), Ground[1].IsSet());
    }
    TestFalse(TEXT("another placement file's ground is refused"), GroundFile::Parse(Written, GroundFile::Fingerprint(TEXT("{}")), 2, Ground, Error));
    TestTrue(TEXT("and says so"), Error.StartsWith(TEXT("made for placement file")));
    TestFalse(TEXT("a count that does not match is refused"), GroundFile::Parse(Written, For, 3, Ground, Error));
    TestTrue(TEXT("and leaves nothing behind"), Ground.IsEmpty());
    TestFalse(TEXT("another format is refused"), GroundFile::Parse(Placements, For, 2, Ground, Error));

    // Each shipped ground file belongs to the shipped placement file beside it (scripts/partition-world.sh ground).
    const auto Matches = [this](const TCHAR* PlacementFile, int32 Count, const FString& Text)
    {
        FString Shipped;
        if (!TestTrue(FString::Printf(TEXT("%s has a ground file"), PlacementFile),
                FFileHelper::LoadFileToString(Shipped, *FPaths::Combine(FPaths::ProjectContentDir(), GroundFile::PathFor(PlacementFile)))))
        {
            return;
        }
        TArray<TOptional<double>> Heights;
        FString Why;
        const bool bMatches = GroundFile::Parse(Shipped, GroundFile::Fingerprint(Text), Count, Heights, Why);
        TestTrue(FString::Printf(TEXT("%s's ground file is its own (%s)"), PlacementFile, bMatches ? TEXT("yes") : *Why), bMatches);
    };
    const auto Read = [this](const TCHAR* PlacementFile, FString& OutText)
    {
        return TestTrue(FString::Printf(TEXT("%s reads"), PlacementFile),
            FFileHelper::LoadFileToString(OutText, *FPaths::Combine(FPaths::ProjectContentDir(), PlacementFile)));
    };
    FString Text;
    TArray<StreetLights::FLamp> Lamps;
    if (Read(StreetLights::LampsFile, Text) && TestTrue(TEXT("the lamps parse"), StreetLights::Parse(Text, Lamps, Error)))
    {
        Matches(StreetLights::LampsFile, Lamps.Num(), Text);
    }
    TArray<Vegetation::FSpecies> Species;
    TArray<Vegetation::FPlant> Plants;
    if (Read(Vegetation::PlantsFile, Text) && TestTrue(TEXT("the plants parse"), Vegetation::Parse(Text, Species, Plants, Error)))
    {
        Matches(Vegetation::PlantsFile, Plants.Num(), Text);
    }
    TArray<StreetFurniture::FPiece> Pieces;
    if (Read(StreetFurniture::FurnitureFile, Text) && TestTrue(TEXT("the street furniture parses"), StreetFurniture::Parse(Text, Pieces, Error)))
    {
        Matches(StreetFurniture::FurnitureFile, Pieces.Num(), Text);
    }
    return true;
}

#endif
