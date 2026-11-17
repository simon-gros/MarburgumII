#include "MarburgumStreetLightContent.h"

#include "AssetCompilingManager.h"
#include "Engine/StaticMesh.h"
#include "FileHelpers.h"
#include "MarburgumStreetLights.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionEyeAdaptationInverse.h"
#include "Materials/MaterialExpressionPerInstanceCustomData.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "MeshDescription.h"
#include "MeshDescriptionBuilder.h"
#include "Misc/PackageName.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumStreetLightContent, Log, All);

namespace MarburgumStreetLightContent
{
    namespace Lights = MarburgumII::StreetLights;

    namespace
    {
        /** Linear colour; alpha is how metallic. */
        const FVector4f Galvanised(0.40f, 0.41f, 0.42f, 0.85f);
        const FVector4f LuminaireGrey(0.28f, 0.29f, 0.30f, 0.35f);
        const FVector4f LanternGreen(0.025f, 0.05f, 0.038f, 0.2f);

        constexpr int32 Sides = 8;

        UPackage* OpenPackage(const TCHAR* PackageName)
        {
            UPackage* Package = FPackageName::DoesPackageExist(PackageName) ? LoadPackage(nullptr, PackageName, LOAD_None) : CreatePackage(PackageName);
            if (Package)
            {
                Package->FullyLoad();
            }
            return Package;
        }

        template <typename T>
        T* Expression(UMaterial* Material)
        {
            T* Node = NewObject<T>(Material);
            Material->GetExpressionCollection().AddExpression(Node);
            return Node;
        }

        UMaterial* OpenMaterial(const TCHAR* PackageName, TArray<UPackage*>& Dirty, FString& OutError)
        {
            UPackage* Package = OpenPackage(PackageName);
            const FString Name = FPackageName::GetShortName(PackageName);
            UMaterial* Material = Package ? FindObject<UMaterial>(Package, *Name) : nullptr;
            if (Material)
            {
                Material->Modify();
                Material->GetExpressionCollection().Empty();
                UMaterialEditorOnlyData* Ed = Material->GetEditorOnlyData();
                Ed->BaseColor = FColorMaterialInput();
                Ed->Metallic = FScalarMaterialInput();
                Ed->Roughness = FScalarMaterialInput();
                Ed->EmissiveColor = FColorMaterialInput();
            }
            else if (Package)
            {
                Material = NewObject<UMaterial>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
            }
            if (!Material)
            {
                OutError = FString::Printf(TEXT("could not create %s"), PackageName);
                return nullptr;
            }
            Material->MaterialDomain = MD_Surface;
            Material->BlendMode = BLEND_Opaque;
            Material->SetShadingModel(MSM_DefaultLit);
            Material->bUsedWithInstancedStaticMeshes = true;
            Material->bUsedWithStaticLighting = false;
            Dirty.AddUnique(Package);
            return Material;
        }

        void Finish(UMaterial* Material)
        {
            Material->PreEditChange(nullptr);
            Material->PostEditChange();
            Material->MarkPackageDirty();
        }

        UMaterial* WriteMetal(TArray<UPackage*>& Dirty, FString& OutError)
        {
            UMaterial* Material = OpenMaterial(Lights::MetalMaterialPackage, Dirty, OutError);
            if (!Material)
            {
                return nullptr;
            }
            // The mesh carries the colour, and in its alpha how metallic: galvanised steel or painted cast iron.
            UMaterialExpressionVertexColor* Paint = Expression<UMaterialExpressionVertexColor>(Material);
            UMaterialExpressionConstant* Rough = Expression<UMaterialExpressionConstant>(Material);
            Rough->R = 0.55f;
            UMaterialEditorOnlyData* Ed = Material->GetEditorOnlyData();
            Ed->BaseColor.Connect(0, Paint);
            Ed->Metallic.Connect(4, Paint);
            Ed->Roughness.Connect(0, Rough);
            Finish(Material);
            return Material;
        }

