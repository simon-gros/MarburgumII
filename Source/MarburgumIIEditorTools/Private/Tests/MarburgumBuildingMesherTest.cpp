#include "MarburgumBuildingMesher.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include <algorithm>

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
    // The rectangle has a ground floor, so its street door is cut (ADR-076); the rest keep painted doors.
    Plans[0].DoorEdge = 0;
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
        TestTrue(FString::Printf(TEXT("%hs is closed apart from its street door (%d open edges)"), F.Id.c_str(), Check.OpenEdges), Check.bClosed);
        if (Mesh.Door.bPresent)
        {
            TestFalse(FString::Printf(TEXT("%hs doorway is a real opening"), F.Id.c_str()), Check.bStrict);
        }
        else
        {
            TestTrue(FString::Printf(TEXT("%hs stays strictly closed"), F.Id.c_str()), Check.bStrict);
        }
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
        // Every window's glass, and only the glass, knows its pane, for the household behind it (ADR-066).
        TestEqual(FString::Printf(TEXT("%hs has a pane per window"), F.Id.c_str()), static_cast<int32>(Mesh.Panes.size()), Mesh.Windows);
        int32 Mislabelled = 0;
        std::vector<int32> PerPane(Mesh.Panes.size(), 0);
        for (const MB::FTriangle& T : Mesh.Triangles)
        {
            const bool bGlass = T.Material == MB::Glass;
            const bool bPane = T.Pane >= 0 && T.Pane < static_cast<int>(Mesh.Panes.size());
            Mislabelled += bGlass != bPane ? 1 : 0;
            if (bPane)
            {
                ++PerPane[T.Pane];
            }
        }
        TestEqual(FString::Printf(TEXT("%hs labels the glass and nothing else"), F.Id.c_str()), Mislabelled, 0);
        TestTrue(FString::Printf(TEXT("%hs each pane is a closed box"), F.Id.c_str()), std::all_of(PerPane.begin(), PerPane.end(), [](int32 N) { return N == 12; }));
    }
    MB::FBuildingMesh Rectangle;
    std::string Error;
    MB::BuildBuilding(Plans[0], Rectangle, Error);
    TestEqual(TEXT("The gabled rectangle has two gable ends"), Rectangle.Gables, 2);
    TestEqual(TEXT("The gabled rectangle keeps its gabled roof"), FString(Rectangle.RoofMethod.c_str()), FString(TEXT("gabled")));
    TestEqual(TEXT("The three-storey rectangle has a plinth, a string course and a cornice"), Rectangle.Bands, 3);
    // Its 18 m long walls have four bays a storey, the door's wall none on the ground floor.
    int32 Upstairs = 0;
    int32 MaxBay = 0;
    for (const MB::FPane& Pane : Rectangle.Panes)
    {
        Upstairs += Pane.Storey > 0 ? 1 : 0;
        MaxBay = FMath::Max(MaxBay, Pane.Bay);
    }
    TestTrue(TEXT("The rectangle has upstairs windows"), Upstairs > 0);
    TestEqual(TEXT("An 18 m wall has bays 0 to 4"), MaxBay, 4);
    TestTrue(TEXT("Its walls are counted"), Rectangle.Facades >= 2);
    TestTrue(TEXT("The rectangle's street door is cut"), Rectangle.Door.bPresent);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

#if WITH_DEV_AUTOMATION_TESTS

#include "MarburgumInteriorCore.h"
#include "MarburgumInteriorSubsystem.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumInteriorDoorCutsTest,
    "Marburgum.Editor.InteriorDoorCuts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// The street door the façade mesher cuts is the one the ground floor opens from, for every footprint in the city
