#include "MarburgumRebuildFacadesCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "FileHelpers.h"
#include "LandscapeProxy.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MeshDescription.h"
#include "MeshDescriptionBuilder.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumFacades, Log, All);

namespace
{
    bool Near(const FVector& A, const FVector& B)
    {
        return FVector::DistSquared(A, B) < 1.0;
    }

    struct FTri
    {
        FVector V[3];
        FVector Normal = FVector::UpVector;
        bool bUsed = false;
    };

    struct FWall
    {
        FVector Bottom0 = FVector::ZeroVector;
        FVector Bottom1 = FVector::ZeroVector;
        FVector Top0 = FVector::ZeroVector;
        FVector Top1 = FVector::ZeroVector;
        FVector Normal = FVector::ForwardVector;
        double Width = 0.0;
        double Height = 0.0;
    };

    struct FOpening
    {
        float U0 = 0.0f;
        float U1 = 0.0f;
        float V0 = 0.0f;
        float V1 = 0.0f;
        bool bDoor = false;
    };

    class FMesh
    {
    public:
        explicit FMesh(FMeshDescription& Description)
        {
            Builder.SetMeshDescription(&Description);
            Builder.EnablePolyGroups();
            Builder.SetNumUVLayers(1);
            Plaster = Builder.AppendPolygonGroup(TEXT("plaster"));
            Roof = Builder.AppendPolygonGroup(TEXT("roof"));
            Frame = Builder.AppendPolygonGroup(TEXT("frame"));
            Glass = Builder.AppendPolygonGroup(TEXT("glass"));
        }

        void Triangle(const FVector& A, const FVector& B, const FVector& C, const FPolygonGroupID& Group, const FVector& Normal)
        {
            const auto Instance = [&](const FVector& Position)
            {
                const FVertexID Vertex = Builder.AppendVertex(Position);
                const FVertexInstanceID InstanceId = Builder.AppendInstance(Vertex);
                Builder.SetInstanceNormal(InstanceId, Normal);
                Builder.SetInstanceUV(InstanceId, FVector2D(Position.X, Position.Z) * 0.01, 0);
                return InstanceId;
            };
            Builder.AppendTriangle(Instance(A), Instance(B), Instance(C), Group);
        }

        void Quad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FPolygonGroupID& Group, const FVector& Normal)
        {
            Triangle(A, B, C, Group, Normal);
            Triangle(A, C, D, Group, Normal);
        }

