#include "MarburgumLentLidar.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/StaticMesh.h"
#include "FileHelpers.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MeshDescriptionBuilder.h"
#include "StaticMeshAttributes.h"
#include "Misc/FileHelper.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

namespace MarburgumMaps
{
    namespace
    {
        struct FPlanPoint
        {
            double East = 0.0;
            double South = 0.0;
        };

        FVector ToUe(double East, double South, double Up)
        {
            return FVector(East * 100.0, South * 100.0, Up * 100.0);
        }

        bool SameSide(const FPlanPoint& P, const FPlanPoint& A, const FPlanPoint& B, const FPlanPoint& C)
        {
            const double Cross = (B.East - A.East) * (P.South - A.South) - (B.South - A.South) * (P.East - A.East);
            const double CrossC = (B.East - A.East) * (C.South - A.South) - (B.South - A.South) * (C.East - A.East);
            return Cross * CrossC >= 0.0;
        }

        bool InTriangle(const FPlanPoint& P, const FPlanPoint& A, const FPlanPoint& B, const FPlanPoint& C)
        {
            return SameSide(P, A, B, C) && SameSide(P, B, C, A) && SameSide(P, C, A, B);
        }

        /** Ear clip. The outline is counter-clockwise in (east, south). */
        bool Triangulate(const TArray<FPlanPoint>& Polygon, TArray<int32>& Triangles)
        {
            TArray<int32> Vertex;
            Vertex.Reserve(Polygon.Num());
            for (int32 Index = 0; Index < Polygon.Num(); ++Index)
            {
                Vertex.Add(Index);
            }
            auto Cross = [&Polygon, &Vertex](int32 I0, int32 I1, int32 I2)
            {
                const FPlanPoint& A = Polygon[Vertex[I0]];
                const FPlanPoint& B = Polygon[Vertex[I1]];
                const FPlanPoint& C = Polygon[Vertex[I2]];
                return (B.East - A.East) * (C.South - A.South) - (B.South - A.South) * (C.East - A.East);
            };
            int32 Guard = Polygon.Num() * Polygon.Num();
            while (Vertex.Num() > 3 && Guard-- > 0)
            {
                bool bClipped = false;
                for (int32 Index = 0; Index < Vertex.Num(); ++Index)
                {
                    const int32 Prev = (Index + Vertex.Num() - 1) % Vertex.Num();
                    const int32 Next = (Index + 1) % Vertex.Num();
                    if (Cross(Prev, Index, Next) <= 1e-8)
                    {
                        continue;
                    }
                    bool bContains = false;
                    for (int32 Other = 0; Other < Vertex.Num(); ++Other)
                    {
                        if (Other == Prev || Other == Index || Other == Next)
                        {
                            continue;
                        }
                        if (InTriangle(Polygon[Vertex[Other]], Polygon[Vertex[Prev]], Polygon[Vertex[Index]], Polygon[Vertex[Next]]))
                        {
                            bContains = true;
                            break;
                        }
                    }
                    if (bContains)
                    {
                        continue;
                    }
                    Triangles.Add(Vertex[Prev]);
                    Triangles.Add(Vertex[Index]);
                    Triangles.Add(Vertex[Next]);
                    Vertex.RemoveAt(Index);
                    bClipped = true;
                    break;
                }
                if (!bClipped)
                {
                    return false;
                }
            }
            if (Vertex.Num() == 3 && Cross(0, 1, 2) > 1e-8)
            {
                Triangles.Append(Vertex);
                return true;
            }
            return false;
        }

        class FCityMesh
        {
        public:
            explicit FCityMesh(FMeshDescriptionBuilder& InBuilder) : Builder(InBuilder) {}

            void Triangle(const FVector& A, const FVector& B, const FVector& C, const FPolygonGroupID& Group)
            {
                const FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
                if (Normal.IsNearlyZero())
                {
                    return;
                }
                const FVertexID Va = Builder.AppendVertex(A);
                const FVertexID Vb = Builder.AppendVertex(B);
                const FVertexID Vc = Builder.AppendVertex(C);
                const FVertexInstanceID Ia = Builder.AppendInstance(Va);
                const FVertexInstanceID Ib = Builder.AppendInstance(Vb);
                const FVertexInstanceID Ic = Builder.AppendInstance(Vc);
                Builder.SetInstanceNormal(Ia, Normal);
                Builder.SetInstanceNormal(Ib, Normal);
                Builder.SetInstanceNormal(Ic, Normal);
                Builder.AppendTriangle(Ia, Ib, Ic, Group);
            }

