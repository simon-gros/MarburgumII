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
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumFacades, Log, All);

namespace
{
    namespace MB = MarburgumBuildings;

    // The footprints of Lent and its eight neighbouring windows, joined across the window edges by
    // `lent-ground merge-buildings` (ADR-045), and the list of the ones the level places, which the
    // ground tool seats.
    FString SourcePath()
    {
        return FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/World/lent_ground/buildings.json"));
    }

    FString PlacedPath()
    {
        return FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/World/lent_ground/placed_buildings.txt"));
    }

    // Every wall reaches this far below its base, so paving and grass meet it at their own height
    // instead of being lifted to its bottom edge. The ground tool must use the same value.
    constexpr double Footing = 2.5;

    // Deeper footings the ground tool asks for, where the surveyed ground beside a building falls
    // further than Footing below its base (`buildings.deep_footings_m` in its ground.json).
    TSharedPtr<FJsonObject> ReadDeepFootings()
    {
        FString Text;
        TSharedPtr<FJsonObject> Root;
        if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/World/lent_ground/ground.json")))
            || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
        {
            return nullptr;
        }
        const TSharedPtr<FJsonObject>* Buildings = nullptr;
        const TSharedPtr<FJsonObject>* Deep = nullptr;
        if (Root->TryGetObjectField(TEXT("buildings"), Buildings) && Buildings && (*Buildings)->TryGetObjectField(TEXT("deep_footings_m"), Deep) && Deep)
        {
            return *Deep;
        }
        return nullptr;
    }

    // A building that was already in Lent keeps its asset under Buildings/lent; a new one goes under
    // the window it mostly stands in.
    FString BuildingPackage(const FString& Id, const FString& Window)
    {
        const FString Lent = FString::Printf(TEXT("/Game/World/Buildings/lent/%s"), *Id);
        return FPackageName::DoesPackageExist(Lent) || Window.IsEmpty() ? Lent : FString::Printf(TEXT("/Game/World/Buildings/%s/%s"), *Window, *Id);
    }

    bool IsBuildingActor(const AStaticMeshActor* Actor)
    {
        const UStaticMesh* Mesh = Actor->GetStaticMeshComponent() ? Actor->GetStaticMeshComponent()->GetStaticMesh() : nullptr;
        return Mesh && Mesh->GetPathName().StartsWith(TEXT("/Game/World/Buildings/"));
    }

    // Plan metres (x east, y south of Stari most, z up) to Unreal centimetres. A rotation, so windings
    // and normals carry over unchanged.
    FVector Ue(const MB::FVec3& P)
    {
        return FVector(-P.Y * 100.0, P.X * 100.0, P.Z * 100.0);
    }

