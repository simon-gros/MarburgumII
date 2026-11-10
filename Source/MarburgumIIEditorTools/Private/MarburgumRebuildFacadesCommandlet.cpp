#include "MarburgumRebuildFacadesCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "FileHelpers.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MeshDescription.h"
#include "MeshDescriptionBuilder.h"
#include "Misc/FileHelper.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "StaticMeshAttributes.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumFacades, Log, All);

namespace
{
    struct FPlan
    {
        FString Id;
        TArray<FVector2D> Outline;
        double Ground = 0.0;
        double Eaves = 0.0;
        double Ridge = 0.0;
        double Angle = 0.0;
        double Area = 0.0;
        double TerrainMin = 0.0;
        bool bTerrain = false;
        FString Roof;
        bool bSimplify = false;
    };

    struct FOpening
    {
        float U0 = 0.0f;
        float U1 = 0.0f;
        float V0 = 0.0f;
        float V1 = 0.0f;
        bool bDoor = false;
    };

    FVector Ue(const FVector2D& Plan, double HeightMetres)
    {
        return FVector(-Plan.Y * 100.0, Plan.X * 100.0, HeightMetres * 100.0);
    }

    double SignedArea(const TArray<FVector2D>& Outline)
    {
        double Area = 0.0;
        for (int32 Index = 0; Index < Outline.Num(); ++Index)
        {
            const FVector2D& A = Outline[Index];
            const FVector2D& B = Outline[(Index + 1) % Outline.Num()];
            Area += A.X * B.Y - B.X * A.Y;
        }
        return Area * 0.5;
    }

    bool Inside(const TArray<FVector2D>& Outline, const FVector2D& Point)
    {
        bool bInside = false;
        for (int32 Index = 0, Previous = Outline.Num() - 1; Index < Outline.Num(); Previous = Index++)
        {
            const FVector2D& A = Outline[Index];
            const FVector2D& B = Outline[Previous];
            const bool bCross = (A.Y > Point.Y) != (B.Y > Point.Y)
                && Point.X < (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y + KINDA_SMALL_NUMBER) + A.X;
            if (bCross)
            {
                bInside = !bInside;
            }
        }
        return bInside;
    }

