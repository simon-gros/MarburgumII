#include "MarburgumVehicleContentCommandlet.h"

#include "MarburgumGodotClip.h"

#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "CollisionQueryParams.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/PlayerStart.h"
#include "MarburgumVehicle.h"
#include "MarburgumVehicleContract.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSmoothStep.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MeshDescription.h"
#include "MeshDescriptionBuilder.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumVehicleContent, Log, All);

namespace MarburgumVehicleContent
{
    constexpr const TCHAR* CataloguePath = TEXT("Data/Gameplay/vehicles.v0.1.json");
    constexpr const TCHAR* MaterialPackage = TEXT("/Game/Vehicles/Materials/M_Vehicle");
    constexpr const TCHAR* LentMap = TEXT("/Game/World/lent");
    constexpr const TCHAR* VehicleFolder = TEXT("Marburgum/Vehicles");

    // ======================================================================================================
    // The bodywork: a port of the original src/shared/vehicle_mesh.cpp. Geometry is built exactly as the
    // original builds it, in the Godot vehicle frame (metres; x across, positive to the right; y up; z along,
    // the front at -z), and turned into Unreal centimetres and winding only when it is written out.
    // ======================================================================================================

    /** A part of a vehicle: its colour as picked from a photograph (sRGB), whether it takes the paint, its finish. */
    struct FPart
    {
        double Red;
        double Green;
        double Blue;
        bool bPainted;
        double Roughness;
        double Metallic;
    };

    // Paint on a 1990s car was solid rather than metallic more often than not, and by the time it was parked
    // on the Lent it had seen a few winters of road salt: satin, not showroom gloss.
    constexpr FPart Paint = { 1.0, 1.0, 1.0, true, 0.55, 0.0 };
    // Tinted a little green, as float glass is edge-on, and nearly black from outside in daylight.
    constexpr FPart Glass = { 0.07, 0.09, 0.10, false, 0.06, 0.0 };
    constexpr FPart Rubber = { 0.06, 0.06, 0.06, false, 0.92, 0.0 };
    // Plain pressed-steel wheels, sprayed silver-grey; alloy wheels were the exception.
    constexpr FPart Hub = { 0.50, 0.51, 0.51, false, 0.5, 0.45 };
    // Bumpers, rubbing strips and grilles were unpainted black plastic by the late 1980s.
    constexpr FPart Plastic = { 0.08, 0.08, 0.08, false, 0.72, 0.0 };
    constexpr FPart Underside = { 0.04, 0.04, 0.04, false, 1.0, 0.0 };
    constexpr FPart Headlamp = { 0.80, 0.81, 0.78, false, 0.18, 0.0 };
    constexpr FPart TailLamp = { 0.62, 0.07, 0.05, false, 0.25, 0.0 };
    constexpr FPart Indicator = { 0.78, 0.45, 0.12, false, 0.24, 0.0 };
    // Blank on purpose: a plate with characters on it would be a real registration or a made-up one.
    constexpr FPart Plate = { 0.84, 0.84, 0.81, false, 0.6, 0.0 };

    constexpr double BumperSetback = 0.06;
    constexpr double BumperDepth = 0.16;
    constexpr double BumperHeight = 0.2;
    constexpr double BumperWrap = 0.012;
    constexpr double Proud = 0.006;
    constexpr double GlassProud = 0.012;
    constexpr double WheelProud = 0.015;
    constexpr double ArchGap = 0.05;
    constexpr double ShoulderCut = 0.04;
    constexpr int32 ArchSteps = 6;
    constexpr int32 WheelSides = 10;

    /** A vehicle's side-elevation shape (VehicleShape), read from a definition's model.shape. */
    struct FShape
    {
        double Length = 4.2;
        double Width = 1.65;
        double Height = 1.4;
        double Clearance = 0.2;
        double Belt = 0.86;
        double Roof = 1.4;
        double BonnetFront = 0.7;
        double WindscreenBase = 1.1;
        double WindscreenTop = 1.75;
        double RoofEnd = 3.0;
        double RearGlassBase = 3.5;
        double TailTop = 0.9;
        double Inset = 0.08;
        double Tumblehome = 0.07;
        int32 SideWindows = 2;
        double APillar = 0.08;
        double BPillar = 0.1;
        double CPillar = 0.14;
        double WheelRadius = 0.3;
        double WheelWidth = 0.18;
        double RearWheelWidth = 0.18;
        double FrontAxle = 0.85;
        double RearAxle = 3.3;
        bool bCargo = false;
        double CargoStart = 0.0;
        double CargoEnd = 0.0;
        double CargoBottom = 0.0;
        double CargoTop = 0.0;
        double CargoWidth = 0.0;
    };

    double Number(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, double Default)
    {
        double Value = Default;
        return Object.IsValid() && Object->TryGetNumberField(Key, Value) ? Value : Default;
    }

    /** VehicleShape::from_archetype, then Vehicle::build_views: the axles go where the physics has its wheels. */
    FShape ShapeOf(const TSharedPtr<FJsonObject>& Definition)
    {
        FShape V;
        const double Length = Number(Definition, TEXT("body_length"), V.Length);
        V.Length = FMath::Max(1.5, Length);
        V.Width = FMath::Max(1.0, Number(Definition, TEXT("body_width"), V.Width));
        V.Height = FMath::Max(0.8, Number(Definition, TEXT("body_height"), V.Height));
        const TSharedPtr<FJsonObject>* Model = nullptr;
        const TSharedPtr<FJsonObject>* ShapeObject = nullptr;
        TSharedPtr<FJsonObject> Shape;
        if (Definition->TryGetObjectField(TEXT("model"), Model) && (*Model)->TryGetObjectField(TEXT("shape"), ShapeObject))
        {
            Shape = *ShapeObject;
        }
        V.Clearance = Number(Shape, TEXT("clearance"), V.Clearance);
        V.Belt = Number(Shape, TEXT("belt"), V.Belt);
        V.Roof = Number(Shape, TEXT("roof"), V.Height);
        V.BonnetFront = Number(Shape, TEXT("bonnet_front"), V.BonnetFront);
        V.WindscreenBase = Number(Shape, TEXT("windscreen_base"), V.WindscreenBase);
        V.WindscreenTop = Number(Shape, TEXT("windscreen_top"), V.WindscreenTop);
        V.RoofEnd = Number(Shape, TEXT("roof_end"), V.RoofEnd);
        V.RearGlassBase = Number(Shape, TEXT("rear_glass_base"), V.RearGlassBase);
        V.TailTop = Number(Shape, TEXT("tail_top"), V.TailTop);
        V.Inset = Number(Shape, TEXT("inset"), V.Inset);
        V.Tumblehome = Number(Shape, TEXT("tumblehome"), V.Tumblehome);
        V.SideWindows = static_cast<int32>(Number(Shape, TEXT("side_windows"), V.SideWindows));
        V.APillar = Number(Shape, TEXT("a_pillar"), V.APillar);
        V.BPillar = Number(Shape, TEXT("b_pillar"), V.BPillar);
        V.CPillar = Number(Shape, TEXT("c_pillar"), V.CPillar);
        V.WheelWidth = Number(Shape, TEXT("wheel_width"), V.WheelWidth);
        V.RearWheelWidth = Number(Shape, TEXT("rear_wheel_width"), V.WheelWidth);
        const TSharedPtr<FJsonObject>* Cargo = nullptr;
        if (Shape.IsValid() && Shape->TryGetObjectField(TEXT("cargo"), Cargo))
        {
            V.bCargo = true;
            V.CargoStart = Number(*Cargo, TEXT("start"), V.Length * 0.3);
            V.CargoEnd = Number(*Cargo, TEXT("end"), V.Length);
            V.CargoBottom = Number(*Cargo, TEXT("bottom"), V.Belt);
            V.CargoTop = Number(*Cargo, TEXT("top"), V.Height);
            V.CargoWidth = Number(*Cargo, TEXT("width"), V.Width);
        }
        const double Wheelbase = Number(Definition, TEXT("wheelbase"), 2.5);
        V.FrontAxle = Shape.IsValid() && Shape->HasField(TEXT("front_axle")) ? Number(Shape, TEXT("front_axle"), V.FrontAxle)
                                                                              : (Length - Wheelbase) / 2.0;
        V.RearAxle = V.FrontAxle + Wheelbase;
        V.WheelRadius = Number(Definition, TEXT("wheel_radius"), V.WheelRadius);
        return V;
    }

    /** A point of a side elevation: S metres back from the front bumper, Y metres up. */
    struct FStation
    {
        double S;
        double Y;
    };

    double HeightAlong(const TArray<FStation>& Line, double S)
    {
        if (Line.IsEmpty())
        {
            return 0.0;
        }
        if (S <= Line[0].S)
        {
            return Line[0].Y;
        }
        for (int32 Index = 1; Index < Line.Num(); ++Index)
        {
            const FStation& A = Line[Index - 1];
            const FStation& B = Line[Index];
            if (S <= B.S)
            {
                const double Run = B.S - A.S;
                return Run > 1e-6 ? FMath::Lerp(A.Y, B.Y, (S - A.S) / Run) : B.Y;
            }
        }
        return Line.Last().Y;
    }

    TArray<FStation> WithoutRepeats(const TArray<FStation>& Line)
    {
        TArray<FStation> Kept;
        for (const FStation& Station : Line)
        {
            if (!Kept.IsEmpty() && Station.S - Kept.Last().S < 0.005)
            {
                continue;
            }
            Kept.Add(Station);
        }
        return Kept;
    }

