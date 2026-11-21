#include "MarburgumWorldAuditCommandlet.h"

#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Landscape.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshResources.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumWorldAudit, Log, All);

namespace MarburgumWorldAudit
{
    // The playable square of ADR-087 is plan +-1280 m (ADR-045 had +-768 m); the distant ring (ADR-054) lies outside it on purpose.
    constexpr double PlayableHalfExtent = 128000.0;
    constexpr double SquareSlack = 2000.0;
    // A prop is anything placed whose footprint fits in 10 m and that is not part of the generated city.
    constexpr double PropMaxHalfWidth = 500.0;
    constexpr double FloatTolerance = 5.0;
    constexpr double SunkTolerance = 30.0;
    constexpr int32 HighPolyProp = 50000;
    constexpr int32 LargeTexture = 4096;

    struct FFinding
    {
        FString Severity;
        FString Check;
        FString Subject;
        FString Detail;
    };

    struct FFolderStats
    {
        int32 Actors = 0;
        int32 Primitives = 0;
        int64 Triangles = 0;
        int32 Nanite = 0;
        int32 SingleLod = 0;
        int32 NoCollision = 0;
        int32 NoDrawDistance = 0;
        int32 Lights = 0;
        int32 ShadowLights = 0;
    };

    FString Sanitize(FString Text)
    {
        Text.ReplaceInline(TEXT("\t"), TEXT(" "));
        Text.ReplaceInline(TEXT("\n"), TEXT(" "));
        return Text;
    }

    bool IsGeneratedCity(const FString& Folder)
    {
        return Folder.StartsWith(TEXT("Buildings")) || Folder.StartsWith(TEXT("Ground")) || Folder.StartsWith(TEXT("Water"))
            || Folder.StartsWith(TEXT("Bridges"));
    }

    bool IsDefaultMaterial(const UMaterialInterface* Material)
    {
        if (!Material)
        {
            return true;
        }
        const UMaterial* Base = Material->GetMaterial();
        return Base == UMaterial::GetDefaultMaterial(MD_Surface) || Base->GetPathName().Contains(TEXT("WorldGridMaterial"));
    }

    int32 Triangles(const UStaticMesh* Mesh)
    {
        const FStaticMeshRenderData* Render = Mesh ? Mesh->GetRenderData() : nullptr;
        return Render && Render->LODResources.Num() > 0 ? Render->LODResources[0].GetNumTriangles() : 0;
    }

    int32 Sections(const UStaticMesh* Mesh)
    {
        const FStaticMeshRenderData* Render = Mesh ? Mesh->GetRenderData() : nullptr;
        return Render && Render->LODResources.Num() > 0 ? Render->LODResources[0].Sections.Num() : 0;
    }

    bool HasCollisionGeometry(const UStaticMesh* Mesh)
    {
        const UBodySetup* Body = Mesh ? Mesh->GetBodySetup() : nullptr;
        return Body && (Body->AggGeom.GetElementCount() > 0 || Body->CollisionTraceFlag == CTF_UseComplexAsSimple);
    }

    class FAudit
    {
    public:
        explicit FAudit(const FString& InOut) : Out(InOut) {}

        void Add(const TCHAR* Severity, const TCHAR* Check, const FString& Subject, const FString& Detail)
        {
            Findings.Add({Severity, Check, Sanitize(Subject), Sanitize(Detail)});
            CheckCounts.FindOrAdd(FString(Check))++;
        }

