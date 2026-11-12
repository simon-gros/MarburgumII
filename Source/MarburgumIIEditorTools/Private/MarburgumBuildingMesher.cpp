#include "MarburgumBuildingMesher.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace MarburgumBuildings
{
namespace
{
    constexpr double Pi = 3.14159265358979323846;
    // Eaves overhang, fascia depth and flat-roof parapet, in metres.
    constexpr double Overhang = 0.35;
    constexpr double Fascia = 0.18;
    constexpr double ParapetWidth = 0.3;
    constexpr double ParapetHeight = 0.45;
    // A roof wider than twice this is cut flat (the source data's roof_max_span / 2).
    constexpr double MaxRoofReach = 8.0;
    // Façade bands, in metres: the plinth's top above the street and how far it stands out; the
    // string course over the ground floor and the cornice under the eaves (top, depth, height).
    // Every band reaches BandIn into the wall. A door starts on the plinth.
    constexpr double SocleTop = 0.45;
    constexpr double SocleOut = 0.05;
    constexpr double CourseOut = 0.06;
    constexpr double CourseHeight = 0.16;
    constexpr double CorniceOut = 0.14;
    constexpr double CorniceDrop = 0.02;
    constexpr double CorniceHeight = 0.22;
    constexpr double BandIn = 0.02;
    // A band's outer face along a party wall stands this far out, so no corner of it lands on the
    // wall's own edges.
    constexpr double PartyBandOut = 0.005;

    FVec2 operator+(const FVec2& A, const FVec2& B) { return { A.X + B.X, A.Y + B.Y }; }
    FVec2 operator-(const FVec2& A, const FVec2& B) { return { A.X - B.X, A.Y - B.Y }; }
    FVec2 operator*(const FVec2& A, double S) { return { A.X * S, A.Y * S }; }
    double Dot(const FVec2& A, const FVec2& B) { return A.X * B.X + A.Y * B.Y; }
    double Cross(const FVec2& A, const FVec2& B) { return A.X * B.Y - A.Y * B.X; }
    double Length(const FVec2& A) { return std::sqrt(Dot(A, A)); }
    FVec2 Normalize(const FVec2& A)
    {
        const double L = Length(A);
        return L > 1.0e-12 ? A * (1.0 / L) : FVec2{};
    }

    FVec3 operator+(const FVec3& A, const FVec3& B) { return { A.X + B.X, A.Y + B.Y, A.Z + B.Z }; }
    FVec3 operator-(const FVec3& A, const FVec3& B) { return { A.X - B.X, A.Y - B.Y, A.Z - B.Z }; }
    FVec3 operator*(const FVec3& A, double S) { return { A.X * S, A.Y * S, A.Z * S }; }
    double Dot(const FVec3& A, const FVec3& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
    FVec3 Cross(const FVec3& A, const FVec3& B)
    {
        return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X };
    }
    double Length(const FVec3& A) { return std::sqrt(Dot(A, A)); }
    FVec3 Normalize(const FVec3& A)
    {
        const double L = Length(A);
        return L > 1.0e-12 ? A * (1.0 / L) : FVec3{};
    }

    FVec3 At(const FVec2& P, double Z) { return { P.X, P.Y, Z }; }

    double SignedArea(const std::vector<FVec2>& Points)
    {
        double Area = 0.0;
        for (size_t I = 0; I < Points.size(); ++I)
        {
            Area += Cross(Points[I], Points[(I + 1) % Points.size()]);
        }
        return Area * 0.5;
    }

    bool SegmentsCross(const FVec2& A, const FVec2& B, const FVec2& C, const FVec2& D)
    {
        const double D1 = Cross(B - A, C - A);
        const double D2 = Cross(B - A, D - A);
        const double D3 = Cross(D - C, A - C);
        const double D4 = Cross(D - C, B - C);
        return ((D1 > 1.0e-9 && D2 < -1.0e-9) || (D1 < -1.0e-9 && D2 > 1.0e-9))
            && ((D3 > 1.0e-9 && D4 < -1.0e-9) || (D3 < -1.0e-9 && D4 > 1.0e-9));
    }

    bool IsSimple(const std::vector<FVec2>& Points)
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

    // ---------------------------------------------------------------------------------------------
    // Mesh assembly: welded positions, polygons triangulated in their own plane.

    class FMeshBuilder
    {
    public:
        explicit FMeshBuilder(FBuildingMesh& InMesh) : Mesh(InMesh) {}

        int Vertex(const FVec3& P)
        {
            // Plan metres are welded at 0.1 mm.
            const int64_t Kx = static_cast<int64_t>(std::llround(P.X * 1.0e4));
            const int64_t Ky = static_cast<int64_t>(std::llround(P.Y * 1.0e4));
            const int64_t Kz = static_cast<int64_t>(std::llround(P.Z * 1.0e4));
            const std::tuple<int64_t, int64_t, int64_t> Key(Kx, Ky, Kz);
            const auto Found = Index.find(Key);
            if (Found != Index.end())
            {
                return Found->second;
            }
            const int New = static_cast<int>(Points.size());
            Points.push_back(P);
            Index.emplace(Key, New);
            return New;
        }

        // Adds a planar polygon whose front faces along Outward. Returns false if it could not be
        // triangulated.
        bool Polygon(const std::vector<FVec3>& Corners, const FVec3& Outward, int Material)
        {
            std::vector<int> Ids;
            for (const FVec3& P : Corners)
            {
                const int Id = Vertex(P);
                if (Ids.empty() || Ids.back() != Id)
                {
                    Ids.push_back(Id);
                }
            }
            while (Ids.size() > 1 && Ids.front() == Ids.back())
            {
                Ids.pop_back();
            }
            if (Ids.size() < 3)
            {
                return true;
            }
            FVec3 Newell;
            for (size_t I = 0; I < Ids.size(); ++I)
            {
                const FVec3& A = Points[Ids[I]];
                const FVec3& B = Points[Ids[(I + 1) % Ids.size()]];
                Newell.X += (A.Y - B.Y) * (A.Z + B.Z);
                Newell.Y += (A.Z - B.Z) * (A.X + B.X);
                Newell.Z += (A.X - B.X) * (A.Y + B.Y);
            }
            if (Length(Newell) < 1.0e-9)
            {
                // Zero area: nothing to draw. Its edges lie on the neighbours' edges.
                return true;
            }
            FVec3 Normal = Normalize(Newell);
            if (Dot(Normal, Outward) < 0.0)
            {
                std::reverse(Ids.begin(), Ids.end());
                Normal = Normal * -1.0;
            }
            // Plane basis: U, V with U x V = Normal, so the polygon is counter-clockwise in (U, V).
            const FVec3 Helper = std::fabs(Normal.Z) < 0.9 ? FVec3{ 0.0, 0.0, 1.0 } : FVec3{ 1.0, 0.0, 0.0 };
            const FVec3 U = Normalize(Cross(Helper, Normal));
            const FVec3 V = Cross(Normal, U);
            std::vector<FVec2> Flat;
            for (const int Id : Ids)
            {
                Flat.push_back({ Dot(Points[Id], U), Dot(Points[Id], V) });
            }
            std::vector<int> Order(Ids.size());
            for (size_t I = 0; I < Order.size(); ++I)
            {
                Order[I] = static_cast<int>(I);
            }
            bool bOk = true;
            while (Order.size() > 3)
            {
                const size_t N = Order.size();
                int Best = -1;
                double BestCross = -1.0;
                for (size_t I = 0; I < N; ++I)
                {
                    const int P = Order[(I + N - 1) % N];
                    const int C = Order[I];
                    const int X = Order[(I + 1) % N];
                    const double Turn = Cross(Flat[C] - Flat[P], Flat[X] - Flat[C]);
                    if (Turn <= 1.0e-10)
                    {
                        continue;
                    }
                    bool bBlocked = false;
                    for (size_t J = 0; J < N && !bBlocked; ++J)
                    {
                        const int O = Order[J];
                        if (O == P || O == C || O == X || Ids[O] == Ids[P] || Ids[O] == Ids[C] || Ids[O] == Ids[X])
                        {
                            continue;
                        }
                        const double C1 = Cross(Flat[C] - Flat[P], Flat[O] - Flat[P]);
                        const double C2 = Cross(Flat[X] - Flat[C], Flat[O] - Flat[C]);
                        const double C3 = Cross(Flat[P] - Flat[X], Flat[O] - Flat[X]);
                        bBlocked = C1 >= -1.0e-10 && C2 >= -1.0e-10 && C3 >= -1.0e-10;
                    }
                    if (!bBlocked && Turn > BestCross)
                    {
                        // Prefer the fattest ear so slivers are left for last.
                        Best = static_cast<int>(I);
                        BestCross = Turn;
                    }
                }
                if (Best < 0)
                {
                    bOk = false;
                    break;
                }
                const size_t I = static_cast<size_t>(Best);
                Emit(Ids[Order[(I + N - 1) % N]], Ids[Order[I]], Ids[Order[(I + 1) % N]], Material);
                Order.erase(Order.begin() + static_cast<std::ptrdiff_t>(I));
            }
            if (bOk && Order.size() == 3)
            {
                const double Turn = Cross(Flat[Order[1]] - Flat[Order[0]], Flat[Order[2]] - Flat[Order[1]]);
                if (Turn > 1.0e-10)
                {
                    Emit(Ids[Order[0]], Ids[Order[1]], Ids[Order[2]], Material);
                }
            }
            return bOk;
        }

        // A closed box from eight corners: 0-3 the back face, 4-7 the front, both counter-clockwise
        // seen from the front.
        void Box(const FVec3 (&C)[8], int Material)
        {
            const FVec3 Front = Normalize(Cross(C[5] - C[4], C[7] - C[4]));
            const FVec3 Right = Normalize(C[1] - C[0]);
            const FVec3 Up = Normalize(C[3] - C[0]);
            Polygon({ C[4], C[5], C[6], C[7] }, Front, Material);
            Polygon({ C[0], C[3], C[2], C[1] }, Front * -1.0, Material);
            Polygon({ C[1], C[2], C[6], C[5] }, Right, Material);
            Polygon({ C[0], C[4], C[7], C[3] }, Right * -1.0, Material);
            Polygon({ C[3], C[7], C[6], C[2] }, Up, Material);
            Polygon({ C[0], C[1], C[5], C[4] }, Up * -1.0, Material);
        }

        void Finish()
        {
            Mesh.Positions = Points;
        }

    private:
        void Emit(int A, int B, int C, int Material)
        {
            if (A == B || B == C || A == C)
            {
                return;
            }
            // (A, B, C) is counter-clockwise about the outward normal; Unreal's front face is the
            // reverse, (C - A) x (B - A).
            FTriangle Tri;
            Tri.A = A;
            Tri.B = C;
            Tri.C = B;
            Tri.Material = Material;
            Mesh.Triangles.push_back(Tri);
        }

        struct FKeyHash
        {
            size_t operator()(const std::tuple<int64_t, int64_t, int64_t>& K) const
            {
                const uint64_t A = static_cast<uint64_t>(std::get<0>(K)) * 0x9E3779B97F4A7C15ull;
                const uint64_t B = static_cast<uint64_t>(std::get<1>(K)) * 0xC2B2AE3D27D4EB4Full;
                const uint64_t C = static_cast<uint64_t>(std::get<2>(K)) * 0x165667B19E3779F9ull;
                return static_cast<size_t>(A ^ (B >> 1) ^ (C << 1));
            }
        };

        FBuildingMesh& Mesh;
        std::vector<FVec3> Points;
        std::unordered_map<std::tuple<int64_t, int64_t, int64_t>, int, FKeyHash> Index;
    };

    // ---------------------------------------------------------------------------------------------
    // Weighted straight skeleton. Every edge line moves inward at its own speed (0 keeps it in
    // place); the roof height over a point is Z0 + Slope * (time the wavefront reached it).

    struct FEdgeLine
    {
        FVec2 Dir;
        FVec2 Normal;
        double Offset = 0.0;
        double Speed = 1.0;
    };

    struct FNode
    {
        FVec2 P;
        double T = 0.0;
    };

    struct FWave
    {
        FVec2 P0;
        double T0 = 0.0;
        FVec2 V;
        int Node = 0;
        int In = 0;
        int Out = 0;
    };

    struct FSkeleton
    {
        std::vector<FEdgeLine> Lines;
        std::vector<FNode> Nodes;
        std::vector<std::vector<std::pair<int, int>>> FaceSegments;
        std::vector<std::vector<int>> Caps;
        // Ordered node cycle per face, starting with the edge's own start and end corner.
        std::vector<std::vector<int>> Faces;
        std::vector<int> Corner;
        std::string Error;
    };

    class FSkeletonSolver
    {
    public:
        FSkeletonSolver(const std::vector<FVec2>& Outline, const std::vector<double>& Speeds, double TimeCap)
            : Cap(TimeCap)
        {
            const size_t N = Outline.size();
            Out.Lines.resize(N);
            Out.FaceSegments.resize(N);
            for (size_t I = 0; I < N; ++I)
            {
                FEdgeLine& Line = Out.Lines[I];
                Line.Dir = Normalize(Outline[(I + 1) % N] - Outline[I]);
                Line.Normal = { -Line.Dir.Y, Line.Dir.X };
                Line.Offset = Dot(Line.Normal, Outline[I]);
                Line.Speed = Speeds[I];
            }
            std::vector<FWave> Loop;
            for (size_t I = 0; I < N; ++I)
            {
                FWave W;
                W.P0 = Outline[I];
                W.T0 = 0.0;
                W.In = static_cast<int>((I + N - 1) % N);
                W.Out = static_cast<int>(I);
                W.V = Velocity(W.In, W.Out);
                W.Node = NodeAt(Outline[I], 0.0);
                Out.Corner.push_back(W.Node);
                Loop.push_back(W);
            }
            Loops.push_back(Loop);
        }

        bool Solve()
        {
            int Guard = 20000;
            while (!Loops.empty())
            {
                if (--Guard < 0)
                {
                    Out.Error = "skeleton did not finish";
                    return false;
                }
                CollapseDegenerate();
                if (Loops.empty())
                {
                    break;
                }
                FEvent Event = Next();
                if (!Event.bValid || Event.T >= Cap)
                {
                    Time = Cap;
                    for (const std::vector<FWave>& Loop : Loops)
                    {
                        CapLoop(Loop);
                    }
                    Loops.clear();
                    break;
                }
                Time = std::max(Time, Event.T);
                if (Event.bSplit)
                {
                    Split(Event);
                }
                else
                {
                    Merge(Event);
                }
            }
            return BuildFaces();
        }

        FSkeleton Out;

    private:
        struct FEvent
        {
            bool bValid = false;
            bool bSplit = false;
            double T = 0.0;
            size_t Loop = 0;
            size_t Vertex = 0;
            size_t EdgeStart = 0;
        };

        FVec2 Velocity(int In, int OutEdge) const
        {
            const FEdgeLine& A = Out.Lines[In];
            const FEdgeLine& B = Out.Lines[OutEdge];
            const double Det = Cross(A.Normal, B.Normal);
            if (std::fabs(Det) > 1.0e-9)
            {
                return { (A.Speed * B.Normal.Y - B.Speed * A.Normal.Y) / Det, (A.Normal.X * B.Speed - B.Normal.X * A.Speed) / Det };
            }
            if (Dot(A.Normal, B.Normal) > 0.0)
            {
                return A.Normal * (0.5 * (A.Speed + B.Speed));
            }
            return {};
        }

        FVec2 Position(const FWave& W, double T) const { return W.P0 + W.V * (T - W.T0); }

        int NodeAt(const FVec2& P, double T)
        {
            for (size_t I = 0; I < Out.Nodes.size(); ++I)
            {
                const FNode& Node = Out.Nodes[I];
                if (Length(Node.P - P) < 1.0e-6 && std::fabs(Node.T - T) < 1.0e-6)
                {
                    return static_cast<int>(I);
                }
            }
            Out.Nodes.push_back({ P, T });
            return static_cast<int>(Out.Nodes.size() - 1);
        }

        void Segment(int Face, int A, int B)
        {
            if (A != B)
            {
                Out.FaceSegments[Face].push_back({ A, B });
            }
        }

        void Arc(const FWave& W, int Node)
        {
            Segment(W.In, W.Node, Node);
            Segment(W.Out, W.Node, Node);
        }

        FEvent Next() const
        {
            FEvent Best;
            for (size_t L = 0; L < Loops.size(); ++L)
            {
                const std::vector<FWave>& Loop = Loops[L];
                const size_t N = Loop.size();
                for (size_t I = 0; I < N; ++I)
                {
                    const FWave& A = Loop[I];
                    const FWave& B = Loop[(I + 1) % N];
                    const FVec2 Dir = Out.Lines[A.Out].Dir;
                    const double Gap = Dot(Position(B, Time) - Position(A, Time), Dir);
                    const double Rate = Dot(B.V - A.V, Dir);
                    double T = -1.0;
                    if (Length(Position(B, Time) - Position(A, Time)) < 1.0e-7)
                    {
                        T = Time;
                    }
                    else if (Rate < -1.0e-12)
                    {
                        T = Time + std::max(Gap, 0.0) / -Rate;
                    }
                    if (T >= 0.0 && (!Best.bValid || T < Best.T - 1.0e-12 || (T < Best.T + 1.0e-12 && Best.bSplit)))
                    {
                        Best.bValid = true;
                        Best.bSplit = false;
                        Best.T = T;
                        Best.Loop = L;
                        Best.EdgeStart = I;
                    }
                }
                for (size_t V = 0; V < N; ++V)
                {
                    const FWave& W = Loop[V];
                    if (Cross(Out.Lines[W.In].Dir, Out.Lines[W.Out].Dir) > -1.0e-9)
                    {
                        continue;
                    }
                    const FVec2 Here = Position(W, Time);
                    for (size_t K = 0; K < N; ++K)
                    {
                        if (K == V || (K + 1) % N == V)
                        {
                            continue;
                        }
                        const FWave& A = Loop[K];
                        const FWave& B = Loop[(K + 1) % N];
                        const FEdgeLine& Line = Out.Lines[A.Out];
                        const double Distance = Dot(Line.Normal, Here) - (Line.Offset + Line.Speed * Time);
                        const double Approach = Line.Speed - Dot(Line.Normal, W.V);
                        if (Approach <= 1.0e-12 || Distance < -1.0e-7)
                        {
                            continue;
                        }
                        const double T = Time + std::max(Distance, 0.0) / Approach;
                        if (Best.bValid && T >= Best.T)
                        {
                            continue;
                        }
                        const FVec2 Hit = Position(W, T);
                        const FVec2 PA = Position(A, T);
                        const FVec2 PB = Position(B, T);
                        const double Along = Dot(Hit - PA, Line.Dir);
                        const double Span = Dot(PB - PA, Line.Dir);
                        if (Span < -1.0e-9 || Along < -1.0e-6 || Along > Span + 1.0e-6)
                        {
                            continue;
                        }
                        Best.bValid = true;
                        Best.bSplit = true;
                        Best.T = T;
                        Best.Loop = L;
                        Best.Vertex = V;
                        Best.EdgeStart = K;
                    }
                }
            }
            return Best;
        }

        void Merge(const FEvent& Event)
        {
            std::vector<FWave>& Loop = Loops[Event.Loop];
            const size_t N = Loop.size();
            const size_t I = Event.EdgeStart;
            const size_t J = (I + 1) % N;
            const FVec2 P = (Position(Loop[I], Time) + Position(Loop[J], Time)) * 0.5;
            const int Node = NodeAt(P, Time);
            Arc(Loop[I], Node);
            Arc(Loop[J], Node);
            FWave W;
            W.P0 = P;
            W.T0 = Time;
            W.In = Loop[I].In;
            W.Out = Loop[J].Out;
            W.V = Velocity(W.In, W.Out);
            W.Node = Node;
            Loop[I] = W;
            Loop.erase(Loop.begin() + static_cast<std::ptrdiff_t>(J));
        }

        void Split(const FEvent& Event)
        {
            const std::vector<FWave> Loop = Loops[Event.Loop];
            const size_t N = Loop.size();
            const size_t V = Event.Vertex;
            const size_t K = Event.EdgeStart;
            const FWave& Reflex = Loop[V];
            const int Edge = Loop[K].Out;
            const FVec2 P = Position(Reflex, Time);
            const int Node = NodeAt(P, Time);
            Arc(Reflex, Node);

            FWave First;
            First.P0 = P;
            First.T0 = Time;
            First.In = Reflex.In;
            First.Out = Edge;
            First.V = Velocity(First.In, First.Out);
            First.Node = Node;
            FWave Second = First;
            Second.In = Edge;
            Second.Out = Reflex.Out;
            Second.V = Velocity(Second.In, Second.Out);

            std::vector<FWave> A{ First };
            for (size_t I = (K + 1) % N; I != V; I = (I + 1) % N)
            {
                A.push_back(Loop[I]);
            }
            std::vector<FWave> B{ Second };
            for (size_t I = (V + 1) % N; I != (K + 1) % N; I = (I + 1) % N)
            {
                B.push_back(Loop[I]);
            }
            Loops.erase(Loops.begin() + static_cast<std::ptrdiff_t>(Event.Loop));
            Loops.push_back(A);
            Loops.push_back(B);
        }

        double LoopArea(const std::vector<FWave>& Loop) const
        {
            std::vector<FVec2> Points;
            for (const FWave& W : Loop)
            {
                Points.push_back(Position(W, Time));
            }
            return SignedArea(Points);
        }

        void CollapseDegenerate()
        {
            for (size_t L = 0; L < Loops.size();)
            {
                const std::vector<FWave>& Loop = Loops[L];
                if (Loop.size() >= 3 && LoopArea(Loop) > 1.0e-8)
                {
                    ++L;
                    continue;
                }
                // The loop has closed up into a ridge (or a point): its sides meet along a line.
                std::vector<int> Nodes;
                for (const FWave& W : Loop)
                {
                    const int Node = NodeAt(Position(W, Time), Time);
                    Arc(W, Node);
                    Nodes.push_back(Node);
                }
                for (size_t I = 0; I < Loop.size(); ++I)
                {
                    ChainSegment(Loop[I].Out, Nodes[I], Nodes[(I + 1) % Loop.size()], Nodes);
                }
                Loops.erase(Loops.begin() + static_cast<std::ptrdiff_t>(L));
            }
        }

        // Adds A-B to a face, split at any of Candidates lying on it, so the faces on both sides of a
        // ridge share every vertex.
        void ChainSegment(int Face, int A, int B, const std::vector<int>& Candidates)
        {
            if (A == B)
            {
                return;
            }
            const FVec2 PA = Out.Nodes[A].P;
            const FVec2 PB = Out.Nodes[B].P;
            const double Len = Length(PB - PA);
            std::vector<std::pair<double, int>> Inside;
            for (const int C : Candidates)
            {
                if (C == A || C == B)
                {
                    continue;
                }
                const FVec2 PC = Out.Nodes[C].P;
                const double S = Len > 1.0e-12 ? Dot(PC - PA, PB - PA) / (Len * Len) : 0.0;
                const FVec2 Foot = PA + (PB - PA) * S;
                if (S > 1.0e-9 && S < 1.0 - 1.0e-9 && Length(PC - Foot) < 1.0e-6)
                {
                    Inside.push_back({ S, C });
                }
            }
            std::sort(Inside.begin(), Inside.end());
            int Prev = A;
            for (const auto& Item : Inside)
            {
                if (Item.second != Prev)
                {
                    Segment(Face, Prev, Item.second);
                    Prev = Item.second;
                }
            }
            Segment(Face, Prev, B);
        }

        void CapLoop(const std::vector<FWave>& Loop)
        {
            std::vector<int> Nodes;
            for (const FWave& W : Loop)
            {
                const int Node = NodeAt(Position(W, Time), Time);
                Arc(W, Node);
                if (Nodes.empty() || Nodes.back() != Node)
                {
                    Nodes.push_back(Node);
                }
            }
            while (Nodes.size() > 1 && Nodes.front() == Nodes.back())
            {
                Nodes.pop_back();
            }
            for (size_t I = 0; I < Loop.size(); ++I)
            {
                const int A = NodeAt(Position(Loop[I], Time), Time);
                const int B = NodeAt(Position(Loop[(I + 1) % Loop.size()], Time), Time);
                Segment(Loop[I].Out, A, B);
            }
            if (Nodes.size() >= 3)
            {
                Out.Caps.push_back(Nodes);
            }
        }

        bool BuildFaces()
        {
            const size_t N = Out.Lines.size();
            Out.Faces.assign(N, {});
            for (size_t E = 0; E < N; ++E)
            {
                // Segments that appear twice in one face are a fold of zero width; they cancel.
                std::map<std::pair<int, int>, int> Count;
                for (const auto& S : Out.FaceSegments[E])
                {
                    const std::pair<int, int> Key(std::min(S.first, S.second), std::max(S.first, S.second));
                    ++Count[Key];
                }
                const int Start = Out.Corner[E];
                const int End = Out.Corner[(E + 1) % N];
                std::multimap<int, int> Links;
                for (const auto& Item : Count)
                {
                    if (Item.second % 2 == 1)
                    {
                        Links.emplace(Item.first.first, Item.first.second);
                        Links.emplace(Item.first.second, Item.first.first);
                    }
                }
                auto Take = [&Links](int From, int To)
                {
                    for (auto It = Links.lower_bound(From); It != Links.end() && It->first == From; ++It)
                    {
                        if (It->second == To)
                        {
                            Links.erase(It);
                            return;
                        }
                    }
                };
                std::vector<int>& Face = Out.Faces[E];
                Face.push_back(Start);
                Face.push_back(End);
                int Prev = Start;
                int Current = End;
                int Guard = static_cast<int>(Count.size()) + 4;
                while (Current != Start && Guard-- > 0)
                {
                    int NextNode = -1;
                    int Choices = 0;
                    for (auto It = Links.lower_bound(Current); It != Links.end() && It->first == Current; ++It)
                    {
                        if (It->second != Prev || Choices == 0)
                        {
                            if (NextNode < 0 || It->second != Prev)
                            {
                                NextNode = It->second;
                            }
                        }
                        ++Choices;
                    }
                    if (NextNode < 0)
                    {
                        Out.Error = "face " + std::to_string(E) + " is not closed";
                        return false;
                    }
                    if (Choices > 2)
                    {
                        Out.Error = "face " + std::to_string(E) + " branches";
                        return false;
                    }
                    Take(Current, NextNode);
                    Take(NextNode, Current);
                    Prev = Current;
                    Current = NextNode;
                    if (Current != Start)
                    {
                        Face.push_back(Current);
                    }
                }
                if (Current != Start)
                {
                    Out.Error = "face " + std::to_string(E) + " does not return";
                    return false;
                }
                // Every segment of the face must be on its boundary.
                size_t Left = 0;
                for (const auto& Item : Links)
                {
                    if (!(Item.first == Start && Item.second == End) && !(Item.first == End && Item.second == Start))
                    {
                        ++Left;
                    }
                }
                if (Left > 0)
                {
                    Out.Error = "face " + std::to_string(E) + " has stray segments";
                    return false;
                }
            }
            return true;
        }

        double Cap;
        double Time = 0.0;
        std::vector<std::vector<FWave>> Loops;
    };

    // ---------------------------------------------------------------------------------------------
    // Building assembly.

    struct FRoofSetup
    {
        std::vector<double> Speed;
        std::vector<double> Offset;
        double Slope = 0.0;
        double Z0 = 0.0;
        double TimeCap = 0.0;
        bool bParapet = false;
    };

    // Moves each edge outward by its offset and mitres the corners. On failure Bad is a corner whose
    // mitre is too long, or the first corner of an edge that turned around or crosses another.
    bool OffsetOutline(const std::vector<FVec2>& Outline, const std::vector<double>& Offset, std::vector<FVec2>& Result, size_t* Bad = nullptr)
    {
        const size_t N = Outline.size();
        auto Fail = [Bad](size_t Corner)
        {
            if (Bad)
            {
                *Bad = Corner;
            }
            return false;
        };
        Result.assign(N, {});
        for (size_t I = 0; I < N; ++I)
        {
            const size_t Prev = (I + N - 1) % N;
            const FVec2 D0 = Normalize(Outline[I] - Outline[Prev]);
            const FVec2 D1 = Normalize(Outline[(I + 1) % N] - Outline[I]);
            // Outward normals of a counter-clockwise outline point right of the edge.
            const FVec2 N0{ D0.Y, -D0.X };
            const FVec2 N1{ D1.Y, -D1.X };
            const double C0 = Dot(N0, Outline[I]) + Offset[Prev];
            const double C1 = Dot(N1, Outline[I]) + Offset[I];
            const double Det = Cross(N0, N1);
            if (std::fabs(Det) < 1.0e-6)
            {
                Result[I] = Outline[I] + N1 * Offset[I];
            }
            else
            {
                Result[I] = { (C0 * N1.Y - C1 * N0.Y) / Det, (N0.X * C1 - N1.X * C0) / Det };
            }
            if (Length(Result[I] - Outline[I]) > 4.0 * Overhang + 1.0e-6)
            {
                return Fail(I);
            }
        }
        for (size_t I = 0; I < N; ++I)
        {
            const FVec2 A = Outline[(I + 1) % N] - Outline[I];
            const FVec2 B = Result[(I + 1) % N] - Result[I];
            if (Dot(A, B) <= 0.25 * Dot(A, A))
            {
                return Fail(Length(Result[I] - Outline[I]) > Length(Result[(I + 1) % N] - Outline[(I + 1) % N]) ? I : (I + 1) % N);
            }
        }
        for (size_t I = 0; I < N; ++I)
        {
            for (size_t J = I + 2; J < N; ++J)
            {
                if (I == 0 && J == N - 1)
                {
                    continue;
                }
                if (SegmentsCross(Result[I], Result[(I + 1) % N], Result[J], Result[(J + 1) % N]))
                {
                    return Fail(I);
                }
            }
        }
        return SignedArea(Result) > 0.0 || Fail(0);
    }

    // Takes the overhang off both edges at each corner that cannot be mitred, until the eave outline
    // is simple. Edges without overhang run their wall straight up to the roof edge.
    bool FitOverhangs(const std::vector<FVec2>& Outline, std::vector<double>& Offset)
    {
        const size_t N = Outline.size();
        std::vector<FVec2> Eave;
        for (size_t Pass = 0; Pass <= N; ++Pass)
        {
            size_t Bad = 0;
            if (OffsetOutline(Outline, Offset, Eave, &Bad))
            {
                return true;
            }
            Offset[Bad] = 0.0;
            Offset[(Bad + N - 1) % N] = 0.0;
        }
        std::fill(Offset.begin(), Offset.end(), 0.0);
        return OffsetOutline(Outline, Offset, Eave);
    }

    void AddOpenings(double Width, double Height, bool bDoor, std::vector<std::array<double, 5>>& Out)
    {
        // Each opening: U0, U1, V0, V1, door (1) or window (0); U along the wall, V above the ground.
        if (Height < 2.4 || Width < 2.2)
        {
            return;
        }
        const int Storeys = std::clamp(static_cast<int>(std::lround(Height / 3.2)), 1, 6);
        const double Storey = Height / Storeys;
        const int Count = std::clamp(static_cast<int>(std::floor(Width / 3.2)), 1, 12);
        const double Step = Width / (Count + 1);
        for (int S = 0; S < Storeys; ++S)
        {
            if (S == 0 && bDoor)
            {
                const double Top = std::min(2.15 + SocleTop, Storey - 0.4);
                if (Top - SocleTop > 1.7)
                {
                    Out.push_back({ Width * 0.5 - 0.55, Width * 0.5 + 0.55, SocleTop + 0.005, Top, 1.0 });
                }
                continue;
            }
            const double Sill = Storey * S + (Storey - 1.4) * 0.45;
            if (Sill < 0.7 || Sill + 1.4 > Height - 0.3)
            {
                continue;
            }
            for (int I = 0; I < Count; ++I)
            {
                const double Centre = Step * (I + 1);
                Out.push_back({ Centre - 0.55, Centre + 0.55, Sill, Sill + 1.4, 0.0 });
            }
        }
    }

    class FAssembler
    {
    public:
        FAssembler(const FFootprint& InFootprint, FBuildingMesh& InMesh) : Footprint(InFootprint), Mesh(InMesh), Builder(InMesh) {}

        bool Build(const FRoofSetup& Setup, std::string& Error)
        {
            const std::vector<FVec2>& P = Footprint.Outline;
            const size_t N = P.size();
            std::vector<FVec2> Eave;
            if (!OffsetOutline(P, Setup.Offset, Eave))
            {
                Error = "eave outline is not simple";
                return false;
            }
            double Reach = Setup.TimeCap;
            FSkeletonSolver Solver(Eave, Setup.Speed, Reach);
            if (!Solver.Solve())
            {
                Error = Solver.Out.Error;
                return false;
            }
            const FSkeleton& S = Solver.Out;
            auto Height = [&](int Node) { return Setup.Z0 + Setup.Slope * S.Nodes[Node].T; };
            auto Point = [&](int Node) { return At(S.Nodes[Node].P, Height(Node)); };

            const double Soffit = Setup.bParapet ? Setup.Z0 : Setup.Z0 - Fascia;
            bool bOk = true;

            // Floor.
            {
                std::vector<FVec3> Floor;
                for (const FVec2& C : P)
                {
                    Floor.push_back(At(C, Footprint.Base));
                }
                bOk &= Builder.Polygon(Floor, { 0.0, 0.0, -1.0 }, Plaster);
            }

            // Roof faces: sloped faces are tiles; faces of edges that do not move are vertical plaster.
            for (size_t E = 0; E < N; ++E)
            {
                const FEdgeLine& Line = S.Lines[E];
                std::vector<FVec3> Face;
                for (const int Node : S.Faces[E])
                {
                    Face.push_back(Point(Node));
                }
                const FVec3 Outward = Normalize(FVec3{ -Setup.Slope * Line.Normal.X, -Setup.Slope * Line.Normal.Y, Line.Speed });
                const bool bVertical = Line.Speed < 1.0e-9;
                const int Material = bVertical || Setup.bParapet ? Plaster : RoofTile;
                bOk &= Builder.Polygon(Face, bVertical ? FVec3{ -Line.Normal.X, -Line.Normal.Y, 0.0 } : Outward, Material);
            }

            // What is left at the height cap: a flat top, or a parapet's inner wall and the flat roof.
            for (const std::vector<int>& CapNodes : S.Caps)
            {
                std::vector<FVec3> Top;
                for (const int Node : CapNodes)
                {
                    Top.push_back(Point(Node));
                }
                if (!Setup.bParapet)
                {
                    bOk &= Builder.Polygon(Top, { 0.0, 0.0, 1.0 }, RoofTile);
                    continue;
                }
                const double Floor = Setup.Z0 - ParapetHeight;
                std::vector<FVec3> Deck;
                for (size_t I = 0; I < CapNodes.size(); ++I)
                {
                    const FVec2 A = S.Nodes[CapNodes[I]].P;
                    const FVec2 B = S.Nodes[CapNodes[(I + 1) % CapNodes.size()]].P;
                    const FVec2 D = Normalize(B - A);
                    // The cap loop runs counter-clockwise, so the courtyard side of the parapet faces
                    // its inward normal.
                    bOk &= Builder.Polygon({ At(A, Setup.Z0), At(A, Floor), At(B, Floor), At(B, Setup.Z0) }, { -D.Y, D.X, 0.0 }, Plaster);
                    Deck.push_back(At(A, Floor));
                }
                bOk &= Builder.Polygon(Deck, { 0.0, 0.0, 1.0 }, FlatRoof);
            }

            // Walls, soffits and fascias, one outline edge at a time.
            size_t Longest = N;
            double LongestLength = 0.0;
            for (size_t I = 0; I < N; ++I)
            {
                const double L = Length(P[(I + 1) % N] - P[I]);
                if (!Footprint.Party[I] && L > LongestLength)
                {
                    LongestLength = L;
                    Longest = I;
                }
            }
            for (size_t I = 0; I < N; ++I)
            {
                const size_t J = (I + 1) % N;
                const size_t H = (I + N - 1) % N;
                const FVec2 A = P[I];
                const FVec2 B = P[J];
                const FVec2 EA = Eave[I];
                const FVec2 EB = Eave[J];
                const FVec2 D = Normalize(B - A);
                const FVec3 Outward{ D.Y, -D.X, 0.0 };
                std::vector<FVec3> Wall{ At(A, Footprint.Base), At(B, Footprint.Base) };
                if (Setup.Offset[I] > 1.0e-9)
                {
                    Wall.push_back(At(B, Soffit));
                    Wall.push_back(At(A, Soffit));
                    bOk &= Builder.Polygon(Wall, Outward, Plaster);
                    bOk &= Builder.Polygon({ At(A, Soffit), At(B, Soffit), At(EB, Soffit), At(EA, Soffit) }, { 0.0, 0.0, -1.0 }, Plaster);
                    bOk &= Builder.Polygon({ At(EA, Soffit), At(EB, Soffit), At(EB, Setup.Z0), At(EA, Setup.Z0) }, Outward, RoofTile);
                }
                else
                {
                    // This wall runs straight up to the roof edge. Where a neighbouring edge has an
                    // overhang, the end of that eave is closed in this wall's plane.
                    if (Setup.Offset[J] > 1.0e-9)
                    {
                        Wall.push_back(At(B, Soffit));
                        Wall.push_back(At(EB, Soffit));
                    }
                    Wall.push_back(At(EB, Setup.Z0));
                    Wall.push_back(At(EA, Setup.Z0));
                    if (Setup.Offset[H] > 1.0e-9)
                    {
                        Wall.push_back(At(EA, Soffit));
                        Wall.push_back(At(A, Soffit));
                    }
                    bOk &= Builder.Polygon(Wall, Outward, Plaster);
                }
                if (!Footprint.Party[I] && !Setup.bParapet)
                {
                    AddFacade(A, B, Soffit, I == Longest);
                }
                else if (!Footprint.Party[I])
                {
                    AddFacade(A, B, Setup.Z0 - ParapetHeight, I == Longest);
                }
            }
            // Façade bands round the whole outline: a darker plinth from the bottom of the wall, a
            // light string course over the ground floor and a cornice under the eaves or the deck.
            {
                const double Top = Setup.bParapet ? Setup.Z0 - ParapetHeight : Soffit;
                const double Rise = Top - Footprint.Ground;
                const int Storeys = std::clamp(static_cast<int>(std::lround(Rise / 3.2)), 1, 6);
                if (Footprint.Ground + SocleTop > Footprint.Base + 0.05)
                {
                    bOk &= Band(Footprint.Base, Footprint.Ground + SocleTop, SocleOut, Socle);
                }
                if (Storeys >= 2 && Rise >= 4.8)
                {
                    const double Course = Footprint.Ground + Rise / Storeys - 0.06;
                    bOk &= Band(Course, Course + CourseHeight, CourseOut, Trim);
                }
                if (Rise >= 2.4)
                {
                    bOk &= Band(Top - CorniceDrop - CorniceHeight, Top - CorniceDrop, CorniceOut, Trim);
                }
            }
            for (size_t E = 0; E < N; ++E)
            {
                Mesh.Gables += S.Lines[E].Speed < 1.0e-9 && !Footprint.Party[E] ? 1 : 0;
            }
            Builder.Finish();
            if (!bOk)
            {
                Error = "a polygon could not be triangulated";
            }
            return bOk;
        }

    private:
        // One band from Z0 to Z1: a closed ring round the outline, Out proud of the free walls and
        // BandIn into them, mitred at the corners. Skipped (true) where the mitres do not fit.
        bool Band(double Z0, double Z1, double Out, int Material)
        {
            const std::vector<FVec2>& P = Footprint.Outline;
            const size_t N = P.size();
            std::vector<double> OuterOffset(N);
            for (size_t I = 0; I < N; ++I)
            {
                OuterOffset[I] = Footprint.Party[I] ? PartyBandOut : Out;
            }
            std::vector<FVec2> Outer;
            std::vector<FVec2> Inner;
            if (!OffsetOutline(P, OuterOffset, Outer) || !OffsetOutline(P, std::vector<double>(N, -BandIn), Inner))
            {
                return true;
            }
            const FVec3 Up{ 0.0, 0.0, 1.0 };
            bool bOk = true;
            for (size_t I = 0; I < N; ++I)
            {
                const size_t J = (I + 1) % N;
                const FVec2 D = Normalize(P[J] - P[I]);
                const FVec3 Outward{ D.Y, -D.X, 0.0 };
                bOk &= Builder.Polygon({ At(Outer[I], Z0), At(Outer[J], Z0), At(Outer[J], Z1), At(Outer[I], Z1) }, Outward, Material);
                bOk &= Builder.Polygon({ At(Inner[J], Z0), At(Inner[I], Z0), At(Inner[I], Z1), At(Inner[J], Z1) }, Outward * -1.0, Material);
                bOk &= Builder.Polygon({ At(Inner[I], Z1), At(Outer[I], Z1), At(Outer[J], Z1), At(Inner[J], Z1) }, Up, Material);
                bOk &= Builder.Polygon({ At(Inner[I], Z0), At(Inner[J], Z0), At(Outer[J], Z0), At(Outer[I], Z0) }, Up * -1.0, Material);
            }
            Mesh.Bands += 1;
            return bOk;
        }

        void AddFacade(const FVec2& A, const FVec2& B, double Top, bool bDoor)
        {
            const double Width = Length(B - A);
            const FVec2 D = Normalize(B - A);
            const FVec2 Outside{ D.Y, -D.X };
            std::vector<std::array<double, 5>> Openings;
            AddOpenings(Width, Top - Footprint.Ground, bDoor, Openings);
            auto Corner = [&](double U, double Z, double W) { return At(A + D * U + Outside * W, Footprint.Ground + Z); };
            auto Box = [&](double U0, double U1, double V0, double V1, double W0, double W1, int Material)
            {
                const FVec3 C[8] = {
                    Corner(U0, V0, W0), Corner(U1, V0, W0), Corner(U1, V1, W0), Corner(U0, V1, W0),
                    Corner(U0, V0, W1), Corner(U1, V0, W1), Corner(U1, V1, W1), Corner(U0, V1, W1)
                };
                Builder.Box(C, Material);
            };
            for (const auto& O : Openings)
            {
                const double U0 = O[0];
                const double U1 = O[1];
                const double V0 = std::max(O[2], Footprint.Base - Footprint.Ground);
                const double V1 = O[3];
                const bool bIsDoor = O[4] > 0.5;
                constexpr double Bar = 0.07;
                constexpr double Back = -0.03;
                // Every part is its own closed solid set into the wall; parts overlap by a centimetre
                // instead of touching, so no two of them share a face.
                if (bIsDoor)
                {
                    Box(U0, U0 + Bar, V0, V1, Back, 0.08, Frame);
                    Box(U1 - Bar, U1, V0, V1, Back, 0.08, Frame);
                    Box(U0 + Bar - 0.01, U1 - Bar + 0.01, V1 - Bar, V1, Back, 0.08, Frame);
                    Box(U0 + Bar - 0.01, U1 - Bar + 0.01, V0, V1 - Bar + 0.01, Back, 0.03, Frame);
                    continue;
                }
                Ring(Corner, U0, U1, V0, V1, Bar, Back, 0.08);
                Box(U0 + Bar - 0.01, U1 - Bar + 0.01, V0 + Bar - 0.01, V1 - Bar + 0.01, Back, 0.02, Glass);
                Box(U0 - 0.06, U1 + 0.06, V0 - 0.06, V0 + 0.01, Back, 0.12, Frame);
                ++Mesh.Windows;
            }
        }

        // A window frame: a closed rectangular ring, Bar wide, from depth W0 to W1.
        template <typename FCorner>
        void Ring(const FCorner& Corner, double U0, double U1, double V0, double V1, double Bar, double W0, double W1)
        {
            const double Us[2][2] = { { U0, U1 }, { U0 + Bar, U1 - Bar } };
            const double Vs[2][2] = { { V0, V1 }, { V0 + Bar, V1 - Bar } };
            // Rectangle corners counter-clockwise seen from outside: bottom-left, bottom-right,
            // top-right, top-left; R 0 is the outer edge, 1 the inner; W 0 the back, 1 the front.
            auto P = [&](int R, int K, double W)
            {
                const double U = Us[R][K == 1 || K == 2 ? 1 : 0];
                const double V = Vs[R][K >= 2 ? 1 : 0];
                return Corner(U, V, W);
            };
            const FVec3 Front = Normalize(Cross(P(0, 1, W1) - P(0, 0, W1), P(0, 3, W1) - P(0, 0, W1)));
            const FVec3 Right = Normalize(P(0, 1, W1) - P(0, 0, W1));
            const FVec3 Up = Normalize(P(0, 3, W1) - P(0, 0, W1));
            const FVec3 Sides[4] = { Up * -1.0, Right, Up, Right * -1.0 };
            for (int K = 0; K < 4; ++K)
            {
                const int L = (K + 1) % 4;
                Builder.Polygon({ P(0, K, W1), P(0, L, W1), P(1, L, W1), P(1, K, W1) }, Front, Frame);
                Builder.Polygon({ P(0, K, W0), P(1, K, W0), P(1, L, W0), P(0, L, W0) }, Front * -1.0, Frame);
                Builder.Polygon({ P(0, K, W0), P(0, L, W0), P(0, L, W1), P(0, K, W1) }, Sides[K], Frame);
                Builder.Polygon({ P(1, K, W0), P(1, K, W1), P(1, L, W1), P(1, L, W0) }, Sides[K] * -1.0, Frame);
            }
        }

        const FFootprint& Footprint;
        FBuildingMesh& Mesh;
        FMeshBuilder Builder;
    };

    struct FProbe
    {
        bool bOk = false;
        // Latest wavefront time: how far in from the eave line the roof peaks.
        double Reach = 0.0;
        // Edges whose face is a triangle ending in a ridge: the short ends of a gabled roof.
        std::vector<size_t> Ends;
    };

    // The full, uncapped skeleton of the eave outline, to size the pitch and find gable ends.
    FProbe Probe(const std::vector<FVec2>& Eave, const std::vector<double>& Speeds)
    {
        FProbe Result;
        FSkeletonSolver Solver(Eave, Speeds, 1.0e6);
        if (!Solver.Solve())
        {
            return Result;
        }
        const FSkeleton& S = Solver.Out;
        Result.bOk = S.Caps.empty();
        for (const FNode& Node : S.Nodes)
        {
            Result.Reach = std::max(Result.Reach, Node.T);
        }
        std::vector<size_t>& Ends = Result.Ends;
        std::map<int, std::vector<int>> Links;
        for (const auto& Segments : S.FaceSegments)
        {
            for (const auto& Seg : Segments)
            {
                Links[Seg.first].push_back(Seg.second);
                Links[Seg.second].push_back(Seg.first);
            }
        }
        for (size_t E = 0; E < S.Faces.size(); ++E)
        {
            const std::vector<int>& Face = S.Faces[E];
            if (Face.size() != 3 || Speeds[E] < 1.0e-9)
            {
                continue;
            }
            const int Apex = Face[2];
            bool bRidge = false;
            for (const int Other : Links[Apex])
            {
                const FNode& A = S.Nodes[Apex];
                const FNode& B = S.Nodes[Other];
                if (Length(A.P - B.P) > 0.5 && std::fabs(A.T - B.T) < 0.2 * Length(A.P - B.P))
                {
                    bRidge = true;
                }
            }
            if (bRidge)
            {
                Ends.push_back(E);
            }
        }
        return Result;
    }
}

bool CleanOutline(std::vector<FVec2>& Outline)
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
            const double OutLen = Length(Next - Here);
            const double Turn = std::fabs(std::atan2(Cross(Here - Prev, Next - Here), Dot(Here - Prev, Next - Here)));
            // Drop repeated points, near-straight corners and spikes that fold back on themselves.
            if (In < 0.25 || Turn < 4.0 * Pi / 180.0 || Turn > 170.0 * Pi / 180.0)
            {
                Outline.erase(Outline.begin() + static_cast<std::ptrdiff_t>(I));
                bChanged = true;
                break;
            }
            (void)OutLen;
        }
    }
    return Outline.size() >= 3 && SignedArea(Outline) > 1.0 && IsSimple(Outline);
}