        FPolygonGroupID Plaster;
        FPolygonGroupID Roof;
        FPolygonGroupID Frame;
        FPolygonGroupID Glass;
        FMeshDescriptionBuilder Builder;
    };

    int32 Shared(const FTri& A, const FTri& B, int32& A0, int32& A1, int32& B0, int32& B1)
    {
        int32 Count = 0;
        for (int32 Ia = 0; Ia < 3; ++Ia)
        {
            for (int32 Ib = 0; Ib < 3; ++Ib)
            {
                if (!Near(A.V[Ia], B.V[Ib]))
                {
                    continue;
                }
                if (Count == 0)
                {
                    A0 = Ia;
                    B0 = Ib;
                }
                else if (Count == 1)
                {
                    A1 = Ia;
                    B1 = Ib;
                }
                ++Count;
            }
        }
        return Count;
    }

    bool MakeWall(const FTri& A, const FTri& B, FWall& Out)
    {
        int32 A0 = 0;
        int32 A1 = 0;
        int32 B0 = 0;
        int32 B1 = 0;
        if (Shared(A, B, A0, A1, B0, B1) != 2)
        {
            return false;
        }
        if (FVector::DotProduct(A.Normal, B.Normal) < 0.99)
        {
            return false;
        }
        const FVector ApexA = A.V[3 - A0 - A1];
        const FVector ApexB = B.V[3 - B0 - B1];
        const FVector S0 = A.V[A0];
        const FVector S1 = A.V[A1];
        const FVector Corners[4] = { ApexA, S0, ApexB, S1 };
        const int32 Edges[4][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 } };
        int32 Bottom = 0;
        float BottomZ = TNumericLimits<float>::Max();
        for (int32 Edge = 0; Edge < 4; ++Edge)
        {
            const FVector& P = Corners[Edges[Edge][0]];
            const FVector& Q = Corners[Edges[Edge][1]];
            if (FMath::Abs(P.Z - Q.Z) > 40.0)
            {
                continue;
            }
            const float Z = static_cast<float>(P.Z + Q.Z) * 0.5f;
            if (Z < BottomZ)
            {
                BottomZ = Z;
                Bottom = Edge;
            }
        }
        const FVector P0 = Corners[Edges[Bottom][0]];
        const FVector P1 = Corners[Edges[Bottom][1]];
        const FVector Along = (P1 - P0).GetSafeNormal();
        if (Along.IsNearlyZero())
        {
            return false;
        }
        FVector Normal = A.Normal;
        if (FVector::DotProduct(FVector::CrossProduct(Along, FVector::UpVector), Normal) < 0.0)
        {
            Normal *= -1.0;
        }
        // The two points that are not this bottom edge are the top, matched by which bottom point they sit over.
        const int32 Other0 = Edges[Bottom][0];
        const int32 Other1 = Edges[Bottom][1];
        FVector TopOver0 = FVector::ZeroVector;
        FVector TopOver1 = FVector::ZeroVector;
        bool b0 = false;
        bool b1 = false;
        for (int32 Corner = 0; Corner < 4; ++Corner)
        {
            if (Corner == Other0 || Corner == Other1)
            {
                continue;
            }
            const FVector Flat = FVector(Corners[Corner].X, Corners[Corner].Y, 0.0);
            const float D0 = static_cast<float>(FVector::DistSquared(Flat, FVector(P0.X, P0.Y, 0.0)));
            const float D1 = static_cast<float>(FVector::DistSquared(Flat, FVector(P1.X, P1.Y, 0.0)));
            if (D0 <= D1 && !b0)
            {
                TopOver0 = Corners[Corner];
                b0 = true;
            }
            else
            {
                TopOver1 = Corners[Corner];
                b1 = true;
            }
        }
        if (!b0 || !b1)
        {
            return false;
        }
        Out.Bottom0 = P0;
        Out.Bottom1 = P1;
        Out.Top0 = TopOver0;
        Out.Top1 = TopOver1;
        Out.Normal = Normal;
        Out.Width = FVector::Dist(P0, P1);
        Out.Height = FMath::Max(TopOver0.Z, TopOver1.Z) - FMath::Min(P0.Z, P1.Z);
        return Out.Width > 120.0 && Out.Height > 200.0 && FMath::Abs(Normal.Z) < 0.35;
    }

    FVector OnWall(const FWall& Wall, float U, float V, float Inset)
    {
        const float Tu = Wall.Width > 1.0 ? U / static_cast<float>(Wall.Width) : 0.0f;
        const float Tv = Wall.Height > 1.0 ? V / static_cast<float>(Wall.Height) : 0.0f;
        const FVector Bottom = FMath::Lerp(Wall.Bottom0, Wall.Bottom1, Tu);
        const FVector Top = FMath::Lerp(Wall.Top0, Wall.Top1, Tu);
        return FMath::Lerp(Bottom, Top, Tv) - Wall.Normal * Inset;
    }

    void Openings(const FWall& Wall, bool bDoor, TArray<FOpening>& Out)
    {
        if (Wall.Height < 240.0 || Wall.Width < 200.0)
        {
            return;
        }
        const int32 Storeys = FMath::Clamp(FMath::RoundToInt(Wall.Height / 320.0), 1, 4);
        const float Storey = static_cast<float>(Wall.Height) / Storeys;
        const float WinW = 110.0f;
        const float WinH = 140.0f;
        const int32 Count = FMath::Clamp(FMath::FloorToInt(static_cast<float>(Wall.Width) / 280.0f), 1, 3);
        const float Step = static_cast<float>(Wall.Width) / (Count + 1);
        auto Window = [&](int32 Index, float Sill)
        {
            const float Center = Step * (Index + 1);
            if (Sill < 60.0f || Sill + WinH > Wall.Height - 40.0f)
            {
                return;
            }
            FOpening Opening;
            Opening.U0 = Center - WinW * 0.5f;
            Opening.U1 = Center + WinW * 0.5f;
            Opening.V0 = Sill;
            Opening.V1 = Sill + WinH;
            Out.Add(Opening);
        };
        for (int32 StoreyIndex = 0; StoreyIndex < Storeys; ++StoreyIndex)
        {
            if (StoreyIndex == 0 && bDoor)
            {
                FOpening Door;
                Door.bDoor = true;
                Door.U0 = static_cast<float>(Wall.Width) * 0.5f - 55.0f;
                Door.U1 = static_cast<float>(Wall.Width) * 0.5f + 55.0f;
                Door.V0 = 0.0f;
                Door.V1 = FMath::Min(215.0f, Storey - 30.0f);
                if (Door.V1 > 170.0f)
                {
                    Out.Add(Door);
                }
                if (Count >= 3)
                {
                    Window(0, 90.0f);
                    Window(Count - 1, 90.0f);
                }
                continue;
            }
            const float Sill = Storey * StoreyIndex + (Storey - WinH) * 0.42f;
            for (int32 Index = 0; Index < Count; ++Index)
            {
                Window(Index, Sill);
            }
        }
    }

    void EmitSealed(FMesh& Mesh, const FWall& Wall, const TArray<FOpening>& Holes)
    {
        TArray<float> Us = { 0.0f, static_cast<float>(Wall.Width) };
        TArray<float> Vs = { 0.0f, static_cast<float>(Wall.Height) };
        for (const FOpening& Hole : Holes)
        {
            Us.Append({ Hole.U0, Hole.U1 });
            Vs.Append({ Hole.V0, Hole.V1 });
        }
        Us.Sort();
        Vs.Sort();
        auto Unique = [](TArray<float>& Values)
        {
            TArray<float> Kept;
            for (float Value : Values)
            {
                if (Kept.Num() == 0 || Value > Kept.Last() + 0.5f)
                {
                    Kept.Add(Value);
                }
            }
            Values = MoveTemp(Kept);
        };
        Unique(Us);
        Unique(Vs);
        for (int32 U = 0; U + 1 < Us.Num(); ++U)
        {
            for (int32 V = 0; V + 1 < Vs.Num(); ++V)
            {
                const float U0 = Us[U];
                const float U1 = Us[U + 1];
                const float V0 = Vs[V];
                const float V1 = Vs[V + 1];
                const FVector2D Center((U0 + U1) * 0.5f, (V0 + V1) * 0.5f);
                const FOpening* Hole = nullptr;
                for (const FOpening& Candidate : Holes)
                {
                    if (Center.X > Candidate.U0 && Center.X < Candidate.U1 && Center.Y > Candidate.V0 && Center.Y < Candidate.V1)
                    {
                        Hole = &Candidate;
                        break;
                    }
                }
                if (Hole)
                {
                    continue;
                }
                Mesh.Quad(OnWall(Wall, U0, V0, 0.0f), OnWall(Wall, U1, V0, 0.0f), OnWall(Wall, U1, V1, 0.0f), OnWall(Wall, U0, V1, 0.0f), Mesh.Plaster, Wall.Normal);
            }
        }
        for (const FOpening& Hole : Holes)
        {
            const float Inset = Hole.bDoor ? 8.0f : 16.0f;
            const FPolygonGroupID Back = Hole.bDoor ? Mesh.Frame : Mesh.Glass;
            const FVector O00 = OnWall(Wall, Hole.U0, Hole.V0, 0.0f);
            const FVector O10 = OnWall(Wall, Hole.U1, Hole.V0, 0.0f);
            const FVector O11 = OnWall(Wall, Hole.U1, Hole.V1, 0.0f);
            const FVector O01 = OnWall(Wall, Hole.U0, Hole.V1, 0.0f);
            const FVector I00 = OnWall(Wall, Hole.U0, Hole.V0, Inset);
            const FVector I10 = OnWall(Wall, Hole.U1, Hole.V0, Inset);
            const FVector I11 = OnWall(Wall, Hole.U1, Hole.V1, Inset);
            const FVector I01 = OnWall(Wall, Hole.U0, Hole.V1, Inset);
            Mesh.Quad(O00, O10, I10, I00, Mesh.Frame, (O10 - O00).GetSafeNormal().Cross((I00 - O00).GetSafeNormal()));
            Mesh.Quad(O10, O11, I11, I10, Mesh.Frame, (O11 - O10).GetSafeNormal().Cross((I10 - O10).GetSafeNormal()));
            Mesh.Quad(O11, O01, I01, I11, Mesh.Frame, (O01 - O11).GetSafeNormal().Cross((I11 - O11).GetSafeNormal()));
            Mesh.Quad(O01, O00, I00, I01, Mesh.Frame, (O00 - O01).GetSafeNormal().Cross((I01 - O01).GetSafeNormal()));
            Mesh.Quad(I00, I10, I11, I01, Back, -Wall.Normal);
        }
    }

    UMaterialInstanceConstant* EnsureMaterial(const TCHAR* Name, UMaterialInterface* Parent, const FLinearColor& Tint, float Roughness)
    {
        const FString Directory = TEXT("/Game/Marburgum/Materials/Instances/building_envelope");
        const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *Directory, Name, Name);
        UMaterialInstanceConstant* Instance = LoadObject<UMaterialInstanceConstant>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!Instance)
        {
            UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
            Factory->InitialParent = Parent;
            IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
            Instance = Cast<UMaterialInstanceConstant>(Tools.CreateAsset(Name, Directory, UMaterialInstanceConstant::StaticClass(), Factory));
        }
        if (!Instance)
        {
            return nullptr;
        }
        Instance->SetParentEditorOnly(Parent);
        Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("BaseColorTint")), Tint);
        Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("RoughnessDefault")), Roughness);
        Instance->PostEditChange();
        Instance->MarkPackageDirty();
        return Instance;
    }
}

