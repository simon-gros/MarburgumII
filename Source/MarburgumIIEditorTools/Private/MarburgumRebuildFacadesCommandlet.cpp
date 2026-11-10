#include "MarburgumRebuildFacadesCommandlet.h"

#include "AssetCompilingManager.h"
#include "AssetToolsModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "FileHelpers.h"
#include "MarburgumBuildingMesher.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MeshDescription.h"
#include "MeshDescriptionBuilder.h"
#include "Misc/FileHelper.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumFacades, Log, All);

namespace
{
    namespace MB = MarburgumBuildings;

    const TCHAR* SourcePath = TEXT("/home/simon/magna-raedarum-marburgum/project/assets/terrain/lent_buildings.json");

    // Plan metres (x east, y south of Stari most, z up) to Unreal centimetres. A rotation, so windings
    // and normals carry over unchanged.
    FVector Ue(const MB::FVec3& P)
    {
        return FVector(-P.Y * 100.0, P.X * 100.0, P.Z * 100.0);
    }

    bool ReadFootprints(TArray<FString>& OutIds, std::vector<MB::FFootprint>& OutFootprints, TArray<FString>& OutDropped)
    {
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, SourcePath))
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
            const FString Id = Object->GetStringField(TEXT("id"));
            double Number = 0.0;
            MB::FFootprint Footprint;
            Footprint.Id = TCHAR_TO_UTF8(*Id);
            Footprint.Ground = Object->TryGetNumberField(TEXT("ground"), Number) ? Number : 0.0;
            Footprint.Eaves = Object->TryGetNumberField(TEXT("eaves"), Number) ? Number : Footprint.Ground + 3.0;
            const double Ridge = Object->TryGetNumberField(TEXT("ridge"), Number) ? Number : Footprint.Eaves;
            Footprint.RoofRise = Ridge - Footprint.Eaves;
            Footprint.PitchDegrees = Object->TryGetNumberField(TEXT("pitch"), Number) ? Number : 40.0;
            const double Area = Object->TryGetNumberField(TEXT("area"), Number) ? Number : 0.0;
            const FString Roof = Object->GetStringField(TEXT("roof"));
            Footprint.Roof = Roof == TEXT("flat") ? MB::ERoof::Flat
                : (Roof == TEXT("hipped") || Roof == TEXT("pyramidal") ? MB::ERoof::Hipped : MB::ERoof::Gabled);

            // Walls start just below the lowest terrain under the footprint, so none floats on a slope.
            Footprint.Base = Footprint.Ground - 0.15;
            const TSharedPtr<FJsonObject>* Contact = nullptr;
            if (Object->TryGetObjectField(TEXT("ground_contact"), Contact) && Contact && (*Contact)->TryGetNumberField(TEXT("terrain_min"), Number))
            {
                const double Gap = Footprint.Ground - Number;
                if (Gap > 0.05 && Gap < 8.0)
                {
                    Footprint.Base = Number - 0.15;
                }
            }
            for (const TSharedPtr<FJsonValue>& Point : Object->GetArrayField(TEXT("outline")))
            {
                const TArray<TSharedPtr<FJsonValue>> Pair = Point->AsArray();
                if (Pair.Num() >= 2)
                {
                    Footprint.Outline.push_back({ Pair[0]->AsNumber(), Pair[1]->AsNumber() });
                }
            }
            // Footprints under 30 square metres are not buildings (sheds, kiosks, mapping slivers), and
            // neither is an outline the cleaner cannot keep simple.
            if (Area < 30.0 || Footprint.Outline.size() < 4 || !MB::CleanOutline(Footprint.Outline))
            {
                OutDropped.Add(Id);
                continue;
            }
            OutIds.Add(Id);
            OutFootprints.push_back(Footprint);
        }
        return !OutFootprints.empty();
    }

    void ToMeshDescription(const MB::FBuildingMesh& Mesh, FMeshDescription& Description)
    {
        FStaticMeshAttributes(Description).Register();
        FMeshDescriptionBuilder Builder;
        Builder.SetMeshDescription(&Description);
        Builder.EnablePolyGroups();
        Builder.SetNumUVLayers(1);
        TArray<FPolygonGroupID> Groups;
        const TCHAR* Names[MB::MaterialCount] = { TEXT("plaster"), TEXT("roof"), TEXT("frame"), TEXT("glass"), TEXT("flat_roof") };
        for (int32 Index = 0; Index < MB::MaterialCount; ++Index)
        {
            Groups.Add(Builder.AppendPolygonGroup(Names[Index]));
        }
        // Welded positions keep the shell closed; each triangle gets its own vertex instances for a
        // flat normal and a planar UV of one unit per metre.
        TArray<FVertexID> Vertices;
        Vertices.Reserve(static_cast<int32>(Mesh.Positions.size()));
        for (const MB::FVec3& P : Mesh.Positions)
        {
            Vertices.Add(Builder.AppendVertex(Ue(P)));
        }
        for (const MB::FTriangle& Tri : Mesh.Triangles)
        {
            const FVector A = Ue(Mesh.Positions[Tri.A]);
            const FVector B = Ue(Mesh.Positions[Tri.B]);
            const FVector C = Ue(Mesh.Positions[Tri.C]);
            const FVector Normal = FVector::CrossProduct(C - A, B - A).GetSafeNormal();
            if (Normal.IsNearlyZero())
            {
                continue;
            }
            // Walls and roofs run U along the level and V up the surface; flat faces use the plan axes.
            const bool bLevel = FMath::Abs(Normal.Z) > 0.9;
            const FVector U = bLevel ? FVector::ForwardVector : FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();
            const FVector V = bLevel ? FVector::RightVector : FVector::CrossProduct(Normal, U).GetSafeNormal();
            const auto Instance = [&](int32 Index, const FVector& Position)
            {
                const FVertexInstanceID Id = Builder.AppendInstance(Vertices[Index]);
                Builder.SetInstanceNormal(Id, Normal);
                Builder.SetInstanceUV(Id, FVector2D(FVector::DotProduct(Position, U), FVector::DotProduct(Position, V)) * 0.01, 0);
                return Id;
            };
            Builder.AppendTriangle(Instance(Tri.A, A), Instance(Tri.B, B), Instance(Tri.C, C), Groups[Tri.Material]);
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
    FString InspectId;
    if (FParse::Value(*Params, TEXT("Inspect="), InspectId))
    {
        // Reports what a saved building mesh holds: its source description and its built render data.
        const FString Path = FString::Printf(TEXT("/Game/World/Buildings/lent/%s.%s"), *InspectId, *InspectId);
        UStaticMesh* Saved = LoadObject<UStaticMesh>(nullptr, *Path);
        if (!Saved)
        {
            UE_LOG(LogMarburgumFacades, Error, TEXT("MB2FACADE inspect %s not found"), *Path);
            return 1;
        }
        const FMeshDescription* Source = Saved->GetMeshDescription(0);
        Saved->ConditionalPostLoad();
        UE_LOG(LogMarburgumFacades, Display, TEXT("MB2FACADE inspect %s source_triangles=%d render_triangles=%d materials=%d bounds=%s"),
            *InspectId, Source ? Source->Triangles().Num() : -1,
            Saved->GetRenderData() && Saved->GetRenderData()->LODResources.Num() ? Saved->GetRenderData()->LODResources[0].GetNumTriangles() : -1,
            Saved->GetStaticMaterials().Num(), *Saved->GetBounds().GetBox().ToString());
        return 0;
    }
    const bool bCheckOnly = FParse::Param(*Params, TEXT("CheckOnly"));
    FString Only;
    FParse::Value(*Params, TEXT("Only="), Only);
    TArray<FString> Ids;
    std::vector<MB::FFootprint> Footprints;
    TArray<FString> Dropped;
    if (!ReadFootprints(Ids, Footprints, Dropped))
    {
        UE_LOG(LogMarburgumFacades, Error, TEXT("MB2FACADE could not read %s"), SourcePath);
        return 1;
    }
    MB::FindPartyWalls(Footprints);

    UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Marburgum/Materials/Masters/M_Building.M_Building"));
    UMaterialInstanceConstant* Frame = Parent ? EnsureMaterial(TEXT("MI_facade_frame"), Parent, FLinearColor(0.22f, 0.14f, 0.09f), 0.58f) : nullptr;
    UMaterialInstanceConstant* Glass = Parent ? EnsureMaterial(TEXT("MI_facade_glass"), Parent, FLinearColor(0.16f, 0.20f, 0.22f), 0.06f) : nullptr;
    UMaterialInterface* Plaster = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Marburgum/Materials/Instances/building_envelope/MI_plaster_mineral.MI_plaster_mineral"));
    UMaterialInterface* RoofTile = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Marburgum/Materials/Instances/building_envelope/MI_roof_tile_clay.MI_roof_tile_clay"));
    UMaterialInterface* FlatRoof = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Marburgum/Materials/Instances/building_envelope/MI_roof_bitumen.MI_roof_bitumen"));
    if (!Frame || !Glass || !Plaster || !RoofTile || !FlatRoof)
    {
        UE_LOG(LogMarburgumFacades, Error, TEXT("MB2FACADE missing materials"));
        return 1;
    }
    if (!bCheckOnly)
    {
        UEditorLoadingAndSavingUtils::SavePackages({ Frame->GetOutermost(), Glass->GetOutermost() }, false);
    }

    int32 Rebuilt = 0;
    int32 Failed = 0;
    int32 Open = 0;
    int64 Triangles = 0;
    TMap<FString, int32> Methods;
    for (int32 Index = 0; Index < Ids.Num(); ++Index)
    {
        const FString& Id = Ids[Index];
        const MB::FFootprint& Footprint = Footprints[Index];
        if (!Only.IsEmpty() && Id != Only)
        {
            continue;
        }
        MB::FBuildingMesh Mesh;
        std::string Error;
        if (!MB::BuildBuilding(Footprint, Mesh, Error))
        {
            UE_LOG(LogMarburgumFacades, Error, TEXT("MB2FACADE %s failed: %s"), *Id, UTF8_TO_TCHAR(Error.c_str()));
            ++Failed;
            continue;
        }
        const MB::FCheck Check = MB::Check(Mesh, Footprint);
        UE_LOG(LogMarburgumFacades, Display, TEXT("MB2FACADE %s roof=%s corners=%d party=%d gables=%d windows=%d triangles=%d closed=%d strict=%d open_edges=%d volume_m3=%.0f"),
            *Id, UTF8_TO_TCHAR(Mesh.RoofMethod.c_str()), static_cast<int32>(Footprint.Outline.size()), Mesh.PartyWalls, Mesh.Gables, Mesh.Windows,
            static_cast<int32>(Mesh.Triangles.size()), Check.bClosed ? 1 : 0, Check.bStrict ? 1 : 0, Check.StrictOpenEdges, Check.Volume);
        if (!Check.bStrict || Check.Volume <= 0.0)
        {
            ++Open;
            continue;
        }
        Methods.FindOrAdd(UTF8_TO_TCHAR(Mesh.RoofMethod.c_str()))++;
        Triangles += static_cast<int64>(Mesh.Triangles.size());
        if (bCheckOnly)
        {
            ++Rebuilt;
            continue;
        }

        FMeshDescription Description;
        ToMeshDescription(Mesh, Description);
        // An existing asset is loaded and changed in place. A fresh package with the same name would
        // be fully loaded from disk when saved, and the old mesh would overwrite the new one.
        const FString PackageName = FString::Printf(TEXT("/Game/World/Buildings/lent/%s"), *Id);
        UPackage* Package = FPackageName::DoesPackageExist(PackageName) ? LoadPackage(nullptr, *PackageName, LOAD_None) : CreatePackage(*PackageName);
        if (!Package)
        {
            UE_LOG(LogMarburgumFacades, Error, TEXT("MB2FACADE %s could not load its package"), *Id);
            ++Failed;
            continue;
        }
        Package->FullyLoad();
        UStaticMesh* StaticMesh = FindObject<UStaticMesh>(Package, *Id);
        if (StaticMesh)
        {
            // Let any post-load build of the old mesh finish before its source model is replaced.
            FAssetCompilingManager::Get().FinishAllCompilation();
            StaticMesh->Modify();
        }
        else
        {
            StaticMesh = NewObject<UStaticMesh>(Package, *Id, RF_Public | RF_Standalone);
        }
        // The mesh description is committed as the asset's source model. BuildFromMeshDescriptions
        // would only build render data in memory; the saved source would stay the old geometry and
        // come back whenever the render data is rebuilt.
        StaticMesh->SetNumSourceModels(1);
        StaticMesh->bAllowCPUAccess = true;
        FStaticMeshSourceModel& Source = StaticMesh->GetSourceModel(0);
        Source.BuildSettings.bRecomputeNormals = false;
        Source.BuildSettings.bRecomputeTangents = true;
        Source.BuildSettings.bRemoveDegenerates = true;
        Source.BuildSettings.bGenerateLightmapUVs = true;
        Source.BuildSettings.SrcLightmapIndex = 0;
        Source.BuildSettings.DstLightmapIndex = 1;
        StaticMesh->SetLightMapCoordinateIndex(1);
        StaticMesh->CreateMeshDescription(0, MoveTemp(Description));
        StaticMesh->CommitMeshDescription(0);
        StaticMesh->SetStaticMaterials({ FStaticMaterial(Plaster, TEXT("plaster"), TEXT("plaster")), FStaticMaterial(RoofTile, TEXT("roof"), TEXT("roof")),
            FStaticMaterial(Frame, TEXT("frame"), TEXT("frame")), FStaticMaterial(Glass, TEXT("glass"), TEXT("glass")),
            FStaticMaterial(FlatRoof, TEXT("flat_roof"), TEXT("flat_roof")) });
        StaticMesh->Build(true);
        if (!StaticMesh->GetRenderData() || StaticMesh->GetRenderData()->LODResources.Num() == 0
            || StaticMesh->GetRenderData()->LODResources[0].GetNumTriangles() == 0)
        {
            UE_LOG(LogMarburgumFacades, Error, TEXT("MB2FACADE %s did not build"), *Id);
            ++Failed;
            continue;
        }
        const int32 Built = StaticMesh->GetRenderData()->LODResources[0].GetNumTriangles();
        if (Built != static_cast<int32>(Mesh.Triangles.size()))
        {
            UE_LOG(LogMarburgumFacades, Warning, TEXT("MB2FACADE %s built %d triangles of %d"), *Id, Built, static_cast<int32>(Mesh.Triangles.size()));
        }
        StaticMesh->PostEditChange();
        StaticMesh->CreateBodySetup();
        // The shell is closed, so its own triangles are the collision and one side is enough.
        StaticMesh->GetBodySetup()->CollisionTraceFlag = CTF_UseComplexAsSimple;
        StaticMesh->GetBodySetup()->bDoubleSidedGeometry = false;
        FAssetCompilingManager::Get().FinishAllCompilation();
        const FMeshDescription* Committed = StaticMesh->GetMeshDescription(0);
        if (!Committed || Committed->Triangles().Num() != static_cast<int32>(Mesh.Triangles.size()) || StaticMesh->GetStaticMaterials().Num() != MB::MaterialCount)
        {
            UE_LOG(LogMarburgumFacades, Error, TEXT("MB2FACADE %s source model was not replaced"), *Id);
            ++Failed;
            continue;
        }
        Package->MarkPackageDirty();
        if (!UEditorLoadingAndSavingUtils::SavePackages({ Package }, false))
        {
            ++Failed;
            continue;
        }
        ++Rebuilt;
    }

    int32 Removed = 0;
    if (!bCheckOnly)
    {
        UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/World/lent"));
        if (!World)
        {
            UE_LOG(LogMarburgumFacades, Error, TEXT("MB2FACADE could not load /Game/World/lent"));
            return 1;
        }
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
    FString MethodText;
    for (const TPair<FString, int32>& Method : Methods)
    {
        MethodText += FString::Printf(TEXT(" %s=%d"), *Method.Key, Method.Value);
    }
    UE_LOG(LogMarburgumFacades, Display, TEXT("MB2FACADE done rebuilt=%d open=%d failed=%d dropped=%d removed=%d triangles=%lld check_only=%d%s"),
        Rebuilt, Open, Failed, Dropped.Num(), Removed, Triangles, bCheckOnly ? 1 : 0, *MethodText);
    return Open > 0 || Failed > 0 ? 1 : 0;
}