        UMaterial* WriteGlass(TArray<UPackage*>& Dirty, FString& OutError)
        {
            UMaterial* Material = OpenMaterial(Lights::GlassMaterialPackage, Dirty, OutError);
            if (!Material)
            {
                return nullptr;
            }
            UMaterialExpressionConstant3Vector* Milky = Expression<UMaterialExpressionConstant3Vector>(Material);
            Milky->Constant = FLinearColor(0.62f, 0.60f, 0.55f);
            UMaterialExpressionConstant* Smooth = Expression<UMaterialExpressionConstant>(Material);
            Smooth->R = 0.15f;

            // The lamp's state, per instance: output, then the light's colour.
            UMaterialExpression* Data[Lights::CustomDataFloats];
            for (int32 Index = 0; Index < Lights::CustomDataFloats; ++Index)
            {
                UMaterialExpressionPerInstanceCustomData* Node = Expression<UMaterialExpressionPerInstanceCustomData>(Material);
                Node->DataIndex = static_cast<uint32>(Index);
                Node->ConstDefaultValue = 0.0f;
                Data[Index] = Node;
            }
            UMaterialExpressionCustom* Glow = Expression<UMaterialExpressionCustom>(Material);
            Glow->Description = TEXT("StreetLampGlow");
            Glow->Code = FString::Printf(TEXT("return float3(R, G, B) * saturate(Output) * %.3f;"), Lights::GlassGlowOnScreen);
            Glow->OutputType = CMOT_Float3;
            Glow->Inputs.Empty();
            const TCHAR* const Names[] = { TEXT("Output"), TEXT("R"), TEXT("G"), TEXT("B") };
            for (int32 Index = 0; Index < Lights::CustomDataFloats; ++Index)
            {
                FCustomInput& Input = Glow->Inputs.AddDefaulted_GetRef();
                Input.InputName = FName(Names[Index]);
                Input.Input.Connect(0, Data[Index]);
            }
            // Bright on screen whatever the eye's exposure, as the weather's curtain is.
            UMaterialExpressionEyeAdaptationInverse* OnScreen = Expression<UMaterialExpressionEyeAdaptationInverse>(Material);
            OnScreen->LightValueInput.Connect(0, Glow);

            UMaterialEditorOnlyData* Ed = Material->GetEditorOnlyData();
            Ed->BaseColor.Connect(0, Milky);
            Ed->Roughness.Connect(0, Smooth);
            Ed->EmissiveColor.Connect(0, OnScreen);
            Finish(Material);
            return Material;
        }

        /** Flat-shaded solids in two sections, metal and glass. */
        struct FShape
        {
            FMeshDescription Description;
            FMeshDescriptionBuilder Builder;
            FPolygonGroupID Metal;
            FPolygonGroupID Glass;
            int32 Triangles = 0;

            FShape()
            {
                FStaticMeshAttributes(Description).Register();
                Builder.SetMeshDescription(&Description);
                Builder.EnablePolyGroups();
                Builder.SetNumUVLayers(1);
                Metal = Builder.AppendPolygonGroup(TEXT("Metal"));
                Glass = Builder.AppendPolygonGroup(TEXT("Glass"));
            }

            /**
             * A flat quad facing Outward, its corners in order round it either way. Wound so its front faces Outward: the
             * curtain's rule (MarburgumWeatherLookContent), where corners whose diagonal cross points away from the side
             * to be seen are taken 0, 2, 1. A quad with two equal corners is a triangle; the build drops the empty half.
             */
            void Quad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Outward, const FVector4f& Colour, FPolygonGroupID Group)
            {
                const FVector Cross = FVector::CrossProduct(C - A, D - B);
                const bool bFlip = FVector::DotProduct(Cross, Outward) < 0.0;
                const FVector Normal = (bFlip ? -Cross : Cross).GetSafeNormal();
                const FVector Corners[4] = { A, B, C, D };
                const FVector2D UVs[4] = { FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) };
                FVertexInstanceID Ids[4];
                for (int32 K = 0; K < 4; ++K)
                {
                    Ids[K] = Builder.AppendInstance(Builder.AppendVertex(Corners[K]));
                    Builder.SetInstanceNormal(Ids[K], Normal);
                    Builder.SetInstanceUV(Ids[K], UVs[K], 0);
                    Builder.SetInstanceColor(Ids[K], Colour);
                }
                if (bFlip)
                {
                    Builder.AppendTriangle(Ids[0], Ids[2], Ids[1], Group);
                    Builder.AppendTriangle(Ids[0], Ids[3], Ids[2], Group);
                }
                else
                {
                    Builder.AppendTriangle(Ids[0], Ids[1], Ids[2], Group);
                    Builder.AppendTriangle(Ids[0], Ids[2], Ids[3], Group);
                }
                Triangles += 2;
            }

