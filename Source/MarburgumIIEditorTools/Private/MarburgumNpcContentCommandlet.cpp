#include "MarburgumNpcContentCommandlet.h"

#include "MarburgumGodotClip.h"
#include "MarburgumNpcContract.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "FileHelpers.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "MeshDescriptionBuilder.h"
#include "Misc/PackageName.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumNpcContent, Log, All);

namespace MarburgumNpcContent
{
    namespace Npc = MarburgumII::Npc;
    using FPose = TMap<FString, FVector>;

    constexpr const TCHAR* LogTag = TEXT("MB2NPCCONTENT");
    constexpr const TCHAR* SkeletonPath = TEXT("/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin");
    constexpr const TCHAR* MaterialPath = TEXT("/Game/Vehicles/Materials/M_Vehicle.M_Vehicle");
    constexpr const TCHAR* RodPackage = TEXT("/Game/Characters/Props/SM_FishingRod");

    // ======================================================================================================
    // The rod: rod-local centimetres, X along the blank from the grip, Z up, Y to the angler's right.
    // ======================================================================================================

    constexpr double ButtX = -34.0;
    constexpr double TipX = 241.0;
    /** The fixed-spool reel hangs under the seat; its handle turns about rod-local Y on the left (-Y). */
    const FVector ReelAxle(0.0, -5.5, -9.0);
    constexpr double CrankRadius = 4.0;
    constexpr double KnobOut = 6.5;

    FVector KnobAt(double Turn)
    {
        return FVector(CrankRadius * FMath::Sin(Turn), -KnobOut, ReelAxle.Z - CrankRadius * FMath::Cos(Turn));
    }

    /** Triangles with a colour, a finish (roughness, metalness in UV0, as M_Vehicle reads them) and smooth normals. */
    struct FRodBuilder
    {
        FMeshDescription Description;
        FMeshDescriptionBuilder Builder;
        FPolygonGroupID Group;
        FVector4f Tint = FVector4f(1.f, 1.f, 1.f, 0.f);
        FVector2f Finish = FVector2f(0.5f, 0.f);
        int32 Triangles = 0;

        FRodBuilder()
        {
            FStaticMeshAttributes(Description).Register();
            Builder.SetMeshDescription(&Description);
            Builder.EnablePolyGroups();
            Builder.SetNumUVLayers(2);
            Group = Builder.AppendPolygonGroup(TEXT("rod"));
        }

        /** sRGB as picked from a photograph, turned linear once; alpha 0 keeps M_Vehicle's paint off. */
        void Use(double R, double G, double B, double Roughness, double Metallic)
        {
            const auto ToLinear = [](double C) { return C <= 0.04045 ? C / 12.92 : FMath::Pow((C + 0.055) / 1.055, 2.4); };
            Tint = FVector4f(static_cast<float>(ToLinear(R)), static_cast<float>(ToLinear(G)), static_cast<float>(ToLinear(B)), 0.f);
            Finish = FVector2f(static_cast<float>(Roughness), static_cast<float>(Metallic));
        }

        void Triangle(const FVector (&P)[3], const FVector (&N)[3])
        {
            // Unreal's front face winds so that (P2 - P0) x (P1 - P0) points out.
            const FVector Out = N[0] + N[1] + N[2];
            int32 Order[3] = { 0, 1, 2 };
            if (FVector::DotProduct(FVector::CrossProduct(P[2] - P[0], P[1] - P[0]), Out) < 0.0)
            {
                Swap(Order[1], Order[2]);
            }
            FVertexInstanceID Instances[3];
            for (int32 Index = 0; Index < 3; ++Index)
            {
                const FVertexID Vertex = Builder.AppendVertex(P[Order[Index]]);
                Instances[Index] = Builder.AppendInstance(Vertex);
                Builder.SetInstanceNormal(Instances[Index], N[Order[Index]].GetSafeNormal());
                Builder.SetInstanceColor(Instances[Index], Tint);
                Builder.SetInstanceUV(Instances[Index], FVector2D(Finish.X, Finish.Y), 0);
                // Well above the ground: no road grime on a rod.
                Builder.SetInstanceUV(Instances[Index], FVector2D(3.0, 0.0), 1);
            }
            Builder.AppendTriangle(Instances[0], Instances[1], Instances[2], Group);
            ++Triangles;
        }

        static void Frame(const FVector& Axis, FVector& OutV, FVector& OutW)
        {
            const FVector Helper = FMath::Abs(Axis.Z) < 0.9 ? FVector::UpVector : FVector::RightVector;
            OutV = FVector::CrossProduct(Axis, Helper).GetSafeNormal();
            OutW = FVector::CrossProduct(Axis, OutV).GetSafeNormal();
        }

        /** A tapered tube from A (radius RA) to B (radius RB), closed at both ends. */
        void Tube(const FVector& A, const FVector& B, double RA, double RB, int32 Segments = 12)
        {
            const FVector Axis = (B - A).GetSafeNormal();
            FVector V, W;
            Frame(Axis, V, W);
            for (int32 Index = 0; Index < Segments; ++Index)
            {
                const double A0 = UE_TWO_PI * Index / Segments;
                const double A1 = UE_TWO_PI * (Index + 1) / Segments;
                const FVector D0 = V * FMath::Cos(A0) + W * FMath::Sin(A0);
                const FVector D1 = V * FMath::Cos(A1) + W * FMath::Sin(A1);
                Triangle({ A + D0 * RA, B + D0 * RB, B + D1 * RB }, { D0, D0, D1 });
                Triangle({ A + D0 * RA, B + D1 * RB, A + D1 * RA }, { D0, D1, D1 });
                Triangle({ A, A + D1 * RA, A + D0 * RA }, { -Axis, -Axis, -Axis });
                Triangle({ B, B + D0 * RB, B + D1 * RB }, { Axis, Axis, Axis });
            }
        }