UMarburgumRebuildFacadesCommandlet::UMarburgumRebuildFacadesCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
}

int32 UMarburgumRebuildFacadesCommandlet::Main(const FString& Params)
{
    (void)Params;
    UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Marburgum/Materials/Masters/M_Building.M_Building"));
    UMaterialInstanceConstant* Frame = Parent ? EnsureMaterial(TEXT("MI_facade_frame"), Parent, FLinearColor(0.22f, 0.14f, 0.09f), 0.58f) : nullptr;
    UMaterialInstanceConstant* Glass = Parent ? EnsureMaterial(TEXT("MI_facade_glass"), Parent, FLinearColor(0.16f, 0.20f, 0.22f), 0.06f) : nullptr;
    if (!Frame || !Glass)
    {
        UE_LOG(LogMarburgumFacades, Error, TEXT("MB2FACADE missing façade materials"));
        return 1;
    }
    UEditorLoadingAndSavingUtils::SavePackages({ Frame->GetOutermost(), Glass->GetOutermost() }, false);

    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/World/lent"));
    ALandscapeProxy* Landscape = nullptr;
    if (World)
    {
        TActorIterator<ALandscapeProxy> It(World);
        if (It)
        {
            Landscape = *It;
        }
    }

    FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> Assets;
    Registry.Get().GetAssetsByPath(TEXT("/Game/World/Buildings/lent"), Assets, false);
    int32 Rebuilt = 0;
    int32 Failed = 0;
    for (const FAssetData& Asset : Assets)
    {
        UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.GetAsset());
        if (!Mesh || !Mesh->GetMeshDescription(0))
        {
            ++Failed;
            continue;
        }
        bool bDone = false;
        for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials())
        {
            if (Slot.MaterialInterface && Slot.MaterialInterface->GetName().Contains(TEXT("facade_glass")))
            {
                bDone = true;
            }
        }
        if (bDone)
        {
            continue;
        }
        FMeshDescription* Source = Mesh->GetMeshDescription(0);
        FStaticMeshAttributes Attributes(*Source);
        const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
        TArray<FTri> Tris;
        for (const FTriangleID TriangleId : Source->Triangles().GetElementIDs())
        {
            const TArrayView<const FVertexID> Ids = Source->GetTriangleVertices(TriangleId);
            FTri Triangle;
            Triangle.V[0] = FVector(Positions[Ids[0]]);
            Triangle.V[1] = FVector(Positions[Ids[1]]);
            Triangle.V[2] = FVector(Positions[Ids[2]]);
            Triangle.Normal = FVector::CrossProduct(Triangle.V[1] - Triangle.V[0], Triangle.V[2] - Triangle.V[0]).GetSafeNormal();
            Tris.Add(Triangle);
        }

        TArray<FWall> Walls;
        TArray<FTri> Kept;
        for (int32 Index = 0; Index < Tris.Num(); ++Index)
        {
            if (Tris[Index].bUsed || FMath::Abs(Tris[Index].Normal.Z) >= 0.35f)
            {
                continue;
            }
            int32 Partner = INDEX_NONE;
            FWall Wall;
            for (int32 Other = Index + 1; Other < Tris.Num(); ++Other)
            {
                if (Tris[Other].bUsed || FMath::Abs(Tris[Other].Normal.Z) >= 0.35f)
                {
                    continue;
                }
                if (MakeWall(Tris[Index], Tris[Other], Wall))
                {
                    Partner = Other;
                    break;
                }
            }
            if (Partner == INDEX_NONE)
            {
                continue;
            }
            Tris[Index].bUsed = true;
            Tris[Partner].bUsed = true;
            Walls.Add(Wall);
        }
        for (const FTri& Triangle : Tris)
        {
            if (!Triangle.bUsed)
            {
                Kept.Add(Triangle);
            }
        }
        if (Walls.Num() == 0)
        {
            UE_LOG(LogMarburgumFacades, Warning, TEXT("MB2FACADE %s has no wall pair"), *Mesh->GetName());
            ++Failed;
            continue;
        }
        int32 Door = 0;
        for (int32 Index = 1; Index < Walls.Num(); ++Index)
        {
            if (Walls[Index].Width > Walls[Door].Width)
            {
                Door = Index;
            }
        }

        FMeshDescription Description;
        FStaticMeshAttributes(Description).Register();
        FMesh Built(Description);
        for (const FTri& Triangle : Kept)
        {
            const bool bRoof = FMath::Abs(Triangle.Normal.Z) >= 0.35f;
            Built.Triangle(Triangle.V[0], Triangle.V[1], Triangle.V[2], bRoof ? Built.Roof : Built.Plaster, Triangle.Normal);
        }
        int32 OpeningCount = 0;
        int32 Foundations = 0;
        for (int32 Index = 0; Index < Walls.Num(); ++Index)
        {
            TArray<FOpening> Holes;
            Openings(Walls[Index], Index == Door, Holes);
            OpeningCount += Holes.Num();
            EmitSealed(Built, Walls[Index], Holes);
            float Ground = static_cast<float>(FMath::Min(Walls[Index].Bottom0.Z, Walls[Index].Bottom1.Z));
            if (Landscape)
            {
                for (const FVector& End : { Walls[Index].Bottom0, Walls[Index].Bottom1 })
                {
                    if (const TOptional<float> Height = Landscape->GetHeightAtLocation(End))
                    {
                        Ground = FMath::Min(Ground, *Height - 15.0f);
                    }
                }
            }
            const float Drop = FMath::Clamp(static_cast<float>(FMath::Min(Walls[Index].Bottom0.Z, Walls[Index].Bottom1.Z)) - Ground, 0.0f, 800.0f);
            if (Drop > 12.0f)
            {
                const FVector Down0(Walls[Index].Bottom0.X, Walls[Index].Bottom0.Y, Walls[Index].Bottom0.Z - Drop);
                const FVector Down1(Walls[Index].Bottom1.X, Walls[Index].Bottom1.Y, Walls[Index].Bottom1.Z - Drop);
                Built.Quad(Down0, Down1, Walls[Index].Bottom1, Walls[Index].Bottom0, Built.Plaster, Walls[Index].Normal);
                ++Foundations;
            }
        }

        UMaterialInterface* Plaster = Mesh->GetStaticMaterials().IsValidIndex(0) ? Mesh->GetStaticMaterials()[0].MaterialInterface.Get() : nullptr;
        UMaterialInterface* RoofMaterial = Mesh->GetStaticMaterials().IsValidIndex(1) ? Mesh->GetStaticMaterials()[1].MaterialInterface.Get() : Plaster;
        Mesh->SetNumSourceModels(1);
        Mesh->bAllowCPUAccess = true;
        UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
        BuildParams.bFastBuild = false;
        BuildParams.bAllowCpuAccess = true;
        BuildParams.bBuildSimpleCollision = false;
        const TArray<const FMeshDescription*> Descriptions = { &Description };
        if (!Mesh->BuildFromMeshDescriptions(Descriptions, BuildParams))
        {
            ++Failed;
            continue;
        }
        Mesh->SetStaticMaterials({ FStaticMaterial(Plaster), FStaticMaterial(RoofMaterial), FStaticMaterial(Frame), FStaticMaterial(Glass) });
        Mesh->CreateBodySetup();
        Mesh->GetBodySetup()->CollisionTraceFlag = CTF_UseComplexAsSimple;
        Mesh->GetBodySetup()->bDoubleSidedGeometry = true;
        Mesh->GetOutermost()->MarkPackageDirty();
        if (!UEditorLoadingAndSavingUtils::SavePackages({ Mesh->GetOutermost() }, false))
        {
            ++Failed;
            continue;
        }
        UE_LOG(LogMarburgumFacades, Display, TEXT("MB2FACADE sealed %s walls=%d openings=%d foundations=%d"), *Mesh->GetName(), Walls.Num(), OpeningCount, Foundations);
        ++Rebuilt;
    }
    UE_LOG(LogMarburgumFacades, Display, TEXT("MB2FACADE done rebuilt=%d failed=%d"), Rebuilt, Failed);
    return Failed > 0 && Rebuilt == 0 ? 1 : 0;
}
