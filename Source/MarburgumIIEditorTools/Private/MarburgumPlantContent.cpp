#include "MarburgumPlantContent.h"

#include "AssetCompilingManager.h"
#include "AssetToolsModule.h"
#include "Engine/StaticMesh.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "MarburgumStreetFurniture.h"
#include "MarburgumStreetLights.h"
#include "MarburgumVegetation.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MeshDescription.h"
#include "MeshDescriptionBuilder.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumPlantContent, Log, All);

namespace MarburgumPlantContent
{
    namespace Vegetation = MarburgumII::Vegetation;

    namespace
    {
        const FName LeafColour(TEXT("LeafColour"));
        const FName BarkColour(TEXT("BarkColour"));
        /** The glTF import's colour parameter on the original's instances. */
        const FName GltfColour(TEXT("BaseColorFactor"));
        /** Light let through a leaf, as a share of its colour. */
        constexpr float Transmission = 0.6f;
        constexpr float LeafRoughness = 0.85f;
        constexpr float WoodRoughness = 0.92f;
        /** Where an original instance has no colour: the catalogue's middle green and a grey-brown bark. */
        const FLinearColor FallbackLeaf(0.30f, 0.46f, 0.20f);
        const FLinearColor FallbackBark(0.40f, 0.33f, 0.25f);

        template <typename T>
        T* Expression(UMaterial* Material)
        {
            T* Node = NewObject<T>(Material);
            Material->GetExpressionCollection().AddExpression(Node);
            return Node;
        }

        UMaterial* OpenMaterial(const TCHAR* PackageName, TArray<UPackage*>& Dirty, FString& OutError)
        {
            UPackage* Package = FPackageName::DoesPackageExist(PackageName) ? LoadPackage(nullptr, PackageName, LOAD_None) : CreatePackage(PackageName);
            if (Package)
            {
                Package->FullyLoad();
            }
            const FString Name = FPackageName::GetShortName(PackageName);
            UMaterial* Material = Package ? FindObject<UMaterial>(Package, *Name) : nullptr;
            if (Material)
            {
                Material->Modify();
                Material->GetExpressionCollection().Empty();
                UMaterialEditorOnlyData* Ed = Material->GetEditorOnlyData();
                Ed->BaseColor = FColorMaterialInput();
                Ed->Roughness = FScalarMaterialInput();
                Ed->SubsurfaceColor = FColorMaterialInput();
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

        UMaterial* WriteFoliage(TArray<UPackage*>& Dirty, FString& OutError)
        {
            UMaterial* Material = OpenMaterial(FoliageMaterialPackage, Dirty, OutError);
            if (!Material)
            {
                return nullptr;
            }
            Material->TwoSided = true;
            Material->SetShadingModel(MSM_TwoSidedFoliage);
            UMaterialExpressionVectorParameter* Colour = Expression<UMaterialExpressionVectorParameter>(Material);
            Colour->ParameterName = LeafColour;
            Colour->DefaultValue = FallbackLeaf;
            UMaterialExpressionConstant* Share = Expression<UMaterialExpressionConstant>(Material);
            Share->R = Transmission;
            UMaterialExpressionMultiply* Through = Expression<UMaterialExpressionMultiply>(Material);
            Through->A.Connect(0, Colour);
            Through->B.Connect(0, Share);
            UMaterialExpressionConstant* Rough = Expression<UMaterialExpressionConstant>(Material);
            Rough->R = LeafRoughness;
            UMaterialEditorOnlyData* Ed = Material->GetEditorOnlyData();
            Ed->BaseColor.Connect(0, Colour);
            Ed->SubsurfaceColor.Connect(0, Through);
            Ed->Roughness.Connect(0, Rough);
            Finish(Material);
            return Material;
        }

        UMaterial* WriteBark(TArray<UPackage*>& Dirty, FString& OutError)
        {
            UMaterial* Material = OpenMaterial(BarkMaterialPackage, Dirty, OutError);
            if (!Material)
            {
                return nullptr;
            }
            Material->TwoSided = false;
            Material->SetShadingModel(MSM_DefaultLit);
            UMaterialExpressionVectorParameter* Colour = Expression<UMaterialExpressionVectorParameter>(Material);
            Colour->ParameterName = BarkColour;
            Colour->DefaultValue = FallbackBark;
            UMaterialExpressionConstant* Rough = Expression<UMaterialExpressionConstant>(Material);
            Rough->R = WoodRoughness;
            UMaterialEditorOnlyData* Ed = Material->GetEditorOnlyData();
            Ed->BaseColor.Connect(0, Colour);
            Ed->Roughness.Connect(0, Rough);
            Finish(Material);
            return Material;
        }

        UMaterialInstanceConstant* WriteInstance(const FString& Directory, const FString& Name, UMaterial* Parent, FName Parameter, const FLinearColor& Colour, TArray<UPackage*>& Dirty)
        {
            const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *Directory, *Name, *Name);
            UMaterialInstanceConstant* Instance = LoadObject<UMaterialInstanceConstant>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
            if (!Instance)
            {
                IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
                UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
                Factory->InitialParent = Parent;
                Instance = Cast<UMaterialInstanceConstant>(Tools.CreateAsset(Name, Directory, UMaterialInstanceConstant::StaticClass(), Factory));
            }
            if (!Instance)
            {
                return nullptr;
            }
            Instance->SetParentEditorOnly(Parent);
            Instance->ClearParameterValuesEditorOnly();
            Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(Parameter), Colour);
            Instance->PostEditChange();
            Instance->MarkPackageDirty();
            Dirty.AddUnique(Instance->GetPackage());
            return Instance;
        }