    /** Collected triangles in the Godot frame, each with the direction it is meant to face. */
    struct FShell
    {
        struct FCorner
        {
            FVector Position;
            FVector4f Colour;
            FVector2f Finish;
        };
        struct FTriangle
        {
            FCorner Corners[3];
            FVector Facing;
        };
        TArray<FTriangle> Triangles;
        FVector4f Tint = FVector4f(1, 1, 1, 1);
        FVector2f Finish = FVector2f(0.5f, 0.f);

        void Use(const FPart& Part)
        {
            // Colours are written as they would be picked from a photograph (sRGB) and turned linear once.
            const auto ToLinear = [](double C)
            {
                return C <= 0.04045 ? C / 12.92 : FMath::Pow((C + 0.055) / 1.055, 2.4);
            };
            Tint = FVector4f(static_cast<float>(ToLinear(Part.Red)), static_cast<float>(ToLinear(Part.Green)),
                static_cast<float>(ToLinear(Part.Blue)), Part.bPainted ? 1.f : 0.f);
            Finish = FVector2f(static_cast<float>(Part.Roughness), static_cast<float>(Part.Metallic));
        }

        void Triangle(const FVector& A, const FVector& B, const FVector& C, const FVector& Facing)
        {
            FTriangle& T = Triangles.AddDefaulted_GetRef();
            T.Corners[0] = { A, Tint, Finish };
            T.Corners[1] = { B, Tint, Finish };
            T.Corners[2] = { C, Tint, Finish };
            T.Facing = Facing;
        }

        void Quad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Facing)
        {
            Triangle(A, B, C, Facing);
            Triangle(A, C, D, Facing);
        }

        /** Shell::box: a block centred on Centre with half sizes Half, all six faces. */
        void Box(const FVector& Centre, const FVector& Half)
        {
            const auto At = [&](double X, double Y, double Z) { return Centre + FVector(Half.X * X, Half.Y * Y, Half.Z * Z); };
            Quad(At(-1, -1, -1), At(1, -1, -1), At(1, 1, -1), At(-1, 1, -1), FVector(0, 0, -1));
            Quad(At(1, -1, 1), At(-1, -1, 1), At(-1, 1, 1), At(1, 1, 1), FVector(0, 0, 1));
            Quad(At(-1, -1, 1), At(-1, -1, -1), At(-1, 1, -1), At(-1, 1, 1), FVector(-1, 0, 0));
            Quad(At(1, -1, -1), At(1, -1, 1), At(1, 1, 1), At(1, 1, -1), FVector(1, 0, 0));
            Quad(At(-1, 1, -1), At(1, 1, -1), At(1, 1, 1), At(-1, 1, 1), FVector(0, 1, 0));
            Quad(At(-1, -1, 1), At(1, -1, 1), At(1, -1, -1), At(-1, -1, -1), FVector(0, -1, 0));
        }