// (ADR-076): the commandlet's shells and party walls equal the layout's, an eligible building's hole is its
// layout's entrance to the centimetre, and a building without a ground floor keeps a closed shell.
bool FMarburgumInteriorDoorCutsTest::RunTest(const FString& Parameters)
{
    namespace MB = MarburgumBuildings;
    namespace MI = MarburgumInteriors;
    std::vector<MI::FPlan> Plans;
    if (!UMarburgumInteriorSubsystem::ReadPlans(Plans, nullptr))
    {
        AddError(TEXT("buildings.json could not be prepared"));
        return false;
    }
    FString Text;
    TSharedPtr<FJsonObject> Root;
    const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/World/lent_ground/buildings.json"));
    if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
    {
        AddError(TEXT("buildings.json could not be read"));
        return false;
    }
    // The shells exactly as MarburgumRebuildFacadesCommandlet's ReadFootprints keeps them.
    std::vector<MB::FFootprint> Footprints;
    std::vector<size_t> PlanOf;
    size_t Index = 0;
    for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("buildings")))
    {
        const size_t This = Index++;
        const TSharedPtr<FJsonObject> Object = Value->AsObject();
        if (!Object.IsValid())
        {
            continue;
        }
        MB::FFootprint F;
        F.Id = TCHAR_TO_UTF8(*Object->GetStringField(TEXT("id")));
        double Number = 0.0;
        F.Ground = Object->TryGetNumberField(TEXT("ground"), Number) ? Number : 0.0;
        F.Eaves = Object->TryGetNumberField(TEXT("eaves"), Number) ? Number : F.Ground + 3.0;
        const double Ridge = Object->TryGetNumberField(TEXT("ridge"), Number) ? Number : F.Eaves;
        F.RoofRise = Ridge - F.Eaves;
        F.PitchDegrees = Object->TryGetNumberField(TEXT("pitch"), Number) ? Number : 40.0;
        const FString Roof = Object->GetStringField(TEXT("roof"));
        F.Roof = Roof == TEXT("flat") ? MB::ERoof::Flat : (Roof == TEXT("hipped") || Roof == TEXT("pyramidal") ? MB::ERoof::Hipped : MB::ERoof::Gabled);
        F.Base = F.Ground - 2.65;
        const double Area = Object->TryGetNumberField(TEXT("area"), Number) ? Number : 0.0;
        for (const TSharedPtr<FJsonValue>& Point : Object->GetArrayField(TEXT("outline")))
        {
            const TArray<TSharedPtr<FJsonValue>> Pair = Point->AsArray();
            if (Pair.Num() >= 2)
            {
                F.Outline.push_back({ Pair[0]->AsNumber(), Pair[1]->AsNumber() });
            }
        }
        if (Area < 30.0 || F.Outline.size() < 4 || !MB::CleanOutline(F.Outline))
        {
            TestFalse(FString::Printf(TEXT("%hs has no shell and no layout"), F.Id.c_str()), This < Plans.size() && Plans[This].bShell);
            continue;
        }
        Footprints.push_back(F);
        PlanOf.push_back(This);
    }
    MB::FindPartyWalls(Footprints);

    int32 Cut = 0;
    int32 Sealed = 0;
    int32 Differ = 0;
    int32 Misplaced = 0;
    int32 Leaky = 0;
    for (size_t F = 0; F < Footprints.size(); ++F)
    {
        MB::FFootprint& Footprint = Footprints[F];
        const MI::FPlan& Plan = Plans[PlanOf[F]];
        bool bSame = Plan.bShell && Plan.Id == Footprint.Id && Plan.Outline.size() == Footprint.Outline.size();
        for (size_t P = 0; bSame && P < Plan.Outline.size(); ++P)
        {
            bSame = FMath::IsNearlyEqual(Plan.Outline[P].X, Footprint.Outline[P].X, 1.0e-6) && FMath::IsNearlyEqual(Plan.Outline[P].Y, Footprint.Outline[P].Y, 1.0e-6)
                && (Plan.Party[P] != 0) == Footprint.Party[P];
        }
        if (!bSame)
        {
            ++Differ;
            AddError(FString::Printf(TEXT("%hs: the layout's outline or party walls differ from the shell's"), Footprint.Id.c_str()));
            continue;
        }
        const MI::FLayout Layout = MI::Generate(Plan);
        Footprint.DoorEdge = Layout.Status == MI::EStatus::Eligible ? Layout.EntranceEdge : -1;
        if (Layout.Status == MI::EStatus::Eligible)
        {
            Footprint.DoorSill = Layout.Openings[0].Z0;
        }
        MB::FBuildingMesh Mesh;
        std::string Error;
        if (!MB::BuildBuilding(Footprint, Mesh, Error))
        {
            continue;
        }
        const MB::FCheck Check = MB::Check(Mesh, Footprint);
        if (Layout.Status != MI::EStatus::Eligible)
        {
            ++Sealed;
            if (Mesh.Door.bPresent || !Check.bClosed)
            {
                ++Leaky;
                AddError(FString::Printf(TEXT("%hs has no ground floor but its shell is open"), Footprint.Id.c_str()));
            }
            continue;
        }
        ++Cut;
        const MI::FOpening& Street = Layout.Openings[0];
        // The hole's bottom corners, in plan metres, against the layout's street opening.
        const MB::FVec3 L = Mesh.Door.Corner[0];
        const MB::FVec3 R = Mesh.Door.Corner[1];
        const bool bAligned = Mesh.Door.bPresent && Check.bClosed && Check.Volume > 0.0
            && FMath::IsNearlyEqual(L.X, Street.A.X, 0.01) && FMath::IsNearlyEqual(L.Y, Street.A.Y, 0.01)
            && FMath::IsNearlyEqual(R.X, Street.B.X, 0.01) && FMath::IsNearlyEqual(R.Y, Street.B.Y, 0.01)
            && FMath::IsNearlyEqual(L.Z, Street.Z0, 0.01) && FMath::IsNearlyEqual(Mesh.Door.Corner[2].Z, Street.Z1, 0.01);
        if (!bAligned)
        {
            ++Misplaced;
            AddError(FString::Printf(TEXT("%hs: the cut door (%.2f,%.2f)-(%.2f,%.2f) is not the layout's (%.2f,%.2f)-(%.2f,%.2f)"), Footprint.Id.c_str(),
                L.X, L.Y, R.X, R.Y, Street.A.X, Street.A.Y, Street.B.X, Street.B.Y));
        }
    }
    TestEqual(TEXT("Every shell's outline and party walls are the layout's"), Differ, 0);
    TestEqual(TEXT("Every cut door is its layout's entrance"), Misplaced, 0);
    TestEqual(TEXT("No building without a ground floor is left open"), Leaky, 0);
    AddInfo(FString::Printf(TEXT("MB2INTERIOR_CUTS shells=%d cut=%d sealed=%d"), static_cast<int32>(Footprints.size()), Cut, Sealed));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