        /** The original's colour of a slot: its glTF instance's BaseColorFactor, or none once ours is in place. */
        bool OriginalColour(UMaterialInterface* Material, FLinearColor& Out)
        {
            const UMaterialInstance* Instance = Cast<UMaterialInstance>(Material);
            return Instance && Instance->GetVectorParameterValue(FHashedMaterialParameterInfo(GltfColour), Out);
        }

        /** Triangles whose vertex normals face away from their winding's normal (the engine's: (P2 - P0) x (P1 - P0)). */
        void AuditNormals(UStaticMesh* Mesh, int32& OutTriangles, int32& OutAway)
        {
            OutTriangles = 0;
            OutAway = 0;
            const FMeshDescription* Description = Mesh->GetMeshDescription(0);
            if (!Description)
            {
                return;
            }
            FStaticMeshConstAttributes Attributes(*Description);
            const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
            const TVertexInstanceAttributesConstRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
            for (const FTriangleID Triangle : Description->Triangles().GetElementIDs())
            {
                const TArrayView<const FVertexInstanceID> Corners = Description->GetTriangleVertexInstances(Triangle);
                const FVector3f P0 = Positions[Description->GetVertexInstanceVertex(Corners[0])];
                const FVector3f P1 = Positions[Description->GetVertexInstanceVertex(Corners[1])];
                const FVector3f P2 = Positions[Description->GetVertexInstanceVertex(Corners[2])];
                const FVector3f Face = FVector3f::CrossProduct(P2 - P0, P1 - P0);
                const FVector3f Vertex = Normals[Corners[0]] + Normals[Corners[1]] + Normals[Corners[2]];
                ++OutTriangles;
                OutAway += FVector3f::DotProduct(Face, Vertex) < 0.0f ? 1 : 0;
            }
        }
    }