        void Translate(const FVector& By)
        {
            for (FTriangle& T : Triangles)
            {
                for (FCorner& Corner : T.Corners)
                {
                    Corner.Position += By;
                }
            }
        }
    };

    /** The body measured once from its shape (Layout). */
    struct FLayout
    {
        double HalfLength = 0.0;
        double HalfWidth = 0.0;
        double BodyFront = 0.0;
        double BodyBack = 0.0;
        FStation WindscreenFoot;
        FStation WindscreenHead;
        FStation RearHead;
        FStation RearFoot;
        double GlassHalfBelt = 0.0;
        double GlassHalfRoof = 0.0;
        double BumperBottom = 0.0;
        double BumperTop = 0.0;
        double LampBottom = 0.0;
        double LampTop = 0.0;

        explicit FLayout(const FShape& V)
        {
            HalfLength = V.Length / 2.0;
            HalfWidth = V.Width / 2.0;
            BodyFront = BumperSetback;
            // A lorry's body is its cab; behind that is a chassis under the box.
            BodyBack = V.bCargo ? FMath::Clamp(V.RearGlassBase, BodyFront + 0.5, V.Length - BumperSetback) : V.Length - BumperSetback;
            const double Foot = FMath::Clamp(V.WindscreenBase, BodyFront, BodyBack);
            const double Head = FMath::Clamp(V.WindscreenTop, Foot, BodyBack);
            const double RoofEnd = FMath::Clamp(V.RoofEnd, Head, BodyBack);
            const double RearFootS = FMath::Clamp(V.RearGlassBase, RoofEnd, BodyBack);
            WindscreenFoot = { Foot, V.Belt };
            WindscreenHead = { Head, V.Roof };
            RearHead = { RoofEnd, V.Roof };
            RearFoot = { RearFootS, V.Belt };
            GlassHalfBelt = FMath::Max(0.2, HalfWidth - V.Inset);
            GlassHalfRoof = FMath::Max(0.15, GlassHalfBelt - V.Tumblehome);
            BumperBottom = V.Clearance + 0.04;
            BumperTop = FMath::Max(BumperBottom + 0.1, FMath::Min(BumperBottom + BumperHeight, V.BonnetFront - 0.2));
            LampBottom = BumperTop + 0.03;
            LampTop = FMath::Max(LampBottom + 0.06, FMath::Min(V.BonnetFront - 0.04, LampBottom + 0.15));
        }

        TArray<FStation> Profile(const FShape& V) const
        {
            TArray<FStation> Line;
            Line.Add({ BodyFront, V.BonnetFront });
            Line.Add(WindscreenFoot);
            Line.Add({ RearFoot.S, V.Belt });
            Line.Add({ BodyBack, V.bCargo ? V.Belt : V.TailTop });
            return WithoutRepeats(Line);
        }
    };

    /** Builder: the original's part-by-part construction, verbatim in its own frame. */
    class FBuilder
    {
    public:
        FBuilder(FShell& InShell, const FShape& InShape) : Shell(InShell), V(InShape), L(InShape) {}

        FShell& Shell;
        const FShape& V;
        const FLayout L;

        enum EFaces
        {
            Front = 1,
            Back = 2,
            Left = 4,
            Right = 8,
            Top = 16,
            Bottom = 32,
            All = 63,
        };

        void Use(const FPart& Part) { Shell.Use(Part); }

        /** A point S metres back from the front bumper, Y up and X across (positive to the right). */
        FVector At(double S, double Y, double X) const { return FVector(X, Y, S - L.HalfLength); }

        void Quad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Facing) { Shell.Quad(A, B, C, D, Facing); }
        void Triangle(const FVector& A, const FVector& B, const FVector& C, const FVector& Facing) { Shell.Triangle(A, B, C, Facing); }

        void PanelAcross(double S, double X0, double X1, double Y0, double Y1, double Facing)
        {
            Quad(At(S, Y0, X0), At(S, Y0, X1), At(S, Y1, X1), At(S, Y1, X0), FVector(0, 0, Facing));
        }

        void PanelAlong(double X, double S0, double S1, double Y0, double Y1)
        {
            Quad(At(S0, Y0, X), At(S1, Y0, X), At(S1, Y1, X), At(S0, Y1, X), FVector(X > 0 ? 1 : -1, 0, 0));
        }

        void Block(double S0, double S1, double Y0, double Y1, double X0, double X1, int32 Faces)
        {
            if (Faces & Front) PanelAcross(S0, X0, X1, Y0, Y1, -1);
            if (Faces & Back) PanelAcross(S1, X0, X1, Y0, Y1, 1);
            if (Faces & Left) PanelAlong(X0, S0, S1, Y0, Y1);
            if (Faces & Right) PanelAlong(X1, S0, S1, Y0, Y1);
            if (Faces & Top) Quad(At(S0, Y1, X0), At(S0, Y1, X1), At(S1, Y1, X1), At(S1, Y1, X0), FVector(0, 1, 0));
            if (Faces & Bottom) Quad(At(S0, Y0, X0), At(S0, Y0, X1), At(S1, Y0, X1), At(S1, Y0, X0), FVector(0, -1, 0));
        }

        void LowerBody(const TArray<FStation>& TopLine, const TArray<FStation>& BottomLine, double Shoulder)
        {
            Use(Paint);
            const double W = L.HalfWidth;
            const double C = Shoulder;
            for (int32 Index = 1; Index < BottomLine.Num(); ++Index)
            {
                const FStation& From = BottomLine[Index - 1];
                const FStation& To = BottomLine[Index];
                if (To.S - From.S < 1e-4)
                {
                    continue;
                }
                TArray<double> Cuts = { From.S };
                for (const FStation& Corner : TopLine)
                {
                    if (Corner.S > From.S + 1e-4 && Corner.S < To.S - 1e-4)
                    {
                        Cuts.Add(Corner.S);
                    }
                }
                Cuts.Add(To.S);
                for (int32 Cut = 1; Cut < Cuts.Num(); ++Cut)
                {
                    const double S0 = Cuts[Cut - 1];
                    const double S1 = Cuts[Cut];
                    const double B0 = FMath::Lerp(From.Y, To.Y, (S0 - From.S) / (To.S - From.S));
                    const double B1 = FMath::Lerp(From.Y, To.Y, (S1 - From.S) / (To.S - From.S));
                    const double T0 = HeightAlong(TopLine, S0) - C;
                    const double T1 = HeightAlong(TopLine, S1) - C;
                    for (int32 Side = -1; Side <= 1; Side += 2)
                    {
                        Quad(At(S0, B0, Side * W), At(S1, B1, Side * W), At(S1, T1, Side * W), At(S0, T0, Side * W), FVector(Side, 0, 0));
                    }
                }
            }
            for (int32 Index = 1; Index < TopLine.Num(); ++Index)
            {
                const FStation& A = TopLine[Index - 1];
                const FStation& B = TopLine[Index];
                const FVector Up = FVector(0, B.S - A.S, -(B.Y - A.Y)).GetSafeNormal();
                Quad(At(A.S, A.Y, -(W - C)), At(A.S, A.Y, W - C), At(B.S, B.Y, W - C), At(B.S, B.Y, -(W - C)), Up);
                if (C <= 0.0)
                {
                    continue;
                }
                for (int32 Side = -1; Side <= 1; Side += 2)
                {
                    Quad(At(A.S, A.Y - C, Side * W), At(B.S, B.Y - C, Side * W), At(B.S, B.Y, Side * (W - C)), At(A.S, A.Y, Side * (W - C)),
                        Up + FVector(Side, 0, 0));
                }
            }
            for (int32 End = 0; End < 2; ++End)
            {
                const FStation& Edge = End == 0 ? TopLine[0] : TopLine.Last();
                const double Facing = End == 0 ? -1.0 : 1.0;
                PanelAcross(Edge.S, -W, W, V.Clearance, Edge.Y - C, Facing);
                if (C > 0.0)
                {
                    Quad(At(Edge.S, Edge.Y - C, -W), At(Edge.S, Edge.Y - C, W), At(Edge.S, Edge.Y, W - C), At(Edge.S, Edge.Y, -(W - C)),
                        FVector(0, 0, Facing));
                }
            }
        }

        TArray<FStation> Sills(TArray<TArray<FStation>>& OutArches) const
        {
            TArray<FStation> Line = { { L.BodyFront, V.Clearance } };
            const double Axles[2] = { V.FrontAxle, V.RearAxle };
            const double Reach = V.WheelRadius + ArchGap;
            for (const double Axle : Axles)
            {
                const double Start = Axle - Reach;
                const double End = Axle + Reach;
                if (Start < L.BodyFront + 0.02 || End > L.BodyBack - 0.02)
                {
                    continue;
                }
                TArray<FStation> Arch = { { Start, V.Clearance } };
                for (int32 K = 0; K <= ArchSteps; ++K)
                {
                    const double Angle = PI - PI * K / ArchSteps;
                    Arch.Add({ Axle + Reach * FMath::Cos(Angle), FMath::Max(V.Clearance, V.WheelRadius + Reach * FMath::Sin(Angle)) });
                }
                Arch.Add({ End, V.Clearance });
                Line.Append(Arch);
                OutArches.Add(Arch);
            }
            Line.Add({ L.BodyBack, V.Clearance });
            return Line;
        }

        void ArchLining(const TArray<FStation>& Arch)
        {
            Use(Underside);
            const double Outer = L.HalfWidth;
            const double Inner = FMath::Max(0.1, Outer - V.WheelWidth - 0.06);
            double TopY = V.Clearance;
            for (const FStation& Point : Arch)
            {
                TopY = FMath::Max(TopY, Point.Y);
            }
            for (int32 Side = -1; Side <= 1; Side += 2)
            {
                for (int32 Index = 1; Index < Arch.Num(); ++Index)
                {
                    const FStation& A = Arch[Index - 1];
                    const FStation& B = Arch[Index];
                    Quad(At(A.S, A.Y, Side * Outer), At(B.S, B.Y, Side * Outer), At(B.S, B.Y, Side * Inner), At(A.S, A.Y, Side * Inner),
                        FVector(0, -(B.S - A.S), B.Y - A.Y));
                }
                PanelAlong(Side * Inner, Arch[0].S, Arch.Last().S, V.Clearance, TopY);
            }
        }

        FVector OnGlasshouseSide(int32 Side, double S, double Y, double Out) const
        {
            const double Rise = FMath::Max(0.01, V.Roof - V.Belt);
            const double T = FMath::Clamp((Y - V.Belt) / Rise, 0.0, 1.0);
            const double X = FMath::Lerp(L.GlassHalfBelt, L.GlassHalfRoof, T);
            const double Lean = L.GlassHalfBelt - L.GlassHalfRoof;
            const FVector Normal = FVector(Rise, Lean, 0).GetSafeNormal();
            return At(S, Y, Side * X) + FVector(Normal.X * Side, Normal.Y, 0) * Out;
        }

        FVector GlasshouseNormal(int32 Side) const
        {
            return FVector(Side * FMath::Max(0.01, V.Roof - V.Belt), L.GlassHalfBelt - L.GlassHalfRoof, 0);
        }

        void Glasshouse()
        {
            const FStation& A = L.WindscreenFoot;
            const FStation& B = L.WindscreenHead;
            const FStation& C = L.RearHead;
            const FStation& D = L.RearFoot;
            const double Hb = L.GlassHalfBelt;
            const double Hr = L.GlassHalfRoof;
            if (V.Roof <= V.Belt + 0.05 || D.S <= A.S + 0.1)
            {
                return;
            }
            Use(Glass);
            Quad(At(A.S, A.Y, -Hb), At(A.S, A.Y, Hb), At(B.S, B.Y, Hr), At(B.S, B.Y, -Hr), FVector(0, B.S - A.S, -(B.Y - A.Y)));
            // A lorry's cab has a painted back wall behind which the box stands.
            Use(V.bCargo ? Paint : Glass);
            Quad(At(C.S, C.Y, -Hr), At(C.S, C.Y, Hr), At(D.S, D.Y, Hb), At(D.S, D.Y, -Hb), FVector(0, D.S - C.S, -(D.Y - C.Y)));
            Use(Paint);
            Quad(At(B.S, B.Y, -Hr), At(B.S, B.Y, Hr), At(C.S, C.Y, Hr), At(C.S, C.Y, -Hr), FVector(0, 1, 0));

            // The sides are painted, and the windows stand on them leaving the pillars between.
            for (int32 Side = -1; Side <= 1; Side += 2)
            {
                Quad(OnGlasshouseSide(Side, A.S, A.Y, 0), OnGlasshouseSide(Side, B.S, B.Y, 0), OnGlasshouseSide(Side, C.S, C.Y, 0),
                    OnGlasshouseSide(Side, D.S, D.Y, 0), GlasshouseNormal(Side));
            }

            Use(Glass);
            const double Sill = V.Belt + 0.035;
            const double Header = V.Roof - FMath::Min(0.07, (V.Roof - V.Belt) * 0.12);
            const auto FrontEdge = [&](double Y) { return FMath::Lerp(A.S, B.S, (Y - V.Belt) / (V.Roof - V.Belt)) + V.APillar; };
            const auto RearEdge = [&](double Y) { return FMath::Lerp(D.S, C.S, (Y - V.Belt) / (V.Roof - V.Belt)) - V.CPillar; };
            int32 Windows = FMath::Max(1, V.SideWindows);
            while (Windows > 1 && (RearEdge(Header) - FrontEdge(Header) - (Windows - 1) * V.BPillar) / Windows < 0.3)
            {
                --Windows;
            }
            for (int32 Side = -1; Side <= 1; Side += 2)
            {
                for (int32 Window = 0; Window < Windows; ++Window)
                {
                    const auto WindowEdge = [&](double Y, int32 K, bool bRear)
                    {
                        const double F = FrontEdge(Y);
                        const double Span = RearEdge(Y) - F;
                        const double Pane = (Span - (Windows - 1) * V.BPillar) / Windows;
                        return F + K * (Pane + V.BPillar) + (bRear ? Pane : 0.0);
                    };
                    const double S0 = WindowEdge(Sill, Window, false);
                    const double S1 = WindowEdge(Sill, Window, true);
                    const double S2 = WindowEdge(Header, Window, true);
                    const double S3 = WindowEdge(Header, Window, false);
                    if (S1 - S0 < 0.05 || S2 - S3 < 0.05)
                    {
                        continue;
                    }
                    Quad(OnGlasshouseSide(Side, S0, Sill, GlassProud), OnGlasshouseSide(Side, S1, Sill, GlassProud),
                        OnGlasshouseSide(Side, S2, Header, GlassProud), OnGlasshouseSide(Side, S3, Header, GlassProud), GlasshouseNormal(Side));
                }
            }
        }

        void Bumpers()
        {
            Use(Plastic);
            const double W = L.HalfWidth + BumperWrap;
            Block(0.0, BumperDepth, L.BumperBottom, L.BumperTop, -W, W, All & ~Back);
            if (V.bCargo)
            {
                // A lorry's rear bumper is the underrun bar across the back of its chassis.
                const double Bar = L.HalfWidth - 0.05;
                Block(V.Length - BumperDepth, V.Length, L.BumperBottom, L.BumperTop, -Bar, Bar, All);
            }
            else
            {
                Block(V.Length - BumperDepth, V.Length, L.BumperBottom, L.BumperTop, -W, W, All & ~Front);
            }
        }

        void LampsAndPlates()
        {
            const double W = L.HalfWidth;
            const double Nose = L.BodyFront - Proud;
            const double LampWidth = FMath::Min(0.32, W * 0.36);
            const double LampOuter = W - 0.08;
            const double LampInner = LampOuter - LampWidth;

            Use(Headlamp);
            for (int32 Side = -1; Side <= 1; Side += 2)
            {
                const double X0 = Side * LampInner;
                const double X1 = Side * LampOuter;
                PanelAcross(Nose, FMath::Min(X0, X1), FMath::Max(X0, X1), L.LampBottom, L.LampTop, -1);
            }
            Use(Indicator);
            for (int32 Side = -1; Side <= 1; Side += 2)
            {
                const double X0 = Side * (LampOuter + 0.005);
                const double X1 = Side * (W - 0.015);
                PanelAcross(Nose, FMath::Min(X0, X1), FMath::Max(X0, X1), L.LampBottom, L.LampTop, -1);
            }
            Use(Plastic);
            PanelAcross(Nose, -(LampInner - 0.03), LampInner - 0.03, L.LampBottom + 0.01, L.LampTop - 0.01, -1);

            const double Tail = V.bCargo ? V.Length + Proud : L.BodyBack + Proud;
            const double TailWidth = FMath::Min(0.34, W * 0.38);
            double TailBottom = V.bCargo ? L.BumperBottom + 0.03 : FMath::Max(L.BumperTop + 0.03, V.TailTop - 0.24);
            const double TailTop = V.bCargo ? L.BumperTop - 0.03 : V.TailTop - 0.05;
            if (TailTop - TailBottom < 0.06)
            {
                TailBottom = TailTop - 0.06;
            }
            const double TailOuter = V.bCargo ? W - 0.1 : W - 0.04;
            Use(TailLamp);
            for (int32 Side = -1; Side <= 1; Side += 2)
            {
                const double X0 = Side * (TailOuter - TailWidth);
                const double X1 = Side * TailOuter;
                PanelAcross(Tail, FMath::Min(X0, X1), FMath::Max(X0, X1), TailBottom, TailTop, 1);
            }

            Use(Plate);
            const double PlateHalf = 0.26;
            const double PlateMiddle = (L.BumperBottom + L.BumperTop) / 2.0;
            PanelAcross(-Proud, -PlateHalf, PlateHalf, PlateMiddle - 0.055, PlateMiddle + 0.055, -1);
            PanelAcross(V.Length + Proud, -PlateHalf, PlateHalf, PlateMiddle - 0.055, PlateMiddle + 0.055, 1);
        }

        void RubbingStrips()
        {
            const double Start = V.FrontAxle + V.WheelRadius + ArchGap + 0.06;
            const double End = FMath::Min(V.RearAxle - V.WheelRadius - ArchGap, L.BodyBack) - 0.06;
            if (End - Start < 0.5)
            {
                return;
            }
            Use(Plastic);
            const double Y = V.Clearance + 0.27;
            for (int32 Side = -1; Side <= 1; Side += 2)
            {
                PanelAlong(Side * (L.HalfWidth + Proud), Start, End, Y, Y + 0.06);
            }
        }

        void Wheel(double Axle, int32 Side, double Width)
        {
            const double Apothem = V.WheelRadius;
            const double Radius = Apothem / FMath::Cos(PI / WheelSides);
            const double HubRadius = Radius * 0.58;
            const double Outer = Side * (L.HalfWidth + WheelProud);
            const double Inner = Outer - Side * Width;
            TArray<FStation> Rim;
            TArray<FStation> Disc;
            for (int32 K = 0; K < WheelSides; ++K)
            {
                // From straight down, half a flat round, so that a flat rests on the ground.
                const double Angle = (K + 0.5) * 2.0 * PI / WheelSides;
                Rim.Add({ Axle + Radius * FMath::Sin(Angle), Apothem - Radius * FMath::Cos(Angle) });
                Disc.Add({ Axle + HubRadius * FMath::Sin(Angle), Apothem - HubRadius * FMath::Cos(Angle) });
            }
            const FVector Out(Side, 0, 0);
            Use(Rubber);
            for (int32 K = 0; K < WheelSides; ++K)
            {
                const FStation& A = Rim[K];
                const FStation& B = Rim[(K + 1) % WheelSides];
                const double Middle = (K + 1.0) * 2.0 * PI / WheelSides;
                Quad(At(A.S, A.Y, Inner), At(A.S, A.Y, Outer), At(B.S, B.Y, Outer), At(B.S, B.Y, Inner),
                    FVector(0, -FMath::Cos(Middle), FMath::Sin(Middle)));
                const FStation& C = Disc[K];
                const FStation& D = Disc[(K + 1) % WheelSides];
                Quad(At(A.S, A.Y, Outer), At(B.S, B.Y, Outer), At(D.S, D.Y, Outer), At(C.S, C.Y, Outer), Out);
            }
            for (int32 K = 1; K + 1 < WheelSides; ++K)
            {
                Triangle(At(Rim[0].S, Rim[0].Y, Inner), At(Rim[K].S, Rim[K].Y, Inner), At(Rim[K + 1].S, Rim[K + 1].Y, Inner), -Out);
            }
            Use(Hub);
            for (int32 K = 1; K + 1 < WheelSides; ++K)
            {
                Triangle(At(Disc[0].S, Disc[0].Y, Outer), At(Disc[K].S, Disc[K].Y, Outer), At(Disc[K + 1].S, Disc[K + 1].Y, Outer), Out);
            }
        }

        void CargoBox()
        {
            if (!V.bCargo)
            {
                return;
            }
            const double Half = V.CargoWidth / 2.0;
            Use(Paint);
            Block(V.CargoStart, V.CargoEnd, V.CargoBottom, V.CargoTop, -Half, Half, All & ~Bottom);
            Use(Underside);
            Block(V.CargoStart, V.CargoEnd, V.CargoBottom, V.CargoTop, -Half, Half, Bottom);
            // The seam between the two back doors.
            Use(Plastic);
            PanelAcross(V.CargoEnd + Proud, -0.015, 0.015, V.CargoBottom + 0.05, V.CargoTop - 0.05, 1);
            Use(Underside);
            const double Rails = FMath::Max(0.2, L.HalfWidth - V.RearWheelWidth - 0.1);
            Block(L.BodyBack - 0.1, V.Length - BumperDepth, L.BumperBottom, V.CargoBottom, -Rails, Rails, Left | Right | Bottom);
        }
    };

    /** vehicle_shell(shape, false): everything but the wheels, which a driven car draws on its own. */
    FShell BodyShell(const FShape& V)
    {
        FShell Shell;
        FBuilder Build(Shell, V);
        TArray<TArray<FStation>> Arches;
        const TArray<FStation> BottomLine = Build.Sills(Arches);
        Build.LowerBody(Build.L.Profile(V), BottomLine, ShoulderCut);
        for (const TArray<FStation>& Arch : Arches)
        {
            Build.ArchLining(Arch);
        }
        Build.Use(Underside);
        const double W = Build.L.HalfWidth;
        Build.Block(Build.L.BodyFront, Build.L.BodyBack, V.Clearance, V.Clearance, -W, W, FBuilder::Bottom);
        Build.Glasshouse();
        Build.Bumpers();
        Build.LampsAndPlates();
        Build.RubbingStrips();
        Build.CargoBox();
        return Shell;
    }

    /** vehicle_wheel_shell: one wheel, moved so its hub is the origin, the point it steers and springs about. */
    FShell WheelShell(const FShape& V, int32 Side, bool bRear)
    {
        FShell Shell;
        FBuilder Build(Shell, V);
        const double Axle = bRear ? V.RearAxle : V.FrontAxle;
        const double Width = bRear ? V.RearWheelWidth : V.WheelWidth;
        Build.Wheel(Axle, Side, Width);
        Shell.Translate(-Build.At(Axle, V.WheelRadius, Side * (Build.L.HalfWidth + WheelProud - Width / 2.0)));
        return Shell;
    }

    /** One door (build_one_door): a painted skin just outside the body, hinged on its forward edge. */
    struct FDoorPanel
    {
        bool bExists = false;
        FVector Hinge = FVector::ZeroVector; // in the body-shell frame (Godot)
        FShell Shell;                        // hinge-local
    };

    FDoorPanel DoorPanel(int32 Side, double Z0, double Z1, double Y0, double Y1, double OuterX)
    {
        FDoorPanel Door;
        const double Length = Z1 - Z0;
        const double Height = Y1 - Y0;
        if (Length < 0.28 || Height < 0.25)
        {
            return Door;
        }
        constexpr double Thickness = 0.045;
        Door.bExists = true;
        Door.Hinge = FVector(OuterX, (Y0 + Y1) * 0.5, Z0);
        const FVector Centre(-Side * Thickness * 0.5, 0.0, Length * 0.5);
        Door.Shell.Use(Paint);
        Door.Shell.Box(Centre, FVector(Thickness * 0.5, Height * 0.5, Length * 0.5));
        const double WindowHeight = FMath::Min(0.32, Height * 0.42);
        Door.Shell.Use(Glass);
        Door.Shell.Box(Centre + FVector(-Side * 0.006, Height * 0.5 - WindowHeight * 0.65, -Length * 0.02),
            FVector(Thickness * 0.28, WindowHeight * 0.5, Length * 0.34));
        Door.Shell.Use(Hub);
        Door.Shell.Box(Centre + FVector(-Side * 0.02, -Height * 0.08, -Length * 0.18), FVector(0.012, 0.018, 0.07));
        return Door;
    }

    /** build_side_doors: a front door each side, and a rear one when the car has two side windows and room. */
    TArray<FDoorPanel> Doors(const FShape& V)
    {
        const auto Along = [&](double FromFront) { return -V.Length * 0.5 + FromFront; };
        double From = V.WindscreenBase + 0.05;
        double To = V.bCargo ? V.RoofEnd - 0.06 : V.RearGlassBase - 0.08;
        if (To - From < 0.4)
        {
            From = V.FrontAxle;
            To = FMath::Min(V.Length - 0.3, From + 1.1);
        }
        const bool bSplit = !V.bCargo && V.SideWindows >= 2 && (To - From) > 1.15;
        const double Mid = (From + To) * 0.5;
        const double Y0 = V.Clearance + 0.08;
        const double Y1 = FMath::Max(Y0 + 0.45, V.Belt - 0.02);
        TArray<FDoorPanel> Out;
        Out.SetNum(4);
        for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
        {
            const int32 Side = SideIndex == 0 ? -1 : 1;
            const double Outer = Side * (V.Width * 0.5 + 0.012);
            Out[SideIndex] = DoorPanel(Side, Along(From), Along(bSplit ? Mid - 0.02 : To), Y0, Y1, Outer);
            if (bSplit)
            {
                Out[2 + SideIndex] = DoorPanel(Side, Along(Mid + 0.02), Along(To), Y0, Y1, Outer);
            }
        }
        return Out;
    }

    // ---- Writing the meshes ----

    /** Godot metres (x right, y up, z back) to Unreal centimetres (X forward, Y right, Z up). */
    FVector ToUnreal(const FVector& Godot)
    {
        return FVector(-Godot.Z, Godot.X, Godot.Y) * 100.0;
    }

    /**
     * One static mesh from a shell, in Unreal axes. Colour and paint mask in the vertex colour, roughness and
     * metalness in UV0 (as the original's vehicle shader read them), height above the ground in UV1 for the
     * road grime. Each triangle is wound to face where the original meant it to.
     */
    UStaticMesh* WriteMesh(const FShell& Shell, const FString& PackageName, double HeightOffset, UMaterialInterface* Material,
        const TArray<TPair<FName, FVector>>& Sockets)
    {
        FMeshDescription Description;
        FStaticMeshAttributes(Description).Register();
        FMeshDescriptionBuilder Builder;
        Builder.SetMeshDescription(&Description);
        Builder.EnablePolyGroups();
        Builder.SetNumUVLayers(2);
        const FPolygonGroupID Group = Builder.AppendPolygonGroup(TEXT("vehicle"));
        int32 Written = 0;
        for (const FShell::FTriangle& Triangle : Shell.Triangles)
        {
            FVector P[3];
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                P[Corner] = ToUnreal(Triangle.Corners[Corner].Position);
            }
            const FVector Facing = FVector(-Triangle.Facing.Z, Triangle.Facing.X, Triangle.Facing.Y).GetSafeNormal();
            FVector Normal = FVector::CrossProduct(P[1] - P[0], P[2] - P[0]);
            if (Normal.IsNearlyZero(1e-6) || Facing.IsNearlyZero())
            {
                continue;
            }
            // Unreal's front face winds so that (P2 - P0) x (P1 - P0) points out (FStaticMeshOperations: a
            // left-handed frame with counter-clockwise winding), the reverse of Normal above.
            int32 Order[3] = { 0, 1, 2 };
            if (FVector::DotProduct(Normal, Facing) > 0.0)
            {
                Swap(Order[1], Order[2]);
            }
            FVertexInstanceID Instances[3];
            for (int32 Index = 0; Index < 3; ++Index)
            {
                const FShell::FCorner& Corner = Triangle.Corners[Order[Index]];
                const FVertexID Vertex = Builder.AppendVertex(ToUnreal(Corner.Position));
                Instances[Index] = Builder.AppendInstance(Vertex);
                Builder.SetInstanceNormal(Instances[Index], Facing);
                Builder.SetInstanceColor(Instances[Index], Corner.Colour);
                Builder.SetInstanceUV(Instances[Index], FVector2D(Corner.Finish.X, Corner.Finish.Y), 0);
                Builder.SetInstanceUV(Instances[Index], FVector2D(Corner.Position.Y + HeightOffset, 0.0), 1);
            }
            Builder.AppendTriangle(Instances[0], Instances[1], Instances[2], Group);
            ++Written;
        }

        UPackage* Package = CreatePackage(*PackageName);
        Package->FullyLoad();
        const FString Name = FPackageName::GetShortName(PackageName);
        UStaticMesh* Mesh = FindObject<UStaticMesh>(Package, *Name);
        if (!Mesh)
        {
            Mesh = NewObject<UStaticMesh>(Package, *Name, RF_Public | RF_Standalone);
            FAssetRegistryModule::AssetCreated(Mesh);
        }
        Mesh->SetNumSourceModels(1);
        FStaticMeshSourceModel& Source = Mesh->GetSourceModel(0);
        Source.BuildSettings.bRecomputeNormals = false;
        Source.BuildSettings.bRecomputeTangents = true;
        Source.BuildSettings.bUseFullPrecisionUVs = true;
        Source.BuildSettings.bGenerateLightmapUVs = false;
        UStaticMesh::FBuildMeshDescriptionsParams Params;
        Params.bFastBuild = false;
        Params.bBuildSimpleCollision = false;
        if (!Mesh->BuildFromMeshDescriptions({ &Description }, Params))
        {
            UE_LOG(LogMarburgumVehicleContent, Error, TEXT("MB2VEHICLE could not build %s"), *PackageName);
            return nullptr;
        }
        Mesh->SetStaticMaterials({ FStaticMaterial(Material, TEXT("vehicle")) });
        // Props, not collision: the vehicle's box collides, the bodywork only draws.
        Mesh->SetBodySetup(nullptr);
        for (UStaticMeshSocket* Old : TArray<TObjectPtr<UStaticMeshSocket>>(Mesh->Sockets))
        {
            Mesh->RemoveSocket(Old);
        }
        for (const TPair<FName, FVector>& Socket : Sockets)
        {
            UStaticMeshSocket* Added = NewObject<UStaticMeshSocket>(Mesh);
            Added->SocketName = Socket.Key;
            Added->RelativeLocation = Socket.Value;
            Mesh->AddSocket(Added);
        }
        Mesh->PostEditChange();
        Package->MarkPackageDirty();
        if (!UEditorLoadingAndSavingUtils::SavePackages({ Package }, false))
        {
            UE_LOG(LogMarburgumVehicleContent, Error, TEXT("MB2VEHICLE could not save %s"), *PackageName);
            return nullptr;
        }
        UE_LOG(LogMarburgumVehicleContent, Display, TEXT("MB2VEHICLE mesh %s triangles=%d bounds=%s sockets=%d"), *PackageName, Written,
            *Mesh->GetBoundingBox().ToString(), Sockets.Num());
        return Mesh;
    }

    template <typename T>
    T* NewExpression(UMaterial* Material, int32 X, int32 Y)
    {
        T* Expression = NewObject<T>(Material);
        Expression->MaterialExpressionEditorX = X;
        Expression->MaterialExpressionEditorY = Y;
        Material->GetExpressionCollection().AddExpression(Expression);
        return Expression;
    }

    /**
     * M_Vehicle, after the original's vehicle.gdshader: the part's colour from the vertex, tinted by Paint
     * where the vertex alpha says bodywork, a little darker toward the sills (grime), roughness and metalness
     * from UV0. The street in a documentary photograph of the period rather than a showroom.
     */
    UMaterial* BuildMaterial()
    {
        UPackage* Package = CreatePackage(MaterialPackage);
        Package->FullyLoad();
        const FString Name = FPackageName::GetShortName(MaterialPackage);
        UMaterial* Material = FindObject<UMaterial>(Package, *Name);
        if (!Material)
        {
            Material = NewObject<UMaterial>(Package, *Name, RF_Public | RF_Standalone);
            FAssetRegistryModule::AssetCreated(Material);
        }
        Material->GetExpressionCollection().Empty();
        UMaterialEditorOnlyData* Inputs = Material->GetEditorOnlyData();

        auto* VertexColour = NewExpression<UMaterialExpressionVertexColor>(Material, -900, 0);
        auto* PaintColour = NewExpression<UMaterialExpressionVectorParameter>(Material, -900, 200);
        PaintColour->ParameterName = TEXT("Paint");
        // The shader's default paint (0.72, 0.72, 0.70 as written, sRGB), linear here.
        PaintColour->DefaultValue = FLinearColor::FromSRGBColor(FColor(184, 184, 179));
        auto* Grime = NewExpression<UMaterialExpressionScalarParameter>(Material, -900, 400);
        Grime->ParameterName = TEXT("Grime");
        Grime->DefaultValue = 0.14f;

        auto* Finish = NewExpression<UMaterialExpressionTextureCoordinate>(Material, -900, 600);
        Finish->CoordinateIndex = 0;
        auto* Roughness = NewExpression<UMaterialExpressionComponentMask>(Material, -600, 600);
        Roughness->R = true;
        Roughness->Input.Connect(0, Finish);
        auto* Metallic = NewExpression<UMaterialExpressionComponentMask>(Material, -600, 700);
        Metallic->G = true;
        Metallic->Input.Connect(0, Finish);

        auto* HeightUv = NewExpression<UMaterialExpressionTextureCoordinate>(Material, -900, 850);
        HeightUv->CoordinateIndex = 1;
        auto* Height = NewExpression<UMaterialExpressionComponentMask>(Material, -700, 850);
        Height->R = true;
        Height->Input.Connect(0, HeightUv);
        auto* Low = NewExpression<UMaterialExpressionSmoothStep>(Material, -500, 850);
        Low->ConstMin = 0.2f;
        Low->ConstMax = 0.75f;
        Low->Value.Connect(0, Height);
        auto* Below = NewExpression<UMaterialExpressionOneMinus>(Material, -350, 850);
        Below->Input.Connect(0, Low);
        auto* GrimeAmount = NewExpression<UMaterialExpressionMultiply>(Material, -200, 850);
        GrimeAmount->A.Connect(0, Grime);
        GrimeAmount->B.Connect(0, Below);
        auto* Dirt = NewExpression<UMaterialExpressionOneMinus>(Material, -50, 850);
        Dirt->Input.Connect(0, GrimeAmount);

        auto* Tinted = NewExpression<UMaterialExpressionMultiply>(Material, -500, 150);
        Tinted->A.Connect(0, VertexColour);   // RGB
        Tinted->B.Connect(0, PaintColour);    // RGB
        auto* Grimed = NewExpression<UMaterialExpressionMultiply>(Material, -300, 150);
        Grimed->A.Connect(0, Tinted);
        Grimed->B.Connect(0, Dirt);
        auto* Albedo = NewExpression<UMaterialExpressionLinearInterpolate>(Material, -100, 0);
        Albedo->A.Connect(0, VertexColour);   // RGB, unpainted parts keep their own colour
        Albedo->B.Connect(0, Grimed);
        Albedo->Alpha.Connect(4, VertexColour); // A: 1 on painted bodywork

        Inputs->BaseColor.Connect(0, Albedo);
        Inputs->Roughness.Connect(0, Roughness);
        Inputs->Metallic.Connect(0, Metallic);
        Material->PostEditChange();
        Package->MarkPackageDirty();
        if (!UEditorLoadingAndSavingUtils::SavePackages({ Package }, false))
        {
            UE_LOG(LogMarburgumVehicleContent, Error, TEXT("MB2VEHICLE could not save %s"), MaterialPackage);
            return nullptr;
        }
        UE_LOG(LogMarburgumVehicleContent, Display, TEXT("MB2VEHICLE material %s expressions=%d"), MaterialPackage,
            Material->GetExpressionCollection().Expressions.Num());
        return Material;
    }

    UMaterialInstanceConstant* BuildPaint(UMaterial* Parent, const FString& Id, const FLinearColor& Colour)
    {
        const FString PackageName = FString::Printf(TEXT("/Game/Vehicles/%s/MI_%s"), *Id, *Id);
        UPackage* Package = CreatePackage(*PackageName);
        Package->FullyLoad();
        const FString Name = FPackageName::GetShortName(PackageName);
        UMaterialInstanceConstant* Instance = FindObject<UMaterialInstanceConstant>(Package, *Name);
        if (!Instance)
        {
            Instance = NewObject<UMaterialInstanceConstant>(Package, *Name, RF_Public | RF_Standalone);
            FAssetRegistryModule::AssetCreated(Instance);
        }
        Instance->SetParentEditorOnly(Parent);
        Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Paint")), Colour);
        Instance->PostEditChange();
        Package->MarkPackageDirty();
        if (!UEditorLoadingAndSavingUtils::SavePackages({ Package }, false))
        {
            return nullptr;
        }
        return Instance;
    }

    bool LoadCatalogue(TArray<TSharedPtr<FJsonValue>>& OutVehicles, TArray<TSharedPtr<FJsonValue>>& OutInstances)
    {
        FString Text;
        const FString Path = FPaths::Combine(FPaths::ProjectDir(), CataloguePath);
        TSharedPtr<FJsonObject> Root;
        if (!FFileHelper::LoadFileToString(Text, *Path)
            || !FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Text), Root) || !Root.IsValid())
        {
            UE_LOG(LogMarburgumVehicleContent, Error, TEXT("MB2VEHICLE cannot read %s"), *Path);
            return false;
        }
        OutVehicles = Root->GetArrayField(TEXT("vehicles"));
        OutInstances = Root->GetArrayField(TEXT("instances"));
        return OutVehicles.Num() > 0;
    }

    bool BuildMeshes()
    {
        using namespace MarburgumII::Vehicles;
        TArray<TSharedPtr<FJsonValue>> Vehicles;
        TArray<TSharedPtr<FJsonValue>> Instances;
        UMaterial* Material = LoadCatalogue(Vehicles, Instances) ? BuildMaterial() : nullptr;
        if (!Material)
        {
            return false;
        }
        bool bOk = true;
        for (const TSharedPtr<FJsonValue>& Value : Vehicles)
        {
            const TSharedPtr<FJsonObject> Definition = Value->AsObject();
            const FString Id = Definition->GetStringField(TEXT("id"));
            const FVehicleDefinition* Contract = FindDefinition(Id);
            if (!Contract)
            {
                UE_LOG(LogMarburgumVehicleContent, Error, TEXT("MB2VEHICLE %s is in the catalogue but not in MarburgumVehicleContract.h"), *Id);
                bOk = false;
                continue;
            }
            const FShape Shape = ShapeOf(Definition);
            // The paint is written as sRGB in the catalogue.
            FLinearColor Written(0.72f, 0.72f, 0.70f);
            const TSharedPtr<FJsonObject>* Model = nullptr;
            const TArray<TSharedPtr<FJsonValue>>* Rgb = nullptr;
            if (Definition->TryGetObjectField(TEXT("model"), Model) && (*Model)->TryGetArrayField(TEXT("paint"), Rgb) && Rgb->Num() == 3)
            {
                Written = FLinearColor((*Rgb)[0]->AsNumber(), (*Rgb)[1]->AsNumber(), (*Rgb)[2]->AsNumber());
            }
            const auto ToLinear = [](float C) { return C <= 0.04045f ? C / 12.92f : FMath::Pow((C + 0.055f) / 1.055f, 2.4f); };
            const FLinearColor LinearPaint(ToLinear(Written.R), ToLinear(Written.G), ToLinear(Written.B));
            UMaterialInstanceConstant* Instance = BuildPaint(Material, Id, LinearPaint);
            if (!Instance)
            {
                bOk = false;
                continue;
            }

            // The body is placed with its origin on the ground under the middle of the axles (the runtime
            // actor hangs it there): the original put the shell's footprint middle at -axles_middle.
            const double AxlesMiddle = (Shape.FrontAxle + Shape.RearAxle) / 2.0 - Shape.Length / 2.0;
            const FVector Shift(0.0, 0.0, -AxlesMiddle);
            FShell Body = BodyShell(Shape);
            Body.Translate(Shift);
            const TArray<FDoorPanel> DoorPanels = Doors(Shape);
            TArray<TPair<FName, FVector>> Sockets;
            for (int32 Door = 0; Door < DoorPanels.Num(); ++Door)
            {
                if (DoorPanels[Door].bExists)
                {
                    Sockets.Add({ FName(*FString::Printf(TEXT("Door%d"), Door)), ToUnreal(DoorPanels[Door].Hinge + Shift) });
                }
            }
            bOk &= WriteMesh(Body, FString::Printf(TEXT("/Game/Vehicles/%s/SM_%s_Body"), *Id, *Id), 0.0, Instance, Sockets) != nullptr;
            for (int32 Wheel = 0; Wheel < WheelCount; ++Wheel)
            {
                const int32 Side = Wheel % 2 == 0 ? -1 : 1;
                const FShell Tyre = WheelShell(Shape, Side, Wheel >= 2);
                bOk &= WriteMesh(Tyre, FString::Printf(TEXT("/Game/Vehicles/%s/SM_%s_Wheel%d"), *Id, *Id, Wheel), Shape.WheelRadius, Instance, {}) != nullptr;
            }
            for (int32 Door = 0; Door < DoorPanels.Num(); ++Door)
            {
                if (DoorPanels[Door].bExists)
                {
                    bOk &= WriteMesh(DoorPanels[Door].Shell, FString::Printf(TEXT("/Game/Vehicles/%s/SM_%s_Door%d"), *Id, *Id, Door),
                        DoorPanels[Door].Hinge.Y, Instance, {}) != nullptr;
                }
            }
            UE_LOG(LogMarburgumVehicleContent, Display, TEXT("MB2VEHICLE definition %s axles=%.2f..%.2f doors=%d paint=%s"), *Id, Shape.FrontAxle,
                Shape.RearAxle, Sockets.Num(), *LinearPaint.ToString());
        }
        return bOk;
    }

    // ======================================================================================================
    // The enter and exit clips (tools/build_character.gd, _vehicle_animation): one-shots played at the door.
    // Hand out to the handle, draw the door, duck under the roof and sit; the exit is the same keys reversed.
    // ======================================================================================================

    struct FPose
    {
        FVector Hips;
        FVector Spine;
        FVector Head;
        FVector LeftUpper;
        FVector LeftLower;
        FVector RightUpper;
        FVector RightLower;
        FVector LeftThigh;
        FVector RightThigh;
        FVector LeftKnee;
        FVector RightKnee;
        FVector HipOffset;
    };

    const FPose Stand = {
        FVector::ZeroVector, FVector(0.02, 0, 0), FVector::ZeroVector,
        FVector(0.06, 0, 0.06), FVector(0.22, 0, 0), FVector(0.06, 0, -0.06), FVector(0.22, 0, 0),
        FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector,
    };
    // Hand out to the handle. Forward is a positive X swing; the door is in front of the body.
    const FPose Reach = {
        FVector(0.22, 0.05, 0), FVector(0.28, 0.08, 0), FVector(0.12, 0, 0),
        FVector(0.15, 0, 0.35), FVector(0.2, 0, 0), FVector(1.45, 0.08, -0.12), FVector(0.12, 0, 0),
        FVector(0.15, 0, 0), FVector(0.05, 0, 0), FVector(-0.2, 0, 0), FVector(-0.08, 0, 0), FVector(0, -0.02, -0.1),
    };
    // Draw the door. The hand comes back and the chest turns with it.
    const FPose Pull = {
        FVector(0.12, -0.28, 0), FVector(0.22, -0.22, 0), FVector(0.08, -0.12, 0),
        FVector(0.2, 0, 0.2), FVector(0.35, 0, 0), FVector(0.45, -0.4, -0.15), FVector(0.75, 0, 0),
        FVector(0.35, 0, 0), FVector(0.1, 0, 0), FVector(-0.45, 0, 0), FVector(-0.15, 0, 0), FVector(0, -0.05, 0.03),
    };
    const FPose Duck = {
        FVector(0.5, 0, 0), FVector(0.6, 0, 0), FVector(0.32, 0, 0),
        FVector(0.6, 0, 0.2), FVector(0.85, 0, 0), FVector(0.55, 0, -0.15), FVector(0.95, 0, 0),
        FVector(1.15, 0, 0), FVector(0.9, 0, 0), FVector(-1.4, 0, 0), FVector(-1.2, 0, 0), FVector(0, -0.24, -0.06),
    };
    const FPose Sat = {
        FVector(0.35, 0, 0), FVector(0.28, 0, 0), FVector(0.08, 0, 0),
        FVector(0.45, 0, 0.15), FVector(1.25, 0, 0), FVector(0.45, 0, -0.15), FVector(1.25, 0, 0),
        FVector(1.2, 0, 0), FVector(1.2, 0, 0), FVector(-1.5, 0, 0), FVector(-1.5, 0, 0), FVector(0, -0.42, -0.04),
    };

    MarburgumGodotClip::FClip VehicleClip(bool bEntering)
    {
        MarburgumGodotClip::FClip Clip;
        Clip.Package = bEntering ? TEXT("/Game/Characters/Animations/A_EnterVehicle") : TEXT("/Game/Characters/Animations/A_ExitVehicle");
        Clip.Title = bEntering ? TEXT("Marburgum enter vehicle") : TEXT("Marburgum exit vehicle");
        Clip.LengthSeconds = bEntering ? 1.2 : 1.05;
        Clip.bLoop = false;
        const TArray<const FPose*> Keys = bEntering ? TArray<const FPose*>{ &Stand, &Reach, &Pull, &Duck, &Sat }
                                                    : TArray<const FPose*>{ &Sat, &Duck, &Pull, &Reach, &Stand };
        const auto Track = [&Keys](const TCHAR* Bone, FVector FPose::*Field)
        {
            MarburgumGodotClip::FTrack Out{ Bone, {} };
            for (const FPose* Pose : Keys)
            {
                Out.Keys.Add(Pose->*Field);
            }
            return Out;
        };
        Clip.Tracks = {
            Track(TEXT("Hips"), &FPose::Hips), Track(TEXT("Spine"), &FPose::Spine), Track(TEXT("Head"), &FPose::Head),
            Track(TEXT("LeftUpperArm"), &FPose::LeftUpper), Track(TEXT("LeftLowerArm"), &FPose::LeftLower),
            Track(TEXT("RightUpperArm"), &FPose::RightUpper), Track(TEXT("RightLowerArm"), &FPose::RightLower),
            Track(TEXT("LeftUpperLeg"), &FPose::LeftThigh), Track(TEXT("RightUpperLeg"), &FPose::RightThigh),
            Track(TEXT("LeftLowerLeg"), &FPose::LeftKnee), Track(TEXT("RightLowerLeg"), &FPose::RightKnee),
        };
        for (const FPose* Pose : Keys)
        {
            Clip.HipsOffsets.Add(Pose->HipOffset);
        }
        return Clip;
    }

    bool BuildAnimations()
    {
        bool bOk = true;
        for (const bool bEntering : { true, false })
        {
            const MarburgumGodotClip::FClip Clip = VehicleClip(bEntering);
            const UAnimSequence* Sequence = MarburgumGodotClip::Build(Clip, TEXT("MB2VEHICLE"));
            bOk &= Sequence != nullptr;
            if (Sequence)
            {
                MarburgumGodotClip::LogLimbCheck(*Sequence, { 0.0, Clip.LengthSeconds * 0.5, Clip.LengthSeconds }, TEXT("MB2VEHICLE"));
            }
        }
        return bOk;
    }

    // ======================================================================================================
    // Placing the authored Lent car.
    // ======================================================================================================

    /** Clear, level, dry ground for a car of this definition at Location facing Yaw; the ground height out. */
    bool IsGoodSpot(UWorld& World, const MarburgumII::Vehicles::FVehicleDefinition& Definition, const FVector& Location, double Yaw,
        double& OutGroundZ, FString& OutWhy)
    {
        using namespace MarburgumII::Vehicles;
        const FQuat Rotation(FRotator(0.0, Yaw, 0.0));
        FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumVehiclePlace), true);
        Params.bReturnFaceIndex = true;
        // Where the level has paved streets (ADR-040), every wheel must stand on carriageway asphalt.
        bool bPavedStreets = false;
        for (TActorIterator<AStaticMeshActor> It(&World); It && !bPavedStreets; ++It)
        {
            bPavedStreets = It->GetActorLabel().StartsWith(TEXT("MB2Ground_"));
        }
        double Low = TNumericLimits<double>::Max();
        double High = -TNumericLimits<double>::Max();
        for (int32 Wheel = 0; Wheel < WheelCount; ++Wheel)
        {
            const FVector Mount = WheelAttachCm(Definition, Wheel);
            const FVector At = Location + Rotation.RotateVector(FVector(Mount.X, Mount.Y, 0.0));
            FHitResult Hit;
            if (!World.LineTraceSingleByChannel(Hit, At + FVector(0, 0, 600), At - FVector(0, 0, 1500), ECC_Visibility, Params))
            {
                OutWhy = TEXT("no_ground");
                return false;
            }
            if (bPavedStreets)
            {
                int32 Section = 0;
                const UPrimitiveComponent* Component = Hit.GetComponent();
                const UMaterialInterface* Surface = Component ? Component->GetMaterialFromCollisionFaceIndex(Hit.FaceIndex, Section) : nullptr;
                if (!Surface || !Surface->GetName().Equals(TEXT("MI_LentAsphalt")))
                {
                    OutWhy = TEXT("off_road");
                    return false;
                }
            }
            for (TActorIterator<APhysicsVolume> It(&World); It; ++It)
            {
                if (It->bWaterVolume && It->EncompassesPoint(Hit.ImpactPoint + FVector(0, 0, 5)))
                {
                    OutWhy = TEXT("water");
                    return false;
                }
            }
            if (Hit.ImpactNormal.Z < 0.97)
            {
                OutWhy = TEXT("slope");
                return false;
            }
            Low = FMath::Min(Low, Hit.ImpactPoint.Z);
            High = FMath::Max(High, Hit.ImpactPoint.Z);
        }
        // 5 cm: a car with one side up a 12 cm kerb (ADR-040) leans more than the 3 degrees a parked car may.
        if (High - Low > 5.0)
        {
            OutWhy = TEXT("uneven");
            return false;
        }
        OutGroundZ = High;
        // The whole body, from just above the ground to the roof, a little wider than the car, must be clear.
        const FVector Half = CollisionHalfExtentCm(Definition);
        const double Roof = Definition.BodyHeight * 100.0;
        const FVector Extent(Half.X + 40.0, Half.Y + 40.0, (Roof - 20.0) * 0.5);
        const FVector Centre(Location.X, Location.Y, High + 20.0 + Extent.Z);
        FCollisionObjectQueryParams Objects;
        Objects.AddObjectTypesToQuery(ECC_WorldStatic);
        Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
        Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
        if (World.OverlapAnyTestByObjectType(Centre, Rotation, Objects, FCollisionShape::MakeBox(Extent), Params))
        {
            OutWhy = TEXT("blocked");
            return false;
        }
        for (TActorIterator<APlayerStart> It(&World); It; ++It)
        {
            if (FVector::Dist2D(It->GetActorLocation(), Location) < Half.X + 300.0)
            {
                OutWhy = TEXT("start");
                return false;
            }
        }
        return true;
    }

    bool Place()
    {
        using namespace MarburgumII::Vehicles;
        TArray<TSharedPtr<FJsonValue>> Vehicles;
        TArray<TSharedPtr<FJsonValue>> Instances;
        UWorld* World = LoadCatalogue(Vehicles, Instances) ? UEditorLoadingAndSavingUtils::LoadMap(LentMap) : nullptr;
        if (!World)
        {
            UE_LOG(LogMarburgumVehicleContent, Error, TEXT("MB2VEHICLE cannot load %s"), LentMap);
            return false;
        }
        // The spot traces must see the paved meshes' collision (ADR-040), not only the landscape under them.
        World->UpdateWorldComponents(true, false);
        FAssetCompilingManager::Get().FinishAllCompilation();
        TArray<AActor*> Previous;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (It->IsA<AMarburgumVehicle>() || It->GetFolderPath() == FName(VehicleFolder))
            {
                Previous.Add(*It);
            }
        }
        for (AActor* Actor : Previous)
        {
            World->DestroyActor(Actor);
        }

        // The river's direction, as the opening pass finds it: cars park along the promenade.
        FVector Along(1.0, 0.0, 0.0);
        for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
        {
            if (It->GetActorLabel() == TEXT("Drava"))
            {
                FVector Origin;
                FVector Extent;
                It->GetActorBounds(false, Origin, Extent);
                Along = Extent.X >= Extent.Y ? FVector(1.0, 0.0, 0.0) : FVector(0.0, 1.0, 0.0);
            }
        }
        const APlayerStart* Arrival = nullptr;
        for (TActorIterator<APlayerStart> It(World); It; ++It)
        {
            if (It->PlayerStartTag == TEXT("arrival") && (!Arrival || It->GetName() < Arrival->GetName()))
            {
                Arrival = *It;
            }
        }
        if (!Arrival)
        {
            UE_LOG(LogMarburgumVehicleContent, Error, TEXT("MB2VEHICLE %s has no arrival start"), LentMap);
            return false;
        }

        int32 Placed = 0;
        for (const TSharedPtr<FJsonValue>& Value : Instances)
        {
            const TSharedPtr<FJsonObject> Row = Value->AsObject();
            const FString Id = Row->GetStringField(TEXT("id"));
            const FVehicleDefinition* Definition = FindDefinition(Row->GetStringField(TEXT("definition")));
            const TArray<TSharedPtr<FJsonValue>>& Position = Row->GetArrayField(TEXT("position_m"));
            const TArray<TSharedPtr<FJsonValue>>& Basis = Row->GetArrayField(TEXT("basis"));
            if (!Definition || Position.Num() != 3 || Basis.Num() != 9)
            {
                UE_LOG(LogMarburgumVehicleContent, Error, TEXT("MB2VEHICLE instance %s is malformed"), *Id);
                return false;
            }
            // The original's frame: x east and z south of the middle of Stari most, y up; its basis columns are
            // the car's right, up and back. Unreal Marburgum's mapping (MarburgumSpace.h): Unreal = 100 (-z, x, y).
            const FVector Original(-Position[2]->AsNumber() * 100.0, Position[0]->AsNumber() * 100.0, Position[1]->AsNumber() * 100.0);
            const FVector Back(Basis[6]->AsNumber(), Basis[7]->AsNumber(), Basis[8]->AsNumber());
            const double OriginalYaw = FVector(Back.Z, -Back.X, 0.0).Rotation().Yaw; // forward = -back, in Unreal axes

            FVector Chosen = FVector::ZeroVector;
            double ChosenYaw = 0.0;
            double Ground = 0.0;
            FString Why;
            FString Source;
            if (IsGoodSpot(*World, *Definition, Original, OriginalYaw, Ground, Why))
            {
                Chosen = Original;
                ChosenYaw = OriginalYaw;
                Source = TEXT("original");
            }
            else
            {
                UE_LOG(LogMarburgumVehicleContent, Display, TEXT("MB2VEHICLE place %s original=%s yaw=%.1f rejected=%s"), *Id, *Original.ToString(),
                    OriginalYaw, *Why);
                // Nearest good spot, parked along the river, rings 1 m apart out to 60 m.
                // The original spot can now have a kerb or a pavement under it (ADR-040).
                const FVector Start = Original;
                // Along the river, heading the way the original car faced, so the river stays on the same side.
                double AlongYaw = Along.Rotation().Yaw;
                if (FMath::Abs(FMath::FindDeltaAngleDegrees(AlongYaw, OriginalYaw)) > 90.0)
                {
                    AlongYaw += 180.0;
                }
                double Best = TNumericLimits<double>::Max();
                for (double Radius = 100.0; Radius <= 6000.0 && Source.IsEmpty(); Radius += 100.0)
                {
                    for (int32 Step = 0; Step < 72; ++Step)
                    {
                        const double Angle = Step * 5.0;
                        const FVector At = Start + FRotator(0.0, Angle, 0.0).RotateVector(FVector(Radius, 0.0, 0.0));
                        for (const double Yaw : { AlongYaw })
                        {
                            double Z = 0.0;
                            FString Unused;
                            if (IsGoodSpot(*World, *Definition, At, Yaw, Z, Unused) && Radius < Best)
                            {
                                Best = Radius;
                                Chosen = At;
                                ChosenYaw = Yaw;
                                Ground = Z;
                                Source = TEXT("near_original");
                            }
                        }
                    }
                }
                if (Source.IsEmpty())
                {
                    UE_LOG(LogMarburgumVehicleContent, Error, TEXT("MB2VEHICLE place %s found no clear level spot near %s"), *Id, *Start.ToString());
                    return false;
                }
            }
            const FVector Location(Chosen.X, Chosen.Y, Ground + RideHeightCm(*Definition) + 2.0);
            AMarburgumVehicle* Car = World->SpawnActorDeferred<AMarburgumVehicle>(AMarburgumVehicle::StaticClass(),
                FTransform(FRotator(0.0, ChosenYaw, 0.0), Location), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if (!Car)
            {
                return false;
            }
            Car->DefinitionId = FName(Definition->Id);
            Car->VehicleId = FName(*Id);
            Car->FinishSpawning(FTransform(FRotator(0.0, ChosenYaw, 0.0), Location));
            Car->SetActorLabel(FString::Printf(TEXT("Vehicle_%s"), *Id));
            Car->SetFolderPath(VehicleFolder);
            ++Placed;

            // A player start beside the driver's door (?MB2Start=<id>), for getting straight to the car.
            const FSeatDefinition* Driver = DriverSeat(*Definition);
            const FTransform CarTransform(FRotator(0.0, ChosenYaw, 0.0), Location);
            const FVector Door = CarTransform.TransformPosition(DoorPointCm(*Driver));
            const FVector Out = CarTransform.TransformVector(DirectionToUnreal(Driver->Door)).GetSafeNormal2D();
            const FVector Beside = Door + Out * 120.0;
            FHitResult Floor;
            FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumVehicleStart), true, Car);
            if (!World->LineTraceSingleByChannel(Floor, Beside + FVector(0, 0, 300), Beside - FVector(0, 0, 600), ECC_Visibility, Params))
            {
                UE_LOG(LogMarburgumVehicleContent, Error, TEXT("MB2VEHICLE no ground for the %s start at %s"), *Id, *Beside.ToString());
                return false;
            }
            APlayerStart* Start = World->SpawnActor<APlayerStart>(Floor.ImpactPoint + FVector(0, 0, 96.0), (-Out).Rotation());
            Start->PlayerStartTag = FName(*Id);
            Start->SetActorLabel(FString::Printf(TEXT("PlayerStart_%s"), *Id));
            Start->SetFolderPath(VehicleFolder);
            UE_LOG(LogMarburgumVehicleContent, Display, TEXT("MB2VEHICLE start %s location=%s door=%s"), *Id,
                *Start->GetActorLocation().ToString(), *Door.ToString());
            UE_LOG(LogMarburgumVehicleContent, Display, TEXT("MB2VEHICLE place %s definition=%s source=%s location=%s yaw=%.1f ground=%.1f arrival=%s"),
                *Id, Definition->Id, *Source, *Location.ToString(), ChosenYaw, Ground, *Arrival->GetActorLocation().ToString());
        }
        if (!UEditorLoadingAndSavingUtils::SaveMap(World, LentMap))
        {
            UE_LOG(LogMarburgumVehicleContent, Error, TEXT("MB2VEHICLE could not save %s"), LentMap);
            return false;
        }
        UE_LOG(LogMarburgumVehicleContent, Display, TEXT("MB2VEHICLE saved %s vehicles=%d"), LentMap, Placed);
        return Placed > 0;
    }
}

UMarburgumVehicleContentCommandlet::UMarburgumVehicleContentCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UMarburgumVehicleContentCommandlet::Main(const FString& Params)
{
    FString Steps = TEXT("mesh,animation,place");
    FParse::Value(*Params, TEXT("Steps="), Steps, false);
    TArray<FString> List;
    Steps.ParseIntoArray(List, TEXT(","));
    bool bOk = true;
    for (const FString& Step : List)
    {
        if (Step == TEXT("mesh"))
        {
            bOk &= MarburgumVehicleContent::BuildMeshes();
        }
        else if (Step == TEXT("animation"))
        {
            bOk &= MarburgumVehicleContent::BuildAnimations();
        }
        else if (Step == TEXT("place"))
        {
            bOk &= MarburgumVehicleContent::Place();
        }
        else
        {
            UE_LOG(LogMarburgumVehicleContent, Error, TEXT("MB2VEHICLE unknown step %s"), *Step);
            bOk = false;
        }
    }
    UE_LOG(LogMarburgumVehicleContent, Display, TEXT("MB2VEHICLE result=%d"), bOk ? 0 : 1);
    return bOk ? 0 : 1;
}