            /** Emit the winding whose left-handed cross product points along Desired. */
            void Face(const FVector& A, const FVector& B, const FVector& C, const FVector& Desired, const FPolygonGroupID& Group)
            {
                if (FVector::DotProduct(FVector::CrossProduct(B - A, C - A), Desired) < 0.0)
                {
                    Triangle(A, C, B, Group);
                }
                else
                {
                    Triangle(A, B, C, Group);
                }
            }

            FMeshDescriptionBuilder& Builder;
        };

        UMaterialInterface* Swatch(const TCHAR* Name)
        {
            return LoadObject<UMaterialInstanceConstant>(nullptr, *FString::Printf(
                TEXT("/Game/Blockout/Materials/MI_Blockout_%s.MI_Blockout_%s"), Name, Name));
        }
    }

    FLentLidarMesh BuildLentLidarMesh()
    {
        FLentLidarMesh Result;
        const FString Path = FPaths::ProjectDir() / TEXT("Data/World/lent_buildings.json");
        FString JsonText;
        if (!FFileHelper::LoadFileToString(JsonText, *Path))
        {
            Result.Error = FString::Printf(TEXT("Cannot read %s"), *Path);
            return Result;
        }
        TSharedPtr<FJsonObject> Root;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        {
            Result.Error = TEXT("lent_buildings.json is not valid JSON");
            return Result;
        }
        const TArray<TSharedPtr<FJsonValue>>* Buildings = nullptr;
        if (!Root->TryGetArrayField(TEXT("buildings"), Buildings))
        {
            Result.Error = TEXT("lent_buildings.json has no buildings");
            return Result;
        }
        const double RoofMaxSpan = FMath::Max(4.0, Root->GetNumberField(TEXT("roof_max_span")));

        const TCHAR* WallNames[] = { TEXT("Ochre"), TEXT("Cream"), TEXT("Grey"), TEXT("Rose"), TEXT("Sage"), TEXT("Umber") };
        UMaterialInterface* Walls[UE_ARRAY_COUNT(WallNames)];
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(WallNames); ++Index)
        {
            Walls[Index] = Swatch(WallNames[Index]);
            if (!Walls[Index])
            {
                Result.Error = FString::Printf(TEXT("Missing blockout material %s"), WallNames[Index]);
                return Result;
            }
        }
        UMaterialInterface* Plinth = Swatch(TEXT("Plinth"));
        UMaterialInterface* Roof = Swatch(TEXT("Roof"));
        UMaterialInterface* Flat = Swatch(TEXT("Ridge"));
        if (!Plinth || !Roof || !Flat)
        {
            Result.Error = TEXT("Missing plinth or roof material");
            return Result;
        }

