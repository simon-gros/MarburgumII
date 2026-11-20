#pragma once

// Ground-floor interiors for Lent's building footprints (ADR-076).
//
// Plain C++: the façade mesher, the game and a standalone check all include this header, so a street
// doorway is the same rectangle in the shell and in the room layout. No randomness. The same plan is
// the same rooms on every machine and after every restart.
//
// One floor only. Upper storeys are counted and not built: a stair does not exist yet, and an
// unreachable floor is worse than none. The shell stays the outside wall. This only decides which
// buildings can take a floor, where the street door is, and how the rooms meet.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace MarburgumInteriors
{
    inline constexpr double Pi = 3.14159265358979323846;
    // Clear opening. The leaf is narrower than this so it can swing inside the frame.
    inline constexpr double DoorWidth = 1.10;
    inline constexpr double DoorHeight = 2.10;
    // A façade shorter than this, or shorter than the door plus a lintel, gets no street door.
    inline constexpr double MinFacadeForDoor = 2.40;
    inline constexpr double MinEdgeForDoor = 2.20;
    // The shell's wall face stops this far under the eaves (fascia, or a flat roof's parapet).
    // The mesher and the layout both use it, so neither cuts a door the other does not.
    inline constexpr double FacadeBelowEaves = 0.50;
    inline constexpr double MinRoom = 2.60;
    inline constexpr double Partition = 0.15;
    inline constexpr double Sample = 0.40;
    inline constexpr double CeilingClear = 2.60;
    inline constexpr double MinHeadroom = 2.45;
    // The façade commandlet builds no shell for a footprint under this, or for an outline of fewer than four
    // points (MarburgumRebuildFacadesCommandlet.cpp, ReadFootprints). No shell, no floor.
    inline constexpr double MinShellArea = 30.0;
    // Half the vestibule from the street door to the rooms. Wider than the door so its walls clear the leaf.
    inline constexpr double VestibuleHalf = 0.70;
    // A door wall needs this much free pavement in front of it, and no other building there.
    inline constexpr double DoorApproach = 1.00;
    // The most the floor may stand above the street at the door: one step a person takes without a stair
    // (the character movement component steps 45 cm).
    inline constexpr double MaxDoorStep = 0.40;
    // The floor is laid this far above the highest ground under the rooms and the street at the door.
    inline constexpr double FloorClearance = 0.03;

    struct FVec2
    {
        double X = 0.0;
        double Y = 0.0;
    };

    inline FVec2 operator+(const FVec2& A, const FVec2& B) { return { A.X + B.X, A.Y + B.Y }; }
    inline FVec2 operator-(const FVec2& A, const FVec2& B) { return { A.X - B.X, A.Y - B.Y }; }
    inline FVec2 operator*(const FVec2& A, double S) { return { A.X * S, A.Y * S }; }
    inline double Dot(const FVec2& A, const FVec2& B) { return A.X * B.X + A.Y * B.Y; }
    inline double Cross(const FVec2& A, const FVec2& B) { return A.X * B.Y - A.Y * B.X; }
    inline double Length(const FVec2& A) { return std::sqrt(Dot(A, A)); }
    inline FVec2 Normalize(const FVec2& A)
    {
        const double L = Length(A);
        return L > 1.0e-12 ? A * (1.0 / L) : FVec2{};
    }

    // Where the street door sits on one wall. U is metres from the edge start, V is metres above the street.
    struct FDoorCut
    {
        bool bFits = false;
        double U0 = 0.0;
        double U1 = 0.0;
        double V0 = 0.0;
        double V1 = 0.0;
    };

    inline double FacadeHeight(double Eaves, double Ground)
    {
        return Eaves - Ground - FacadeBelowEaves;
    }

    inline FDoorCut DoorCut(double Width, double HeightAboveGround)
    {
        FDoorCut Door;
        if (Width < MinEdgeForDoor || HeightAboveGround < MinFacadeForDoor || DoorHeight + 0.20 > HeightAboveGround)
        {
            return Door;
        }
        Door.bFits = true;
        Door.U0 = Width * 0.5 - DoorWidth * 0.5;
        Door.U1 = Width * 0.5 + DoorWidth * 0.5;
        Door.V0 = 0.0;
        Door.V1 = DoorHeight;
        return Door;
    }

    enum class EUse : uint8_t
    {
        Residential,
        Commercial,
        Industrial,
        Public,
        Other
    };

    enum class EStatus : uint8_t
    {
        Eligible,
        Exception
    };

    inline const char* UseName(EUse Use)
    {
        switch (Use)
        {
        case EUse::Residential: return "residential";
        case EUse::Commercial: return "commercial";
        case EUse::Industrial: return "industrial";
        case EUse::Public: return "public";
        default: return "other";
        }
    }

    inline EUse ClassifyUse(const std::string& Kind)
    {
        if (Kind == "apartments" || Kind == "house" || Kind == "residential" || Kind == "dormitory" || Kind == "detached" || Kind == "hotel")
        {
            return EUse::Residential;
        }
        if (Kind == "commercial" || Kind == "retail" || Kind == "office" || Kind == "kiosk" || Kind == "mall")
        {
            return EUse::Commercial;
        }
        if (Kind == "industrial" || Kind == "warehouse" || Kind == "service" || Kind == "garage" || Kind == "garages" || Kind == "shed")
        {
            return EUse::Industrial;
        }
        if (Kind == "university" || Kind == "school" || Kind == "hospital" || Kind == "government" || Kind == "civic" || Kind == "church"
            || Kind == "synagogue" || Kind == "kindergarten" || Kind == "college" || Kind == "public" || Kind == "sports_centre"
            || Kind == "transportation")
        {
            return EUse::Public;
        }
        return EUse::Other;
    }

    // Not a place a person walks into, even when the footprint is large.
    inline bool IsOccupiableKind(const std::string& Kind)
    {
        return Kind != "roof" && Kind != "construction" && Kind != "grandstand" && Kind != "transformer_tower";
    }

    struct FPlan
    {
        std::string Id;
        std::string Kind;
        // Plan metres, the same frame as buildings.json. Cleaned before generation.
        std::vector<FVec2> Outline;
        // One flag per outline edge. Empty until MarkPartyWalls.
        std::vector<char> Party;
        // One flag per outline edge: another building stands in front of its middle (PrepareCity).
        std::vector<char> Blocked;
        // Points in buildings.json before cleaning, and whether the façade commandlet builds a shell.
        int RawPoints = 0;
        bool bShell = true;
        double Ground = 0.0;
        double Eaves = 3.0;
        int Levels = 0;
        double Area = 0.0;
        // Measured in the level (interior_floors.json, MarburgumInteriorAudit -WriteFloors): the floor that clears
        // the ground under the rooms, and the street in front of the door. NaN when not measured: the floor is
        // then Ground. Refused names what the level puts in the way, and makes the building an exception.
        double Floor = std::numeric_limits<double>::quiet_NaN();
        double Street = std::numeric_limits<double>::quiet_NaN();
        std::string Refused;
        // Rooms only within this rectangle, in the street door's frame (U metres along the door wall from its
        // start, V metres in from it): the low part of a footprint on a slope, where the ground stays within a
        // step of the street. NaN: the whole footprint.
        double WithinU0 = std::numeric_limits<double>::quiet_NaN();
        double WithinU1 = 0.0;
        double WithinV0 = 0.0;
        double WithinV1 = 0.0;
    };

    inline double FloorLevel(const FPlan& Plan)
    {
        return std::isfinite(Plan.Floor) ? Plan.Floor : Plan.Ground;
    }

    struct FRoom
    {
        // Counter-clockwise in plan metres.
        FVec2 Corner[4];
        double FloorZ = 0.0;
        double CeilingZ = 0.0;
    };

    struct FOpening
    {
        FVec2 A;
        FVec2 B;
        double Z0 = 0.0;
        double Z1 = 0.0;
        // -1 is the street. Otherwise a room index.
        int RoomA = -1;
        int RoomB = -1;
        bool bEntrance = false;
        // A hinged leaf hangs here. The vestibule's inner opening is a plain passage.
        bool bLeaf = true;
    };

    struct FLayout
    {
        std::string Id;
        EUse Use = EUse::Other;
        EStatus Status = EStatus::Exception;
        std::string Reason;
        std::vector<FRoom> Rooms;
        std::vector<FOpening> Openings;
        int EntranceEdge = -1;
        int FloorsNoted = 1;
        int FloorsGenerated = 0;
        bool bUpperFloorsOmitted = false;
    };

    inline double SignedArea(const std::vector<FVec2>& Points)
    {
        double Area = 0.0;
        for (size_t I = 0; I < Points.size(); ++I)
        {
            Area += Cross(Points[I], Points[(I + 1) % Points.size()]);
        }
        return Area * 0.5;
    }

    inline bool SegmentsCross(const FVec2& A, const FVec2& B, const FVec2& C, const FVec2& D)
    {
        const double D1 = Cross(B - A, C - A);
        const double D2 = Cross(B - A, D - A);
        const double D3 = Cross(D - C, A - C);
        const double D4 = Cross(D - C, B - C);
        return ((D1 > 1.0e-9 && D2 < -1.0e-9) || (D1 < -1.0e-9 && D2 > 1.0e-9))
            && ((D3 > 1.0e-9 && D4 < -1.0e-9) || (D3 < -1.0e-9 && D4 > 1.0e-9));
    }

    inline bool IsSimple(const std::vector<FVec2>& Points)
    {
        const size_t N = Points.size();
        for (size_t I = 0; I < N; ++I)
        {
            for (size_t J = I + 1; J < N; ++J)
            {
                if (J == I + 1 || (I == 0 && J == N - 1))
                {
                    continue;
                }
                if (SegmentsCross(Points[I], Points[(I + 1) % N], Points[J], Points[(J + 1) % N]))
                {
                    return false;
                }
            }
        }
        return true;
    }

    // The façade mesher's CleanOutline, copied so a layout's entrance edge is the mesher's door wall.
    // Keep the two in step (MarburgumBuildingMesher.cpp).
    inline bool CleanOutline(std::vector<FVec2>& Outline)
    {
        if (Outline.size() > 1 && Length(Outline.front() - Outline.back()) < 1.0e-6)
        {
            Outline.pop_back();
        }
        if (SignedArea(Outline) < 0.0)
        {
            std::reverse(Outline.begin(), Outline.end());
        }
        bool bChanged = true;
        while (bChanged && Outline.size() >= 3)
        {
            bChanged = false;
            const size_t N = Outline.size();
            for (size_t I = 0; I < N; ++I)
            {
                const FVec2& Prev = Outline[(I + N - 1) % N];
                const FVec2& Here = Outline[I];
                const FVec2& Next = Outline[(I + 1) % N];
                const double In = Length(Here - Prev);
                const double Turn = std::fabs(std::atan2(Cross(Here - Prev, Next - Here), Dot(Here - Prev, Next - Here)));
                if (In < 0.25 || Turn < 4.0 * Pi / 180.0 || Turn > 170.0 * Pi / 180.0)
                {
                    Outline.erase(Outline.begin() + static_cast<std::ptrdiff_t>(I));
                    bChanged = true;
                    break;
                }
            }
        }
        return Outline.size() >= 3 && SignedArea(Outline) > 1.0 && IsSimple(Outline);
    }

    // The façade mesher's FindPartyWalls, for the same reason. Tolerance is metres. Only footprints that get a
    // shell take part, as in the commandlet, which runs FindPartyWalls on the shells it builds.
    inline void MarkPartyWalls(std::vector<FPlan>& Plans, double Tolerance = 0.35)
    {
        for (FPlan& Plan : Plans)
        {
            Plan.Party.assign(Plan.Outline.size(), 0);
        }
        for (size_t I = 0; I < Plans.size(); ++I)
        {
            FPlan& Plan = Plans[I];
            if (!Plan.bShell)
            {
                continue;
            }
            const size_t N = Plan.Outline.size();
            for (size_t E = 0; E < N; ++E)
            {
                const FVec2 A = Plan.Outline[E];
                const FVec2 B = Plan.Outline[(E + 1) % N];
                const double Len = Length(B - A);
                const FVec2 D = Normalize(B - A);
                double Covered = 0.0;
                for (size_t J = 0; J < Plans.size(); ++J)
                {
                    if (J == I || !Plans[J].bShell)
                    {
                        continue;
                    }
                    const std::vector<FVec2>& Other = Plans[J].Outline;
                    for (size_t K = 0; K < Other.size(); ++K)
                    {
                        const FVec2 C = Other[K];
                        const FVec2 G = Other[(K + 1) % Other.size()];
                        const FVec2 DO = Normalize(G - C);
                        if (Dot(D, DO) > -0.995)
                        {
                            continue;
                        }
                        const FVec2 Normal{ -D.Y, D.X };
                        if (std::fabs(Dot(C - A, Normal)) > Tolerance || std::fabs(Dot(G - A, Normal)) > Tolerance)
                        {
                            continue;
                        }
                        const double S0 = std::clamp(Dot(C - A, D), 0.0, Len);
                        const double S1 = std::clamp(Dot(G - A, D), 0.0, Len);
                        Covered += std::fabs(S1 - S0);
                    }
                }
                Plan.Party[E] = Len > 1.0 && Covered > 0.7 * Len ? 1 : 0;
            }
        }
    }

    inline bool PointInside(const std::vector<FVec2>& Outline, const FVec2& P)
    {
        const size_t N = Outline.size();
        bool bInside = false;
        for (size_t I = 0, J = N - 1; I < N; J = I++)
        {
            const FVec2& A = Outline[J];
            const FVec2& B = Outline[I];
            const FVec2 AB = B - A;
            const double L2 = Dot(AB, AB);
            if (L2 > 1.0e-12)
            {
                const double T = std::clamp(Dot(P - A, AB) / L2, 0.0, 1.0);
                if (Length(P - (A + AB * T)) < 1.0e-4)
                {
                    return true;
                }
            }
            const bool bCross = ((A.Y > P.Y) != (B.Y > P.Y)) && (P.X < (B.X - A.X) * (P.Y - A.Y) / (B.Y - A.Y) + A.X);
            bInside = bCross ? !bInside : bInside;
        }
        return bInside;
    }

    // The street door's wall: the longest wall that is not shared and has nothing built in front of its middle.
    inline int LongestFreeEdge(const std::vector<FVec2>& Outline, const std::vector<char>& Party, const std::vector<char>& Blocked = {})
    {
        int Best = -1;
        double BestLength = 0.0;
        for (size_t I = 0; I < Outline.size(); ++I)
        {
            if ((I < Party.size() && Party[I]) || (I < Blocked.size() && Blocked[I]))
            {
                continue;
            }
            const double L = Length(Outline[(I + 1) % Outline.size()] - Outline[I]);
            if (L > BestLength)
            {
                BestLength = L;
                Best = static_cast<int>(I);
            }
        }
        return Best;
    }

    // Shells, cleaned outlines, party walls and blocked door walls for the whole city, the way the façade
    // commandlet sees them. The game, the commandlet and the tests all call this, so the wall the mesher cuts
    // is the wall the rooms open from.
    inline void PrepareCity(std::vector<FPlan>& Plans)
    {
        for (FPlan& Plan : Plans)
        {
            if (Plan.RawPoints == 0)
            {
                Plan.RawPoints = static_cast<int>(Plan.Outline.size());
            }
            const bool bClean = CleanOutline(Plan.Outline);
            Plan.bShell = Plan.Area >= MinShellArea && Plan.RawPoints >= 4 && bClean;
        }
        MarkPartyWalls(Plans);
        for (size_t I = 0; I < Plans.size(); ++I)
        {
            FPlan& Plan = Plans[I];
            const size_t N = Plan.Outline.size();
            Plan.Blocked.assign(N, 0);
            if (!Plan.bShell)
            {
                continue;
            }
            for (size_t E = 0; E < N; ++E)
            {
                const FVec2 A = Plan.Outline[E];
                const FVec2 B = Plan.Outline[(E + 1) % N];
                const FVec2 Along = Normalize(B - A);
                const FVec2 Outward{ Along.Y, -Along.X };
                const FVec2 Mid = (A + B) * 0.5;
                const FVec2 Probes[3] = { Mid + Outward * DoorApproach, Mid + Outward * DoorApproach + Along * (DoorWidth * 0.5),
                    Mid + Outward * DoorApproach - Along * (DoorWidth * 0.5) };
                for (size_t J = 0; J < Plans.size() && !Plan.Blocked[E]; ++J)
                {
                    if (J == I || !Plans[J].bShell)
                    {
                        continue;
                    }
                    for (const FVec2& Probe : Probes)
                    {
                        if (PointInside(Plans[J].Outline, Probe))
                        {
                            Plan.Blocked[E] = 1;
                            break;
                        }
                    }
                }
            }
        }
    }

    inline bool RoomsConnected(const FLayout& Layout)
    {
        if (Layout.Rooms.empty())
        {
            return Layout.Status == EStatus::Exception && !Layout.Reason.empty();
        }
        if (Layout.Openings.empty() || !Layout.Openings[0].bEntrance)
        {
            return false;
        }
        std::vector<char> Seen(Layout.Rooms.size(), 0);
        std::vector<int> Stack;
        for (const FOpening& Opening : Layout.Openings)
        {
            if (Opening.RoomA < -1 || Opening.RoomB < -1 || Opening.RoomA >= static_cast<int>(Layout.Rooms.size())
                || Opening.RoomB >= static_cast<int>(Layout.Rooms.size()))
            {
                return false;
            }
            if (Length(Opening.B - Opening.A) < 0.90 || Opening.Z1 - Opening.Z0 < 1.90)
            {
                return false;
            }
        }
        for (const FOpening& Opening : Layout.Openings)
        {
            if (Opening.RoomA == -1 && Opening.RoomB >= 0)
            {
                Stack.push_back(Opening.RoomB);
            }
            if (Opening.RoomB == -1 && Opening.RoomA >= 0)
            {
                Stack.push_back(Opening.RoomA);
            }
        }
        for (size_t I = 0; I < Stack.size(); ++I)
        {
            const int Room = Stack[I];
            if (Room < 0 || Seen[Room])
            {
                continue;
            }
            Seen[Room] = 1;
            for (const FOpening& Opening : Layout.Openings)
            {
                const int Other = Opening.RoomA == Room ? Opening.RoomB : Opening.RoomB == Room ? Opening.RoomA : -2;
                if (Other >= 0 && !Seen[Other])
                {
                    Stack.push_back(Other);
                }
            }
        }
        for (const char Flag : Seen)
        {
            if (!Flag)
            {
                return false;
            }
        }
        return true;
    }

    // Where a leaf hangs in an opening and which way it swings. Plan metres. The leaf turns about Hinge from
    // ClosedDir towards Into, which points into the room it opens into, by SwingDegrees.
    struct FLeaf
    {
        bool bValid = false;
        FVec2 Hinge;
        FVec2 ClosedDir;
        FVec2 Into;
        double Width = 0.0;
        double Height = 0.0;
        double Z = 0.0;
    };

    inline constexpr double SwingDegrees = 88.0;
    inline constexpr double LeafThickness = 0.04;
    // The façade's jambs stand 3 cm into the wall (MarburgumBuildingMesher.cpp, AddFacade). The street leaf
    // hangs behind them, overlapping each jamb like a rebate, so it never cuts through a jamb as it swings.
    inline constexpr double StreetLeafDepth = 0.055;
    inline constexpr double LeafSideGap = 0.03;

    inline FVec2 RoomMiddle(const FRoom& Room)
    {
        return (Room.Corner[0] + Room.Corner[1] + Room.Corner[2] + Room.Corner[3]) * 0.25;
    }

    inline FLeaf LeafFor(const FLayout& Layout, const FOpening& Opening)
    {
        FLeaf Leaf;
        const double Width = Length(Opening.B - Opening.A);
        if (!Opening.bLeaf || Width < 0.9)
        {
            return Leaf;
        }
        const int RoomIndex = Opening.RoomB >= 0 ? Opening.RoomB : Opening.RoomA;
        if (RoomIndex < 0 || RoomIndex >= static_cast<int>(Layout.Rooms.size()))
        {
            return Leaf;
        }
        const FVec2 Dir = Normalize(Opening.B - Opening.A);
        FVec2 Into{ -Dir.Y, Dir.X };
        if (Dot(RoomMiddle(Layout.Rooms[static_cast<size_t>(RoomIndex)]) - (Opening.A + Opening.B) * 0.5, Into) < 0.0)
        {
            Into = Into * -1.0;
        }
        const double Depth = Opening.bEntrance ? StreetLeafDepth : 0.0;
        Leaf.bValid = true;
        Leaf.Hinge = Opening.A + Dir * LeafSideGap + Into * (Depth + LeafThickness * 0.5);
        Leaf.ClosedDir = Dir;
        Leaf.Into = Into;
        Leaf.Width = Width - 2.0 * LeafSideGap;
        Leaf.Height = Opening.Z1 - Opening.Z0 - 0.02;
        Leaf.Z = Opening.Z0 + 0.01;
        return Leaf;
    }

    // The free edge of a leaf swung fully open.
    inline FVec2 OpenLeafTip(const FLeaf& Leaf)
    {
        const double R = SwingDegrees * Pi / 180.0;
        return Leaf.Hinge + (Leaf.ClosedDir * std::cos(R) + Leaf.Into * std::sin(R)) * Leaf.Width;
    }

    inline FLayout Generate(FPlan Plan)
    {
        FLayout Layout;
        Layout.Id = Plan.Id;
        Layout.Use = ClassifyUse(Plan.Kind);
        const double Level = FloorLevel(Plan);
        const double Height = FacadeHeight(Plan.Eaves, Level);
        const int Noted = Plan.Levels > 0 ? Plan.Levels : std::max(1, static_cast<int>(std::lround(std::max(0.0, Plan.Eaves - Plan.Ground) / 3.2)));
        Layout.FloorsNoted = Noted;
        Layout.bUpperFloorsOmitted = Noted > 1;

        auto Fail = [&](const char* Reason) {
            Layout.Status = EStatus::Exception;
            Layout.Reason = Reason;
            Layout.FloorsGenerated = 0;
            return Layout;
        };

        if (!IsOccupiableKind(Plan.Kind))
        {
            return Fail("not an occupiable building");
        }
        if (!Plan.bShell)
        {
            return Fail("no shell (under 30 m2 or not a simple outline)");
        }
        if (!Plan.Refused.empty())
        {
            return Fail(Plan.Refused.c_str());
        }
        if (std::isfinite(Plan.Floor) && std::isfinite(Plan.Street) && Plan.Floor - Plan.Street > MaxDoorStep + 1.0e-6)
        {
            return Fail("ground inside rises more than a step above the street");
        }
        if (Plan.Area > 0.0 && Plan.Area < 16.0)
        {
            return Fail("footprint under 16 m2");
        }
        if (!CleanOutline(Plan.Outline))
        {
            return Fail("outline is not a simple building");
        }
        if (Height < MinHeadroom)
        {
            return Fail("interior lower than a person");
        }
        if (Plan.Party.size() != Plan.Outline.size())
        {
            Plan.Party.assign(Plan.Outline.size(), 0);
        }
        if (Plan.Blocked.size() != Plan.Outline.size())
        {
            Plan.Blocked.assign(Plan.Outline.size(), 0);
        }

        const int Edge = LongestFreeEdge(Plan.Outline, Plan.Party, Plan.Blocked);
        Layout.EntranceEdge = Edge;
        if (Edge < 0)
        {
            return Fail("no street wall can take a door");
        }
        const FVec2 A = Plan.Outline[static_cast<size_t>(Edge)];
        const FVec2 B = Plan.Outline[(static_cast<size_t>(Edge) + 1) % Plan.Outline.size()];
        const FVec2 Along = Normalize(B - A);
        const FVec2 Inward{ -Along.Y, Along.X };
        const double Width = Length(B - A);
        const FDoorCut Door = DoorCut(Width, Height);
        if (!Door.bFits)
        {
            return Fail("no street wall can take a door");
        }
        const FVec2 Approach = A + Along * (Width * 0.5) + Inward * 0.20;
        if (!PointInside(Plan.Outline, Approach))
        {
            return Fail("door does not open into the footprint");
        }

        double UMin = 1.0e9, UMax = -1.0e9, VMin = 1.0e9, VMax = -1.0e9;
        for (const FVec2& P : Plan.Outline)
        {
            const FVec2 D = P - A;
            UMin = std::min(UMin, Dot(D, Along));
            UMax = std::max(UMax, Dot(D, Along));
            VMin = std::min(VMin, Dot(D, Inward));
            VMax = std::max(VMax, Dot(D, Inward));
        }
        UMin -= Sample;
        VMin = std::min(VMin, 0.0);
        const int Cols = std::clamp(static_cast<int>(std::ceil((UMax - UMin) / Sample)), 1, 480);
        const int Rows = std::clamp(static_cast<int>(std::ceil((VMax - VMin) / Sample)), 1, 480);
        std::vector<char> Inside(static_cast<size_t>(Cols * Rows), 0);
        auto CellInside = [&](int C, int R) -> bool {
            if (C < 0 || R < 0 || C >= Cols || R >= Rows)
            {
                return false;
            }
            return Inside[static_cast<size_t>(R * Cols + C)] != 0;
        };
        const bool bWithin = std::isfinite(Plan.WithinU0);
        for (int R = 0; R < Rows; ++R)
        {
            for (int C = 0; C < Cols; ++C)
            {
                const double U = UMin + (C + 0.5) * Sample;
                const double V = VMin + (R + 0.5) * Sample;
                const FVec2 P = A + Along * U + Inward * V;
                const bool bAllowed = !bWithin || (U >= Plan.WithinU0 && U <= Plan.WithinU1 && V >= Plan.WithinV0 && V <= Plan.WithinV1);
                Inside[static_cast<size_t>(R * Cols + C)] = bAllowed && PointInside(Plan.Outline, P) ? 1 : 0;
            }
        }
        const int DoorCol = std::clamp(static_cast<int>(std::floor((Width * 0.5 - UMin) / Sample)), 0, Cols - 1);
        // The front room spans the whole door, and a little more, once inset from the cells: a person comes
        // straight in through the vestibule instead of having to turn in a space shallower than they are.
        const int DoorColL = std::clamp(static_cast<int>(std::floor((Width * 0.5 - DoorWidth * 0.5 - 0.30 - UMin) / Sample)), 0, Cols - 1);
        const int DoorColR = std::clamp(static_cast<int>(std::floor((Width * 0.5 + DoorWidth * 0.5 + 0.30 - UMin) / Sample)), 0, Cols - 1);
        int DoorRow = -1;
        for (int R = 0; R < Rows; ++R)
        {
            const double V = VMin + (R + 0.5) * Sample;
            if (V < 0.05 || V > 3.0)
            {
                continue;
            }
            if (CellInside(DoorCol, R))
            {
                DoorRow = R;
                break;
            }
        }
        if (DoorRow < 0)
        {
            return Fail("door does not open into the footprint");
        }

        struct FRect
        {
            int C0 = 0, C1 = 0, R0 = 0, R1 = 0;
            double Area = 0.0;
        };
        FRect Best;
        std::vector<int> BarHeight(static_cast<size_t>(Cols), 0);
        std::vector<int> Stack;
        for (int R = 0; R < Rows; ++R)
        {
            for (int C = 0; C < Cols; ++C)
            {
                BarHeight[static_cast<size_t>(C)] = CellInside(C, R) ? BarHeight[static_cast<size_t>(C)] + 1 : 0;
            }
            Stack.clear();
            for (int C = 0; C <= Cols; ++C)
            {
                const int H = C == Cols ? 0 : BarHeight[static_cast<size_t>(C)];
                while (!Stack.empty() && BarHeight[static_cast<size_t>(Stack.back())] > H)
                {
                    const int Bar = BarHeight[static_cast<size_t>(Stack.back())];
                    Stack.pop_back();
                    const int Left = Stack.empty() ? 0 : Stack.back() + 1;
                    const int Right = C - 1;
                    const int Top = R - Bar + 1;
                    if (DoorColL < Left || DoorColR > Right || DoorRow < Top || DoorRow > R)
                    {
                        continue;
                    }
                    const double W = (Right - Left + 1) * Sample - 0.50;
                    const double D = (R - Top + 1) * Sample - 0.50;
                    if (W < MinRoom || D < MinRoom)
                    {
                        continue;
                    }
                    const double Area = W * D;
                    if (Area > Best.Area)
                    {
                        Best = { Left, Right, Top, R, Area };
                    }
                }
                if (C < Cols)
                {
                    Stack.push_back(C);
                }
            }
        }
        if (Best.Area <= 0.0)
        {
            return Fail("inscribed room too small");
        }

        const double Inset = 0.25;
        const double U0 = UMin + Best.C0 * Sample + Inset;
        const double U1 = UMin + (Best.C1 + 1) * Sample - Inset;
        const double V0 = VMin + Best.R0 * Sample + Inset;
        const double V1 = VMin + (Best.R1 + 1) * Sample - Inset;
        if (U1 - U0 < MinRoom || V1 - V0 < MinRoom)
        {
            return Fail("inscribed room too small");
        }

        auto At = [&](double U, double V) { return A + Along * U + Inward * V; };
        const double FloorZ = Level;
        const double CeilingZ = Level + std::min(CeilingClear, std::max(MinHeadroom, Plan.Eaves - Level - 0.40));
        if (CeilingZ - FloorZ < MinHeadroom)
        {
            return Fail("interior lower than a person");
        }

        struct FBox
        {
            double U0, U1, V0, V1;
        };
        std::vector<FBox> Boxes;
        // A short vestibule from the shell to the inscribed floor, so the step through the hole has a floor.
        // It is always centred on the door and wider than it: its side walls stand clear of the jambs and the
        // leaf, whatever the inscribed rectangle's own edges are.
        double Tu0 = Width * 0.5 - VestibuleHalf;
        double Tu1 = Width * 0.5 + VestibuleHalf;
        if (V0 > 0.01)
        {
            bool bThreshold = true;
            const FVec2 Corners[4] = { At(Tu0, 0.0), At(Tu1, 0.0), At(Tu1, V0), At(Tu0, V0) };
            for (const FVec2& Corner : Corners)
            {
                bThreshold = bThreshold && PointInside(Plan.Outline, Corner);
            }
            if (!bThreshold)
            {
                return Fail("entrance does not reach the interior");
            }
            Boxes.push_back({ Tu0, Tu1, 0.0, V0 });
        }
        double HallDepth = V1 - V0;
        bool bBack = false;
        if (V1 - V0 >= MinRoom * 2.0)
        {
            HallDepth = std::min(3.20, std::max(MinRoom, (V1 - V0) * 0.38));
            if (V1 - (V0 + HallDepth) < MinRoom)
            {
                HallDepth = V1 - V0 - MinRoom;
            }
            bBack = V1 - (V0 + HallDepth) >= MinRoom - 1.0e-6;
        }
        const double FrontV1 = bBack ? V0 + HallDepth : V1;
        Boxes.push_back({ U0, U1, V0, FrontV1 });
        if (bBack)
        {
            // Floors meet on the shared edge. The partition is a wall on that edge, not a gap in the floor.
            const double Span = U1 - U0;
            int Columns = std::clamp(static_cast<int>(std::floor(Span / MinRoom)), 1, 4);
            while (Columns > 1 && Span / Columns < MinRoom)
            {
                --Columns;
            }
            const double RoomW = Span / Columns;
            for (int Column = 0; Column < Columns; ++Column)
            {
                const double Cu0 = U0 + Column * RoomW;
                Boxes.push_back({ Cu0, Cu0 + RoomW, FrontV1, V1 });
            }
        }

        Layout.Rooms.reserve(Boxes.size());
        for (const FBox& Box : Boxes)
        {
            FRoom Room;
            Room.Corner[0] = At(Box.U0, Box.V0);
            Room.Corner[1] = At(Box.U1, Box.V0);
            Room.Corner[2] = At(Box.U1, Box.V1);
            Room.Corner[3] = At(Box.U0, Box.V1);
            Room.FloorZ = FloorZ;
            Room.CeilingZ = CeilingZ;
            Layout.Rooms.push_back(Room);
        }

        auto AddOpening = [&](double Ua, double Ub, double V, int RoomA, int RoomB, bool bEntrance) {
            if (Ub - Ua < 0.90)
            {
                return;
            }
            FOpening Opening;
            Opening.A = At(Ua, V);
            Opening.B = At(Ub, V);
            Opening.Z0 = FloorZ;
            // The street door is the hole the mesher cut. Every other opening is as tall, so the street leaf swung
            // open through the vestibule clears the lintel behind it.
            Opening.Z1 = std::min(FloorZ + DoorHeight, CeilingZ - 0.05);
            Opening.RoomA = RoomA;
            Opening.RoomB = RoomB;
            Opening.bEntrance = bEntrance;
            Layout.Openings.push_back(Opening);
        };

        const int Front = Boxes.size() > 1 && V0 > 0.01 ? 1 : 0;
        AddOpening(Door.U0, Door.U1, 0.0, -1, 0, true);
        if (Front == 1)
        {
            // The vestibule opens into the front room across its whole width, with no second leaf: the street
            // door's own leaf swings through here.
            const double Ou0 = std::max(Boxes[0].U0, Boxes[1].U0);
            const double Ou1 = std::min(Boxes[0].U1, Boxes[1].U1);
            AddOpening(Ou0, Ou1, V0, 0, 1, false);
            if (Layout.Openings.size() > 1)
            {
                Layout.Openings.back().bLeaf = false;
            }
        }
        if (bBack)
        {
            const int BackFirst = Front + 1;
            for (int Index = BackFirst; Index < static_cast<int>(Boxes.size()); ++Index)
            {
                const FBox& Box = Boxes[static_cast<size_t>(Index)];
                const double Mid = (Box.U0 + Box.U1) * 0.5;
                const double Half = std::min(0.50, (Box.U1 - Box.U0 - 0.80) * 0.5);
                AddOpening(Mid - Half, Mid + Half, FrontV1, Front, Index, false);
            }
        }

        if (!RoomsConnected(Layout))
        {
            Layout.Rooms.clear();
            Layout.Openings.clear();
            return Fail("generated rooms are not all reachable");
        }
        Layout.Status = EStatus::Eligible;
        Layout.Reason.clear();
        Layout.FloorsGenerated = 1;
        return Layout;
    }
}
