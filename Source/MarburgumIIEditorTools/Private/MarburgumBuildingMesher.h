#pragma once

// Watertight Lent building shells. Plain standard C++ so the same code runs in the editor commandlet
// and in a standalone check outside Unreal.
//
// A building is one solid: a floor, the walls, a soffit and fascia under the eaves, and a roof.
// The roof is a weighted straight skeleton of the footprint. Every roof face starts on a wall line,
// so the roof always meets the walls. Gable ends and party walls are edges that do not move inward;
// their faces come out vertical and are plaster. Window frames, glass, sills and the street-door frame are
// separate closed boxes set into the wall, and the plinth, string course and cornice closed rings round it.
// A building with a ground floor has its street door cut through the wall its layout chose (FFootprint::DoorEdge,
// MarburgumInteriorCore.h); every other building keeps a painted door. Check() allows that one doorway and no other
// crack: every other edge is used by exactly two triangles, once in each direction.

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace MarburgumBuildings
{
    struct FVec2
    {
        double X = 0.0;
        double Y = 0.0;
    };

    struct FVec3
    {
        double X = 0.0;
        double Y = 0.0;
        double Z = 0.0;
    };

    enum class ERoof
    {
        Flat,
        Hipped,
        Gabled
    };

    enum EMaterial : int
    {
        Plaster = 0,
        RoofTile = 1,
        Frame = 2,
        Glass = 3,
        FlatRoof = 4,
        // The plinth, and the string course and cornice.
        Socle = 5,
        Trim = 6,
        MaterialCount = 7
    };

    struct FFootprint
    {
        std::string Id;
        // Plan metres (x east, y south of Stari most); any winding.
        std::vector<FVec2> Outline;
        // Per outline edge (edge i runs from point i to point i + 1): a wall shared with a neighbour.
        std::vector<bool> Party;
        // Bottom of the walls (below the lowest terrain under the footprint) and the street level the
        // storeys, windows and door are measured from.
        double Base = 0.0;
        double Ground = 0.0;
        double Eaves = 3.0;
        // Highest the roof may rise above the eaves; a wider roof is cut flat at this height.
        double RoofRise = 4.0;
        double PitchDegrees = 40.0;
        ERoof Roof = ERoof::Gabled;
        // The wall the street door is cut through, from the building's interior layout
        // (MarburgumInteriorCore.h, Generate's EntranceEdge), or -1: no hole, a painted door on the longest free wall.
        int DoorEdge = -1;
        // The cut door's sill: the ground floor's measured floor (MarburgumInteriorCore.h, FPlan::Floor). NaN: Ground.
        double DoorSill = std::numeric_limits<double>::quiet_NaN();
        double Sill() const { return std::isfinite(DoorSill) ? DoorSill : Ground; }
    };

    struct FTriangle
    {
        // Indices into FBuildingMesh::Positions, in Unreal's front-face winding: the outward normal is
        // (C - A) x (B - A).
        int A = 0;
        int B = 0;
        int C = 0;
        int Material = Plaster;
        // A window's glass: its index in FBuildingMesh::Panes; -1 for everything else.
        int Pane = -1;
    };

    // Where a window pane is, for the household behind it (MarburgumLitWindows.h, ADR-066).
    struct FPane
    {
        // The façade, in the order they are built; the storey, 0 the ground floor; the bay, 0 the first along the wall.
        int Facade = 0;
        int Storey = 0;
        int Bay = 0;
    };

    struct FBuildingMesh
    {
        std::vector<FVec3> Positions;
        std::vector<FTriangle> Triangles;
        // How the roof was built: "skeleton", "hipped" (gable pass failed), "flat" or "flat-fallback".
        std::string RoofMethod;
        int Gables = 0;
        int PartyWalls = 0;
        int Windows = 0;
        // Every window's glass, in the order built.
        std::vector<FPane> Panes;
        int Facades = 0;
        // Façade bands built: plinth, string course, cornice.
        int Bands = 0;
        // The street doorway, when the longest free wall can take one. Corner order is bottom-left,
        // bottom-right, top-right, top-left, on the outer wall, in plan metres.
        struct FDoorPortal
        {
            bool bPresent = false;
            FVec3 Corner[4] = {};
        };
        FDoorPortal Door;
    };

    struct FCheck
    {
        // Closed: after splitting edges at any welded vertex lying on them, every edge is used exactly
        // once in each direction, so no crack can show. Strict: the same without the split (no
        // T-junctions at all).
        bool bClosed = false;
        bool bStrict = false;
        int OpenEdges = 0;
        int OverusedEdges = 0;
        int StrictOpenEdges = 0;
        int Components = 0;
        // Signed volume of the shell in cubic metres; negative means it is inside out.
        double Volume = 0.0;
        // Footprint area covered by the roof seen from above, against the footprint area.
        double RoofCover = 0.0;
        double FootprintArea = 0.0;
    };

    // Marks edges that lie along another footprint's edge (within Tolerance metres and over most of
    // their length). Fills every footprint's Party array.
    void FindPartyWalls(std::vector<FFootprint>& Footprints, double Tolerance = 0.35);

    // Removes repeated and nearly collinear points and makes the outline counter-clockwise. Returns
    // false when nothing usable remains.
    bool CleanOutline(std::vector<FVec2>& Outline);

    bool BuildBuilding(const FFootprint& Footprint, FBuildingMesh& Out, std::string& Error);

    FCheck Check(const FBuildingMesh& Mesh, const FFootprint& Footprint);
}