        /** A ring of Radius round Centre in the plane square to Axis, of tube radius Thickness. */
        void Torus(const FVector& Centre, const FVector& Axis, double Radius, double Thickness, int32 Segments = 14, int32 Sides = 6)
        {
            FVector V, W;
            Frame(Axis.GetSafeNormal(), V, W);
            const auto Point = [&](int32 Segment, int32 Side, FVector& OutNormal)
            {
                const double U = UE_TWO_PI * Segment / Segments;
                const double S = UE_TWO_PI * Side / Sides;
                const FVector Out = V * FMath::Cos(U) + W * FMath::Sin(U);
                OutNormal = Out * FMath::Cos(S) + Axis.GetSafeNormal() * FMath::Sin(S);
                return Centre + Out * Radius + OutNormal * Thickness;
            };
            for (int32 Segment = 0; Segment < Segments; ++Segment)
            {
                for (int32 Side = 0; Side < Sides; ++Side)
                {
                    FVector N00, N10, N11, N01;
                    const FVector P00 = Point(Segment, Side, N00);
                    const FVector P10 = Point(Segment + 1, Side, N10);
                    const FVector P11 = Point(Segment + 1, Side + 1, N11);
                    const FVector P01 = Point(Segment, Side + 1, N01);
                    Triangle({ P00, P10, P11 }, { N00, N10, N11 });
                    Triangle({ P00, P11, P01 }, { N00, N11, N01 });
                }
            }
        }
    };

    /** The blank's radius at X: a steady taper from the winding check to the tip. */
    double BlankRadius(double X)
    {
        return FMath::Lerp(0.8, 0.14, FMath::Clamp((X - 19.0) / (TipX - 19.0), 0.0, 1.0));
    }

    bool BuildRodMesh()
    {
        UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, MaterialPath);
        if (!Material)
        {
            UE_LOG(LogMarburgumNpcContent, Error, TEXT("%s missing %s (run scripts/create-vehicle-content.sh first)"), LogTag, MaterialPath);
            return false;
        }
        FRodBuilder Rod;
        // Butt cap, rear cork, screw reel seat, fore cork, winding check.
        Rod.Use(0.05, 0.05, 0.05, 0.85, 0.0);
        Rod.Tube(FVector(ButtX, 0, 0), FVector(-31.0, 0, 0), 1.75, 1.7, 14);
        Rod.Use(0.66, 0.5, 0.33, 0.92, 0.0);
        Rod.Tube(FVector(-31.0, 0, 0), FVector(-3.0, 0, 0), 1.6, 1.45, 14);
        Rod.Use(0.17, 0.17, 0.19, 0.38, 0.7);
        Rod.Tube(FVector(-3.0, 0, 0), FVector(6.0, 0, 0), 1.25, 1.25, 14);
        Rod.Tube(FVector(-3.6, 0, 0), FVector(-2.4, 0, 0), 1.5, 1.5, 14);
        Rod.Use(0.66, 0.5, 0.33, 0.92, 0.0);
        Rod.Tube(FVector(6.0, 0, 0), FVector(18.0, 0, 0), 1.4, 1.1, 14);
        Rod.Use(0.62, 0.5, 0.24, 0.35, 0.8);
        Rod.Tube(FVector(18.0, 0, 0), FVector(19.0, 0, 0), 0.95, 0.9, 12);
        // The varnished glass blank, with a whipped ferrule halfway.
        Rod.Use(0.2, 0.16, 0.1, 0.3, 0.0);
        Rod.Tube(FVector(19.0, 0, 0), FVector(TipX, 0, 0), BlankRadius(19.0), BlankRadius(TipX), 10);
        Rod.Use(0.42, 0.08, 0.06, 0.4, 0.0);
        Rod.Tube(FVector(124.0, 0, 0), FVector(130.0, 0, 0), BlankRadius(124.0) + 0.12, BlankRadius(130.0) + 0.12, 10);
        // Seven guides under the blank, shrinking toward the tip, and the tip ring.
        const double Guides[][2] = { { 58.0, 2.4 }, { 96.0, 1.75 }, { 132.0, 1.3 }, { 162.0, 1.0 }, { 189.0, 0.8 }, { 212.0, 0.68 }, { 230.0, 0.6 } };
        for (const auto& Guide : Guides)
        {
            const double X = Guide[0];
            const double Ring = Guide[1];
            const double Under = BlankRadius(X);
            const double RingZ = -(Under + Ring * 1.5 + Ring);
            Rod.Use(0.62, 0.5, 0.24, 0.35, 0.8);
            Rod.Tube(FVector(X - 1.2, 0, -Under * 0.6), FVector(X - 0.6, 0, -(Under + Ring * 1.5)), 0.13, 0.13, 6);
            Rod.Tube(FVector(X + 1.2, 0, -Under * 0.6), FVector(X + 0.6, 0, -(Under + Ring * 1.5)), 0.13, 0.13, 6);
            Rod.Use(0.78, 0.78, 0.8, 0.25, 0.9);
            Rod.Torus(FVector(X, 0, RingZ), FVector::ForwardVector, Ring, 0.12, 12, 5);
        }
        Rod.Use(0.78, 0.78, 0.8, 0.25, 0.9);
        Rod.Torus(FVector(TipX, 0, -0.45), FVector::ForwardVector, 0.32, 0.08, 10, 4);
        // The reel: foot on the seat, stem down between the fingers, gearbox, spool with line, handle on the left.
        Rod.Use(0.22, 0.23, 0.25, 0.35, 0.75);
        Rod.Tube(FVector(-1.5, 0, -1.3), FVector(4.5, 0, -1.3), 0.45, 0.45, 8);
        Rod.Tube(FVector(1.5, 0, -1.3), FVector(1.5, 0, -6.6), 0.55, 0.7, 8);
        Rod.Tube(FVector(-3.0, 0, -9.0), FVector(3.0, 0, -9.0), 2.5, 2.7, 16);
        Rod.Use(0.62, 0.63, 0.66, 0.3, 0.85);
        Rod.Tube(FVector(3.0, 0, -9.0), FVector(6.6, 0, -9.0), 2.9, 2.9, 16);
        Rod.Use(0.45, 0.56, 0.48, 0.5, 0.0);
        Rod.Tube(FVector(3.4, 0, -9.0), FVector(6.2, 0, -9.0), 3.02, 3.02, 16);
        Rod.Use(0.22, 0.23, 0.25, 0.35, 0.75);
        Rod.Tube(FVector(6.6, 0, -9.0), FVector(7.4, 0, -9.0), 2.3, 1.6, 16);
        Rod.Use(0.78, 0.78, 0.8, 0.25, 0.9);
        Rod.Torus(FVector(5.0, 0, -9.0), FVector::ForwardVector, 3.6, 0.16, 16, 4);
        Rod.Use(0.22, 0.23, 0.25, 0.35, 0.75);
        Rod.Tube(FVector(ReelAxle.X, 0.0, ReelAxle.Z), FVector(ReelAxle.X, ReelAxle.Y, ReelAxle.Z), 0.45, 0.45, 8);
        Rod.Tube(FVector(ReelAxle.X, ReelAxle.Y, ReelAxle.Z), KnobAt(0.0) + FVector(0.0, KnobOut + ReelAxle.Y, 0.0), 0.5, 0.4, 8);
        Rod.Use(0.08, 0.07, 0.06, 0.6, 0.0);
        Rod.Tube(KnobAt(0.0) + FVector(0.0, KnobOut + ReelAxle.Y, 0.0), KnobAt(0.0) + FVector(0.0, -1.4, 0.0), 0.75, 0.85, 10);