    bool Triangulate(const TArray<FVector2D>& Outline, TArray<int32>& Triangles)
    {
        TArray<int32> Vertex;
        for (int32 Index = 0; Index < Outline.Num(); ++Index)
        {
            Vertex.Add(Index);
        }
        auto Cross = [&](int32 I0, int32 I1, int32 I2)
        {
            const FVector2D& A = Outline[Vertex[I0]];
            const FVector2D& B = Outline[Vertex[I1]];
            const FVector2D& C = Outline[Vertex[I2]];
            return (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
        };
        int32 Guard = Outline.Num() * Outline.Num();
        while (Vertex.Num() > 3 && Guard-- > 0)
        {
            bool bClipped = false;
            for (int32 Index = 0; Index < Vertex.Num(); ++Index)
            {
                const int32 Prev = (Index + Vertex.Num() - 1) % Vertex.Num();
                const int32 Next = (Index + 1) % Vertex.Num();
                if (Cross(Prev, Index, Next) <= 1.0e-8)
                {
                    continue;
                }
                bool bContains = false;
                const FVector2D& A = Outline[Vertex[Prev]];
                const FVector2D& B = Outline[Vertex[Index]];
                const FVector2D& C = Outline[Vertex[Next]];
                for (int32 Other = 0; Other < Vertex.Num(); ++Other)
                {
                    if (Other == Prev || Other == Index || Other == Next)
                    {
                        continue;
                    }
                    const FVector2D& P = Outline[Vertex[Other]];
                    const double C1 = (B.X - A.X) * (P.Y - A.Y) - (B.Y - A.Y) * (P.X - A.X);
                    const double C2 = (C.X - B.X) * (P.Y - B.Y) - (C.Y - B.Y) * (P.X - B.X);
                    const double C3 = (A.X - C.X) * (P.Y - C.Y) - (A.Y - C.Y) * (P.X - C.X);
                    if (C1 >= -1.0e-8 && C2 >= -1.0e-8 && C3 >= -1.0e-8)
                    {
                        bContains = true;
                        break;
                    }
                }
                if (bContains)
                {
                    continue;
                }
                Triangles.Append({ Vertex[Prev], Vertex[Index], Vertex[Next] });
                Vertex.RemoveAt(Index);
                bClipped = true;
                break;
            }
            if (!bClipped)
            {
                return false;
            }
        }
        if (Vertex.Num() == 3 && Cross(0, 1, 2) > 1.0e-8)
        {
            Triangles.Append(Vertex);
            return true;
        }
        return false;
    }

    class FShell
    {
    public:
        explicit FShell(FMeshDescription& Description)
        {
            Builder.SetMeshDescription(&Description);
            Builder.EnablePolyGroups();
            Builder.SetNumUVLayers(1);
            Plaster = Builder.AppendPolygonGroup(TEXT("plaster"));
            Roof = Builder.AppendPolygonGroup(TEXT("roof"));
            Frame = Builder.AppendPolygonGroup(TEXT("frame"));
            Glass = Builder.AppendPolygonGroup(TEXT("glass"));
        }

        void Triangle(const FVector& A, const FVector& B, const FVector& C, const FPolygonGroupID& Group)
        {
            const FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
            if (Normal.IsNearlyZero())
            {
                return;
            }
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

        void Quad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FPolygonGroupID& Group)
        {
            Triangle(A, B, C, Group);
            Triangle(A, C, D, Group);
        }

        FPolygonGroupID Plaster;
        FPolygonGroupID Roof;
        FPolygonGroupID Frame;
        FPolygonGroupID Glass;
        FMeshDescriptionBuilder Builder;
    };

    FVector OnWall(const FVector& Bottom0, const FVector& Bottom1, const FVector& Top0, const FVector& Top1, float U, float V, float Width, float Height, const FVector& Inset)
    {
        const float Tu = Width > 1.0f ? U / Width : 0.0f;
        const float Tv = Height > 1.0f ? V / Height : 0.0f;
        return FMath::Lerp(FMath::Lerp(Bottom0, Bottom1, Tu), FMath::Lerp(Top0, Top1, Tu), Tv) + Inset;
    }

    void AddOpenings(float Width, float Height, bool bDoor, TArray<FOpening>& Out)
    {
        if (Height < 240.0f || Width < 220.0f)
        {
            return;
        }
        const int32 Storeys = FMath::Clamp(FMath::RoundToInt(Height / 320.0f), 1, 4);
        const float Storey = Height / Storeys;
        const int32 Count = FMath::Clamp(FMath::FloorToInt(Width / 320.0f), 1, 3);
        const float Step = Width / (Count + 1);
        auto Window = [&](int32 Index, float Sill)
        {
            const float Center = Step * (Index + 1);
            if (Sill < 70.0f || Sill + 140.0f > Height - 40.0f)
            {
                return;
            }
            FOpening Opening;
            Opening.U0 = Center - 55.0f;
            Opening.U1 = Center + 55.0f;
            Opening.V0 = Sill;
            Opening.V1 = Sill + 140.0f;
            Out.Add(Opening);
        };
        for (int32 StoreyIndex = 0; StoreyIndex < Storeys; ++StoreyIndex)
        {
            if (StoreyIndex == 0 && bDoor)
            {
                FOpening Door;
                Door.bDoor = true;
                Door.U0 = Width * 0.5f - 55.0f;
                Door.U1 = Width * 0.5f + 55.0f;
                Door.V0 = 0.0f;
                Door.V1 = FMath::Min(215.0f, Storey - 40.0f);
                if (Door.V1 > 170.0f)
                {
                    Out.Add(Door);
                }
                continue;
            }
            const float Sill = Storey * StoreyIndex + (Storey - 140.0f) * 0.45f;
            for (int32 Index = 0; Index < Count; ++Index)
            {
                Window(Index, Sill);
            }
        }
    }

    FVector2D Outward(const FVector2D& From, const FVector2D& To, double Metres);

    void EmitWall(FShell& Shell, const FVector2D& A, const FVector2D& B, double Base, double Eaves, bool bDoor)
    {
        const FVector Bottom0 = Ue(A, Base);
        const FVector Bottom1 = Ue(B, Base);
        const FVector Top0 = Ue(A, Eaves);
        const FVector Top1 = Ue(B, Eaves);
        Shell.Quad(Bottom0, Bottom1, Top1, Top0, Shell.Plaster);
        Shell.Quad(Bottom0, Top0, Top1, Bottom1, Shell.Plaster);
        const float Width = static_cast<float>(FVector::Dist(Bottom0, Bottom1));
        const float Height = static_cast<float>(FMath::Max(Top0.Z, Top1.Z) - FMath::Min(Bottom0.Z, Bottom1.Z));
        const FVector2D Out = Outward(A, B, 0.08);
        const FVector Shift = Ue(A + Out, Base) - Bottom0;
        TArray<FOpening> Holes;
        AddOpenings(Width, Height, bDoor, Holes);
        for (const FOpening& Hole : Holes)
        {
            const FVector O00 = OnWall(Bottom0, Bottom1, Top0, Top1, Hole.U0, Hole.V0, Width, Height, FVector::ZeroVector) + Shift;
            const FVector O10 = OnWall(Bottom0, Bottom1, Top0, Top1, Hole.U1, Hole.V0, Width, Height, FVector::ZeroVector) + Shift;
            const FVector O11 = OnWall(Bottom0, Bottom1, Top0, Top1, Hole.U1, Hole.V1, Width, Height, FVector::ZeroVector) + Shift;
            const FVector O01 = OnWall(Bottom0, Bottom1, Top0, Top1, Hole.U0, Hole.V1, Width, Height, FVector::ZeroVector) + Shift;
            const FVector Face = Shift.GetSafeNormal() * 6.0;
            Shell.Quad(O00, O10, O10 + Face, O00 + Face, Shell.Frame);
            Shell.Quad(O10, O11, O11 + Face, O10 + Face, Shell.Frame);
            Shell.Quad(O11, O01, O01 + Face, O11 + Face, Shell.Frame);
            Shell.Quad(O01, O00, O00 + Face, O01 + Face, Shell.Frame);
            Shell.Quad(O00 + Face, O10 + Face, O11 + Face, O01 + Face, Hole.bDoor ? Shell.Frame : Shell.Glass);
        }
    }

    FVector2D Outward(const FVector2D& From, const FVector2D& To, double Metres)
    {
        const FVector2D Edge = To - From;
        const double Length = Edge.Size();
        if (Length < 1.0e-4)
        {
            return FVector2D::ZeroVector;
        }
        return FVector2D(Edge.Y, -Edge.X) * (Metres / Length);
    }

    void AddRoof(FShell& Shell, FVector A, FVector B, FVector C)
    {
        FVector Normal = FVector::CrossProduct(B - A, C - A);
        if (Normal.Z < 0.0)
        {
            Swap(B, C);
            Normal *= -1.0;
        }
        Normal = Normal.GetSafeNormal();
        const FVector Thick = Normal * -18.0;
        Shell.Triangle(A, B, C, Shell.Roof);
        Shell.Triangle(A + Thick, C + Thick, B + Thick, Shell.Roof);
        Shell.Quad(A, B, B + Thick, A + Thick, Shell.Roof);
        Shell.Quad(B, C, C + Thick, B + Thick, Shell.Roof);
        Shell.Quad(C, A, A + Thick, C + Thick, Shell.Roof);
    }

    void Eave(FShell& Shell, const FVector2D& A, const FVector2D& B, double Height)
    {
        const FVector2D Out = Outward(A, B, 0.45);
        if (Out.IsNearlyZero())
        {
            return;
        }
        const FVector TopA = Ue(A, Height);
        const FVector TopB = Ue(B, Height);
        const FVector OutA = Ue(A + Out, Height);
        const FVector OutB = Ue(B + Out, Height);
        const FVector SkirtA = Ue(A + Out, Height - 0.28);
        const FVector SkirtB = Ue(B + Out, Height - 0.28);
        const FVector LipA = Ue(A, Height - 0.28);
        const FVector LipB = Ue(B, Height - 0.28);
        AddRoof(Shell, TopA, TopB, OutB);
        AddRoof(Shell, TopA, OutB, OutA);
        Shell.Quad(OutA, OutB, SkirtB, SkirtA, Shell.Roof);
        Shell.Quad(SkirtA, SkirtB, LipB, LipA, Shell.Roof);
    }

    void Lid(FShell& Shell, const TArray<FVector2D>& Outline, double Height)
    {
        TArray<int32> Triangles;
        if (!Triangulate(Outline, Triangles))
        {
            return;
        }
        for (int32 Index = 0; Index + 2 < Triangles.Num(); Index += 3)
        {
            AddRoof(Shell, Ue(Outline[Triangles[Index]], Height), Ue(Outline[Triangles[Index + 1]], Height), Ue(Outline[Triangles[Index + 2]], Height));
        }
        for (int32 Index = 0; Index < Outline.Num(); ++Index)
        {
            Eave(Shell, Outline[Index], Outline[(Index + 1) % Outline.Num()], Height);
        }
    }

    void Pitched(FShell& Shell, const TArray<FVector2D>& Rect, double Eaves, double Ridge, double Base, bool bHip)
    {
        const FVector2D Along = Rect[1] - Rect[0];
        const double Length = Along.Size();
        const FVector2D Unit = Along / Length;
        const FVector2D Across = Rect[3] - Rect[0];
        const double Span = Across.Size();
        const double Inset = bHip ? FMath::Min(Span * 0.5, Length * 0.5) : 0.0;
        const FVector2D RidgeA = Rect[0] + Unit * Inset + Across * 0.5;
        const FVector2D RidgeB = Rect[1] - Unit * Inset + Across * 0.5;
        const FVector2D Over0 = Rect[0] + Outward(Rect[0], Rect[1], 0.45);
        const FVector2D Over1 = Rect[1] + Outward(Rect[0], Rect[1], 0.45);
        const FVector2D Over2 = Rect[2] + Outward(Rect[3], Rect[2], 0.45);
        const FVector2D Over3 = Rect[3] + Outward(Rect[3], Rect[2], 0.45);
        AddRoof(Shell, Ue(Over0, Eaves), Ue(Over1, Eaves), Ue(RidgeB, Ridge));
        AddRoof(Shell, Ue(Over0, Eaves), Ue(RidgeB, Ridge), Ue(RidgeA, Ridge));
        AddRoof(Shell, Ue(Over2, Eaves), Ue(Over3, Eaves), Ue(RidgeA, Ridge));
        AddRoof(Shell, Ue(Over2, Eaves), Ue(RidgeA, Ridge), Ue(RidgeB, Ridge));
        if (!bHip)
        {
            AddRoof(Shell, Ue(Rect[0], Eaves), Ue(Rect[3], Eaves), Ue(RidgeA, Ridge));
            AddRoof(Shell, Ue(Rect[1], Eaves), Ue(RidgeB, Ridge), Ue(Rect[2], Eaves));
        }
        else
        {
            AddRoof(Shell, Ue(Over0, Eaves), Ue(Over3, Eaves), Ue(RidgeA, Ridge));
            AddRoof(Shell, Ue(Over1, Eaves), Ue(RidgeB, Ridge), Ue(Over2, Eaves));
        }
        (void)Base;
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

    bool ReadPlans(TArray<FPlan>& OutPlans, TArray<FString>& OutDropped)
    {
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, TEXT("/home/simon/magna-raedarum-marburgum/project/assets/terrain/lent_buildings.json")))
        {
            return false;
        }
        TSharedPtr<FJsonObject> Root;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        {
            return false;
        }
        for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("buildings")))
        {
            const TSharedPtr<FJsonObject> Object = Value->AsObject();
            if (!Object.IsValid())
            {
                continue;
            }
            FPlan Plan;
            Plan.Id = Object->GetStringField(TEXT("id"));
            double Number = 0.0;
            Plan.Ground = Object->TryGetNumberField(TEXT("ground"), Number) ? Number : 0.0;
            Plan.Eaves = Object->TryGetNumberField(TEXT("eaves"), Number) ? Number : Plan.Ground + 3.0;
            Plan.Ridge = Object->TryGetNumberField(TEXT("ridge"), Number) ? Number : Plan.Eaves;
            Plan.Angle = Object->TryGetNumberField(TEXT("ridge_angle"), Number) ? Number : 0.0;
            Plan.Area = Object->TryGetNumberField(TEXT("area"), Number) ? Number : 0.0;
            Plan.Roof = Object->GetStringField(TEXT("roof"));
            const TSharedPtr<FJsonObject>* Contact = nullptr;
            if (Object->TryGetObjectField(TEXT("ground_contact"), Contact) && Contact && (*Contact)->TryGetNumberField(TEXT("terrain_min"), Number))
            {
                Plan.TerrainMin = Number;
                Plan.bTerrain = true;
            }
            for (const TSharedPtr<FJsonValue>& Point : Object->GetArrayField(TEXT("outline")))
            {
                const TArray<TSharedPtr<FJsonValue>> Pair = Point->AsArray();
                if (Pair.Num() >= 2)
                {
                    Plan.Outline.Add(FVector2D(Pair[0]->AsNumber(), Pair[1]->AsNumber()));
                }
            }
            if (SignedArea(Plan.Outline) < 0.0)
            {
                Algo::Reverse(Plan.Outline);
            }
            const bool bTiny = Plan.Area < 30.0 || Plan.Outline.Num() < 4;
            if (bTiny)
            {
                OutDropped.Add(Plan.Id);
                continue;
            }
            const FVector2D Dir(FMath::Cos(Plan.Angle), FMath::Sin(Plan.Angle));
            const FVector2D Across(-Dir.Y, Dir.X);
            double U0 = TNumericLimits<double>::Max();
            double U1 = TNumericLimits<double>::Lowest();
            double V0 = U0;
            double V1 = U1;
            for (const FVector2D& Point : Plan.Outline)
            {
                const double U = FVector2D::DotProduct(Point, Dir);
                const double V = FVector2D::DotProduct(Point, Across);
                U0 = FMath::Min(U0, U);
                U1 = FMath::Max(U1, U);
                V0 = FMath::Min(V0, V);
                V1 = FMath::Max(V1, V);
            }
            const double BoxArea = FMath::Max(0.0, (U1 - U0) * (V1 - V0));
            Plan.bSimplify = BoxArea > 1.0 && Plan.Area / BoxArea < 0.88;
            if (!Plan.bSimplify)
            {
                Plan.Outline = {
                    Dir * U0 + Across * V0,
                    Dir * U1 + Across * V0,
                    Dir * U1 + Across * V1,
                    Dir * U0 + Across * V1
                };
            }
            OutPlans.Add(Plan);
        }
        return OutPlans.Num() > 0;
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
    TArray<FPlan> Plans;
    TArray<FString> Dropped;
    if (!ReadPlans(Plans, Dropped))
    {
        UE_LOG(LogMarburgumFacades, Error, TEXT("MB2FACADE could not read lent_buildings.json"));
        return 1;
    }
    UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Marburgum/Materials/Masters/M_Building.M_Building"));
    UMaterialInstanceConstant* Frame = Parent ? EnsureMaterial(TEXT("MI_facade_frame"), Parent, FLinearColor(0.22f, 0.14f, 0.09f), 0.58f) : nullptr;
    UMaterialInstanceConstant* Glass = Parent ? EnsureMaterial(TEXT("MI_facade_glass"), Parent, FLinearColor(0.16f, 0.20f, 0.22f), 0.06f) : nullptr;
    UMaterialInterface* Plaster = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Marburgum/Materials/Instances/building_envelope/MI_plaster_mineral.MI_plaster_mineral"));
    UMaterialInterface* RoofMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Marburgum/Materials/Instances/building_envelope/MI_roof_tile_clay.MI_roof_tile_clay"));
    if (!Frame || !Glass || !Plaster || !RoofMaterial)
    {
        UE_LOG(LogMarburgumFacades, Error, TEXT("MB2FACADE missing materials"));
        return 1;
    }
    UEditorLoadingAndSavingUtils::SavePackages({ Frame->GetOutermost(), Glass->GetOutermost() }, false);

    int32 Rebuilt = 0;
    int32 ComplexRoofs = 0;
    int32 Failed = 0;
    for (const FPlan& Plan : Plans)
    {
        const double Gap = Plan.bTerrain ? Plan.Ground - Plan.TerrainMin : 0.0;
        const double Base = (Gap > 0.05 && Gap < 8.0) ? Plan.TerrainMin - 0.15 : Plan.Ground;
        const bool bFlat = Plan.bSimplify || Plan.Roof == TEXT("flat") || Plan.Ridge - Plan.Eaves < 0.2;
        FMeshDescription Description;
        FStaticMeshAttributes(Description).Register();
        FShell Shell(Description);
        int32 Longest = 0;
        double LongestLength = 0.0;
        for (int32 Index = 0; Index < Plan.Outline.Num(); ++Index)
        {
            const double Length = FVector2D::Distance(Plan.Outline[Index], Plan.Outline[(Index + 1) % Plan.Outline.Num()]);
            if (Length > LongestLength)
            {
                LongestLength = Length;
                Longest = Index;
            }
        }
        for (int32 Index = 0; Index < Plan.Outline.Num(); ++Index)
        {
            const FVector2D& A = Plan.Outline[Index];
            const FVector2D& B = Plan.Outline[(Index + 1) % Plan.Outline.Num()];
            if (FVector2D::Distance(A, B) < 0.05)
            {
                continue;
            }
            EmitWall(Shell, A, B, Base, Plan.Eaves, Index == Longest);
        }
        Lid(Shell, Plan.Outline, Plan.Eaves);
        if (!bFlat && Plan.Outline.Num() == 4)
        {
            Pitched(Shell, Plan.Outline, Plan.Eaves, Plan.Ridge, Base, Plan.Roof == TEXT("hipped") || Plan.Roof == TEXT("pyramidal"));
        }
        else
        {
            ++ComplexRoofs;
        }
        const FVector2D Centroid = [&]()
        {
            FVector2D Sum = FVector2D::ZeroVector;
            for (const FVector2D& Point : Plan.Outline)
            {
                Sum += Point;
            }
            return Sum / Plan.Outline.Num();
        }();
        if (bFlat && Inside(Plan.Outline, Centroid) && Plan.Ridge - Plan.Eaves > 0.3)
        {
            const FVector Peak = Ue(Centroid, Plan.Ridge);
            for (int32 Index = 0; Index < Plan.Outline.Num(); ++Index)
            {
                const FVector2D& From = Plan.Outline[Index];
                const FVector2D& To = Plan.Outline[(Index + 1) % Plan.Outline.Num()];
                const FVector2D Out = Outward(From, To, 0.45);
                AddRoof(Shell, Ue(From + Out, Plan.Eaves), Ue(To + Out, Plan.Eaves), Peak);
            }
        }

        UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Game/World/Buildings/lent/%s"), *Plan.Id));
        UStaticMesh* Mesh = FindObject<UStaticMesh>(Package, *Plan.Id);
        if (!Mesh)
        {
            Mesh = NewObject<UStaticMesh>(Package, *Plan.Id, RF_Public | RF_Standalone);
        }
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
        Package->MarkPackageDirty();
        if (!UEditorLoadingAndSavingUtils::SavePackages({ Package }, false))
        {
            ++Failed;
            continue;
        }
        ++Rebuilt;
    }

    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/World/lent"));
    int32 Removed = 0;
    if (World)
    {
        TArray<AActor*> Junk;
        for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
        {
            if (Dropped.Contains(It->GetActorLabel()))
            {
                Junk.Add(*It);
            }
        }
        for (AActor* Actor : Junk)
        {
            Actor->Destroy();
            ++Removed;
        }
        if (!UEditorLoadingAndSavingUtils::SaveMap(World, TEXT("/Game/World/lent")))
        {
            UE_LOG(LogMarburgumFacades, Error, TEXT("MB2FACADE could not save the level"));
            return 1;
        }
    }
    UE_LOG(LogMarburgumFacades, Display, TEXT("MB2FACADE done rebuilt=%d complex_roofs=%d dropped=%d removed=%d failed=%d"), Rebuilt, ComplexRoofs, Dropped.Num(), Removed, Failed);
    return Failed > 0 && Rebuilt == 0 ? 1 : 0;
}
