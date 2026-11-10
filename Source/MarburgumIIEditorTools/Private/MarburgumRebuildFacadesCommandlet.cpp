#include "MarburgumRebuildFacadesCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Engine/StaticMesh.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MeshDescription.h"
#include "MeshDescriptionBuilder.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumFacades, Log, All);

namespace
{
    struct FWall
    {
        TArray<FVector2D> Coverage;
        FVector Normal = FVector::UpVector;
        FVector Origin = FVector::ZeroVector;
        FVector AxisU = FVector::ForwardVector;
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

    bool NearlyVertical(const FVector& Normal)
    {
        return FMath::Abs(Normal.Z) < 0.35;
    }

    FVector PolygonNormal(const TArray<FVector>& Vertices)
    {
        FVector Accum = FVector::ZeroVector;
        for (int32 Index = 0; Index < Vertices.Num(); ++Index)
        {
            const FVector& A = Vertices[Index];
            const FVector& B = Vertices[(Index + 1) % Vertices.Num()];
            Accum += FVector::CrossProduct(A, B);
        }
        return Accum.GetSafeNormal();
    }

    bool Inside(const TArray<FVector2D>& Polygon, const FVector2D& Point)
    {
        bool bInside = false;
        for (int32 Index = 0, Previous = Polygon.Num() - 1; Index < Polygon.Num(); Previous = Index++)
        {
            const FVector2D& A = Polygon[Index];
            const FVector2D& B = Polygon[Previous];
            const bool bCross = (A.Y > Point.Y) != (B.Y > Point.Y)
                && Point.X < (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y + KINDA_SMALL_NUMBER) + A.X;
            if (bCross)
            {
                bInside = !bInside;
            }
        }
        return bInside;
    }

    class FFacadeMesh
    {
    public:
        explicit FFacadeMesh(FMeshDescription& Description)
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
            const FVector Side = (B - A).GetSafeNormal();
            const auto Instance = [&](const FVector& Position, const FVector2D& UV)
            {
                const FVertexID Vertex = Builder.AppendVertex(Position);
                const FVertexInstanceID InstanceId = Builder.AppendInstance(Vertex);
                Builder.SetInstanceNormal(InstanceId, Normal);
                Builder.SetInstanceUV(InstanceId, UV, 0);
                return InstanceId;
            };
            const FVertexInstanceID Ia = Instance(A, FVector2D(A.X, A.Z) * 0.01);
            const FVertexInstanceID Ib = Instance(B, FVector2D(B.X, B.Z) * 0.01);
            const FVertexInstanceID Ic = Instance(C, FVector2D(C.X, C.Z) * 0.01);
            Builder.AppendTriangle(Ia, Ib, Ic, Group);
            (void)Side;
        }

