#pragma once

#include "CoreMinimal.h"

/**
 * Place names (ADR-063): what the location banner calls the quarter or village a player is in, the street or
 * square they are on and the landmark they are at. A port of the original's Marburgum::Places and the resolvers
 * of its MarburgumLocationBanner.h (issue #263), reading one file instead of the original's per-window topology
 * and register files: Content/Marburgum/Places/lent_places.json, written by `lent-ground places`.
 *
 * - **Districts** are the Register prostorskih enot's 11 quarters, 4 local communities and 12 settlements, and the
 *   answer is the smallest one a point is inside: Magdalena in the city, the village out of it.
 * - **Streets** are the named ways and squares of the map's OpenStreetMap extract, and the answer is the one nearest
 *   a point, measured to the centreline (a square: zero anywhere on it). Unlike the original, pedestrian ways and
 *   squares are included, because most of Lent and the old town is pedestrian.
 * - **Landmarks** are the heritage register's bespoke tier (churches, the Rotovž, the towers, the castle), each with
 *   the register's outline, and the answer is the ones a point is at or near.
 *
 * The file is in plan metres (x east, y south of the middle of Stari most); everything here is in Unreal
 * centimetres in the ground plane (X north, Y east), converted once on load. Only the ground plane counts: a street
 * under a bridge is still the street a player stands over. Engine-neutral; the server resolves each player's place
 * with these (UMarburgumWorldAuthority) and the client looks the names up for the banner.
 */
namespace MarburgumII::Places
{
    /** Relative to the project's content directory, staged with the build (DefaultGame.ini). */
    inline constexpr const TCHAR* PlacesFile = TEXT("Marburgum/Places/lent_places.json");

    /**
     * The map whose frame the file is in (X north, Y east of the middle of Stari most). The blockout and test maps lay
     * the original's metres out another way (X east), so places are resolved on this map only.
     */
    inline constexpr const TCHAR* PlacesMap = TEXT("/Game/World/lent");

    struct FDistrict
    {
        /** `quarter:<EID>`, `local_community:<EID>` or `settlement:<EID>`, stable across builds. */
        FString Id;
        /** quarter, local_community or settlement. */
        FString Kind;
        /** As the register spells it. */
        FString Name;
        /** What a player is told. */
        FString DisplayName;
        /** Of the ground it covers; the smaller of two districts is the answer. */
        double AreaSquareMetres = 0.0;
        /** Every ring, outer and inner; containment is even-odd over all of them, as the generator tests it. */
        TArray<TArray<FVector2D>> Rings;
        FBox2D Bounds = FBox2D(ForceInit);
    };

    struct FStreet
    {
        /** The OpenStreetMap way (`w…`) or relation (`r…`). */
        FString Id;
        FString Name;
        /** residential, pedestrian, footway and the rest, as OpenStreetMap classes them. */
        FString Highway;
        bool bCarRoad = false;
        /** A square or a pedestrian area: Rings rather than Line, and standing on it is distance zero. */
        bool bSquare = false;
        TArray<FVector2D> Line;
        TArray<TArray<FVector2D>> Rings;
        FBox2D Bounds = FBox2D(ForceInit);
    };

    struct FLandmark
    {
        /** The heritage register's EID ("1-00445"). */
        FString Eid;
        /** As the register spells it: "Maribor - Vodni stolp". */
        FString Name;
        /** What a player is told: "Vodni stolp". */
        FString DisplayName;
        /** bespoke, facade or generated (the original's tools/landmarks.py tiers). */
        FString Tier;
        /** The register's dating, free text; empty when it has none. */
        FString Dating;
        double AreaSquareMetres = 0.0;
        TArray<TArray<FVector2D>> Rings;
        FBox2D Bounds = FBox2D(ForceInit);
    };

    struct FStreetHit
    {
        /** Null when no named street is in reach. Valid as long as the catalogue. */
        const FStreet* Street = nullptr;
        /** Horizontal, to the centreline (or the square's edge, zero on it), in centimetres. */
        double Distance = 0.0;
    };