    bool Write(FString& OutError)
    {
        // The species the map draws.
        FString Text;
        TArray<Vegetation::FSpecies> Species;
        TArray<Vegetation::FPlant> Plants;
        const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), Vegetation::PlantsFile);
        if (!FFileHelper::LoadFileToString(Text, *Path) || !Vegetation::Parse(Text, Species, Plants, OutError))
        {
            OutError = FString::Printf(TEXT("cannot read %s: %s"), *Path, *OutError);
            return false;
        }

        TArray<UPackage*> Dirty;
        UMaterial* Foliage = WriteFoliage(Dirty, OutError);
        UMaterial* Bark = Foliage ? WriteBark(Dirty, OutError) : nullptr;
        if (!Bark)
        {
            return false;
        }
        for (const Vegetation::FSpecies& Kind : Species)
        {
            UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Vegetation::MeshPath(Kind.Id));
            if (!Mesh)
            {
                OutError = FString::Printf(TEXT("no mesh %s"), *Vegetation::MeshPath(Kind.Id));
                return false;
            }
            int32 Triangles = 0;
            int32 Away = 0;
            AuditNormals(Mesh, Triangles, Away);
            const FString Directory = FPackageName::GetLongPackagePath(Mesh->GetPackage()->GetName());
            TArray<FStaticMaterial>& Slots = Mesh->GetStaticMaterials();
            Mesh->Modify();
            for (FStaticMaterial& Slot : Slots)
            {
                const FString SlotName = Slot.MaterialSlotName.ToString();
                const FString Old = Slot.MaterialInterface ? Slot.MaterialInterface->GetPathName() : TEXT("none");
                const bool bLeaves = SlotName.Contains(TEXT("Foliage")) || Old.Contains(TEXT("Foliage"));
                FLinearColor Colour;
                const bool bOriginal = OriginalColour(Slot.MaterialInterface, Colour);
                if (!bOriginal)
                {
                    // Already ours, from an earlier run: keep its colour.
                    const UMaterialInstance* Ours = Cast<UMaterialInstance>(Slot.MaterialInterface);
                    if (!Ours || !Ours->GetVectorParameterValue(FHashedMaterialParameterInfo(bLeaves ? LeafColour : BarkColour), Colour))
                    {
                        Colour = bLeaves ? FallbackLeaf : FallbackBark;
                    }
                }
                Colour.A = 1.0f;
                const FString Name = FString::Printf(TEXT("MI_%s_%s"), *Kind.Id, bLeaves ? TEXT("Foliage") : TEXT("Bark"));
                UMaterialInstanceConstant* Instance = WriteInstance(Directory, Name, bLeaves ? Foliage : Bark, bLeaves ? LeafColour : BarkColour, Colour, Dirty);
                if (!Instance)
                {
                    OutError = FString::Printf(TEXT("could not write %s/%s"), *Directory, *Name);
                    return false;
                }
                UE_LOG(LogMarburgumPlantContent, Display, TEXT("MB2PLANTCONTENT species=%s slot=%s old=%s colour=(%.3f,%.3f,%.3f) from=%s new=%s"),
                    *Kind.Id, *SlotName, *Old, Colour.R, Colour.G, Colour.B, bOriginal ? TEXT("original") : TEXT("kept"), *Instance->GetPathName());
                Slot.MaterialInterface = Instance;
            }
            Mesh->PostEditChange();
            Mesh->MarkPackageDirty();
            Dirty.AddUnique(Mesh->GetPackage());
            UE_LOG(LogMarburgumPlantContent, Display, TEXT("MB2PLANTCONTENT species=%s slots=%d triangles=%d normals_away=%d"), *Kind.Id, Slots.Num(), Triangles, Away);
        }
        FAssetCompilingManager::Get().FinishAllCompilation();
        if (!UEditorLoadingAndSavingUtils::SavePackages(Dirty, false))
        {
            OutError = TEXT("could not save the plant materials");
            return false;
        }
        UE_LOG(LogMarburgumPlantContent, Display, TEXT("MB2PLANTCONTENT saved=%d species=%d"), Dirty.Num(), Species.Num());
        return true;
    }
}


namespace MarburgumStreetKitContent
{
    namespace Furniture = MarburgumII::StreetFurniture;

    namespace
    {
        /** Painted steel, the street lanterns' dark green (MarburgumStreetLightContent); alpha is how metallic. */
        const FVector4f RailingGreen(0.025f, 0.05f, 0.038f, 0.2f);
        constexpr double RailHalfLengthCm = 100.0;
        constexpr double RailTopCm = 110.0;
        constexpr double PostHalfCm = 3.0;
        constexpr double RailHalfCm = 2.5;
        /** The mid rails, centimetres above the ground. */
        constexpr double MidRailsCm[] = { 40.0, 75.0 };

        /** Boxes into one mesh description with vertex colours: the street lamps' way of building a kit piece. */
        struct FBoxes
        {
            FMeshDescription Description;
            FMeshDescriptionBuilder Builder;
            FPolygonGroupID Group;
            int32 Triangles = 0;

            FBoxes()
            {
                FStaticMeshAttributes(Description).Register();
                Builder.SetMeshDescription(&Description);
                Builder.EnablePolyGroups();
                Builder.SetNumUVLayers(1);
                Group = Builder.AppendPolygonGroup(TEXT("Metal"));
            }

            /** A quad whose front faces Outward, corners in order round it. */
            void Quad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Outward, const FVector4f& Colour)
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

            void Box(const FVector& Min, const FVector& Max, const FVector4f& Colour)
            {
                const FVector P[8] = {
                    FVector(Min.X, Min.Y, Min.Z), FVector(Max.X, Min.Y, Min.Z), FVector(Max.X, Max.Y, Min.Z), FVector(Min.X, Max.Y, Min.Z),
                    FVector(Min.X, Min.Y, Max.Z), FVector(Max.X, Min.Y, Max.Z), FVector(Max.X, Max.Y, Max.Z), FVector(Min.X, Max.Y, Max.Z),
                };
                Quad(P[0], P[3], P[2], P[1], -FVector::ZAxisVector, Colour);
                Quad(P[4], P[5], P[6], P[7], FVector::ZAxisVector, Colour);
                Quad(P[0], P[1], P[5], P[4], -FVector::YAxisVector, Colour);
                Quad(P[1], P[2], P[6], P[5], FVector::XAxisVector, Colour);
                Quad(P[2], P[3], P[7], P[6], FVector::YAxisVector, Colour);
                Quad(P[3], P[0], P[4], P[7], -FVector::XAxisVector, Colour);
            }
        };