        void AuditLevel(UWorld* World)
        {
            TMap<FString, int32> Placement;
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                AActor* Actor = *It;
                const FString Folder = Actor->GetFolderPath().IsNone() ? TEXT("(root)") : Actor->GetFolderPath().ToString();
                const FString Name = Actor->GetActorLabel();
                FFolderStats& Stats = Folders.FindOrAdd(Folder);
                ++Stats.Actors;
                if (!IsGeneratedCity(Folder))
                {
                    // How far the actor's origin stands above the first surface under it that is not itself.
                    const FVector At = Actor->GetActorLocation();
                    FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumWorldAudit), true, Actor);
                    FHitResult Hit;
                    const bool bGround = World->LineTraceSingleByChannel(Hit, At + FVector(0, 0, 50.0), At - FVector(0, 0, 3000.0), ECC_Visibility, Params);
                    ActorRows.Add(FString::Printf(TEXT("%s\t%s\t%s\t%.2f,%.2f,%.2f\t%s\t%s"), *Folder, *Sanitize(Name), *Actor->GetClass()->GetName(), At.X / 100.0, At.Y / 100.0,
                        At.Z / 100.0, bGround ? *FString::Printf(TEXT("%.2f"), (At.Z - Hit.ImpactPoint.Z) / 100.0) : TEXT("-"),
                        bGround && Hit.GetActor() ? *Sanitize(Hit.GetActor()->GetActorLabel()) : TEXT("-")));
                }
                // A placed prop is judged as a whole: a person's head stands on its body, not on the ground.
                FBox PropBox(ForceInit);
                FString PropSubject;
                int32 PropTriangles = 0;
                TInlineComponentArray<UActorComponent*> Components(Actor);
                for (UActorComponent* Component : Components)
                {
                    if (const ULightComponent* Light = Cast<ULightComponent>(Component))
                    {
                        ++Stats.Lights;
                        Stats.ShadowLights += Light->CastShadows ? 1 : 0;
                    }
                    UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Component);
                    if (!MeshComponent)
                    {
                        continue;
                    }
                    ++Stats.Primitives;
                    const FString Subject = FString::Printf(TEXT("%s | %s.%s"), *Folder, *Name, *MeshComponent->GetName());
                    UStaticMesh* Mesh = MeshComponent->GetStaticMesh();
                    if (!Mesh)
                    {
                        Add(TEXT("high"), TEXT("missing_mesh"), Subject, TEXT("static mesh component with no mesh"));
                        continue;
                    }
                    const UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(MeshComponent);
                    const int32 Count = Instanced ? Instanced->GetInstanceCount() : 1;
                    Stats.Triangles += int64(Triangles(Mesh)) * Count;
                    Stats.Nanite += Mesh->IsNaniteEnabled() ? 1 : 0;
                    Stats.SingleLod += Mesh->GetNumLODs() <= 1 && !Mesh->IsNaniteEnabled() ? 1 : 0;
                    const bool bCollides = MeshComponent->IsCollisionEnabled();
                    Stats.NoCollision += bCollides ? 0 : 1;
                    Stats.NoDrawDistance += MeshComponent->LDMaxDrawDistance <= 0.f && MeshComponent->CachedMaxDrawDistance <= 0.f ? 1 : 0;
                    if (bCollides && !HasCollisionGeometry(Mesh))
                    {
                        Add(TEXT("medium"), TEXT("collision_without_geometry"), Subject,
                            FString::Printf(TEXT("%s collides but its mesh has no simple or complex-as-simple collision"), *Mesh->GetPathName()));
                    }
                    for (int32 Slot = 0; Slot < MeshComponent->GetNumMaterials(); ++Slot)
                    {
                        UMaterialInterface* Material = MeshComponent->GetMaterial(Slot);
                        if (IsDefaultMaterial(Material))
                        {
                            Add(TEXT("high"), TEXT("default_material"), Subject,
                                FString::Printf(TEXT("slot %d of %s draws %s"), Slot, *Mesh->GetPathName(), Material ? *Material->GetPathName() : TEXT("nothing (null)")));
                        }
                    }
                    if (!Instanced)
                    {
                        const FTransform Transform = MeshComponent->GetComponentTransform();
                        const FVector L = Transform.GetLocation();
                        const FRotator R = Transform.Rotator();
                        const FVector S = Transform.GetScale3D();
                        const FString Key = FString::Printf(TEXT("%s|%.0f,%.0f,%.0f|%.1f,%.1f,%.1f|%.2f,%.2f,%.2f"), *Mesh->GetPathName(), L.X, L.Y, L.Z,
                            R.Pitch, R.Yaw, R.Roll, S.X, S.Y, S.Z);
                        if (int32* Previous = Placement.Find(Key))
                        {
                            ++*Previous;
                            Add(TEXT("high"), TEXT("duplicate_placement"), Subject, FString::Printf(TEXT("same mesh and transform as an earlier component: %s"), *Key));
                        }
                        else
                        {
                            Placement.Add(Key, 1);
                        }
                    }
                    const FBoxSphereBounds Bounds = MeshComponent->Bounds;
                    const FVector Centre = Bounds.Origin;
                    if (!Folder.StartsWith(TEXT("Ground/Ring")) && Count == 1
                        && (FMath::Abs(Centre.X) > PlayableHalfExtent + SquareSlack || FMath::Abs(Centre.Y) > PlayableHalfExtent + SquareSlack))
                    {
                        Add(TEXT("low"), TEXT("outside_square"), Subject, FString::Printf(TEXT("centre at %.0f, %.0f m"), Centre.X / 100.0, Centre.Y / 100.0));
                    }
                    // A car's shell rides on its wheels' ground clearance; the parking check of ADR-040 owns its grounding.
                    const bool bVehicle = Folder.StartsWith(TEXT("Marburgum/Vehicles"));
                    if (!IsGeneratedCity(Folder) && !bVehicle && Count == 1 && Bounds.BoxExtent.X <= PropMaxHalfWidth && Bounds.BoxExtent.Y <= PropMaxHalfWidth
                        && MeshComponent->IsVisible() && !Actor->IsHidden())
                    {
                        PropBox += Bounds.GetBox();
                        PropSubject = FString::Printf(TEXT("%s | %s"), *Folder, *Name);
                        PropTriangles += Triangles(Mesh);
                    }
                }
                if (PropBox.IsValid)
                {
                    CheckGrounding(World, Actor, PropSubject, FBoxSphereBounds(PropBox));
                    if (PropTriangles > HighPolyProp)
                    {
                        Add(TEXT("low"), TEXT("high_poly_prop"), PropSubject, FString::Printf(TEXT("%d triangles at LOD0"), PropTriangles));
                    }
                }
            }
        }

        void CheckGrounding(UWorld* World, AActor* Actor, const FString& Subject, const FBoxSphereBounds& Bounds)
        {
            const double Bottom = Bounds.Origin.Z - Bounds.BoxExtent.Z;
            const double Top = Bounds.Origin.Z + Bounds.BoxExtent.Z;
            FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumWorldAudit), true, Actor);
            FHitResult Hit;
            // From the top of the prop down: the first surface that is not the prop itself.
            if (!World->LineTraceSingleByChannel(Hit, FVector(Bounds.Origin.X, Bounds.Origin.Y, Top + 50.0),
                    FVector(Bounds.Origin.X, Bounds.Origin.Y, Bottom - 2000.0), ECC_Visibility, Params))
            {
                Add(TEXT("high"), TEXT("no_ground"), Subject, FString::Printf(TEXT("nothing under the prop within 20 m (bottom z %.1f m)"), Bottom / 100.0));
                return;
            }
            const double Gap = Bottom - Hit.ImpactPoint.Z;
            const FString Under = Hit.GetActor() ? Hit.GetActor()->GetActorLabel() : TEXT("?");
            if (Gap > FloatTolerance)
            {
                Add(Gap > 50.0 ? TEXT("high") : TEXT("medium"), TEXT("floating_prop"), Subject,
                    FString::Printf(TEXT("bottom %.0f cm above %s at (%.1f, %.1f, %.2f) m"), Gap, *Under, Hit.ImpactPoint.X / 100.0, Hit.ImpactPoint.Y / 100.0, Hit.ImpactPoint.Z / 100.0));
            }
            else if (-Gap > SunkTolerance)
            {
                Add(-Gap > 100.0 ? TEXT("high") : TEXT("medium"), TEXT("sunk_prop"), Subject,
                    FString::Printf(TEXT("bottom %.0f cm below %s at (%.1f, %.1f, %.2f) m; prop height %.0f cm"), -Gap, *Under, Hit.ImpactPoint.X / 100.0,
                        Hit.ImpactPoint.Y / 100.0, Hit.ImpactPoint.Z / 100.0, Top - Bottom));
            }
            ++GroundedProps;
        }

        template <typename T>
        TArray<T*> LoadAll()
        {
            IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
            Registry.SearchAllAssets(true);
            FARFilter Filter;
            Filter.PackagePaths.Add(TEXT("/Game"));
            Filter.bRecursivePaths = true;
            Filter.ClassPaths.Add(T::StaticClass()->GetClassPathName());
            Filter.bRecursiveClasses = true;
            TArray<FAssetData> Assets;
            Registry.GetAssets(Filter, Assets);
            TArray<T*> Loaded;
            for (const FAssetData& Asset : Assets)
            {
                if (T* Object = Cast<T>(Asset.GetAsset()))
                {
                    Loaded.Add(Object);
                }
                else
                {
                    Add(TEXT("high"), TEXT("asset_load_failed"), Asset.GetObjectPathString(), TEXT("registry lists it but it does not load"));
                }
            }
            return Loaded;
        }

        void AuditMeshes()
        {
            TArray<UStaticMesh*> Meshes = LoadAll<UStaticMesh>();
            FAssetCompilingManager::Get().FinishAllCompilation();
            MeshRows.Add(TEXT("path\ttriangles\tlods\tnanite\tcollision\tuv_channels\tsections\tsize_m"));
            for (UStaticMesh* Mesh : Meshes)
            {
                const FString Path = Mesh->GetPathName();
                const FVector Size = Mesh->GetBounds().BoxExtent * 2.0 / 100.0;
                const int32 Uvs = Mesh->GetNumUVChannels(0);
                MeshRows.Add(FString::Printf(TEXT("%s\t%d\t%d\t%d\t%d\t%d\t%d\t%.2fx%.2fx%.2f"), *Path, Triangles(Mesh), Mesh->GetNumLODs(), Mesh->IsNaniteEnabled() ? 1 : 0,
                    HasCollisionGeometry(Mesh) ? 1 : 0, Uvs, Sections(Mesh), Size.X, Size.Y, Size.Z));
                if (Triangles(Mesh) == 0)
                {
                    Add(TEXT("high"), TEXT("empty_mesh"), Path, TEXT("no triangles at LOD0"));
                }
                if (Uvs == 0)
                {
                    Add(TEXT("medium"), TEXT("no_uvs"), Path, TEXT("no texture coordinates: textured materials cannot map onto it"));
                }
                if (Size.GetMax() < 0.01 || Size.GetMax() > 3000.0)
                {
                    Add(TEXT("medium"), TEXT("implausible_size"), Path, FString::Printf(TEXT("bounds %.3f x %.3f x %.3f m"), Size.X, Size.Y, Size.Z));
                }
                for (int32 Slot = 0; Slot < Mesh->GetStaticMaterials().Num(); ++Slot)
                {
                    const UMaterialInterface* Material = Mesh->GetStaticMaterials()[Slot].MaterialInterface;
                    if (IsDefaultMaterial(Material))
                    {
                        Add(TEXT("medium"), TEXT("mesh_default_material"), Path, FString::Printf(TEXT("slot %d (%s) has %s"), Slot,
                            *Mesh->GetStaticMaterials()[Slot].MaterialSlotName.ToString(), Material ? *Material->GetPathName() : TEXT("no material")));
                    }
                }
                TotalMeshTriangles += Triangles(Mesh);
            }
            MeshCount = Meshes.Num();
        }

        void AuditTextures()
        {
            TArray<UTexture2D*> Textures = LoadAll<UTexture2D>();
            TextureRows.Add(TEXT("path\tsource\tmips\tcompression\tsrgb\tlod_group\tnever_stream\tmax_size"));
            for (UTexture2D* Texture : Textures)
            {
                const FString Path = Texture->GetPathName();
                const int32 X = Texture->Source.GetSizeX();
                const int32 Y = Texture->Source.GetSizeY();
                const bool bNoMips = Texture->MipGenSettings == TMGS_NoMipmaps;
                const bool bNormal = Texture->CompressionSettings == TC_Normalmap;
                const FString Compression = StaticEnum<TextureCompressionSettings>()->GetNameStringByValue(Texture->CompressionSettings);
                const FString Group = StaticEnum<TextureGroup>()->GetNameStringByValue(Texture->LODGroup);
                TextureRows.Add(FString::Printf(TEXT("%s\t%dx%d\t%s\t%s\t%d\t%s\t%d\t%d"), *Path, X, Y, bNoMips ? TEXT("none") : TEXT("generated"), *Compression,
                    Texture->SRGB ? 1 : 0, *Group, Texture->NeverStream ? 1 : 0, Texture->MaxTextureSize));
                const FString Lower = Path.ToLower();
                const bool bLooksNormal = Lower.EndsWith(TEXT("_n")) || Lower.Contains(TEXT("normal")) || Lower.Contains(TEXT("_nrm"));
                if (bLooksNormal && !bNormal)
                {
                    Add(TEXT("medium"), TEXT("normal_map_settings"), Path, FString::Printf(TEXT("named as a normal map but compressed as %s, sRGB %d"), *Compression, Texture->SRGB ? 1 : 0));
                }
                if (bNormal && Texture->SRGB)
                {
                    Add(TEXT("medium"), TEXT("normal_map_srgb"), Path, TEXT("normal map with sRGB on"));
                }
                if (bNoMips && FMath::Max(X, Y) > 256 && Texture->LODGroup != TEXTUREGROUP_UI)
                {
                    Add(TEXT("medium"), TEXT("no_mips"), Path, FString::Printf(TEXT("%dx%d world texture without mips: shimmers at a distance and cannot stream"), X, Y));
                }
                if (!FMath::IsPowerOfTwo(X) || !FMath::IsPowerOfTwo(Y))
                {
                    if (Texture->LODGroup != TEXTUREGROUP_UI && Texture->PowerOfTwoMode == ETexturePowerOfTwoSetting::None)
                    {
                        Add(TEXT("low"), TEXT("non_power_of_two"), Path, FString::Printf(TEXT("%dx%d: no mips beyond the first and no streaming"), X, Y));
                    }
                }
                if (FMath::Max(X, Y) > LargeTexture)
                {
                    Add(TEXT("low"), TEXT("large_texture"), Path, FString::Printf(TEXT("%dx%d source, max size %d"), X, Y, Texture->MaxTextureSize));
                }
                if (Texture->NeverStream && FMath::Max(X, Y) > 1024)
                {
                    Add(TEXT("low"), TEXT("never_stream"), Path, FString::Printf(TEXT("%dx%d always fully resident"), X, Y));
                }
            }
            TextureCount = Textures.Num();
        }

        void AuditMaterials()
        {
            TArray<UMaterialInstance*> Instances = LoadAll<UMaterialInstance>();
            for (UMaterialInstance* Instance : Instances)
            {
                if (!Instance->Parent)
                {
                    Add(TEXT("high"), TEXT("orphan_material_instance"), Instance->GetPathName(), TEXT("material instance without a parent renders the default material"));
                }
            }
            MaterialCount = Instances.Num();
        }

        bool Write(const FString& MapName) const
        {
            IFileManager::Get().MakeDirectory(*Out, true);
            TArray<FString> Rows;
            Rows.Add(TEXT("severity\tcheck\tsubject\tdetail"));
            for (const FFinding& Finding : Findings)
            {
                Rows.Add(FString::Printf(TEXT("%s\t%s\t%s\t%s"), *Finding.Severity, *Finding.Check, *Finding.Subject, *Finding.Detail));
            }
            TArray<FString> FolderRows;
            FolderRows.Add(TEXT("folder\tactors\tmesh_components\ttriangles\tnanite\tsingle_lod_no_nanite\tno_collision\tno_draw_distance\tlights\tshadow_lights"));
            TArray<FString> Names;
            Folders.GetKeys(Names);
            Names.Sort();
            for (const FString& Name : Names)
            {
                const FFolderStats& S = Folders[Name];
                FolderRows.Add(FString::Printf(TEXT("%s\t%d\t%d\t%lld\t%d\t%d\t%d\t%d\t%d\t%d"), *Name, S.Actors, S.Primitives, S.Triangles, S.Nanite, S.SingleLod, S.NoCollision,
                    S.NoDrawDistance, S.Lights, S.ShadowLights));
            }
            TArray<FString> Summary;
            Summary.Add(FString::Printf(TEXT("map=%s"), *MapName));
            Summary.Add(FString::Printf(TEXT("findings=%d grounded_props_checked=%d"), Findings.Num(), GroundedProps));
            Summary.Add(FString::Printf(TEXT("static_meshes=%d (LOD0 triangles %lld) textures=%d material_instances=%d"), MeshCount, TotalMeshTriangles, TextureCount, MaterialCount));
            TArray<FString> Checks;
            CheckCounts.GetKeys(Checks);
            Checks.Sort();
            for (const FString& Check : Checks)
            {
                Summary.Add(FString::Printf(TEXT("check %s=%d"), *Check, CheckCounts[Check]));
            }
            for (const FString& Line : Summary)
            {
                UE_LOG(LogMarburgumWorldAudit, Display, TEXT("MB2AUDIT %s"), *Line);
            }
            return FFileHelper::SaveStringArrayToFile(Rows, *FPaths::Combine(Out, TEXT("findings.tsv")))
                && FFileHelper::SaveStringArrayToFile(MeshRows, *FPaths::Combine(Out, TEXT("meshes.tsv")))
                && FFileHelper::SaveStringArrayToFile(TextureRows, *FPaths::Combine(Out, TEXT("textures.tsv")))
                && FFileHelper::SaveStringArrayToFile(FolderRows, *FPaths::Combine(Out, TEXT("folders.tsv")))
                && FFileHelper::SaveStringArrayToFile(ActorRows, *FPaths::Combine(Out, TEXT("actors.tsv")))
                && FFileHelper::SaveStringArrayToFile(Summary, *FPaths::Combine(Out, TEXT("summary.txt")));
        }

    private:
        FString Out;
        TArray<FFinding> Findings;
        TMap<FString, int32> CheckCounts;
        TMap<FString, FFolderStats> Folders;
        TArray<FString> MeshRows;
        TArray<FString> TextureRows;
        TArray<FString> ActorRows{TEXT("folder\tlabel\tclass\tlocation_m\torigin_above_ground_m\tground")};
        int32 GroundedProps = 0;
        int32 MeshCount = 0;
        int32 TextureCount = 0;
        int32 MaterialCount = 0;
        int64 TotalMeshTriangles = 0;
    };
}