            /** A tapered prism between two rings about an axis from Foot to Head; capped at both ends. */
            void Prism(const FVector& Foot, const FVector& Head, double FootRadius, double HeadRadius, const FVector4f& Colour, FPolygonGroupID Group)
            {
                const FVector Axis = (Head - Foot).GetSafeNormal();
                const FVector Side = FMath::Abs(Axis.Z) > 0.9 ? FVector::XAxisVector : FVector::ZAxisVector;
                const FVector U = FVector::CrossProduct(Side, Axis).GetSafeNormal();
                const FVector V = FVector::CrossProduct(Axis, U);
                auto Ring = [&](const FVector& Centre, double Radius, int32 K)
                {
                    const double A = UE_TWO_PI * K / Sides;
                    return Centre + (U * FMath::Cos(A) + V * FMath::Sin(A)) * Radius;
                };
                for (int32 K = 0; K < Sides; ++K)
                {
                    const FVector Middle = (Ring(Foot, FootRadius, K) + Ring(Foot, FootRadius, K + 1) + Ring(Head, HeadRadius, K) + Ring(Head, HeadRadius, K + 1)) * 0.25;
                    const FVector Out = Middle - (Foot + Head) * 0.5 - Axis * FVector::DotProduct(Middle - (Foot + Head) * 0.5, Axis);
                    Quad(Ring(Foot, FootRadius, K), Ring(Foot, FootRadius, K + 1), Ring(Head, HeadRadius, K + 1), Ring(Head, HeadRadius, K), Out, Colour, Group);
                }
                // Caps as fans of triangles round the centre.
                for (int32 K = 0; K < Sides; ++K)
                {
                    if (HeadRadius > 0.0)
                    {
                        Quad(Head, Head, Ring(Head, HeadRadius, K), Ring(Head, HeadRadius, K + 1), Axis, Colour, Group);
                    }
                    Quad(Foot, Foot, Ring(Foot, FootRadius, K + 1), Ring(Foot, FootRadius, K), -Axis, Colour, Group);
                }
            }

