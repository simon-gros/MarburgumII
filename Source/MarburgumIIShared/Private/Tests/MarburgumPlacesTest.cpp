#include "MarburgumPlaces.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Ports of the original's MarburgumPlacesTest.cpp and the resolver half of MarburgumLocationBannerTest.cpp (#263) onto
// the places file (ADR-063). Positions are the original's Godot x and z, which are this map's plan metres, so its
// walks carry over unchanged; the expected names and distances are the original's and still hold with this map's
// street set, squares included (tools/lent-ground places).

namespace
{
    using namespace MarburgumII::Places;

    constexpr EAutomationTestFlags PlacesTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
        | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter;

    FVector2D At(double East, double South)
    {
        return FromPlan(East, South);
    }

    /** A district's display name at a point, as a fresh look gives it; empty outside every district. */
    FString DistrictAt(const FCatalogue& Catalogue, double East, double South)
    {
        const FDistrict* District = Catalogue.FindDistrict(At(East, South));
        return District ? District->DisplayName : FString();
    }

    FString DistrictName(const FCatalogue& Catalogue, const FString& Id)
    {
        const FDistrict* District = Catalogue.FindDistrictById(Id);
        return District ? District->DisplayName : FString();
    }

    FString LandmarkName(const FCatalogue& Catalogue, const FString& Eid)
    {
        const FLandmark* Landmark = Catalogue.FindLandmark(Eid);
        return Landmark ? Landmark->DisplayName : FString();
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumPlacesParseTest, "Marburgum.Shared.Places.Parse", PlacesTestFlags)

bool FMarburgumPlacesParseTest::RunTest(const FString& Parameters)
{
    FCatalogue Catalogue;
    FString Error;
    TestFalse(TEXT("another format is refused"), Catalogue.Parse(TEXT("{\"format\":\"marburgumii.walk_graph\",\"version\":1}"), Error));
    TestFalse(TEXT("and reads nothing"), Catalogue.IsLoaded());

    // A 10 m square district, a street along its north edge, a 4 m square, and one of each kind malformed.
    const FString Json = TEXT(R"({"format":"marburgumii.places","version":1,
        "districts":[
            {"id":"quarter:1","kind":"quarter","name":"Q","display_name":"Q","area_m2":100,"rings":[[[0,0],[10,0],[10,10],[0,10]]]},
            {"id":"quarter:2","kind":"quarter","display_name":"Bad","area_m2":true,"rings":[[[0,0],[1,0],[1,1]]]}],
        "streets":[
            {"id":"w1","name":"Ulica","highway":"residential","car_road":true,"line":[[0,0],[10,0]]},
            {"id":"w2","name":"Trg","highway":"pedestrian","rings":[[[20,0],[24,0],[24,4],[20,4]]]},
            {"id":"w3","name":"Kratka","line":[[0,0]]}],
        "landmarks":[
            {"eid":"1-1","display_name":"Stolp","tier":"bespoke","area_m2":4,"rings":[[[2,2],[4,2],[4,4],[2,4]]]},
            {"eid":"","display_name":"Brez","tier":"bespoke","area_m2":4,"rings":[[[2,2],[4,2],[4,4]]]}]})");
    AddExpectedMessage(TEXT("is malformed and is left out"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 3);
    TestTrue(TEXT("a places document parses"), Catalogue.Parse(Json, Error));
    TestEqual(TEXT("one district"), Catalogue.GetDistricts().Num(), 1);
    TestEqual(TEXT("two streets"), Catalogue.GetStreets().Num(), 2);
    TestEqual(TEXT("one landmark"), Catalogue.GetLandmarks().Num(), 1);
    TestEqual(TEXT("three malformed records are left out"), Catalogue.GetRejected(), 3);

    TestEqual(TEXT("inside the district"), DistrictAt(Catalogue, 5.0, 5.0), FString(TEXT("Q")));
    TestEqual(TEXT("outside it"), DistrictAt(Catalogue, 15.0, 5.0), FString());
    TestEqual(TEXT("its edge is 5 m from its middle"), FCatalogue::DistanceToBoundary(Catalogue.GetDistricts()[0], At(5.0, 5.0)), 500.0, 1e-6);

    const FStreetHit OnLine = Catalogue.FindStreet(At(5.0, 3.0), 2000.0);
    TestTrue(TEXT("3 m off the street is on it"), OnLine.Street && OnLine.Street->Name == TEXT("Ulica"));
    TestEqual(TEXT("measured to the centreline"), OnLine.Distance, 300.0, 1e-6);
    TestTrue(TEXT("and it is a car road"), OnLine.Street && OnLine.Street->bCarRoad);
    const FStreetHit OnSquare = Catalogue.FindStreet(At(22.0, 2.0), 2000.0);
    TestTrue(TEXT("the middle of a square is on it"), OnSquare.Street && OnSquare.Street->Name == TEXT("Trg"));
    TestEqual(TEXT("at no distance"), OnSquare.Distance, 0.0);
    TestNull(TEXT("nothing out of reach"), Catalogue.FindStreet(At(5.0, 50.0), 2000.0).Street);
    TestEqual(TEXT("distance by name"), Catalogue.DistanceToStreetNamed(At(26.0, 2.0), TEXT("Trg"), 2000.0), 200.0, 1e-6);
    TestTrue(TEXT("none by a name not in reach"), Catalogue.DistanceToStreetNamed(At(5.0, 50.0), TEXT("Ulica"), 2000.0) < 0.0);

    const TArray<FLandmarkHit> Inside = Catalogue.GetNearbyLandmarks(At(3.0, 3.0), 800.0, BannerLandmarkTier);
    TestEqual(TEXT("standing in the tower"), Inside.Num(), 1);
    TestEqual(TEXT("is distance zero"), Inside.Num() ? Inside[0].Distance : -1.0, 0.0);
    TestEqual(TEXT("a metre deep"), Inside.Num() ? Inside[0].Depth : -1.0, 100.0, 1e-6);
    TestEqual(TEXT("another tier is not named"), Catalogue.GetNearbyLandmarks(At(3.0, 3.0), 800.0, TEXT("facade")).Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumPlacesLentTest, "Marburgum.Shared.Places.Lent", PlacesTestFlags)

bool FMarburgumPlacesLentTest::RunTest(const FString& Parameters)
{
    const FCatalogue& Data = Get();
    if (!TestTrue(TEXT("Content/Marburgum/Places/lent_places.json loads"), Data.IsLoaded()))
    {
        return false;
    }
    TestEqual(TEXT("the register's 27 districts"), Data.GetDistricts().Num(), 27);
    TestEqual(TEXT("the 58 bespoke landmarks"), Data.GetLandmarks().Num(), 58);
    TestTrue(TEXT("several hundred named ways and squares"), Data.GetStreets().Num() > 400);
    TestEqual(TEXT("nothing left out"), Data.GetRejected(), 0);

    TestEqual(TEXT("on Stari most the quarter is Center"), DistrictAt(Data, 0.0, 0.0), FString(TEXT("Center")));
    TestEqual(TEXT("over the bridge it is Magdalena"), DistrictAt(Data, 0.0, 150.0), FString(TEXT("Magdalena")));
    TestEqual(TEXT("out of town the village"), DistrictAt(Data, -4769.7, -2490.6), FString(TEXT("Bresternica")));
    TestEqual(TEXT("outside every district, none"), DistrictAt(Data, 40000.0, 40000.0), FString());

    // A walk down the x = 0 line from Lent over Stari most to Tabor and back. Magdalena meets Tabor 906.44 m south of
    // the bridge (the original's tools/build_districts.py --at).
    {
        FDistrictResolver Walk;
        const auto Step = [&](double South, bool bInArea = false) { return DistrictName(Data, Walk.Resolve(Data, At(0.0, South), bInArea)); };
        TestEqual(TEXT("walk: an area holds no district"), Step(-78.0, true), FString());
        TestTrue(TEXT("walk: and none is held"), Walk.GetHeldId().IsEmpty());
        TestEqual(TEXT("walk: Magdalena over the bridge"), Step(150.0), FString(TEXT("Magdalena")));
        TestEqual(TEXT("walk: still Magdalena 5 m into Tabor"), Step(911.4), FString(TEXT("Magdalena")));
        TestEqual(TEXT("walk: still Magdalena 9 m into Tabor"), Step(915.4), FString(TEXT("Magdalena")));
        TestEqual(TEXT("walk: Tabor 15 m in"), Step(921.4), FString(TEXT("Tabor")));
        TestEqual(TEXT("walk: back 5 m into Magdalena it is still Tabor"), Step(901.4), FString(TEXT("Tabor")));
        TestEqual(TEXT("walk: and Magdalena 15 m in"), Step(891.4), FString(TEXT("Magdalena")));
    }

    // Into a village from the quarter around it: Počehova lies inside Ivan Cankar, its border on the z = -2200 line at
    // x = 344.37. In at once; out only past the hold.
    {
        FDistrictResolver Walk;
        const auto Step = [&](double East) { return DistrictName(Data, Walk.Resolve(Data, At(East, -2200.0))); };
        TestEqual(TEXT("village: Ivan Cankar"), Step(380.0), FString(TEXT("Ivan Cankar")));
        TestEqual(TEXT("village: Počehova 2 m in"), Step(342.4), FString(TEXT("Počehova")));
        TestEqual(TEXT("village: still Počehova 5 m out"), Step(349.4), FString(TEXT("Počehova")));
        TestEqual(TEXT("village: Ivan Cankar 15 m out"), Step(359.4), FString(TEXT("Ivan Cankar")));
    }

    // Where the village runs up to its quarter's own edge (Za Kalvarijo in Center, 5.2 m from Koroška vrata): being near
    // the held quarter's edge must not hold its name while the player is still inside it.
    {
        FDistrictResolver Walk;
        TestEqual(TEXT("edge village: Center"), DistrictName(Data, Walk.Resolve(Data, At(-115.47, -1308.14))), FString(TEXT("Center")));
        TestEqual(TEXT("edge village: Za Kalvarijo 3 m in, at once"), DistrictName(Data, Walk.Resolve(Data, At(-120.83, -1310.82))),
            FString(TEXT("Za Kalvarijo")));
    }

    // Off Stari most onto Glavni trg along x = 1.9. The bridge and the square meet at z = -128.5; the square is 2.17 m
    // nearer at -131, 3.88 m at -133 and 4.73 m at -134, so the bridge holds to -133 and Glavni trg takes over at -134.
    {
        FStreetResolver Walk;
        const auto Step = [&](double South) { return Walk.Resolve(Data, At(1.9, South)); };
        TestEqual(TEXT("junction: on the bridge"), Step(-100.0), FString(TEXT("Stari most")));
        TestEqual(TEXT("junction: at the crossing"), Step(-128.5), FString(TEXT("Stari most")));
        TestEqual(TEXT("junction: 2.5 m past it"), Step(-131.0), FString(TEXT("Stari most")));
        TestEqual(TEXT("junction: 4.5 m past it"), Step(-133.0), FString(TEXT("Stari most")));
        TestEqual(TEXT("junction: Glavni trg 5.5 m past it"), Step(-134.0), FString(TEXT("Glavni trg")));
        TestEqual(TEXT("junction: back to the crossing it is still Glavni trg"), Step(-130.0), FString(TEXT("Glavni trg")));
        TestEqual(TEXT("junction: and the bridge again on it"), Step(-100.0), FString(TEXT("Stari most")));
    }

    // Straight off Gorkega ulica where no other named street comes within 35 m: in reach to 20 m, held to 24 m.
    {
        const auto Off = [](double Metres) { return At(-233.33 - 0.886373 * Metres, 220.045 - 0.462971 * Metres); };
        FStreetResolver Walk;
        TestEqual(TEXT("leaving: 19 m off"), Walk.Resolve(Data, Off(19.0)), FString(TEXT("Gorkega ulica")));
        TestEqual(TEXT("leaving: held 22 m off"), Walk.Resolve(Data, Off(22.0)), FString(TEXT("Gorkega ulica")));
        TestEqual(TEXT("leaving: gone 25 m off"), Walk.Resolve(Data, Off(25.0)), FString());
        FStreetResolver Fresh;
        TestEqual(TEXT("leaving: 22 m off is out of reach for a player who was not on it"), Fresh.Resolve(Data, Off(22.0)), FString());
    }

    // From the Jewish tower to the synagogue, 22 m apart across Židovska ulica, and back. Reach 8 m, margin 3 m.
    {
        const auto Line = [](double East) { return At(East, -99.0 + (East - 180.6) * (3.7 / -43.7)); };
        FLandmarkResolver Walk;
        const auto Step = [&](double East) { return LandmarkName(Data, Walk.Resolve(Data, Line(East))); };
        TestEqual(TEXT("landmark: the tower 0.65 m off"), Step(176.0), FString(TEXT("Židovski stolp")));
        TestEqual(TEXT("landmark: the tower 6.65 m off"), Step(170.0), FString(TEXT("Židovski stolp")));
        TestEqual(TEXT("landmark: held 10.15 m off, the synagogue still 11.88 m"), Step(166.5), FString(TEXT("Židovski stolp")));
        TestEqual(TEXT("landmark: gone 12.65 m off, the synagogue 9.39 m is out of reach"), Step(164.0), FString());
        TestEqual(TEXT("landmark: the synagogue 6.40 m off"), Step(161.0), FString(TEXT("Sinagoga")));
        TestEqual(TEXT("landmark: the synagogue held at 9.39 m"), Step(164.0), FString(TEXT("Sinagoga")));
        TestEqual(TEXT("landmark: neither at 11.88 / 10.15 m"), Step(166.5), FString());
        TestEqual(TEXT("landmark: the tower again at 7.65 m"), Step(169.0), FString(TEXT("Židovski stolp")));
        TestEqual(TEXT("landmark: held by EID"), Walk.GetHeldEid(), FString(TEXT("1-06255")));
        FLandmarkResolver Fresh;
        TestEqual(TEXT("landmark: 9.15 m off is out of reach for a player who was not at it"), Fresh.Resolve(Data, Line(167.5)), FString());
    }

    // Along Koroška cesta from number 24 to number 26, 1.9 m apart.
    {
        const auto Koroska = [](double East) { return At(East, -263.6 + (East + 288.48) * (5.28 / 21.76)); };
        FLandmarkResolver Walk;
        const auto Step = [&](double East) { return LandmarkName(Data, Walk.Resolve(Data, Koroska(East))); };
        TestEqual(TEXT("front: inside 24"), Step(-296.2), FString(TEXT("Hiša Koroška 24")));
        TestEqual(TEXT("front: 24 at 0.91 m, 26 at 1.85 m"), Step(-299.6), FString(TEXT("Hiša Koroška 24")));
        TestEqual(TEXT("front: 24 held at 1.93 m, 26 only 1.1 m nearer"), Step(-300.6), FString(TEXT("Hiša Koroška 24")));
        TestEqual(TEXT("front: 0.19 m inside 26, 24 held at 2.95 m"), Step(-301.6), FString(TEXT("Hiša Koroška 24")));
        TestEqual(TEXT("front: 26 once 24 is 4.38 m off"), Step(-303.0), FString(TEXT("Hiša Koroška 26")));
        TestEqual(TEXT("front: back out, 26 held at 2.36 m"), Step(-299.1), FString(TEXT("Hiša Koroška 26")));
        TestEqual(TEXT("front: 24 again, 26 3.38 m off"), Step(-298.1), FString(TEXT("Hiša Koroška 24")));
    }

    // The Jesuit college holds the church of St Aloysius within its outline. Inside both, the smaller is the answer;
    // walking into the church names it a metre in, and back out a metre out.
    {
        const auto Nave = [](double East) { return At(East, -151.17); };
        FLandmarkResolver Walk;
        const auto Step = [&](double East) { return LandmarkName(Data, Walk.Resolve(Data, Nave(East))); };
        TestEqual(TEXT("college: in the college, the church 5.6 m off"), Step(-106.96), FString(TEXT("Jezuitski kolegij")));
        TestEqual(TEXT("college: the church 1.70 m off"), Step(-110.96), FString(TEXT("Jezuitski kolegij")));
        TestEqual(TEXT("college: 0.24 m into the church is not yet in it"), Step(-112.96), FString(TEXT("Jezuitski kolegij")));
        TestEqual(TEXT("college: 1.21 m in, the church"), Step(-113.96), FString(TEXT("Cerkev sv. Alojzija")));
        TestEqual(TEXT("college: back to 0.24 m in"), Step(-112.96), FString(TEXT("Cerkev sv. Alojzija")));
        TestEqual(TEXT("college: 0.73 m out, still the church"), Step(-111.96), FString(TEXT("Cerkev sv. Alojzija")));
        TestEqual(TEXT("college: 1.70 m out, the college"), Step(-110.96), FString(TEXT("Jezuitski kolegij")));
        TestEqual(TEXT("college: a fresh look inside both names the smaller"),
            LandmarkName(Data, FLandmarkResolver().Resolve(Data, Nave(-112.96))), FString(TEXT("Cerkev sv. Alojzija")));
        TestEqual(TEXT("college: the other tiers are not in the file"), FLandmarkResolver().Resolve(Data, At(115.9, -146.1)), FString());
    }

    // Lent's own squares and pedestrian streets, which the original's car-road topology left unnamed.
    {
        TestEqual(TEXT("the middle of Glavni trg is on it"), FStreetResolver().Resolve(Data, At(-20.0, -175.0)), FString(TEXT("Glavni trg")));
        const FStreetHit Square = Data.FindStreet(At(-20.0, -175.0), StreetReachCentimetres);
        TestTrue(TEXT("Glavni trg is a square"), Square.Street && Square.Street->bSquare);
    }
    return true;
}

#endif