UMarburgumWorldAuditCommandlet::UMarburgumWorldAuditCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UMarburgumWorldAuditCommandlet::Main(const FString& Params)
{
    using namespace MarburgumWorldAudit;
    FString Map = TEXT("/Game/World/lent");
    FParse::Value(*Params, TEXT("Map="), Map);
    FString Out;
    if (!FParse::Value(*Params, TEXT("Out="), Out))
    {
        Out = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VisualAudit"), FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ")));
    }
    FAudit Audit(Out);
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
    if (!World)
    {
        UE_LOG(LogMarburgumWorldAudit, Error, TEXT("MB2AUDIT cannot load %s"), *Map);
        return 1;
    }
    World->UpdateWorldComponents(true, false);
    FAssetCompilingManager::Get().FinishAllCompilation();
    FString Probes;
    if (FParse::Value(*Params, TEXT("Probe="), Probes, false))
    {
        // Every surface a vertical ray meets at each X,Y (Unreal centimetres), top down: what stands on what.
        TArray<FString> Points;
        Probes.TrimQuotes().ParseIntoArray(Points, TEXT(";"));
        for (const FString& Point : Points)
        {
            FString X, Y;
            if (!Point.Split(TEXT(","), &X, &Y))
            {
                continue;
            }
            const FVector2D At(FCString::Atod(*X), FCString::Atod(*Y));
            double Z = 100000.0;
            for (int32 Layer = 0; Layer < 8; ++Layer)
            {
                FHitResult Hit;
                if (!World->LineTraceSingleByChannel(Hit, FVector(At, Z), FVector(At, -100000.0), ECC_Visibility, FCollisionQueryParams(SCENE_QUERY_STAT(MarburgumWorldAuditProbe), true)))
                {
                    break;
                }
                UE_LOG(LogMarburgumWorldAudit, Display, TEXT("MB2AUDIT probe %.0f,%.0f layer=%d z=%.1f actor=%s component=%s"), At.X, At.Y, Layer, Hit.ImpactPoint.Z,
                    Hit.GetActor() ? *Hit.GetActor()->GetActorLabel() : TEXT("?"), Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("?"));
                Z = Hit.ImpactPoint.Z - 1.0;
            }
        }
        return 0;
    }
    Audit.AuditLevel(World);
    Audit.AuditMeshes();
    Audit.AuditTextures();
    Audit.AuditMaterials();
    if (!Audit.Write(Map))
    {
        UE_LOG(LogMarburgumWorldAudit, Error, TEXT("MB2AUDIT cannot write to %s"), *Out);
        return 1;
    }
    UE_LOG(LogMarburgumWorldAudit, Display, TEXT("MB2AUDIT wrote %s"), *Out);
    return 0;
}