    struct FLandmarkHit
    {
        /** Never null in a returned hit. */
        const FLandmark* Landmark = nullptr;
        /** Horizontal, to the outline, in centimetres; 0 inside it. */
        double Distance = 0.0;
        /** Inside the outline, how far to its nearest edge, in centimetres; 0 outside it. */
        double Depth = 0.0;
    };

    /** The place names of one map, parsed from a places file. Immutable once parsed. */
    class MARBURGUMIISHARED_API FCatalogue
    {
    public:
        /**
         * Reads a `marburgumii.places` version 1 document. A malformed record is left out with a complaint rather
         * than becoming a place nobody can find (the original's rule); a document of the wrong format reads nothing.
         */
        bool Parse(const FString& Json, FString& OutError);
        bool IsLoaded() const { return bLoaded; }

        const TArray<FDistrict>& GetDistricts() const { return Districts; }
        const TArray<FStreet>& GetStreets() const { return Streets; }
        const TArray<FLandmark>& GetLandmarks() const { return Landmarks; }
        /** Records the parser left out as malformed. */
        int32 GetRejected() const { return Rejected; }

        /** The smallest district holding the point, or nullptr outside all of them. A tie keeps file order. */
        const FDistrict* FindDistrict(const FVector2D& Point) const;
        const FDistrict* FindDistrictById(const FString& Id) const;
        const FLandmark* FindLandmark(const FString& Eid) const;

        /** The nearest named street within reach, or a hit with no street. An exact tie keeps file order. */
        FStreetHit FindStreet(const FVector2D& Point, double WithinCentimetres) const;

        /**
         * How far the point is from the nearest way carrying this name, or a negative number when none is within
         * reach. By name and not by way: OpenStreetMap cuts one street into many ways, and a player is told the name.
         */
        double DistanceToStreetNamed(const FVector2D& Point, const FString& Name, double WithinCentimetres) const;

        /**
         * Every landmark of the given tier whose outline comes within reach, nearest first; inside two at once the
         * smaller first, the EID breaking an exact tie. An empty tier takes every tier.
         */
        TArray<FLandmarkHit> GetNearbyLandmarks(const FVector2D& Point, double WithinCentimetres, const FString& Tier) const;

        static bool Contains(const FDistrict& District, const FVector2D& Point);
        /** Horizontal distance from a point to the nearest edge of a district, inside or out. */
        static double DistanceToBoundary(const FDistrict& District, const FVector2D& Point);

    private:
        struct FSegment
        {
            int32 Street = INDEX_NONE;
            FVector2D A;
            FVector2D B;
        };

        void Index();
        void ForEachSegmentNear(const FVector2D& Point, double Within, TFunctionRef<void(const FSegment&)> Visit) const;
        double DistanceToStreet(int32 StreetIndex, const FVector2D& Point, double Segment) const;

        bool bLoaded = false;
        int32 Rejected = 0;
        TArray<FDistrict> Districts;
        TArray<FStreet> Streets;
        TArray<FLandmark> Landmarks;
        /** Every centreline segment and square edge, bucketed by a coarse grid so a lookup reads only its neighbours. */
        TArray<FSegment> Segments;
        TMap<FIntPoint, TArray<int32>> Cells;
        TArray<int32> Squares;
    };

    /**
     * The catalogue in the project's content (PlacesFile), read once on first use and kept. Empty, with an error
     * logged once, when the file is missing or unreadable, so nothing is named rather than anything failing.
     */
    MARBURGUMIISHARED_API const FCatalogue& Get();

    /** Plan metres (x east, y south) to the ground plane in Unreal centimetres (X north, Y east). */
    inline FVector2D FromPlan(double EastMetres, double SouthMetres)
    {
        return FVector2D(-SouthMetres * 100.0, EastMetres * 100.0);
    }

    // ---- Resolvers: a moving player's place, held at borders, junctions and shared walls (the original's
    // MarburgumLocationBanner.h). Each keeps the last answer, so one per player.

