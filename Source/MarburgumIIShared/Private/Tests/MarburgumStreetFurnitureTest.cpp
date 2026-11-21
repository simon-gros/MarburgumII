#include "MarburgumStreetFurniture.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// The street furniture (GFX-01): the file tools/lent-ground writes and the kit's meshes.

namespace
{
    constexpr EAutomationTestFlags FurnitureTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
        | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumStreetFurnitureFileTest, "Marburgum.Shared.StreetFurniture.File", FurnitureTestFlags)
bool FMarburgumStreetFurnitureFileTest::RunTest(const FString& Parameters)
{
    using namespace MarburgumII::StreetFurniture;

    TArray<FPiece> Pieces;
    FString Error;
    const FString Two = TEXT(R"({"format":"marburgumii.street_furniture","version":1,"furniture":[[10.5,-20.25,3.1,1,90.0,0],[0,0,0,4,0,3]]})");
    TestTrue(TEXT("a small file reads"), Parse(Two, Pieces, Error));
    if (TestEqual(TEXT("two pieces"), Pieces.Num(), 2))
    {
        TestEqual(TEXT("plan east is Unreal Y"), Pieces[0].Location.Y, 1050.0);
        TestEqual(TEXT("plan south is Unreal -X"), Pieces[0].Location.X, 2025.0);
        TestEqual(TEXT("the top is Z"), Pieces[0].Location.Z, 310.0);
        TestEqual(TEXT("a shelter"), Pieces[0].Piece, EPiece::Shelter);
        TestEqual(TEXT("at a stop"), Pieces[0].Source, ESource::Stop);
        TestEqual(TEXT("open to the south (plan 90) is Unreal 180"), Pieces[0].Yaw, 180.0f);
        TestEqual(TEXT("a wall panel"), Pieces[1].Piece, EPiece::WallPanel);
        TestEqual(TEXT("of a barrier run"), Pieces[1].Source, ESource::Barrier);
    }
    TestEqual(TEXT("the shelter's mesh"), MeshPath(EPiece::Shelter), FString(TEXT("/Game/World/StreetFurniture/SM_Street_Shelter_01.SM_Street_Shelter_01")));
    TestEqual(TEXT("a panel is drawn far"), CullDistanceCm(EPiece::WallPanel), LargeCullCm);
    TestEqual(TEXT("a bollard near"), CullDistanceCm(EPiece::Bollard), SmallCullCm);
    TestFalse(TEXT("a wrong format is refused"), Parse(TEXT(R"({"format":"marburgumii.street_lights","version":1,"furniture":[]})"), Pieces, Error));
    TestFalse(TEXT("a bad piece is refused"), Parse(TEXT(R"({"format":"marburgumii.street_furniture","version":1,"furniture":[[0,0,0,9,0,0]]})"), Pieces, Error));
    TestFalse(TEXT("a short row is refused"), Parse(TEXT(R"({"format":"marburgumii.street_furniture","version":1,"furniture":[[0,0,0,0,0]]})"), Pieces, Error));

    // The shipped file.
    FString Text;
    const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), FurnitureFile);
    if (!TestTrue(TEXT("Content/Marburgum/Furniture/lent_street_furniture.json exists"), FFileHelper::LoadFileToString(Text, *Path)))
    {
        return false;
    }
    TestTrue(TEXT("and reads"), Parse(Text, Pieces, Error));
    TestTrue(TEXT("hundreds of pieces"), Pieces.Num() >= 500);
    int32 ByPiece[static_cast<int32>(EPiece::Count)] = {};
    for (const FPiece& Piece : Pieces)
    {
        ++ByPiece[static_cast<int32>(Piece.Piece)];
        if (FMath::Abs(Piece.Location.X) > 76800.0 || FMath::Abs(Piece.Location.Y) > 76800.0)
        {
            AddError(FString::Printf(TEXT("a piece outside the square at %s"), *Piece.Location.ToString()));
            break;
        }
    }
    TestTrue(TEXT("stops, shelters and benches"), ByPiece[0] > 0 && ByPiece[1] > 0 && ByPiece[2] > 0);
    TestTrue(TEXT("fences and walls"), ByPiece[3] > 0 && ByPiece[4] > 0);
    return true;
}

#endif