            /** An axis-aligned box from Min to Max. */
            void Box(const FVector& Min, const FVector& Max, const FVector4f& Colour, FPolygonGroupID Group)
            {
                const FVector P[8] = {
                    FVector(Min.X, Min.Y, Min.Z), FVector(Max.X, Min.Y, Min.Z), FVector(Max.X, Max.Y, Min.Z), FVector(Min.X, Max.Y, Min.Z),
                    FVector(Min.X, Min.Y, Max.Z), FVector(Max.X, Min.Y, Max.Z), FVector(Max.X, Max.Y, Max.Z), FVector(Min.X, Max.Y, Max.Z),
                };
                Quad(P[0], P[3], P[2], P[1], -FVector::ZAxisVector, Colour, Group);
                Quad(P[4], P[5], P[6], P[7], FVector::ZAxisVector, Colour, Group);
                Quad(P[0], P[1], P[5], P[4], -FVector::YAxisVector, Colour, Group);
                Quad(P[1], P[2], P[6], P[5], FVector::XAxisVector, Colour, Group);
                Quad(P[2], P[3], P[7], P[6], FVector::YAxisVector, Colour, Group);
                Quad(P[3], P[0], P[4], P[7], -FVector::XAxisVector, Colour, Group);
            }
        };

        void BuildMast(FShape& S)
        {
            const float Top = Lights::MastHeightCm;
            // The base, where the door is, then the pole tapering up to the arm.
            S.Prism(FVector(0, 0, 0), FVector(0, 0, 110), 12.5, 11.0, Galvanised, S.Metal);
            S.Prism(FVector(0, 0, 110), FVector(0, 0, Top - 25), 9.0, 5.5, Galvanised, S.Metal);
            // The arm: up and out, then nearly level to the luminaire.
            S.Prism(FVector(0, 0, Top - 40), FVector(55, 0, Top - 2), 4.5, 4.0, Galvanised, S.Metal);
            S.Prism(FVector(55, 0, Top - 2), FVector(Lights::MastReachCm - 25, 0, Top + 10), 4.0, 3.6, Galvanised, S.Metal);
            // The luminaire: a flat housing, its bowl underneath.
            const float Reach = Lights::MastReachCm;
            S.Box(FVector(Reach - 32, -15, Top + 2), FVector(Reach + 38, 15, Top + 16), LuminaireGrey, S.Metal);
            S.Box(FVector(Reach - 24, -11, Top - 6), FVector(Reach + 30, 11, Top + 2.5), FVector4f(1, 1, 1, 0), S.Glass);
        }

        void BuildLantern(FShape& S)
        {
            const float Top = Lights::LanternHeightCm;
            S.Prism(FVector(0, 0, 0), FVector(0, 0, 45), 10.0, 8.0, LanternGreen, S.Metal);
            S.Prism(FVector(0, 0, 45), FVector(0, 0, Top - 5), 6.0, 4.5, LanternGreen, S.Metal);
            S.Prism(FVector(0, 0, Top - 5), FVector(0, 0, Top + 2), 9.0, 9.0, LanternGreen, S.Metal);
            // The lantern, widening upwards, under its roof and finial.
            S.Prism(FVector(0, 0, Top + 2), FVector(0, 0, Top + 45), 11.0, 16.0, FVector4f(1, 1, 1, 0), S.Glass);
            S.Prism(FVector(0, 0, Top + 45), FVector(0, 0, Top + 50), 19.0, 19.0, LanternGreen, S.Metal);
            S.Prism(FVector(0, 0, Top + 50), FVector(0, 0, Top + 68), 19.0, 3.0, LanternGreen, S.Metal);
            S.Prism(FVector(0, 0, Top + 68), FVector(0, 0, Top + 76), 2.5, 1.5, LanternGreen, S.Metal);
        }

        UStaticMesh* WriteMesh(const TCHAR* PackageName, bool bMast, UMaterial* Metal, UMaterial* Glass, TArray<UPackage*>& Dirty, FString& OutError)
        {
            UPackage* Package = OpenPackage(PackageName);
            const FString Name = FPackageName::GetShortName(PackageName);
            UStaticMesh* Mesh = Package ? FindObject<UStaticMesh>(Package, *Name) : nullptr;
            if (Mesh)
            {
                FAssetCompilingManager::Get().FinishAllCompilation();
                Mesh->Modify();
            }
            else if (Package)
            {
                Mesh = NewObject<UStaticMesh>(Package, *Name, RF_Public | RF_Standalone);
            }
            if (!Mesh)
            {
                OutError = FString::Printf(TEXT("could not create %s"), PackageName);
                return nullptr;
            }
            FShape Shape;
            bMast ? BuildMast(Shape) : BuildLantern(Shape);
            const int32 Triangles = Shape.Triangles;

            Mesh->SetNumSourceModels(1);
            FStaticMeshSourceModel& Source = Mesh->GetSourceModel(0);
            Source.BuildSettings.bRecomputeNormals = false;
            Source.BuildSettings.bRecomputeTangents = true;
            Source.BuildSettings.bRemoveDegenerates = true;
            Source.BuildSettings.bGenerateLightmapUVs = false;
            Mesh->CreateMeshDescription(0, MoveTemp(Shape.Description));
            Mesh->CommitMeshDescription(0);
            Mesh->SetStaticMaterials({ FStaticMaterial(Metal, TEXT("Metal"), TEXT("Metal")), FStaticMaterial(Glass, TEXT("Glass"), TEXT("Glass")) });
            Mesh->GetNaniteSettings().bEnabled = false;

            // The post, and only the post, blocks.
            Mesh->CreateBodySetup();
            UBodySetup* Body = Mesh->GetBodySetup();
            Body->Modify();
            Body->AggGeom.EmptyElements();
            FKSphylElem Post;
            const float Radius = bMast ? Lights::MastPostRadiusCm + 2.0f : Lights::LanternPostRadiusCm + 3.0f;
            const float Height = bMast ? Lights::MastHeightCm - 20.0f : Lights::LanternHeightCm + 40.0f;
            Post.Radius = Radius;
            Post.Length = Height - 2.0f * Radius;
            Post.Center = FVector(0.0, 0.0, Height * 0.5);
            Body->AggGeom.SphylElems.Add(Post);
            Body->CollisionTraceFlag = CTF_UseSimpleAsComplex;
            Body->bNeverNeedsCookedCollisionData = false;
            Body->CreatePhysicsMeshes();

            Mesh->Build(true);
            Mesh->PostEditChange();
            FAssetCompilingManager::Get().FinishAllCompilation();
            Mesh->MarkPackageDirty();
            Dirty.AddUnique(Package);
            UE_LOG(LogMarburgumStreetLightContent, Display, TEXT("MB2LAMPCONTENT %s triangles=%d post_radius_cm=%.0f post_height_cm=%.0f"), *Name, Triangles, Radius, Height);
            return Mesh;
        }
    }

    bool Write(FString& OutError)
    {
        TArray<UPackage*> Dirty;
        UMaterial* Metal = WriteMetal(Dirty, OutError);
        UMaterial* Glass = Metal ? WriteGlass(Dirty, OutError) : nullptr;
        if (!Glass || !WriteMesh(Lights::MastMeshPackage, true, Metal, Glass, Dirty, OutError)
            || !WriteMesh(Lights::LanternMeshPackage, false, Metal, Glass, Dirty, OutError))
        {
            return false;
        }
        FAssetCompilingManager::Get().FinishAllCompilation();
        if (!UEditorLoadingAndSavingUtils::SavePackages(Dirty, false))
        {
            OutError = TEXT("could not save the street lamps");
            return false;
        }
        UE_LOG(LogMarburgumStreetLightContent, Display, TEXT("MB2LAMPCONTENT saved=%d mast=%s lantern=%s"), Dirty.Num(), Lights::MastMesh, Lights::LanternMesh);
        return true;
    }
}