        void CopyPolygon(const TArray<FVector>& Vertices, const FPolygonGroupID& Group)
        {
            if (Vertices.Num() < 3)
            {
                return;
            }
            const FVector Normal = PolygonNormal(Vertices);
            for (int32 Index = 1; Index + 1 < Vertices.Num(); ++Index)
            {
                Triangle(Vertices[0], Vertices[Index], Vertices[Index + 1], Group, Normal);
            }
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

    void OpeningsFor(const FWall& Wall, bool bDoorWall, uint32 Hash, TArray<FOpening>& OutOpenings)
    {
        const float FloorHeight = Wall.Height < 420.0 ? static_cast<float>(Wall.Height) : 340.0f;
        const int32 Floors = FMath::Clamp(FMath::FloorToInt(static_cast<float>(Wall.Height - 24.0) / FloorHeight), 1, 6);
        const float Band = static_cast<float>(Wall.Height - 22.0) / Floors;
        const float WindowWidth = 96.0f + static_cast<float>(Hash % 3) * 14.0f;
        const float Step = WindowWidth + 78.0f + static_cast<float>((Hash / 3) % 4) * 12.0f;
        for (int32 Floor = 0; Floor < Floors; ++Floor)
        {
            const float BandBottom = Band * Floor;
            if (Floor == 0 && bDoorWall && Wall.Width > 180.0)
            {
                const float DoorWidth = 120.0f;
                const float Center = static_cast<float>(Wall.Width) * 0.5f;
                FOpening Door;
                Door.U0 = Center - DoorWidth * 0.5f;
                Door.U1 = Center + DoorWidth * 0.5f;
                Door.V0 = 0.0f;
                Door.V1 = FMath::Min(230.0f, Band - 30.0f);
                Door.bDoor = true;
                if (Door.V1 > 160.0f)
                {
                    OutOpenings.Add(Door);
                }
            }
            const float WindowHeight = Floor == 0 ? 150.0f : 146.0f;
            const float Sill = BandBottom + (Floor == 0 ? 78.0f : 62.0f);
            if (Sill + WindowHeight > BandBottom + Band - 16.0f)
            {
                continue;
            }
            for (float U = 48.0f; U + WindowWidth < Wall.Width - 36.0; U += Step)
            {
                const float Center = U + WindowWidth * 0.5f;
                bool bBlocked = false;
                for (const FOpening& Opening : OutOpenings)
                {
                    if (Opening.bDoor && Center > Opening.U0 - 20.0f && Center < Opening.U1 + 20.0f && Sill < Opening.V1)
                    {
                        bBlocked = true;
                        break;
                    }
                }
                if (bBlocked)
                {
                    continue;
                }
                FOpening Window;
                Window.U0 = U;
                Window.U1 = U + WindowWidth;
                Window.V0 = Sill;
                Window.V1 = Sill + WindowHeight;
                OutOpenings.Add(Window);
            }
        }
    }

    void EmitWall(FFacadeMesh& Mesh, const FWall& Wall, const TArray<FOpening>& Openings)
    {
        TArray<float> Us = { 0.0f, static_cast<float>(Wall.Width) };
        TArray<float> Vs = { 0.0f, static_cast<float>(Wall.Height), static_cast<float>(Wall.Height - 22.0) };
        for (const FOpening& Opening : Openings)
        {
            Us.Add(Opening.U0 - 8.0f);
            Us.Add(Opening.U0);
            Us.Add(Opening.U1);
            Us.Add(Opening.U1 + 8.0f);
            Vs.Add(Opening.V0 - 8.0f);
            Vs.Add(Opening.V0);
            Vs.Add(Opening.V1);
            Vs.Add(Opening.V1 + 8.0f);
        }
        Us.Sort();
        Vs.Sort();
        auto Unique = [](TArray<float>& Values)
        {
            TArray<float> Kept;
            for (float Value : Values)
            {
                if (Value < -1.0f)
                {
                    continue;
                }
                if (Kept.Num() == 0 || Value - Kept.Last() > 1.5f)
                {
                    Kept.Add(Value);
                }
            }
            Values = MoveTemp(Kept);
        };
        Unique(Us);
        Unique(Vs);

        const FVector Inward = -Wall.Normal;
        auto Point = [&](float U, float V, float Inset)
        {
            return Wall.Origin + Wall.AxisU * U + FVector::UpVector * V + Inward * Inset;
        };
        for (int32 UIndex = 0; UIndex + 1 < Us.Num(); ++UIndex)
        {
            for (int32 VIndex = 0; VIndex + 1 < Vs.Num(); ++VIndex)
            {
                const float U0 = Us[UIndex];
                const float U1 = Us[UIndex + 1];
                const float V0 = Vs[VIndex];
                const float V1 = Vs[VIndex + 1];
                if (U1 - U0 < 2.0f || V1 - V0 < 2.0f)
                {
                    continue;
                }
                const FVector2D Center((U0 + U1) * 0.5f, (V0 + V1) * 0.5f);
                bool bCovered = false;
                for (int32 Triangle = 0; Triangle + 2 < Wall.Coverage.Num(); Triangle += 3)
                {
                    const FVector2D& A = Wall.Coverage[Triangle];
                    const FVector2D& B = Wall.Coverage[Triangle + 1];
                    const FVector2D& C = Wall.Coverage[Triangle + 2];
                    const FVector2D Edge0 = B - A;
                    const FVector2D Edge1 = C - A;
                    const FVector2D Edge2 = Center - A;
                    const float Den = static_cast<float>(Edge0.X * Edge1.Y - Edge1.X * Edge0.Y);
                    if (FMath::Abs(Den) < KINDA_SMALL_NUMBER)
                    {
                        continue;
                    }
                    const float U = static_cast<float>((Edge2.X * Edge1.Y - Edge1.X * Edge2.Y) / Den);
                    const float V = static_cast<float>((Edge0.X * Edge2.Y - Edge2.X * Edge0.Y) / Den);
                    if (U >= -0.02f && V >= -0.02f && U + V <= 1.02f)
                    {
                        bCovered = true;
                        break;
                    }
                }
                if (!bCovered)
                {
                    continue;
                }
                const FOpening* Hit = nullptr;
                bool bFrame = false;
                for (const FOpening& Opening : Openings)
                {
                    const bool bInner = Center.X > Opening.U0 && Center.X < Opening.U1 && Center.Y > Opening.V0 && Center.Y < Opening.V1;
                    const bool bOuter = Center.X > Opening.U0 - 8.0f && Center.X < Opening.U1 + 8.0f && Center.Y > Opening.V0 - 8.0f && Center.Y < Opening.V1 + 8.0f;
                    if (bInner)
                    {
                        Hit = &Opening;
                        break;
                    }
                    if (bOuter)
                    {
                        bFrame = true;
                    }
                }
                FPolygonGroupID Group = Mesh.Plaster;
                float Inset = 0.0f;
                if (Hit && Hit->bDoor)
                {
                    Group = Mesh.Frame;
                    Inset = 10.0f;
                }
                else if (Hit)
                {
                    Group = Mesh.Glass;
                    Inset = 14.0f;
                }
                else if (bFrame || Center.Y > Wall.Height - 22.0)
                {
                    Group = Mesh.Frame;
                    Inset = bFrame ? 2.0f : 0.0f;
                }
                const FVector A = Point(U0, V0, Inset);
                const FVector B = Point(U1, V0, Inset);
                const FVector C = Point(U1, V1, Inset);
                const FVector D = Point(U0, V1, Inset);
                Mesh.Quad(A, B, C, D, Group, Wall.Normal);
            }
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
    if (!Parent)
    {
        UE_LOG(LogMarburgumFacades, Error, TEXT("MB2FACADE missing M_Building"));
        return 1;
    }
    UMaterialInstanceConstant* Frame = EnsureMaterial(TEXT("MI_facade_frame"), Parent, FLinearColor(0.22f, 0.14f, 0.09f), 0.58f);
    UMaterialInstanceConstant* Glass = EnsureMaterial(TEXT("MI_facade_glass"), Parent, FLinearColor(0.16f, 0.20f, 0.22f), 0.06f);
    if (!Frame || !Glass)
    {
        UE_LOG(LogMarburgumFacades, Error, TEXT("MB2FACADE could not create façade materials"));
        return 1;
    }
    UEditorLoadingAndSavingUtils::SavePackages({ Frame->GetOutermost(), Glass->GetOutermost() }, false);

    FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> Assets;
    Registry.Get().GetAssetsByPath(TEXT("/Game/World/Buildings/lent"), Assets, false);
    int32 Rebuilt = 0;
    int32 Skipped = 0;
    int32 Failed = 0;
    for (const FAssetData& Asset : Assets)
    {
        UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.GetAsset());
        if (!Mesh)
        {
            continue;
        }
        FMeshDescription* Source = Mesh->GetMeshDescription(0);
        if (!Source)
        {
            UE_LOG(LogMarburgumFacades, Warning, TEXT("MB2FACADE %s has no mesh description"), *Mesh->GetName());
            ++Failed;
            continue;
        }
        bool bAlready = false;
        for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials())
        {
            if (Slot.MaterialInterface && Slot.MaterialInterface->GetName().Contains(TEXT("facade_glass")))
            {
                bAlready = true;
                break;
            }
        }
        if (bAlready)
        {
            ++Skipped;
            continue;
        }
        FStaticMeshAttributes Attributes(*Source);
        const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
        struct FSourceTri
        {
            FVector A;
            FVector B;
            FVector C;
            FVector Normal;
            int32 Key = 0;
        };
        TArray<FSourceTri> Triangles;
        for (const FTriangleID TriangleId : Source->Triangles().GetElementIDs())
        {
            const TArrayView<const FVertexID> Ids = Source->GetTriangleVertices(TriangleId);
            FSourceTri Triangle;
            Triangle.A = FVector(Positions[Ids[0]]);
            Triangle.B = FVector(Positions[Ids[1]]);
            Triangle.C = FVector(Positions[Ids[2]]);
            Triangle.Normal = FVector::CrossProduct(Triangle.B - Triangle.A, Triangle.C - Triangle.A).GetSafeNormal();
            Triangles.Add(Triangle);
        }
        TArray<FWall> Walls;
        TArray<FSourceTri> Kept;
        TArray<int32> GroupOf;
        GroupOf.Init(INDEX_NONE, Triangles.Num());
        for (int32 Index = 0; Index < Triangles.Num(); ++Index)
        {
            if (!NearlyVertical(Triangles[Index].Normal) || Triangles[Index].Normal.IsNearlyZero())
            {
                Kept.Add(Triangles[Index]);
                continue;
            }
            int32 Group = INDEX_NONE;
            for (int32 WallIndex = 0; WallIndex < Walls.Num(); ++WallIndex)
            {
                const double Plane = FVector::DotProduct(Walls[WallIndex].Origin, Walls[WallIndex].Normal);
                const double Point = FVector::DotProduct(Triangles[Index].A, Walls[WallIndex].Normal);
                if (FVector::DotProduct(Triangles[Index].Normal, Walls[WallIndex].Normal) > 0.985
                    && FMath::Abs(Point - Plane) < 25.0)
                {
                    Group = WallIndex;
                    break;
                }
            }
            if (Group == INDEX_NONE)
            {
                FWall Wall;
                Wall.Normal = Triangles[Index].Normal;
                Wall.Origin = Triangles[Index].A;
                FVector Axis = FVector::CrossProduct(FVector::UpVector, Wall.Normal).GetSafeNormal();
                if (Axis.IsNearlyZero())
                {
                    Kept.Add(Triangles[Index]);
                    continue;
                }
                Wall.AxisU = Axis;
                Group = Walls.Add(Wall);
            }
            GroupOf[Index] = Group;
            auto Cover = [&](const FVector& Position)
            {
                Walls[Group].Coverage.Add(FVector2D(
                    FVector::DotProduct(Position - Walls[Group].Origin, Walls[Group].AxisU), Position.Z));
            };
            Cover(Triangles[Index].A);
            Cover(Triangles[Index].B);
            Cover(Triangles[Index].C);
        }
        for (int32 Index = Walls.Num() - 1; Index >= 0; --Index)
        {
            float MinU = TNumericLimits<float>::Max();
            float MaxU = TNumericLimits<float>::Lowest();
            float MinZ = TNumericLimits<float>::Max();
            float MaxZ = TNumericLimits<float>::Lowest();
            for (const FVector2D& Point : Walls[Index].Coverage)
            {
                MinU = FMath::Min(MinU, static_cast<float>(Point.X));
                MaxU = FMath::Max(MaxU, static_cast<float>(Point.X));
                MinZ = FMath::Min(MinZ, static_cast<float>(Point.Y));
                MaxZ = FMath::Max(MaxZ, static_cast<float>(Point.Y));
            }
            Walls[Index].Origin = Walls[Index].Origin + Walls[Index].AxisU * MinU + FVector(0.0, 0.0, MinZ - Walls[Index].Origin.Z);
            Walls[Index].Width = MaxU - MinU;
            Walls[Index].Height = MaxZ - MinZ;
            for (FVector2D& Point : Walls[Index].Coverage)
            {
                Point.X -= MinU;
                Point.Y -= MinZ;
            }
            if (Walls[Index].Width < 80.0 || Walls[Index].Height < 180.0 || Walls[Index].Coverage.Num() < 3)
            {
                for (int32 TriangleIndex = 0; TriangleIndex < Triangles.Num(); ++TriangleIndex)
                {
                    if (GroupOf[TriangleIndex] == Index)
                    {
                        Kept.Add(Triangles[TriangleIndex]);
                    }
                }
                Walls.RemoveAt(Index);
            }
        }
        if (Walls.Num() == 0)
        {
            UE_LOG(LogMarburgumFacades, Warning, TEXT("MB2FACADE %s has no wall quads"), *Mesh->GetName());
            ++Failed;
            continue;
        }
        int32 DoorWall = 0;
        for (int32 Index = 1; Index < Walls.Num(); ++Index)
        {
            if (Walls[Index].Width > Walls[DoorWall].Width)
            {
                DoorWall = Index;
            }
        }

        FMeshDescription Description;
        FStaticMeshAttributes(Description).Register();
        FFacadeMesh Facade(Description);
        for (const FSourceTri& Triangle : Kept)
        {
            const bool bRoof = !NearlyVertical(Triangle.Normal);
            Facade.Triangle(Triangle.A, Triangle.B, Triangle.C, bRoof ? Facade.Roof : Facade.Plaster, Triangle.Normal);
        }
        const uint32 Hash = FCrc::StrCrc32(*Mesh->GetName());
        int32 OpeningCount = 0;
        for (int32 Index = 0; Index < Walls.Num(); ++Index)
        {
            TArray<FOpening> Openings;
            OpeningsFor(Walls[Index], Index == DoorWall, Hash ^ static_cast<uint32>(Index * 17), Openings);
            OpeningCount += Openings.Num();
            EmitWall(Facade, Walls[Index], Openings);
        }
        // One triangle in every slot so section indices stay plaster, roof, frame, glass.
        const FVector Marker = Walls[0].Origin + Walls[0].Normal * 0.2;
        Facade.Triangle(Marker, Marker + FVector(0.4, 0.0, 0.0), Marker + FVector(0.0, 0.0, 0.4), Facade.Roof, Walls[0].Normal);
        Facade.Triangle(Marker, Marker + FVector(0.4, 0.0, 0.0), Marker + FVector(0.0, 0.0, 0.4), Facade.Frame, Walls[0].Normal);
        Facade.Triangle(Marker, Marker + FVector(0.4, 0.0, 0.0), Marker + FVector(0.0, 0.0, 0.4), Facade.Glass, Walls[0].Normal);

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
            UE_LOG(LogMarburgumFacades, Error, TEXT("MB2FACADE %s failed to build"), *Mesh->GetName());
            ++Failed;
            continue;
        }
        TArray<FStaticMaterial> Materials;
        Materials.Add(FStaticMaterial(Plaster));
        Materials.Add(FStaticMaterial(RoofMaterial));
        Materials.Add(FStaticMaterial(Frame));
        Materials.Add(FStaticMaterial(Glass));
        Mesh->SetStaticMaterials(Materials);
        Mesh->CreateBodySetup();
        Mesh->GetBodySetup()->CollisionTraceFlag = CTF_UseComplexAsSimple;
        Mesh->GetBodySetup()->bDoubleSidedGeometry = true;
        Mesh->GetOutermost()->MarkPackageDirty();
        if (!UEditorLoadingAndSavingUtils::SavePackages({ Mesh->GetOutermost() }, false))
        {
            UE_LOG(LogMarburgumFacades, Error, TEXT("MB2FACADE %s failed to save"), *Mesh->GetName());
            ++Failed;
            continue;
        }
        UE_LOG(LogMarburgumFacades, Display, TEXT("MB2FACADE rebuilt %s walls=%d openings=%d"), *Mesh->GetName(), Walls.Num(), OpeningCount);
        ++Rebuilt;
    }
    UE_LOG(LogMarburgumFacades, Display, TEXT("MB2FACADE done rebuilt=%d skipped=%d failed=%d"), Rebuilt, Skipped, Failed);
    return Failed > 0 && Rebuilt == 0 ? 1 : 0;
}