        /**
         * The quay railing: a 2 m module along Y, its front +X as every panel of the kit. A post at each end (the next
         * module's post stands in the same place on a straight run), a top rail at 1.10 m and two mid rails. One box
         * blocks it, so nobody walks or falls through.
         */
        bool WriteQuayRailing(TArray<UPackage*>& Dirty, FString& OutError)
        {
            const FString Path = Furniture::MeshPath(Furniture::EPiece::QuayRailing);
            const FString PackageName = FPackageName::ObjectPathToPackageName(Path);
            UMaterial* Metal = LoadObject<UMaterial>(nullptr, *FString::Printf(TEXT("%s.%s"), MarburgumII::StreetLights::MetalMaterialPackage,
                *FPackageName::GetShortName(MarburgumII::StreetLights::MetalMaterialPackage)));
            if (!Metal)
            {
                OutError = TEXT("no M_StreetLampMetal (scripts/create-weather-content.sh lamps)");
                return false;
            }
            UPackage* Package = FPackageName::DoesPackageExist(PackageName) ? LoadPackage(nullptr, *PackageName, LOAD_None) : CreatePackage(*PackageName);
            if (Package)
            {
                Package->FullyLoad();
            }
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
                OutError = FString::Printf(TEXT("could not create %s"), *PackageName);
                return false;
            }
            FBoxes Shape;
            for (const double End : { -RailHalfLengthCm, RailHalfLengthCm })
            {
                Shape.Box(FVector(-PostHalfCm, End - PostHalfCm, 0.0), FVector(PostHalfCm, End + PostHalfCm, RailTopCm + 2.0), RailingGreen);
            }
            Shape.Box(FVector(-RailHalfCm - 1.0, -RailHalfLengthCm, RailTopCm - 2.0 * RailHalfCm), FVector(RailHalfCm + 1.0, RailHalfLengthCm, RailTopCm), RailingGreen);
            for (const double Z : MidRailsCm)
            {
                Shape.Box(FVector(-RailHalfCm, -RailHalfLengthCm, Z - RailHalfCm), FVector(RailHalfCm, RailHalfLengthCm, Z + RailHalfCm), RailingGreen);
            }
            const int32 Triangles = Shape.Triangles;
            Mesh->SetNumSourceModels(1);
            FStaticMeshSourceModel& Source = Mesh->GetSourceModel(0);
            Source.BuildSettings.bRecomputeNormals = false;
            Source.BuildSettings.bRecomputeTangents = true;
            Source.BuildSettings.bRemoveDegenerates = true;
            Source.BuildSettings.bGenerateLightmapUVs = false;
            Mesh->CreateMeshDescription(0, MoveTemp(Shape.Description));
            Mesh->CommitMeshDescription(0);
            Mesh->SetStaticMaterials({ FStaticMaterial(Metal, TEXT("Metal"), TEXT("Metal")) });
            Mesh->GetNaniteSettings().bEnabled = false;
            Mesh->CreateBodySetup();
            UBodySetup* Body = Mesh->GetBodySetup();
            Body->Modify();
            Body->AggGeom.EmptyElements();
            FKBoxElem Solid(2.0 * (RailHalfCm + 3.0), 2.0 * (RailHalfLengthCm + PostHalfCm), RailTopCm + 2.0);
            Solid.Center = FVector(0.0, 0.0, (RailTopCm + 2.0) * 0.5);
            Body->AggGeom.BoxElems.Add(Solid);
            Body->CollisionTraceFlag = CTF_UseSimpleAsComplex;
            Body->bNeverNeedsCookedCollisionData = false;
            Body->CreatePhysicsMeshes();
            Mesh->Build(true);
            Mesh->PostEditChange();
            FAssetCompilingManager::Get().FinishAllCompilation();
            Mesh->MarkPackageDirty();
            Dirty.AddUnique(Package);
            UE_LOG(LogMarburgumPlantContent, Display, TEXT("MB2KITCONTENT mesh=%s generated triangles=%d"), *Name, Triangles);
            return true;
        }
    }

    bool Write(FString& OutError)
    {
        TArray<UPackage*> Dirty;
        if (!WriteQuayRailing(Dirty, OutError))
        {
            return false;
        }
        UMaterial* Paint = LoadObject<UMaterial>(nullptr, PaintMaterial);
        if (!Paint)
        {
            OutError = FString::Printf(TEXT("no %s"), PaintMaterial);
            return false;
        }
        // Saved every time: a material copied from another project recompiles at every launch until it is resaved here.
        Paint->Modify();
        Paint->bUsedWithInstancedStaticMeshes = true;
        Paint->PreEditChange(nullptr);
        Paint->PostEditChange();
        Paint->MarkPackageDirty();
        Dirty.Add(Paint->GetPackage());
        // The original's six pieces (the railing above is ours, with its own collision).
        for (int32 Index = 0; Index < static_cast<int32>(Furniture::EPiece::QuayRailing); ++Index)
        {
            const FString Path = Furniture::MeshPath(static_cast<Furniture::EPiece>(Index));
            UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
            if (!Mesh)
            {
                OutError = FString::Printf(TEXT("no %s"), *Path);
                return false;
            }
            const FBox Box = Mesh->GetBoundingBox();
            UBodySetup* Body = Mesh->GetBodySetup();
            if (!Body)
            {
                Mesh->CreateBodySetup();
                Body = Mesh->GetBodySetup();
            }
            const int32 Before = Body->AggGeom.BoxElems.Num() + Body->AggGeom.ConvexElems.Num();
            const ECollisionTraceFlag OldTrace = Body->CollisionTraceFlag;
            // The original collided against every triangle (complex as simple), so a shelter's roof stopped whoever
            // stood under it. Ours is boxes: the shelter's back and side walls (its open side is +X), and each other
            // piece's bounds.
            Body->Modify();
            Body->AggGeom.BoxElems.Reset();
            Body->AggGeom.ConvexElems.Reset();
            auto AddBox = [Body](const FVector& Centre, const FVector& Half)
            {
                FKBoxElem Elem(2.0 * Half.X, 2.0 * Half.Y, 2.0 * Half.Z);
                Elem.Center = Centre;
                Body->AggGeom.BoxElems.Add(Elem);
            };
            if (static_cast<Furniture::EPiece>(Index) == Furniture::EPiece::Shelter)
            {
                const double WallTop = ShelterWallTopCm;
                const double Panel = ShelterPanelHalfCm;
                AddBox(FVector(Box.Min.X + ShelterRoofOverhangCm + Panel, 0.0, WallTop / 2.0), FVector(Panel, Box.GetExtent().Y - ShelterRoofOverhangCm, WallTop / 2.0));
                for (const double Side : { -1.0, 1.0 })
                {
                    AddBox(FVector(Box.GetCenter().X, Side * (Box.GetExtent().Y - ShelterRoofOverhangCm - Panel), WallTop / 2.0),
                        FVector(Box.GetExtent().X - ShelterRoofOverhangCm, Panel, WallTop / 2.0));
                }
            }
            else
            {
                AddBox(Box.GetCenter(), Box.GetExtent());
            }
            Body->CollisionTraceFlag = CTF_UseDefault;
            Body->InvalidatePhysicsData();
            Body->CreatePhysicsMeshes();
            Mesh->Modify();
            // The original built the kit with Nanite on, which its paint is not compiled for (the engine draws the default
            // material instead). Nanite for the city is GFX-06's measured decision, not this kit's.
            const bool bWasNanite = Mesh->GetNaniteSettings().bEnabled;
            Mesh->GetNaniteSettings().bEnabled = false;
            Mesh->PostEditChange();
            Mesh->MarkPackageDirty();
            Dirty.AddUnique(Mesh->GetPackage());
            UE_LOG(LogMarburgumPlantContent, Display, TEXT("MB2KITCONTENT mesh=%s min=(%.0f,%.0f,%.0f) max=(%.0f,%.0f,%.0f) old_simple=%d old_trace=%d was_nanite=%d boxes=%d materials=%d"),
                *Mesh->GetName(), Box.Min.X, Box.Min.Y, Box.Min.Z, Box.Max.X, Box.Max.Y, Box.Max.Z, Before, static_cast<int32>(OldTrace), bWasNanite ? 1 : 0,
                Body->AggGeom.BoxElems.Num(), Mesh->GetStaticMaterials().Num());
        }
        FAssetCompilingManager::Get().FinishAllCompilation();
        if (!Dirty.IsEmpty() && !UEditorLoadingAndSavingUtils::SavePackages(Dirty, false))
        {
            OutError = TEXT("could not save the street kit");
            return false;
        }
        UE_LOG(LogMarburgumPlantContent, Display, TEXT("MB2KITCONTENT saved=%d"), Dirty.Num());
        return true;
    }
}