    bool ReadFootprints(TArray<FString>& OutIds, TArray<FString>& OutWindows, std::vector<MB::FFootprint>& OutFootprints, TArray<FString>& OutDropped)
    {
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, *SourcePath()))
        {
            return false;
        }
        TSharedPtr<FJsonObject> Root;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        {
            return false;
        }
        const TSharedPtr<FJsonObject> DeepFootings = ReadDeepFootings();
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

            // Walls start just below the lowest terrain under the footprint, so none floats on a slope,
            // and reach their footing below that.
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
            double Deep = Footing;
            if (DeepFootings.IsValid() && DeepFootings->TryGetNumberField(Id, Deep))
            {
                Deep = std::max(Deep, Footing);
            }
            Footprint.Base -= Deep;
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
            OutWindows.Add(Object->HasTypedField<EJson::String>(TEXT("window")) ? Object->GetStringField(TEXT("window")) : FString());
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
        const TCHAR* Names[MB::MaterialCount] = { TEXT("plaster"), TEXT("roof"), TEXT("frame"), TEXT("glass"), TEXT("flat_roof"), TEXT("socle"), TEXT("trim") };
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

    // A negative roughness leaves the parent's.
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
        if (Roughness >= 0.0f)
        {
            Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("RoughnessDefault")), Roughness);
        }
        Instance->PostEditChange();
        Instance->MarkPackageDirty();
        return Instance;
    }

    struct FShade
    {
        const TCHAR* Name;
        // Multiplies the parent's tint: the target colour (sRGB in the comment) in linear terms over
        // the texture's linear mean (plaster_mineral 0.413 0.333 0.222, roof_bitumen 0.036 0.035 0.032).
        FLinearColor Gain;
        // How often it comes up, out of the palette's total.
        int32 Weight;
    };

    // Render colours of 1990s Maribor: Maria Theresa yellow and ochre in the old town, cream and pale
    // yellow, faded salmon and sage, and the grey render of post-war blocks. The original's beige stays
    // one of them.
    const FShade PlasterShades[] = {
        { TEXT("MI_plaster_beige"), FLinearColor(1.051f, 1.054f, 1.040f), 3 },        // 176 160 132
        { TEXT("MI_plaster_ochre"), FLinearColor(1.629f, 1.369f, 0.787f), 2 },        // 214 180 116
        { TEXT("MI_plaster_pale_yellow"), FLinearColor(1.800f, 1.560f, 1.000f), 2 },  // 225 194 134
        { TEXT("MI_plaster_cream"), FLinearColor(1.750f, 1.650f, 1.700f), 2 },        // 222 198 166
        { TEXT("MI_plaster_warm_grey"), FLinearColor(1.078f, 1.221f, 1.541f), 2 },    // 178 171 158
        { TEXT("MI_plaster_salmon"), FLinearColor(1.595f, 1.206f, 1.335f), 1 },       // 212 170 148
        { TEXT("MI_plaster_sage"), FLinearColor(1.189f, 1.581f, 1.718f), 1 },         // 186 192 166
        { TEXT("MI_plaster_off_white"), FLinearColor(1.550f, 1.700f, 2.200f), 1 },    // 210 198 186
        { TEXT("MI_plaster_render_grey"), FLinearColor(0.805f, 0.955f, 1.276f), 2 },  // 156 153 145
    };

    // Flat roofs: gravel ballast and weathered grey bitumen rather than the texture's near-black.
    const FShade FlatRoofShades[] = {
        { TEXT("MI_roof_flat_gravel"), FLinearColor(7.08f, 6.76f, 6.38f), 2 },        // 138 133 124
        { TEXT("MI_roof_flat_grey"), FLinearColor(4.18f, 4.24f, 4.29f), 2 },          // 108 107 103
        { TEXT("MI_roof_flat_dark"), FLinearColor(2.59f, 2.68f, 2.81f), 1 },          //  86  86  84
    };

    // Façade bands, on every building: a plinth of dark grey-brown render, and light trim for the
    // string course and cornice.
    const FShade SocleShades[] = {
        { TEXT("MI_facade_socle"), FLinearColor(0.392f, 0.432f, 0.550f), 1 },        // 112 106  98
    };
    const FShade TrimShades[] = {
        { TEXT("MI_facade_trim"), FLinearColor(1.840f, 2.150f, 2.779f), 1 },         // 226 220 206
    };

    // One instance per shade under Parent, its tint the parent's times the shade's gain.
    template <size_t N>
    TArray<UMaterialInstanceConstant*> EnsureShades(const FShade (&Shades)[N], UMaterialInterface* Parent)
    {
        FLinearColor ParentTint(1.0f, 1.0f, 1.0f, 1.0f);
        Parent->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("BaseColorTint")), ParentTint);
        UE_LOG(LogMarburgumFacades, Display, TEXT("MB2FACADE %s BaseColorTint %s"), *Parent->GetName(), *ParentTint.ToString());
        TArray<UMaterialInstanceConstant*> Out;
        for (const FShade& Shade : Shades)
        {
            const FLinearColor Tint(ParentTint.R * Shade.Gain.R, ParentTint.G * Shade.Gain.G, ParentTint.B * Shade.Gain.B, ParentTint.A);
            for (int32 K = 0; K < Shade.Weight; ++K)
            {
                Out.Add(K == 0 ? EnsureMaterial(Shade.Name, Parent, Tint, -1.0f) : Out.Last());
            }
        }
        return Out;
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
        FString InspectWindow;
        FParse::Value(*Params, TEXT("Window="), InspectWindow);
        const FString Package = BuildingPackage(InspectId, InspectWindow);
        const FString Path = FString::Printf(TEXT("%s.%s"), *Package, *InspectId);
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
    TArray<FString> Windows;
    std::vector<MB::FFootprint> Footprints;
    TArray<FString> Dropped;
    if (!ReadFootprints(Ids, Windows, Footprints, Dropped))
    {
        UE_LOG(LogMarburgumFacades, Error, TEXT("MB2FACADE could not read %s (run lent-ground merge-buildings)"), *SourcePath());
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
    // Kept through the garbage collections between buildings.
    for (UObject* Material : { static_cast<UObject*>(Parent), static_cast<UObject*>(Frame), static_cast<UObject*>(Glass), static_cast<UObject*>(Plaster), static_cast<UObject*>(RoofTile), static_cast<UObject*>(FlatRoof) })
    {
        Material->AddToRoot();
    }
    // Each building takes a render colour and a flat-roof finish from its id, so a rebuild keeps them.
    const TArray<UMaterialInstanceConstant*> Plasters = EnsureShades(PlasterShades, Plaster);
    const TArray<UMaterialInstanceConstant*> FlatRoofs = EnsureShades(FlatRoofShades, FlatRoof);
    const TArray<UMaterialInstanceConstant*> Socles = EnsureShades(SocleShades, Plaster);
    const TArray<UMaterialInstanceConstant*> Trims = EnsureShades(TrimShades, Plaster);
    TArray<UPackage*> MaterialPackages{ Frame->GetOutermost(), Glass->GetOutermost() };
    for (const TArray<UMaterialInstanceConstant*>* Shades : { &Plasters, &FlatRoofs, &Socles, &Trims })
    {
        for (UMaterialInstanceConstant* Shade : *Shades)
        {
            if (!Shade)
            {
                UE_LOG(LogMarburgumFacades, Error, TEXT("MB2FACADE missing materials"));
                return 1;
            }
            Shade->AddToRoot();
            MaterialPackages.AddUnique(Shade->GetOutermost());
        }
    }
    if (!bCheckOnly)
    {
        UEditorLoadingAndSavingUtils::SavePackages(MaterialPackages, false);
    }

    int32 Rebuilt = 0;
    int32 Failed = 0;
    int32 Open = 0;
    // The buildings that built and saved, and where their meshes are, for placing them.
    TMap<FString, FString> Saved;
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
        UE_LOG(LogMarburgumFacades, Display, TEXT("MB2FACADE %s roof=%s corners=%d party=%d gables=%d windows=%d bands=%d triangles=%d closed=%d strict=%d open_edges=%d volume_m3=%.0f"),
            *Id, UTF8_TO_TCHAR(Mesh.RoofMethod.c_str()), static_cast<int32>(Footprint.Outline.size()), Mesh.PartyWalls, Mesh.Gables, Mesh.Windows, Mesh.Bands,
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
        const FString PackageName = BuildingPackage(Id, Windows[Index]);
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
        const uint32 Pick = FCrc::StrCrc32(*Id);
        UMaterialInterface* Walls = Plasters[Pick % Plasters.Num()];
        UMaterialInterface* Deck = FlatRoofs[(Pick / 7) % FlatRoofs.Num()];
        StaticMesh->SetStaticMaterials({ FStaticMaterial(Walls, TEXT("plaster"), TEXT("plaster")), FStaticMaterial(RoofTile, TEXT("roof"), TEXT("roof")),
            FStaticMaterial(Frame, TEXT("frame"), TEXT("frame")), FStaticMaterial(Glass, TEXT("glass"), TEXT("glass")),
            FStaticMaterial(Deck, TEXT("flat_roof"), TEXT("flat_roof")), FStaticMaterial(Socles[0], TEXT("socle"), TEXT("socle")),
            FStaticMaterial(Trims[0], TEXT("trim"), TEXT("trim")) });
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
        Saved.Add(Id, FString::Printf(TEXT("%s.%s"), *PackageName, *Id));
        ++Rebuilt;
        if (Rebuilt % 50 == 0)
        {
            // Hundreds of meshes stay referenced by nothing once saved; let them go.
            CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        }
    }

    int32 Removed = 0;
    int32 Placed = 0;
    if (!bCheckOnly && Only.IsEmpty())
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
        // Buildings of the neighbouring windows are placed like Lent's own: meshes in world
        // coordinates, actors at the origin, labelled with the footprint id.
        TSet<FString> Present;
        for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
        {
            if (IsBuildingActor(*It))
            {
                Present.Add(It->GetActorLabel());
            }
        }
        for (int32 Index = 0; Index < Ids.Num(); ++Index)
        {
            const FString* MeshPath = Saved.Find(Ids[Index]);
            if (!MeshPath || Present.Contains(Ids[Index]))
            {
                continue;
            }
            UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, **MeshPath);
            AStaticMeshActor* Actor = Mesh ? World->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator) : nullptr;
            if (!Actor)
            {
                UE_LOG(LogMarburgumFacades, Error, TEXT("MB2FACADE could not place %s"), *Ids[Index]);
                ++Failed;
                continue;
            }
            Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
            Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
            Actor->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            Actor->SetActorLabel(Ids[Index]);
            Actor->SetFolderPath(*FString::Printf(TEXT("Buildings/%s"), *Windows[Index]));
            Present.Add(Ids[Index]);
            ++Placed;
        }
        // The neighbouring windows' bridges and river sheets, as the original game placed them: a
        // bridge mesh about its own centre, at the origin its import recorded; a river sheet in
        // world coordinates. The swim commandlet fills every `Drava` sheet with water volumes.
        struct FStructure { const TCHAR* Mesh; const TCHAR* Label; bool bBridge; };
        const FStructure Structures[] = {
            { TEXT("/Game/World/Bridges/pristan_studenska.pristan_studenska"), TEXT("Bridge_pristan_studenska"), true },
            { TEXT("/Game/World/Bridges/titov_most_titov.titov_most_titov"), TEXT("Bridge_titov_most_titov"), true },
            { TEXT("/Game/World/Bridges/titov_most_zelezniski.titov_most_zelezniski"), TEXT("Bridge_titov_most_zelezniski"), true },
            { TEXT("/Game/World/Water/pristan.pristan"), TEXT("Drava_pristan"), false },
            { TEXT("/Game/World/Water/titov_most.titov_most"), TEXT("Drava_titov_most"), false },
        };
        TSet<FString> Labels;
        const AStaticMeshActor* LentDrava = nullptr;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            Labels.Add(It->GetActorLabel());
            if (It->GetActorLabel() == TEXT("Drava"))
            {
                LentDrava = Cast<AStaticMeshActor>(*It);
            }
        }
        for (const FStructure& Structure : Structures)
        {
            if (Labels.Contains(Structure.Label))
            {
                continue;
            }
            UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Structure.Mesh);
            FVector Origin = FVector::ZeroVector;
            if (Mesh && Structure.bBridge && !Origin.InitFromString(Mesh->GetOutermost()->GetMetaData().GetValue(Mesh, TEXT("MarburgumOrigin"))))
            {
                Mesh = nullptr;
            }
            AStaticMeshActor* Actor = Mesh ? World->SpawnActor<AStaticMeshActor>(Origin, FRotator::ZeroRotator) : nullptr;
            if (!Actor)
            {
                UE_LOG(LogMarburgumFacades, Error, TEXT("MB2FACADE could not place %s (%s)"), Structure.Label, Structure.Mesh);
                ++Failed;
                continue;
            }
            Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
            Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
            if (Structure.bBridge)
            {
                Actor->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            }
            else if (LentDrava)
            {
                // Collision as on Lent's own sheet; the volumes under it do the swimming.
                Actor->GetStaticMeshComponent()->SetCollisionProfileName(LentDrava->GetStaticMeshComponent()->GetCollisionProfileName());
                Actor->GetStaticMeshComponent()->SetCollisionEnabled(LentDrava->GetStaticMeshComponent()->GetCollisionEnabled());
                Actor->GetStaticMeshComponent()->SetMaterial(0, LentDrava->GetStaticMeshComponent()->GetMaterial(0));
            }
            Actor->SetActorLabel(Structure.Label);
            Actor->SetFolderPath(Structure.bBridge ? TEXT("Bridges") : TEXT("Water"));
            UE_LOG(LogMarburgumFacades, Display, TEXT("MB2FACADE placed %s at %s"), Structure.Label, *Origin.ToString());
            ++Placed;
        }
        // The ground tool seats every placed building (tools/lent-ground --placed).
        TArray<FString> PlacedIds;
        for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
        {
            if (IsBuildingActor(*It))
            {
                PlacedIds.Add(It->GetActorLabel());
            }
        }
        PlacedIds.Sort();
        if (!FFileHelper::SaveStringToFile(FString::Join(PlacedIds, TEXT("\n")) + TEXT("\n"), *PlacedPath()))
        {
            UE_LOG(LogMarburgumFacades, Error, TEXT("MB2FACADE could not write %s"), *PlacedPath());
            return 1;
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
    UE_LOG(LogMarburgumFacades, Display, TEXT("MB2FACADE done rebuilt=%d open=%d failed=%d dropped=%d removed=%d placed=%d triangles=%lld check_only=%d%s"),
        Rebuilt, Open, Failed, Dropped.Num(), Removed, Placed, Triangles, bCheckOnly ? 1 : 0, *MethodText);
    return Open > 0 || Failed > 0 ? 1 : 0;
}