    /**
     * How far past a district's edge its name still holds. Quarter boundaries run down the middle of streets, so
     * without it a player walking along one would see the name swap at every step.
     */
    inline constexpr double BorderHoldCentimetres = 1000.0;

    /** How near a street's centreline the player must be to be on it: the far pavement of a four-lane road. */
    inline constexpr double StreetReachCentimetres = 2000.0;

    /**
     * How much nearer another street must be before it takes over, and how far past the reach the one shown still
     * holds. At a junction both centrelines meet, so without it the name would swap with every step.
     */
    inline constexpr double StreetSwitchMarginCentimetres = 400.0;

    /** How near a landmark's outline the player must be to be at it: across the pavement from its wall. */
    inline constexpr double LandmarkReachCentimetres = 800.0;

    /** As the street margin, for neighbouring buildings that share a wall. */
    inline constexpr double LandmarkSwitchMarginCentimetres = 300.0;

    /** How far into another landmark, and out of the one shown, the player must be before walking in names it. */
    inline constexpr double LandmarkWalkInCentimetres = 100.0;

    /** The tier the banner names: the 390 townhouses of the other tiers would be noise. */
    inline constexpr const TCHAR* BannerLandmarkTier = TEXT("bespoke");

    /**
     * The district a moving player is in. One just walked out of keeps its name until they are BorderHold past its
     * edge; walking into a district, including a village inside the quarter, changes it at once. Inside a named
     * area (Lent) no district is held, so leaving the area hands the name straight back to the district around it.
     */
    struct MARBURGUMIISHARED_API FDistrictResolver
    {
        /** The district's id, or empty outside every district. */
        FString Resolve(const FCatalogue& Catalogue, const FVector2D& Point, bool bInNamedArea = false);
        void Reset() { HeldId.Reset(); }
        const FString& GetHeldId() const { return HeldId; }

    private:
        FString HeldId;
    };

    /**
     * The named street at the player's feet, held through junctions: the one shown stays until another is the switch
     * margin nearer, or the player is that far past the reach. Held by name.
     */
    struct MARBURGUMIISHARED_API FStreetResolver
    {
        /** The street's name, or empty when none is in reach. */
        FString Resolve(const FCatalogue& Catalogue, const FVector2D& Point);
        void Reset() { HeldName.Reset(); }
        const FString& GetHeldName() const { return HeldName; }

    private:
        FString HeldName;
    };

    /**
     * The landmark at the player's feet, held along a street front: the one shown stays until another is the switch
     * margin nearer, or the player is that far past the reach. Walking into an outline names it at once, once the
     * player is the walk-in distance inside it and as far out of the one shown, or inside a smaller one within it (a
     * church within its college). Held by EID.
     */
    struct MARBURGUMIISHARED_API FLandmarkResolver
    {
        /** The landmark's EID, or empty when none is in reach. */
        FString Resolve(const FCatalogue& Catalogue, const FVector2D& Point);
        void Reset() { HeldEid.Reset(); }
        const FString& GetHeldEid() const { return HeldEid; }

    private:
        FString HeldEid;
    };

    /** One player's place as the server tells it: ids and a name, never a position. */
    struct FPlaceIds
    {
        FString District;
        FString Street;
        FString Landmark;

        bool operator==(const FPlaceIds& Other) const
        {
            return District == Other.District && Street == Other.Street && Landmark == Other.Landmark;
        }
        bool operator!=(const FPlaceIds& Other) const { return !(*this == Other); }
    };

    /** The three resolvers one player needs. */
    struct FPlaceTracker
    {
        FDistrictResolver District;
        FStreetResolver Street;
        FLandmarkResolver Landmark;

        FPlaceIds Resolve(const FCatalogue& Catalogue, const FVector2D& Point, bool bInNamedArea)
        {
            return { District.Resolve(Catalogue, Point, bInNamedArea), Street.Resolve(Catalogue, Point), Landmark.Resolve(Catalogue, Point) };
        }
    };
}