        FMeshDescription Description;
        FStaticMeshAttributes Attributes(Description);
        Attributes.Register();
        FMeshDescriptionBuilder Builder;
        Builder.SetMeshDescription(&Description);
        Builder.EnablePolyGroups();
        FPolygonGroupID WallGroup[UE_ARRAY_COUNT(WallNames)];
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(WallNames); ++Index)
        {
            WallGroup[Index] = Builder.AppendPolygonGroup(*FString::Printf(TEXT("Wall%d"), Index));
        }
        const FPolygonGroupID PlinthGroup = Builder.AppendPolygonGroup(TEXT("Plinth"));
        const FPolygonGroupID RoofGroup = Builder.AppendPolygonGroup(TEXT("Roof"));
        const FPolygonGroupID FlatGroup = Builder.AppendPolygonGroup(TEXT("FlatRoof"));
        FCityMesh City(Builder);

        int32 Raised = 0;
        for (int32 BuildingIndex = 0; BuildingIndex < Buildings->Num(); ++BuildingIndex)
        {
            const TSharedPtr<FJsonObject> Building = (*Buildings)[BuildingIndex]->AsObject();
            if (!Building.IsValid())
            {
                continue;
            }
            const TArray<TSharedPtr<FJsonValue>>* Outline = nullptr;
            if (!Building->TryGetArrayField(TEXT("outline"), Outline) || Outline->Num() < 3)
            {
                continue;
            }
            TArray<FPlanPoint> Plan;
            double EastSum = 0.0;
            double SouthSum = 0.0;
            for (const TSharedPtr<FJsonValue>& Value : *Outline)
            {
                const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
                if (!Value.IsValid() || !Value->TryGetArray(Pair) || Pair->Num() < 2)
                {
                    continue;
                }
                FPlanPoint Point{ (*Pair)[0]->AsNumber(), (*Pair)[1]->AsNumber() };
                Plan.Add(Point);
                EastSum += Point.East;
                SouthSum += Point.South;
            }
            if (Plan.Num() < 3)
            {
                continue;
            }
            const double CentroidSouth = SouthSum / Plan.Num();
            // Keep the Drava and the bridge deck clear. The banks themselves stay.
            if (CentroidSouth > -48.0 && CentroidSouth < 68.0)
            {
                continue;
            }

            double TwiceArea = 0.0;
            for (int32 Index = 0; Index < Plan.Num(); ++Index)
            {
                const FPlanPoint& A = Plan[Index];
                const FPlanPoint& B = Plan[(Index + 1) % Plan.Num()];
                TwiceArea += A.East * B.South - B.East * A.South;
            }
            if (TwiceArea < 0.0)
            {
                Algo::Reverse(Plan);
            }

            // Seat every floor on the promenade. LiDAR still sets the storeys and the roof rise.
            // The hillside datum is not a terrain mesh yet, so a raw ground height would float.
            const double SurveyGround = Building->GetNumberField(TEXT("ground"));
            const double SurveyEaves = Building->GetNumberField(TEXT("eaves"));
            const double SurveyRidge = Building->GetNumberField(TEXT("ridge"));
            const double Eaves = FMath::Max(SurveyEaves - SurveyGround, 2.5);
            const double Ridge = FMath::Max(SurveyRidge - SurveyGround, Eaves);
            const FString Shape = Building->GetStringField(TEXT("roof"));
            const double Angle = Building->GetNumberField(TEXT("ridge_angle"));
            const bool bFlat = Shape == TEXT("flat") || Ridge - Eaves <= 0.15;
            const FPolygonGroupID Wall = WallGroup[BuildingIndex % UE_ARRAY_COUNT(WallNames)];
            const FPolygonGroupID Cap = bFlat ? FlatGroup : RoofGroup;

            for (int32 Index = 0; Index < Plan.Num(); ++Index)
            {
                const FPlanPoint& A = Plan[Index];
                const FPlanPoint& B = Plan[(Index + 1) % Plan.Num()];
                const double Edge = FMath::Sqrt(FMath::Square(B.East - A.East) + FMath::Square(B.South - A.South));
                if (Edge < 0.05)
                {
                    continue;
                }
                const FVector Outward(B.South - A.South, -(B.East - A.East), 0.0);
                const FVector BottomA = ToUe(A.East, A.South, 0.0);
                const FVector BottomB = ToUe(B.East, B.South, 0.0);
                const FVector TopB = ToUe(B.East, B.South, Eaves);
                const FVector TopA = ToUe(A.East, A.South, Eaves);
                City.Face(BottomA, BottomB, TopB, Outward, Wall);
                City.Face(BottomA, TopB, TopA, Outward, Wall);
            }

            TArray<int32> CapTriangles;
            if (Triangulate(Plan, CapTriangles))
            {
                for (int32 Index = 0; Index + 2 < CapTriangles.Num(); Index += 3)
                {
                    const FPlanPoint& A = Plan[CapTriangles[Index]];
                    const FPlanPoint& B = Plan[CapTriangles[Index + 1]];
                    const FPlanPoint& C = Plan[CapTriangles[Index + 2]];
                    City.Face(ToUe(A.East, A.South, Eaves), ToUe(B.East, B.South, Eaves), ToUe(C.East, C.South, Eaves),
                        FVector::UpVector, Cap);
                }
            }

            if (!bFlat)
            {
                const FVector2D Along(FMath::Cos(Angle), FMath::Sin(Angle));
                const FVector2D Across(-Along.Y, Along.X);
                double U0 = 1e30, U1 = -1e30, V0 = 1e30, V1 = -1e30;
                for (const FPlanPoint& Point : Plan)
                {
                    const double U = Point.East * Along.X + Point.South * Along.Y;
                    const double V = Point.East * Across.X + Point.South * Across.Y;
                    U0 = FMath::Min(U0, U); U1 = FMath::Max(U1, U);
                    V0 = FMath::Min(V0, V); V1 = FMath::Max(V1, V);
                }
                auto Corner = [&](double U, double V, double Height)
                {
                    return ToUe(Along.X * U + Across.X * V, Along.Y * U + Across.Y * V, Height);
                };
                const int32 Strips = FMath::Max(1, FMath::CeilToInt32((V1 - V0) / RoofMaxSpan - 1e-6));
                const double Span = (V1 - V0) / Strips;
                const bool bHip = Shape == TEXT("hipped") || Shape == TEXT("pyramidal");
                for (int32 Strip = 0; Strip < Strips; ++Strip)
                {
                    const double A = V0 + Strip * Span;
                    const double B = A + Span;
                    const double Middle = (A + B) * 0.5;
                    const double Inset = bHip ? FMath::Min(Span * 0.5, (U1 - U0) * 0.5) : 0.0;
                    const double Ridge0 = U0 + Inset;
                    const double Ridge1 = U1 - Inset;
                    const FVector SlopeA = Corner(U0, A, Eaves);
                    const FVector SlopeB = Corner(U1, A, Eaves);
                    const FVector RidgeEnd = Corner(Ridge1, Middle, Ridge);
                    const FVector RidgeStart = Corner(Ridge0, Middle, Ridge);
                    City.Face(SlopeA, SlopeB, RidgeEnd, FVector::CrossProduct(SlopeB - SlopeA, RidgeEnd - SlopeA), Cap);
                    City.Face(SlopeA, RidgeEnd, RidgeStart, FVector::CrossProduct(RidgeEnd - SlopeA, RidgeStart - SlopeA), Cap);
                    const FVector FarA = Corner(U1, B, Eaves);
                    const FVector FarB = Corner(U0, B, Eaves);
                    City.Face(FarA, FarB, RidgeStart, FVector::CrossProduct(FarB - FarA, RidgeStart - FarA), Cap);
                    City.Face(FarA, RidgeStart, RidgeEnd, FVector::CrossProduct(RidgeStart - FarA, RidgeEnd - FarA), Cap);
                    const FVector GableA = FVector(-Along.X, -Along.Y, 0.0);
                    const FVector GableB = FVector(Along.X, Along.Y, 0.0);
                    if (!bHip)
                    {
                        City.Face(Corner(U0, A, Eaves), Corner(U0, B, Eaves), Corner(U0, Middle, Ridge), GableA, Wall);
                        City.Face(Corner(U1, B, Eaves), Corner(U1, A, Eaves), Corner(U1, Middle, Ridge), GableB, Wall);
                    }
                    else
                    {
                        City.Face(Corner(U0, A, Eaves), Corner(U0, B, Eaves), Corner(Ridge0, Middle, Ridge), GableA, Cap);
                        City.Face(Corner(U1, B, Eaves), Corner(U1, A, Eaves), Corner(Ridge1, Middle, Ridge), GableB, Cap);
                    }
                }
            }
            ++Raised;
        }
        if (Raised == 0)
        {
            Result.Error = TEXT("No Lent buildings were built");
            return Result;
        }

        const TCHAR* AssetPath = TEXT("/Game/Blockout/LentLidar/SM_LentLidar");
        UPackage* Package = CreatePackage(AssetPath);
        UStaticMesh* Mesh = FindObject<UStaticMesh>(Package, TEXT("SM_LentLidar"));
        if (!Mesh)
        {
            Mesh = NewObject<UStaticMesh>(Package, TEXT("SM_LentLidar"), RF_Public | RF_Standalone);
        }
        Mesh->SetNumSourceModels(1);
        Mesh->bAllowCPUAccess = true;
        UStaticMesh::FBuildMeshDescriptionsParams Params;
        Params.bFastBuild = true;
        Params.bAllowCpuAccess = true;
        Params.bBuildSimpleCollision = false;
        const TArray<const FMeshDescription*> Descriptions = { &Description };
        if (!Mesh->BuildFromMeshDescriptions(Descriptions, Params))
        {
            Result.Error = TEXT("Could not build the Lent LiDAR mesh");
            return Result;
        }
        UMaterialInterface* Slots[] = { Walls[0], Walls[1], Walls[2], Walls[3], Walls[4], Walls[5], Plinth, Roof, Flat };
        TArray<FStaticMaterial> Materials;
        for (UMaterialInterface* Material : Slots)
        {
            Materials.Add(FStaticMaterial(Material));
        }
        Mesh->SetStaticMaterials(Materials);
        Mesh->CreateBodySetup();
        Mesh->GetBodySetup()->CollisionTraceFlag = CTF_UseComplexAsSimple;
        Mesh->GetBodySetup()->CreatePhysicsMeshes();
        Package->MarkPackageDirty();
        if (!UEditorLoadingAndSavingUtils::SavePackages({ Package }, false))
        {
            Result.Error = TEXT("Could not save the Lent LiDAR mesh");
            return Result;
        }
        Result.Mesh = Mesh;
        Result.Buildings = Raised;
        return Result;
    }
}