void FindPartyWalls(std::vector<FFootprint>& Footprints, double Tolerance)
{
    for (FFootprint& F : Footprints)
    {
        F.Party.assign(F.Outline.size(), false);
    }
    for (size_t I = 0; I < Footprints.size(); ++I)
    {
        FFootprint& F = Footprints[I];
        const size_t N = F.Outline.size();
        for (size_t E = 0; E < N; ++E)
        {
            const FVec2 A = F.Outline[E];
            const FVec2 B = F.Outline[(E + 1) % N];
            const double Len = Length(B - A);
            const FVec2 D = Normalize(B - A);
            double Covered = 0.0;
            for (size_t J = 0; J < Footprints.size(); ++J)
            {
                if (J == I)
                {
                    continue;
                }
                const std::vector<FVec2>& O = Footprints[J].Outline;
                for (size_t K = 0; K < O.size(); ++K)
                {
                    const FVec2 C = O[K];
                    const FVec2 G = O[(K + 1) % O.size()];
                    const FVec2 DO = Normalize(G - C);
                    // Shared walls run in opposite directions on two counter-clockwise outlines.
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
            F.Party[E] = Len > 1.0 && Covered > 0.7 * Len;
        }
    }
}

bool BuildBuilding(const FFootprint& Footprint, FBuildingMesh& Out, std::string& Error)
{
    const size_t N = Footprint.Outline.size();
    if (N < 3 || Footprint.Party.size() != N)
    {
        Error = "outline needs at least three points and a party flag per edge";
        return false;
    }
    const double Rise = std::clamp(Footprint.RoofRise, 1.0, 9.0);
    const double MinSlope = std::tan(18.0 * Pi / 180.0);
    const double MaxSlope = std::tan(55.0 * Pi / 180.0);
    const double DefaultSlope = std::tan(std::clamp(Footprint.PitchDegrees, 18.0, 55.0) * Pi / 180.0);

    // Party walls and gable ends do not move; they carry no overhang. The pitch makes the roof peak
    // at the measured rise; a block too wide for that keeps the lowest pitch and is cut flat on top.
    auto Pitched = [&](const std::vector<size_t>& Gables)
    {
        FRoofSetup Setup;
        Setup.Speed.assign(N, 1.0);
        Setup.Offset.assign(N, Overhang);
        for (size_t E = 0; E < N; ++E)
        {
            if (Footprint.Party[E])
            {
                Setup.Speed[E] = 0.0;
                Setup.Offset[E] = 0.0;
            }
        }
        for (const size_t E : Gables)
        {
            Setup.Speed[E] = 0.0;
            Setup.Offset[E] = 0.0;
        }
        FitOverhangs(Footprint.Outline, Setup.Offset);
        std::vector<FVec2> Eave;
        OffsetOutline(Footprint.Outline, Setup.Offset, Eave);
        const FProbe Shape = Probe(Eave, Setup.Speed);
        Setup.Slope = DefaultSlope;
        double Reach = MaxRoofReach;
        if (Shape.bOk && Shape.Reach > Overhang + 0.25)
        {
            Setup.Slope = std::clamp(Rise / (Shape.Reach - Overhang), MinSlope, MaxSlope);
            Reach = Shape.Reach - Overhang;
        }
        Setup.Z0 = Footprint.Eaves - Overhang * Setup.Slope;
        const double Allowed = std::min(Rise / Setup.Slope, MaxRoofReach);
        // A roof that reaches its ridge within the allowed rise is never cut.
        Setup.TimeCap = Overhang + (Reach <= Allowed + 1.0e-6 ? 1.0e6 : Allowed);
        return std::make_pair(Setup, Shape);
    };
    auto Flat = [&]()
    {
        FRoofSetup Setup;
        Setup.Slope = 0.0;
        Setup.Speed.assign(N, 1.0);
        Setup.Offset.assign(N, 0.0);
        Setup.Z0 = Footprint.Eaves;
        Setup.TimeCap = ParapetWidth;
        Setup.bParapet = true;
        return Setup;
    };
    auto Attempt = [&](const FRoofSetup& Setup, const char* Method)
    {
        FBuildingMesh Mesh;
        Mesh.RoofMethod = Method;
        FAssembler Assembler(Footprint, Mesh);
        std::string Why;
        if (!Assembler.Build(Setup, Why))
        {
            Error += std::string(Method) + ": " + Why + "; ";
            return false;
        }
        const FCheck Result = Check(Mesh, Footprint);
        if (!Result.bClosed || Result.Volume <= 0.0)
        {
            Error += std::string(Method) + ": not closed (" + std::to_string(Result.OpenEdges) + " open, "
                + std::to_string(Result.OverusedEdges) + " overused); ";
            return false;
        }
        for (size_t E = 0; E < N; ++E)
        {
            Mesh.PartyWalls += Footprint.Party[E] ? 1 : 0;
        }
        Out = Mesh;
        return true;
    };

    if (Footprint.Roof != ERoof::Flat)
    {
        const auto Hipped = Pitched({});
        if (Footprint.Roof == ERoof::Gabled && !Hipped.second.Ends.empty())
        {
            if (Attempt(Pitched(Hipped.second.Ends).first, "gabled"))
            {
                return true;
            }
        }
        if (Attempt(Hipped.first, "hipped"))
        {
            return true;
        }
        // Without overhangs the eave outline is the footprint itself, which is always simple.
        FRoofSetup Plain = Hipped.first;
        Plain.Offset.assign(N, 0.0);
        Plain.Z0 = Footprint.Eaves;
        Plain.TimeCap = Plain.TimeCap > 1.0e5 ? Plain.TimeCap : Plain.TimeCap - Overhang;
        if (Attempt(Plain, "hipped-no-overhang"))
        {
            return true;
        }
    }
    if (Attempt(Flat(), Footprint.Roof == ERoof::Flat ? "flat" : "flat-fallback"))
    {
        return true;
    }
    return false;
}

FCheck Check(const FBuildingMesh& Mesh, const FFootprint& Footprint)
{
    FCheck Result;
    const size_t NV = Mesh.Positions.size();
    // Strict: directed edges must pair up exactly.
    auto Count = [](const std::vector<std::array<int, 2>>& Edges, int& Open, int& Overused)
    {
        std::map<std::pair<int, int>, int> Directed;
        for (const auto& E : Edges)
        {
            ++Directed[{ E[0], E[1] }];
        }
        Open = 0;
        Overused = 0;
        for (const auto& Item : Directed)
        {
            const auto Back = Directed.find({ Item.first.second, Item.first.first });
            const int Reverse = Back == Directed.end() ? 0 : Back->second;
            if (Item.second > 1)
            {
                Overused += Item.second - 1;
            }
            if (Reverse < Item.second)
            {
                Open += Item.second - Reverse;
            }
        }
    };
    std::vector<std::array<int, 2>> Edges;
    for (const FTriangle& T : Mesh.Triangles)
    {
        // Edges in counter-clockwise order about the outward normal.
        Edges.push_back({ T.A, T.C });
        Edges.push_back({ T.C, T.B });
        Edges.push_back({ T.B, T.A });
    }
    int Unused = 0;
    Count(Edges, Result.StrictOpenEdges, Unused);
    Result.bStrict = Result.StrictOpenEdges == 0 && Unused == 0;

    // Geometric: split every edge at welded vertices lying on it, so T-junctions on a straight line
    // still count as covered.
    std::vector<std::array<int, 2>> Split;
    for (const auto& E : Edges)
    {
        const FVec3 A = Mesh.Positions[E[0]];
        const FVec3 B = Mesh.Positions[E[1]];
        const FVec3 AB = B - A;
        const double L2 = Dot(AB, AB);
        std::vector<std::pair<double, int>> On;
        for (size_t V = 0; V < NV; ++V)
        {
            if (static_cast<int>(V) == E[0] || static_cast<int>(V) == E[1])
            {
                continue;
            }
            const FVec3 P = Mesh.Positions[V];
            const double S = Dot(P - A, AB) / L2;
            if (S <= 1.0e-7 || S >= 1.0 - 1.0e-7)
            {
                continue;
            }
            if (Length(P - (A + AB * S)) < 2.0e-4)
            {
                On.push_back({ S, static_cast<int>(V) });
            }
        }
        std::sort(On.begin(), On.end());
        int Prev = E[0];
        for (const auto& Item : On)
        {
            Split.push_back({ Prev, Item.second });
            Prev = Item.second;
        }
        Split.push_back({ Prev, E[1] });
    }
    Count(Split, Result.OpenEdges, Result.OverusedEdges);
    Result.bClosed = Result.OpenEdges == 0;

    // Connected components and volume.
    std::vector<int> Parent(NV);
    for (size_t I = 0; I < NV; ++I)
    {
        Parent[I] = static_cast<int>(I);
    }
    auto Find = [&Parent](int X)
    {
        while (Parent[X] != X)
        {
            Parent[X] = Parent[Parent[X]];
            X = Parent[X];
        }
        return X;
    };
    std::vector<bool> Used(NV, false);
    for (const FTriangle& T : Mesh.Triangles)
    {
        Parent[Find(T.A)] = Find(T.B);
        Parent[Find(T.B)] = Find(T.C);
        Used[T.A] = Used[T.B] = Used[T.C] = true;
        const FVec3& A = Mesh.Positions[T.A];
        const FVec3& B = Mesh.Positions[T.C];
        const FVec3& C = Mesh.Positions[T.B];
        Result.Volume += Dot(A, Cross(B, C)) / 6.0;
        const double Projected = 0.5 * ((B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X));
        if (T.Material == RoofTile || T.Material == FlatRoof || (T.Material == Plaster && Projected > 0.0))
        {
            Result.RoofCover += std::max(Projected, 0.0);
        }
    }
    for (size_t I = 0; I < NV; ++I)
    {
        Result.Components += Used[I] && Find(static_cast<int>(I)) == static_cast<int>(I) ? 1 : 0;
    }
    Result.FootprintArea = SignedArea(Footprint.Outline);
    return Result;
}
}