        UPackage* Package = CreatePackage(RodPackage);
        Package->FullyLoad();
        const FString Name = FPackageName::GetShortName(RodPackage);
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
        if (!Mesh->BuildFromMeshDescriptions({ &Rod.Description }, Params))
        {
            UE_LOG(LogMarburgumNpcContent, Error, TEXT("%s could not build %s"), LogTag, RodPackage);
            return false;
        }
        Mesh->SetStaticMaterials({ FStaticMaterial(Material, TEXT("rod")) });
        // A prop in a hand: it draws, it never collides.
        Mesh->SetBodySetup(nullptr);
        for (UStaticMeshSocket* Old : TArray<TObjectPtr<UStaticMeshSocket>>(Mesh->Sockets))
        {
            Mesh->RemoveSocket(Old);
        }
        UStaticMeshSocket* Tip = NewObject<UStaticMeshSocket>(Mesh);
        Tip->SocketName = Npc::RodTipSocket;
        Tip->RelativeLocation = FVector(TipX, 0.0, -0.45);
        Mesh->AddSocket(Tip);
        Mesh->PostEditChange();
        Package->MarkPackageDirty();
        if (!UEditorLoadingAndSavingUtils::SavePackages({ Package }, false))
        {
            UE_LOG(LogMarburgumNpcContent, Error, TEXT("%s could not save %s"), LogTag, RodPackage);
            return false;
        }
        UE_LOG(LogMarburgumNpcContent, Display, TEXT("%s saved %s triangles=%d length=%.0fcm"), LogTag, RodPackage, Rod.Triangles, TipX - ButtX);
        return true;
    }

    // ======================================================================================================
    // Poses, in the original's convention: model-space Euler radians per Godot bone. Rest is arms hanging; a
    // positive X swings a limb forward, a positive Z lifts the left arm out (and a negative Z the right arm).
    // ======================================================================================================

    /**
     * How far the right elbow comes in across the body in the hold (the right arm's Z). FitHold searches it: the rod has
     * to sit where the left hand can rest on the reel handle.
     */
    double RightArmIn = -0.22;

    FPose HoldPose(double Breath = 0.0)
    {
        return {
            { TEXT("Spine"), FVector(0.05 + 0.02 * Breath, 0.0, 0.0) },
            { TEXT("Head"), FVector(0.14, 0.0, 0.0) },
            { TEXT("RightUpperArm"), FVector(0.38 + 0.02 * Breath, 0.0, RightArmIn) },
            { TEXT("RightLowerArm"), FVector(1.12, 0.0, 0.0) },
            { TEXT("LeftUpperLeg"), FVector(0.0, 0.0, 0.05) },
            { TEXT("RightUpperLeg"), FVector(-0.04, 0.0, -0.05) },
        };
    }

    /** A relaxed left arm for the moments the left hand is off the rod. */
    void FreeLeftArm(FPose& Pose)
    {
        Pose.Add(TEXT("LeftUpperArm"), FVector(0.25, 0.0, 0.18));
        Pose.Add(TEXT("LeftLowerArm"), FVector(0.55, 0.0, 0.0));
    }

    struct FSolved
    {
        FPose Pose;
        double Residual = TNumericLimits<double>::Max();
    };

    struct FRig
    {
        const FReferenceSkeleton* Reference = nullptr;
        TArray<FName> Names;

        int32 Index(const TCHAR* Bone) const
        {
            return Names.IndexOfByKey(FName(Bone));
        }

        /**
         * Where a hand closes: the knuckle line pulled a little toward the palm. The palm faces where (index - pinky) x
         * (wrist to knuckles) points for a left hand and the opposite way for a right one (palm down, fingers forward:
         * the right thumb is on the left). False when a bone is missing.
         */
        bool Palm(const TArray<FTransform>& Pose, const TCHAR* Side, FVector& OutCentre, FVector& OutNormal) const
        {
            const int32 Hand = Index(*FString::Printf(TEXT("hand_%s"), Side));
            const int32 IndexKnuckle = Index(*FString::Printf(TEXT("index_01_%s"), Side));
            const int32 Middle = Index(*FString::Printf(TEXT("middle_01_%s"), Side));
            const int32 Pinky = Index(*FString::Printf(TEXT("pinky_01_%s"), Side));
            if (Hand == INDEX_NONE || IndexKnuckle == INDEX_NONE || Middle == INDEX_NONE || Pinky == INDEX_NONE)
            {
                return false;
            }
            const FVector H = Pose[Hand].GetLocation();
            const FVector M = Pose[Middle].GetLocation();
            const FVector Across = Pose[IndexKnuckle].GetLocation() - Pose[Pinky].GetLocation();
            const double Sign = FCString::Strcmp(Side, TEXT("r")) == 0 ? -1.0 : 1.0;
            OutNormal = (FVector::CrossProduct(Across, M - H) * Sign).GetSafeNormal();
            OutCentre = M - (M - H).GetSafeNormal() * 1.0 + OutNormal * 2.6;
            return true;
        }
    };

    bool Evaluate(const FPose& Pose, TArray<FTransform>& Out, FRig& Rig)
    {
        return MarburgumGodotClip::EvaluatePose(Pose, Out, Rig.Names);
    }

    /**
     * The left arm that puts the left palm on Target (component space, centimetres) with the rest of Base as it is:
     * a coarse grid, then two finer passes round the best. A little cost for twisting the upper arm keeps the elbow
     * hanging the way a person's does.
     */
    FSolved SolveLeftArm(const FPose& Base, const FVector& Target, FRig& Rig)
    {
        FSolved Best;
        TArray<FTransform> Out;
        const auto Try = [&](const FVector& Upper, double Lower)
        {
            FPose Pose = Base;
            Pose.Add(TEXT("LeftUpperArm"), Upper);
            Pose.Add(TEXT("LeftLowerArm"), FVector(Lower, 0.0, 0.0));
            FVector Centre, Normal;
            if (!Evaluate(Pose, Out, Rig) || !Rig.Palm(Out, TEXT("l"), Centre, Normal))
            {
                return;
            }
            const double Distance = FVector::Dist(Centre, Target);
            const double Cost = Distance + 1.5 * FMath::Abs(Upper.Y) + 0.5 * FMath::Max(Upper.Z - 0.6, 0.0);
            if (Cost < Best.Residual)
            {
                Best.Residual = Cost;
                Best.Pose = MoveTemp(Pose);
            }
        };
        for (double X = -0.6; X <= 2.81; X += 0.2)
        {
            for (double Y = -1.2; Y <= 1.21; Y += 0.3)
            {
                for (double Z = -1.4; Z <= 1.21; Z += 0.2)
                {
                    for (double L = 0.0; L <= 2.51; L += 0.15)
                    {
                        Try(FVector(X, Y, Z), L);
                    }
                }
            }
        }
        for (const double Step : { 0.05, 0.0125 })
        {
            const FVector Upper = Best.Pose.FindChecked(TEXT("LeftUpperArm"));
            const double Lower = Best.Pose.FindChecked(TEXT("LeftLowerArm")).X;
            for (int32 I = -4; I <= 4; ++I)
            {
                for (int32 J = -4; J <= 4; ++J)
                {
                    for (int32 K = -4; K <= 4; ++K)
                    {
                        for (int32 M = -4; M <= 4; ++M)
                        {
                            Try(Upper + FVector(I, J, K) * Step, Lower + M * Step);
                        }
                    }
                }
            }
        }
        // The residual reported is the plain distance, not the cost.
        FVector Centre, Normal;
        if (Evaluate(Best.Pose, Out, Rig) && Rig.Palm(Out, TEXT("l"), Centre, Normal))
        {
            Best.Residual = FVector::Dist(Centre, Target);
        }
        return Best;
    }

    /**
     * The right wrist's held turn (hand_r, local), so the knuckle line runs along the rod and the fingers close across it
     * rather than along it; found by SocketFor and held in every fishing clip.
     */
    FQuat RightWrist = FQuat::Identity;

    /** Turns Side's hand and everything under it by the world rotation Turn about the wrist, in a component-space pose. */
    void TurnHand(TArray<FTransform>& Pose, const FRig& Rig, const TCHAR* Side, const FQuat& Turn)
    {
        const int32 Hand = Rig.Index(*FString::Printf(TEXT("hand_%s"), Side));
        if (Hand == INDEX_NONE)
        {
            return;
        }
        const FVector Wrist = Pose[Hand].GetLocation();
        for (int32 Bone = 0; Bone < Pose.Num(); ++Bone)
        {
            int32 Walk = Bone;
            while (Walk != INDEX_NONE && Walk != Hand)
            {
                Walk = Rig.Reference->GetRawParentIndex(Walk);
            }
            if (Walk == Hand)
            {
                Pose[Bone].SetLocation(Wrist + Turn.RotateVector(Pose[Bone].GetLocation() - Wrist));
                Pose[Bone].SetRotation((Turn * Pose[Bone].GetRotation()).GetNormalized());
            }
        }
    }

    /** A pose as the fishing clips draw it: Evaluate, then the right wrist turned by RightWrist. */
    bool EvaluateHeld(const FPose& Pose, TArray<FTransform>& Out, FRig& Rig)
    {
        if (!Evaluate(Pose, Out, Rig))
        {
            return false;
        }
        const int32 Hand = Rig.Index(TEXT("hand_r"));
        if (Hand != INDEX_NONE)
        {
            const FQuat World = Out[Hand].GetRotation();
            TurnHand(Out, Rig, TEXT("r"), World * RightWrist * World.Inverse());
        }
        return true;
    }

    /** Where the rod sits in component space for a pose: the socket on hand_r carried by that pose's (turned) hand. */
    FTransform RodInPose(const FPose& Pose, const FTransform& Socket, FRig& Rig)
    {
        TArray<FTransform> Out;
        const int32 Hand = Rig.Index(TEXT("hand_r"));
        if (!EvaluateHeld(Pose, Out, Rig) || Hand == INDEX_NONE)
        {
            return FTransform::Identity;
        }
        return Socket * Out[Hand];
    }

    /**
     * fishing_rod_r on hand_r for the hold pose: the grip in the closed right hand, the blank 32 degrees up from level,
     * a little out to the right, the reel hanging under it.
     */
    bool SocketFor(FRig& Rig, FTransform& OutSocket)
    {
        TArray<FTransform> Out;
        FVector Grip, Normal;
        // Bone names are known once a pose has been evaluated.
        const bool bEvaluated = Evaluate(HoldPose(), Out, Rig);
        const int32 Hand = Rig.Index(TEXT("hand_r"));
        const int32 IndexKnuckle = Rig.Index(TEXT("index_01_r"));
        const int32 Pinky = Rig.Index(TEXT("pinky_01_r"));
        if (!bEvaluated || Hand == INDEX_NONE || IndexKnuckle == INDEX_NONE || Pinky == INDEX_NONE)
        {
            UE_LOG(LogMarburgumNpcContent, Error, TEXT("%s cannot evaluate the hold pose on %s"), LogTag, SkeletonPath);
            return false;
        }
        // Mannequin component space: +Y forward, +Z up, -X the body's right.
        const double Pitch = FMath::DegreesToRadians(32.0);
        const FVector Along = FVector(-0.1, FMath::Cos(Pitch), FMath::Sin(Pitch)).GetSafeNormal();
        // A rod held like a torch: the wrist turns so the knuckle line runs along the blank, index toward the tip.
        const FVector Knuckles = (Out[IndexKnuckle].GetLocation() - Out[Pinky].GetLocation()).GetSafeNormal();
        const FQuat Turn = FQuat::FindBetweenNormals(Knuckles, Along);
        const FQuat World = Out[Hand].GetRotation();
        RightWrist = (World.Inverse() * Turn * World).GetNormalized();
        TurnHand(Out, Rig, TEXT("r"), Turn);
        if (!Rig.Palm(Out, TEXT("r"), Grip, Normal))
        {
            return false;
        }
        const FTransform RodInComponent(FRotationMatrix::MakeFromXZ(Along, FVector::UpVector).ToQuat(), Grip);
        OutSocket = RodInComponent.GetRelativeTransform(Out[Hand]);
        return true;
    }

    bool SaveSocket(const FTransform& Transform)
    {
        USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, SkeletonPath);
        if (!Skeleton)
        {
            return false;
        }
        USkeletalMeshSocket* Socket = Skeleton->FindSocket(Npc::RodSocket);
        if (!Socket)
        {
            Socket = NewObject<USkeletalMeshSocket>(Skeleton);
            Socket->SocketName = Npc::RodSocket;
            Skeleton->Sockets.Add(Socket);
        }
        Socket->BoneName = TEXT("hand_r");
        Socket->RelativeLocation = Transform.GetLocation();
        Socket->RelativeRotation = Transform.Rotator();
        Socket->RelativeScale = FVector::OneVector;
        Skeleton->MarkPackageDirty();
        if (!UEditorLoadingAndSavingUtils::SavePackages({ Skeleton->GetOutermost() }, false))
        {
            UE_LOG(LogMarburgumNpcContent, Error, TEXT("%s could not save the skeleton"), LogTag);
            return false;
        }
        UE_LOG(LogMarburgumNpcContent, Display, TEXT("%s socket %s on hand_r at=(%.2f, %.2f, %.2f) rot=(%s)"), LogTag, Npc::RodSocket,
            Transform.GetLocation().X, Transform.GetLocation().Y, Transform.GetLocation().Z, *Transform.Rotator().ToString());
        return true;
    }

    /**
     * Brings the right elbow in until the left palm can rest on the reel handle of the hold (2 cm or better), the
     * least far that does it; then the socket for that hold. Logs every try as evidence.
     */
    bool FitHold(FRig& Rig, FTransform& OutSocket)
    {
        double BestIn = RightArmIn;
        double BestResidual = TNumericLimits<double>::Max();
        for (double In = -0.22; In <= 0.9; In += 0.04)
        {
            RightArmIn = In;
            FTransform Socket;
            if (!SocketFor(Rig, Socket))
            {
                return false;
            }
            const FTransform Rod = RodInPose(HoldPose(), Socket, Rig);
            const FSolved Solved = SolveLeftArm(HoldPose(), Rod.TransformPosition(KnobAt(0.0)), Rig);
            UE_LOG(LogMarburgumNpcContent, Display, TEXT("%s fit right_in=%.2f left_on_knob=%.2fcm"), LogTag, In, Solved.Residual);
            if (Solved.Residual < BestResidual)
            {
                BestResidual = Solved.Residual;
                BestIn = In;
            }
            if (Solved.Residual <= 2.0)
            {
                break;
            }
        }
        RightArmIn = BestIn;
        UE_LOG(LogMarburgumNpcContent, Display, TEXT("%s fitted right_in=%.2f left_on_knob=%.2fcm"), LogTag, BestIn, BestResidual);
        return SocketFor(Rig, OutSocket) && SaveSocket(OutSocket);
    }

    /**
     * Four fingers closed round a grip by Amount (1 is a fist round a rod) and the thumb a little, in Pose's hand. Each
     * finger bends about the axis square to the finger and to the palm normal, so it closes toward the palm rather than
     * twisting about its own length; the axis is written in each bone's own frame of Pose. Logs how far the fingertips
     * end from Line (the grip) as evidence.
     */
    double CloseHand(TMap<FName, FQuat>& Held, const TArray<FTransform>& Pose, const FRig& Rig, const TCHAR* Side, double Amount,
        const FVector& LinePoint, const FVector& LineDirection, bool bLog = true)
    {
        FVector Centre, Normal;
        if (!Rig.Palm(Pose, Side, Centre, Normal))
        {
            return TNumericLimits<double>::Max();
        }
        const TArray<FTransform>& LocalRest = Rig.Reference->GetRawRefBonePose();
        double Worst = 0.0;
        const auto Chain = [&](const TCHAR* Finger, const double (&Joints)[3])
        {
            int32 Bones[3];
            for (int32 Link = 0; Link < 3; ++Link)
            {
                Bones[Link] = Rig.Index(*FString::Printf(TEXT("%s_0%d_%s"), Finger, Link + 1, Side));
                if (Bones[Link] == INDEX_NONE)
                {
                    return;
                }
            }
            const FVector Along = (Pose[Bones[1]].GetLocation() - Pose[Bones[0]].GetLocation()).GetSafeNormal();
            const FVector Axis = FVector::CrossProduct(Along, Normal).GetSafeNormal();
            // Forward kinematics of the closed finger, for the evidence line.
            FTransform At = Pose[Rig.Reference->GetRawParentIndex(Bones[0])];
            for (int32 Link = 0; Link < 3; ++Link)
            {
                const FVector Local = Pose[Bones[Link]].GetRotation().Inverse().RotateVector(Axis);
                const FQuat Turn(Local, Joints[Link] * Amount);
                Held.Add(Rig.Names[Bones[Link]], Turn);
                FTransform Bone = LocalRest[Bones[Link]];
                Bone.SetRotation(Bone.GetRotation() * Turn);
                At = Bone * At;
            }
            const FVector Tip = At.TransformPosition(LocalRest[Bones[2]].GetLocation());
            Worst = FMath::Max(Worst, FMath::PointDistToLine(Tip, LineDirection, LinePoint));
        };
        const double FingerJoints[3] = { 1.0, 1.25, 0.75 };
        for (const TCHAR* Finger : { TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky") })
        {
            Chain(Finger, FingerJoints);
        }
        const double ThumbJoints[3] = { 0.0, 0.35, 0.45 };
        Chain(TEXT("thumb"), ThumbJoints);
        if (bLog)
        {
            UE_LOG(LogMarburgumNpcContent, Display, TEXT("%s close %s amount=%.2f palm=(%.2f, %.2f, %.2f) furthest_tip_from_grip=%.1fcm"), LogTag, Side,
                Amount, Normal.X, Normal.Y, Normal.Z, Worst);
        }
        return Worst;
    }

    /** The closing amount (0.5 to 1.6) that brings every fingertip nearest the grip, then that hand closed. */
    void CloseHandTightest(TMap<FName, FQuat>& Held, const TArray<FTransform>& Pose, const FRig& Rig, const TCHAR* Side,
        const FVector& LinePoint, const FVector& LineDirection)
    {
        double BestAmount = 1.0;
        double BestWorst = TNumericLimits<double>::Max();
        for (double Amount = 0.5; Amount <= 1.601; Amount += 0.05)
        {
            TMap<FName, FQuat> Trial;
            const double Worst = CloseHand(Trial, Pose, Rig, Side, Amount, LinePoint, LineDirection, false);
            if (Worst < BestWorst)
            {
                BestWorst = Worst;
                BestAmount = Amount;
            }
        }
        CloseHand(Held, Pose, Rig, Side, BestAmount, LinePoint, LineDirection);
    }

    /** One track per bone any key names (a bone a key leaves out is at rest in that key), then Build. */
    bool BuildClip(const TCHAR* Package, const TCHAR* Title, double Seconds, bool bLoop, const TArray<FPose>& Keys,
        const TArray<double>& KeyTimes, const TMap<FName, FQuat>& Held)
    {
        // FTrack holds its bone name as a TCHAR*: these strings outlive the build.
        TArray<FString> Bones;
        for (const FPose& Key : Keys)
        {
            for (const TPair<FString, FVector>& Pair : Key)
            {
                Bones.AddUnique(Pair.Key);
            }
        }
        MarburgumGodotClip::FClip Clip;
        Clip.Package = Package;
        Clip.Title = Title;
        Clip.LengthSeconds = Seconds;
        Clip.bLoop = bLoop;
        Clip.KeyTimes = KeyTimes;
        Clip.HeldLocal = Held;
        for (const FString& Bone : Bones)
        {
            MarburgumGodotClip::FTrack Track{ *Bone, {} };
            for (const FPose& Key : Keys)
            {
                Track.Keys.Add(Key.FindRef(Bone));
            }
            Clip.Tracks.Add(MoveTemp(Track));
        }
        return MarburgumGodotClip::Build(Clip, LogTag) != nullptr;
    }

    /** Each key's left arm searched onto TargetOf(key's rod placement); the residuals are the evidence. */
    bool SolveKeys(TArray<FPose>& Keys, const FTransform& Socket, FRig& Rig, TFunctionRef<FVector(int32 Key)> RodLocalTarget,
        double MaxResidual, const TCHAR* Clip)
    {
        double Worst = 0.0;
        for (int32 Key = 0; Key < Keys.Num(); ++Key)
        {
            const FTransform Rod = RodInPose(Keys[Key], Socket, Rig);
            const FVector Target = Rod.TransformPosition(RodLocalTarget(Key));
            const FSolved Solved = SolveLeftArm(Keys[Key], Target, Rig);
            if (Solved.Residual <= MaxResidual)
            {
                Keys[Key] = Solved.Pose;
            }
            else
            {
                FreeLeftArm(Keys[Key]);
            }
            Worst = FMath::Max(Worst, Solved.Residual <= MaxResidual ? Solved.Residual : 0.0);
            const FVector Upper = Solved.Pose.FindRef(TEXT("LeftUpperArm"));
            UE_LOG(LogMarburgumNpcContent, Display, TEXT("%s solve %s key=%d residual=%.2fcm %s upper=(%.3f, %.3f, %.3f) lower=%.3f"), LogTag, Clip,
                Key, Solved.Residual, Solved.Residual <= MaxResidual ? TEXT("on_rod") : TEXT("free"), Upper.X, Upper.Y, Upper.Z,
                Solved.Pose.FindRef(TEXT("LeftLowerArm")).X);
        }
        UE_LOG(LogMarburgumNpcContent, Display, TEXT("%s solved %s keys=%d worst_on_rod=%.2fcm"), LogTag, Clip, Keys.Num(), Worst);
        return true;
    }

    bool BuildAnimations(FRig& Rig)
    {
        FTransform Socket;
        if (!FitHold(Rig, Socket))
        {
            return false;
        }
        TArray<FTransform> Out;
        if (!EvaluateHeld(HoldPose(), Out, Rig))
        {
            return false;
        }
        UE_LOG(LogMarburgumNpcContent, Display, TEXT("%s wrist r turn=%.1fdeg"), LogTag, FMath::RadiansToDegrees(RightWrist.GetAngle()));
        // The right hand closes round the grip, in the hold pose, its wrist held turned in every fishing clip.
        const FTransform HoldRod = RodInPose(HoldPose(), Socket, Rig);
        TMap<FName, FQuat> Grip;
        CloseHandTightest(Grip, Out, Rig, TEXT("r"), HoldRod.GetLocation(), HoldRod.GetUnitAxis(EAxis::X));
        Grip.Add(TEXT("hand_r"), RightWrist);

        bool bOk = true;
        const auto OnKnob = [](double Turn) { return [Turn](int32) { return KnobAt(Turn); }; };

        // Hold: standing with the rod, breathing.
        {
            TArray<FPose> Keys = { HoldPose(0.0), HoldPose(1.0), HoldPose(0.0) };
            SolveKeys(Keys, Socket, Rig, OnKnob(0.0), 2.5, TEXT("hold"));
            TArray<FTransform> Left;
            Evaluate(Keys[0], Left, Rig);
            const FTransform Rod = RodInPose(Keys[0], Socket, Rig);
            const FVector Knob = Rod.TransformPosition(KnobAt(0.0));
            CloseHandTightest(Grip, Left, Rig, TEXT("l"), Knob, Rod.TransformVector(FVector::RightVector).GetSafeNormal());
            bOk &= BuildClip(TEXT("/Game/Characters/Animations/A_FishHold"), TEXT("Marburgum fishing hold"), Npc::FishHoldLoopSeconds, true, Keys, {},
                Grip);
        }
        // Wait: the line is out; the rod tip dips and lifts a little, the head watches the water.
        {
            TArray<FPose> Keys = { HoldPose(0.0), HoldPose(0.5), HoldPose(1.0), HoldPose(0.0) };
            Keys[1].Add(TEXT("RightLowerArm"), FVector(1.06, 0.0, 0.0));
            Keys[1].Add(TEXT("Head"), FVector(0.19, 0.05, 0.0));
            Keys[2].Add(TEXT("RightLowerArm"), FVector(1.16, 0.0, 0.0));
            Keys[2].Add(TEXT("Head"), FVector(0.16, -0.06, 0.0));
            SolveKeys(Keys, Socket, Rig, OnKnob(0.0), 2.5, TEXT("wait"));
            bOk &= BuildClip(TEXT("/Game/Characters/Animations/A_FishWait"), TEXT("Marburgum fishing wait"), Npc::FishWaitLoopSeconds, true, Keys, {},
                Grip);
        }
        // Reel: the rod lifted, the left hand turning the handle once per loop.
        {
            constexpr int32 Steps = 8;
            TArray<FPose> Keys;
            for (int32 Step = 0; Step <= Steps; ++Step)
            {
                FPose Key = HoldPose(0.0);
                Key.Add(TEXT("RightUpperArm"), FVector(0.48, 0.0, RightArmIn));
                Key.Add(TEXT("RightLowerArm"), FVector(1.3 + 0.02 * FMath::Sin(UE_TWO_PI * Step / Steps), 0.0, 0.0));
                Keys.Add(Key);
            }
            SolveKeys(Keys, Socket, Rig, [](int32 Key) { return KnobAt(UE_TWO_PI * (Key % Steps) / Steps); }, 2.5, TEXT("reel"));
            Keys.Last() = Keys[0];
            bOk &= BuildClip(TEXT("/Game/Characters/Animations/A_FishReel"), TEXT("Marburgum fishing reel"), Npc::FishReelLoopSeconds, true, Keys, {},
                Grip);
        }
        // Cast: rod back over the shoulder, a flick forward, follow through, back to the hold. The right arm is keyed;
        // the left follows the rod butt for as long as it can reach it, sampled finely so it stays on it.
        {
            FPose Wind = HoldPose(0.0);
            Wind.Add(TEXT("Spine"), FVector(-0.04, 0.22, 0.0));
            Wind.Add(TEXT("Head"), FVector(0.02, 0.1, 0.0));
            Wind.Add(TEXT("RightUpperArm"), FVector(2.35, 0.0, RightArmIn - 0.2));
            Wind.Add(TEXT("RightLowerArm"), FVector(1.55, 0.0, 0.0));
            FPose Release = HoldPose(0.0);
            Release.Add(TEXT("Spine"), FVector(0.16, -0.12, 0.0));
            Release.Add(TEXT("Head"), FVector(0.08, -0.05, 0.0));
            Release.Add(TEXT("RightUpperArm"), FVector(1.55, 0.0, RightArmIn));
            Release.Add(TEXT("RightLowerArm"), FVector(0.4, 0.0, 0.0));
            FPose Follow = HoldPose(0.0);
            Follow.Add(TEXT("Spine"), FVector(0.12, -0.05, 0.0));
            Follow.Add(TEXT("RightUpperArm"), FVector(0.85, 0.0, RightArmIn));
            Follow.Add(TEXT("RightLowerArm"), FVector(0.65, 0.0, 0.0));
            const TArray<FPose> Designed = { HoldPose(0.0), Wind, Release, Follow, HoldPose(0.0) };
            const TArray<double> DesignedTimes = { 0.0, 0.75, 1.1, 1.55, Npc::FishCastSeconds };
            TArray<FPose> Keys;
            TArray<double> Times;
            constexpr int32 Samples = 13;
            for (int32 Sample = 0; Sample < Samples; ++Sample)
            {
                const double T = Npc::FishCastSeconds * Sample / (Samples - 1);
                FPose Key;
                for (const TPair<FString, FVector>& Pair : Designed[0])
                {
                    TArray<FVector> Values;
                    for (const FPose& Pose : Designed)
                    {
                        Values.Add(Pose.FindRef(Pair.Key));
                    }
                    Key.Add(Pair.Key, MarburgumGodotClip::SampleTimed(Values, DesignedTimes, T));
                }
                Keys.Add(Key);
                Times.Add(T);
            }
            SolveKeys(Keys, Socket, Rig, [](int32) { return FVector(ButtX + 4.0, 0.0, 0.0); }, 4.0, TEXT("cast"));
            // The first and last keys are the hold: the left hand back on the knob.
            for (const int32 Key : { 0, Samples - 1 })
            {
                const FTransform Rod = RodInPose(Keys[Key], Socket, Rig);
                Keys[Key] = SolveLeftArm(Keys[Key], Rod.TransformPosition(KnobAt(0.0)), Rig).Pose;
            }
            bOk &= BuildClip(TEXT("/Game/Characters/Animations/A_FishCast"), TEXT("Marburgum fishing cast"), Npc::FishCastSeconds, false, Keys, Times,
                Grip);
        }
        // Talk: the open-handed gestures of somebody explaining a street to a stranger.
        {
            const auto Key = [](double RU, double RUY, double RL, double LU, double LL, double HeadX, double HeadY, double Spine)
            {
                return FPose{
                    { TEXT("Spine"), FVector(Spine, 0.0, 0.0) },
                    { TEXT("Head"), FVector(HeadX, HeadY, 0.0) },
                    { TEXT("RightUpperArm"), FVector(RU, RUY, -0.18) },
                    { TEXT("RightLowerArm"), FVector(RL, 0.0, 0.0) },
                    { TEXT("LeftUpperArm"), FVector(LU, 0.0, 0.1) },
                    { TEXT("LeftLowerArm"), FVector(LL, 0.0, 0.0) },
                };
            };
            const TArray<FPose> Keys = {
                Key(0.22, 0.0, 0.85, 0.1, 0.45, 0.02, 0.06, 0.03),
                Key(0.42, 0.12, 1.35, 0.14, 0.6, -0.04, -0.04, 0.05),
                Key(0.3, -0.05, 1.0, 0.32, 1.15, 0.05, 0.1, 0.02),
                Key(0.48, -0.12, 1.25, 0.12, 0.55, -0.02, -0.08, 0.05),
                Key(0.22, 0.0, 0.85, 0.1, 0.45, 0.02, 0.06, 0.03),
            };
            bOk &= BuildClip(TEXT("/Game/Characters/Animations/A_NpcTalk"), TEXT("Marburgum conversation gestures"), Npc::TalkLoopSeconds, true, Keys,
                {}, {});
        }
        return bOk;
    }
}

UMarburgumNpcContentCommandlet::UMarburgumNpcContentCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UMarburgumNpcContentCommandlet::Main(const FString& Params)
{
    using namespace MarburgumNpcContent;
    FString Steps = TEXT("rod,animation");
    FParse::Value(*Params, TEXT("Steps="), Steps, false);
    TArray<FString> List;
    Steps.ParseIntoArray(List, TEXT(","));
    const USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, SkeletonPath);
    if (!Skeleton)
    {
        UE_LOG(LogMarburgumNpcContent, Error, TEXT("%s missing %s"), LogTag, SkeletonPath);
        return 1;
    }
    FRig Rig;
    Rig.Reference = &Skeleton->GetReferenceSkeleton();
    bool bOk = true;
    for (const FString& Step : List)
    {
        if (Step == TEXT("rod"))
        {
            bOk &= BuildRodMesh();
        }
        else if (Step == TEXT("animation"))
        {
            bOk &= BuildAnimations(Rig);
        }
        else
        {
            UE_LOG(LogMarburgumNpcContent, Error, TEXT("%s unknown step %s"), LogTag, *Step);
            bOk = false;
        }
    }
    UE_LOG(LogMarburgumNpcContent, Display, TEXT("%s result=%d"), LogTag, bOk ? 0 : 1);
    return bOk ? 0 : 1;
}
