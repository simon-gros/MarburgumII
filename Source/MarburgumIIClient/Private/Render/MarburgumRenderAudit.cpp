#include "MarburgumRenderAudit.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "Dom/JsonObject.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "RHIGlobals.h"
#include "RHIShaderPlatform.h"
#include "RHIStats.h"
#include "Scalability.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "StaticMeshResources.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumRenderAudit, Log, All);

namespace
{
    // The settings that decide what GFX-01–07 would change. A name this engine does not know is written as null, so a
    // renamed variable shows up instead of silently disappearing.
    const TCHAR* const AuditedCVars[] = {
        // Global illumination and reflections
        TEXT("r.DynamicGlobalIlluminationMethod"), TEXT("r.ReflectionMethod"), TEXT("r.Lumen.DiffuseIndirect.Allow"),
        TEXT("r.Lumen.Reflections.Allow"), TEXT("r.Lumen.HardwareRayTracing"), TEXT("r.Lumen.TraceMeshSDFs"),
        TEXT("r.Lumen.ScreenProbeGather.DownsampleFactor"), TEXT("r.SSR.Quality"), TEXT("r.SkyLight.RealTimeReflectionCapture"),
        TEXT("r.GenerateMeshDistanceFields"), TEXT("r.AllowStaticLighting"),
        // Shadows
        TEXT("r.Shadow.Virtual.Enable"), TEXT("r.Shadow.CSM.MaxCascades"), TEXT("r.Shadow.MaxResolution"),
        TEXT("r.Shadow.DistanceScale"), TEXT("r.ShadowQuality"), TEXT("r.ContactShadows"),
        // Anti-aliasing and resolution
        TEXT("r.AntiAliasingMethod"), TEXT("r.ScreenPercentage"), TEXT("r.TemporalAA.Upsampling"), TEXT("r.TSR.History.ScreenPercentage"),
        TEXT("r.DynamicRes.OperationMode"), TEXT("r.VSync"), TEXT("t.MaxFPS"),
        // Geometry
        TEXT("r.Nanite"), TEXT("r.Nanite.ProjectEnabled"), TEXT("r.ViewDistanceScale"), TEXT("r.StaticMeshLODDistanceScale"),
        TEXT("r.HZBOcclusion"), TEXT("foliage.DensityScale"), TEXT("grass.DensityScale"),
        // Lights, ray tracing, materials
        TEXT("r.MegaLights.EnableForProject"), TEXT("r.MegaLights.Allowed"), TEXT("r.RayTracing"), TEXT("r.RayTracing.Enable"), TEXT("r.Substrate"),
        // Sky, fog, water
        TEXT("r.VolumetricFog"), TEXT("r.VolumetricCloud"), TEXT("r.SkyAtmosphere"), TEXT("r.Water.SingleLayer"),
        // Exposure and post-processing
        TEXT("r.DefaultFeature.AutoExposure"), TEXT("r.DefaultFeature.AutoExposure.Method"),
        TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange"), TEXT("r.DefaultFeature.Bloom"),
        TEXT("r.DefaultFeature.MotionBlur"), TEXT("r.DefaultFeature.AmbientOcclusion"), TEXT("r.AmbientOcclusionLevels"),
        TEXT("r.BloomQuality"), TEXT("r.MotionBlurQuality"), TEXT("r.DepthOfFieldQuality"), TEXT("r.Tonemapper.Quality"),
        TEXT("r.TonemapperGamma"), TEXT("r.SceneColorFormat"), TEXT("r.LocalExposure"),
        // Textures and memory
        TEXT("r.Streaming.PoolSize"), TEXT("r.Streaming.LimitPoolSizeToVRAM"), TEXT("r.MaxAnisotropy"),
        // Optional NVIDIA features (ADR-029/030); absent plugins read null
        TEXT("r.NGX.DLSS.Enable"), TEXT("r.Streamline.DLSSG.Enable"), TEXT("t.Streamline.Reflex.Enable"), TEXT("r.DLSS5.Enable"),
    };

    const TCHAR* MobilityName(EComponentMobility::Type Mobility)
    {
        switch (Mobility)
        {
        case EComponentMobility::Static: return TEXT("static");
        case EComponentMobility::Stationary: return TEXT("stationary");
        default: return TEXT("movable");
        }
    }

    TSharedRef<FJsonObject> Rhi()
    {
        TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetStringField(TEXT("name"), GDynamicRHI ? GDynamicRHI->GetName() : TEXT("none"));
        Out->SetStringField(TEXT("shader_platform"), FDataDrivenShaderPlatformInfo::GetName(GMaxRHIShaderPlatform).ToString());
        Out->SetStringField(TEXT("adapter"), GRHIAdapterName);
        Out->SetStringField(TEXT("driver"), GRHIAdapterUserDriverVersion);
        Out->SetStringField(TEXT("driver_internal"), GRHIAdapterInternalDriverVersion);
        Out->SetStringField(TEXT("vendor_id"), FString::Printf(TEXT("0x%04x"), GRHIVendorId));
        Out->SetBoolField(TEXT("ray_tracing_supported"), GRHISupportsRayTracing);
        FTextureMemoryStats Texture;
        RHIGetTextureMemoryStats(Texture);
        Out->SetNumberField(TEXT("dedicated_video_memory_mb"), Texture.DedicatedVideoMemory >= 0 ? Texture.DedicatedVideoMemory / (1024.0 * 1024.0) : -1.0);
        Out->SetNumberField(TEXT("texture_pool_mb"), Texture.TexturePoolSize / (1024.0 * 1024.0));
        if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
        {
            const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
            Out->SetStringField(TEXT("viewport"), FString::Printf(TEXT("%dx%d"), Size.X, Size.Y));
        }
        return Out;
    }

    TSharedRef<FJsonObject> Build()
    {
        TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetStringField(TEXT("engine"), FEngineVersion::Current().ToString());
        Out->SetStringField(TEXT("configuration"), LexToString(FApp::GetBuildConfiguration()));
        Out->SetBoolField(TEXT("editor_process"), GIsEditor || WITH_EDITOR);
        Out->SetStringField(TEXT("project_version"), FApp::GetBuildVersion());
        return Out;
    }

    TSharedRef<FJsonObject> ScalabilityLevels()
    {
        const Scalability::FQualityLevels Levels = Scalability::GetQualityLevels();
        TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetNumberField(TEXT("resolution"), Levels.ResolutionQuality);
        Out->SetNumberField(TEXT("view_distance"), Levels.ViewDistanceQuality);
        Out->SetNumberField(TEXT("anti_aliasing"), Levels.AntiAliasingQuality);
        Out->SetNumberField(TEXT("shadow"), Levels.ShadowQuality);
        Out->SetNumberField(TEXT("global_illumination"), Levels.GlobalIlluminationQuality);
        Out->SetNumberField(TEXT("reflection"), Levels.ReflectionQuality);
        Out->SetNumberField(TEXT("post_process"), Levels.PostProcessQuality);
        Out->SetNumberField(TEXT("texture"), Levels.TextureQuality);
        Out->SetNumberField(TEXT("effects"), Levels.EffectsQuality);
        Out->SetNumberField(TEXT("foliage"), Levels.FoliageQuality);
        Out->SetNumberField(TEXT("shading"), Levels.ShadingQuality);
        Out->SetNumberField(TEXT("landscape"), Levels.LandscapeQuality);
        return Out;
    }

    TSharedRef<FJsonObject> CVars()
    {
        TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
        for (const TCHAR* Name : AuditedCVars)
        {
            if (const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
            {
                Out->SetStringField(Name, Variable->GetString());
            }
            else
            {
                Out->SetField(Name, MakeShared<FJsonValueNull>());
            }
        }
        return Out;
    }

    // Only what a volume overrides; everything else falls through to the cvar defaults above.
    TSharedRef<FJsonObject> PostProcessOverrides(const FPostProcessSettings& Settings)
    {
        TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
        if (Settings.bOverride_AutoExposureMethod) { Out->SetNumberField(TEXT("auto_exposure_method"), Settings.AutoExposureMethod.GetValue()); }
        if (Settings.bOverride_AutoExposureBias) { Out->SetNumberField(TEXT("auto_exposure_bias"), Settings.AutoExposureBias); }
        if (Settings.bOverride_AutoExposureMinBrightness) { Out->SetNumberField(TEXT("auto_exposure_min"), Settings.AutoExposureMinBrightness); }
        if (Settings.bOverride_AutoExposureMaxBrightness) { Out->SetNumberField(TEXT("auto_exposure_max"), Settings.AutoExposureMaxBrightness); }
        if (Settings.bOverride_DynamicGlobalIlluminationMethod) { Out->SetNumberField(TEXT("gi_method"), Settings.DynamicGlobalIlluminationMethod.GetValue()); }
        if (Settings.bOverride_ReflectionMethod) { Out->SetNumberField(TEXT("reflection_method"), Settings.ReflectionMethod.GetValue()); }
        if (Settings.bOverride_LumenSceneLightingQuality) { Out->SetNumberField(TEXT("lumen_scene_lighting_quality"), Settings.LumenSceneLightingQuality); }
        if (Settings.bOverride_LumenFinalGatherQuality) { Out->SetNumberField(TEXT("lumen_final_gather_quality"), Settings.LumenFinalGatherQuality); }
        if (Settings.bOverride_BloomIntensity) { Out->SetNumberField(TEXT("bloom_intensity"), Settings.BloomIntensity); }
        if (Settings.bOverride_VignetteIntensity) { Out->SetNumberField(TEXT("vignette"), Settings.VignetteIntensity); }
        if (Settings.bOverride_FilmGrainIntensity) { Out->SetNumberField(TEXT("film_grain"), Settings.FilmGrainIntensity); }
        if (Settings.bOverride_MotionBlurAmount) { Out->SetNumberField(TEXT("motion_blur"), Settings.MotionBlurAmount); }
        if (Settings.bOverride_AmbientOcclusionIntensity) { Out->SetNumberField(TEXT("ao_intensity"), Settings.AmbientOcclusionIntensity); }
        if (Settings.bOverride_WhiteTemp) { Out->SetNumberField(TEXT("white_temp"), Settings.WhiteTemp); }
        if (Settings.bOverride_ColorSaturation) { Out->SetStringField(TEXT("color_saturation"), Settings.ColorSaturation.ToString()); }
        if (Settings.bOverride_ColorContrast) { Out->SetStringField(TEXT("color_contrast"), Settings.ColorContrast.ToString()); }
        return Out;
    }

    TSharedRef<FJsonObject> PostProcessEntry(const FString& Owner, bool bUnbound, float Priority, float BlendWeight, const FPostProcessSettings& Settings)
    {
        TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetStringField(TEXT("owner"), Owner);
        Out->SetBoolField(TEXT("unbound"), bUnbound);
        Out->SetNumberField(TEXT("priority"), Priority);
        Out->SetNumberField(TEXT("blend_weight"), BlendWeight);
        Out->SetObjectField(TEXT("overrides"), PostProcessOverrides(Settings));
        return Out;
    }

    TSharedRef<FJsonObject> Scene(UWorld& World)
    {
        struct FLightCount
        {
            int32 Count = 0;
            int32 Shadowed = 0;
            TMap<FString, int32> Mobility;
        };
        TMap<FString, FLightCount> Lights;
        TArray<TSharedPtr<FJsonValue>> Directional, SkyLights, Fogs, PostProcess;
        int32 Atmospheres = 0, Clouds = 0, SkeletalComponents = 0;

        int32 MeshComponents = 0, InstancedComponents = 0, NoDrawDistance = 0, NaniteComponents = 0;
        int64 Instances = 0, Lod0Triangles = 0, NaniteLod0Triangles = 0;
        TSet<const UStaticMesh*> Meshes, NaniteMeshes, SingleLodMeshes;
        TSet<const UMaterialInterface*> Materials;
        int32 TranslucentSections = 0;

        for (TActorIterator<AActor> It(&World); It; ++It)
        {
            if (const APostProcessVolume* Volume = Cast<APostProcessVolume>(*It))
            {
                PostProcess.Add(MakeShared<FJsonValueObject>(PostProcessEntry(Volume->GetName(), Volume->bUnbound, Volume->Priority, Volume->BlendWeight, Volume->Settings)));
            }
            It->ForEachComponent<UActorComponent>(false, [&](UActorComponent* Component)
            {
                if (!Component->IsRegistered())
                {
                    return;
                }
                if (const ULightComponent* Light = Cast<ULightComponent>(Component))
                {
                    const FString Kind = Light->IsA<UDirectionalLightComponent>() ? TEXT("directional")
                        : Light->IsA<URectLightComponent>() ? TEXT("rect")
                        : Light->IsA<USpotLightComponent>() ? TEXT("spot")
                        : Light->IsA<UPointLightComponent>() ? TEXT("point") : TEXT("other");
                    FLightCount& Count = Lights.FindOrAdd(Kind);
                    ++Count.Count;
                    Count.Shadowed += Light->CastShadows && Light->IsVisible() ? 1 : 0;
                    ++Count.Mobility.FindOrAdd(MobilityName(Light->Mobility));
                    if (const UDirectionalLightComponent* Sun = Cast<UDirectionalLightComponent>(Light))
                    {
                        TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
                        Entry->SetStringField(TEXT("owner"), It->GetName());
                        Entry->SetBoolField(TEXT("visible"), Sun->IsVisible());
                        Entry->SetNumberField(TEXT("intensity_lux"), Sun->Intensity);
                        Entry->SetStringField(TEXT("mobility"), MobilityName(Sun->Mobility));
                        Entry->SetBoolField(TEXT("cast_shadows"), Sun->CastShadows != 0);
                        Entry->SetBoolField(TEXT("atmosphere_sun_light"), Sun->IsUsedAsAtmosphereSunLight());
                        Entry->SetNumberField(TEXT("atmosphere_index"), Sun->GetAtmosphereSunLightIndex());
                        Entry->SetNumberField(TEXT("dynamic_shadow_distance_m"), Sun->DynamicShadowDistanceMovableLight / 100.0);
                        Entry->SetNumberField(TEXT("cascades"), Sun->DynamicShadowCascades);
                        Entry->SetStringField(TEXT("pitch_yaw"), FString::Printf(TEXT("%.1f,%.1f"), Sun->GetComponentRotation().Pitch, Sun->GetComponentRotation().Yaw));
                        Directional.Add(MakeShared<FJsonValueObject>(Entry));
                    }
                }
                else if (const USkyLightComponent* Sky = Cast<USkyLightComponent>(Component))
                {
                    TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
                    Entry->SetStringField(TEXT("owner"), It->GetName());
                    Entry->SetBoolField(TEXT("real_time_capture"), Sky->bRealTimeCapture);
                    Entry->SetStringField(TEXT("mobility"), MobilityName(Sky->Mobility));
                    Entry->SetNumberField(TEXT("intensity"), Sky->Intensity);
                    Entry->SetNumberField(TEXT("cubemap_resolution"), Sky->CubemapResolution);
                    SkyLights.Add(MakeShared<FJsonValueObject>(Entry));
                }
                else if (const UExponentialHeightFogComponent* Fog = Cast<UExponentialHeightFogComponent>(Component))
                {
                    TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
                    Entry->SetStringField(TEXT("owner"), It->GetName());
                    Entry->SetNumberField(TEXT("density"), Fog->FogDensity);
                    Entry->SetBoolField(TEXT("volumetric"), Fog->bEnableVolumetricFog);
                    Fogs.Add(MakeShared<FJsonValueObject>(Entry));
                }
                else if (Component->IsA<USkyAtmosphereComponent>())
                {
                    ++Atmospheres;
                }
                else if (Component->IsA<UVolumetricCloudComponent>())
                {
                    ++Clouds;
                }
                else if (const UPostProcessComponent* Post = Cast<UPostProcessComponent>(Component))
                {
                    PostProcess.Add(MakeShared<FJsonValueObject>(PostProcessEntry(It->GetName() + TEXT(".") + Post->GetName(), Post->bUnbound, Post->Priority, Post->BlendWeight, Post->Settings)));
                }
                else if (Component->IsA<USkeletalMeshComponent>())
                {
                    ++SkeletalComponents;
                }
                else if (const UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Component))
                {
                    const UStaticMesh* Mesh = MeshComponent->GetStaticMesh();
                    if (!Mesh || !MeshComponent->IsVisible())
                    {
                        return;
                    }
                    const UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(MeshComponent);
                    const int64 Count = Instanced ? Instanced->GetInstanceCount() : 1;
                    ++MeshComponents;
                    InstancedComponents += Instanced ? 1 : 0;
                    Instances += Count;
                    NoDrawDistance += MeshComponent->LDMaxDrawDistance <= 0.f && MeshComponent->CachedMaxDrawDistance <= 0.f ? 1 : 0;
                    Meshes.Add(Mesh);
                    const FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
                    const int64 Triangles = RenderData && RenderData->LODResources.Num() > 0 ? RenderData->LODResources[0].GetNumTriangles() * Count : 0;
                    Lod0Triangles += Triangles;
                    if (Mesh->HasValidNaniteData())
                    {
                        ++NaniteComponents;
                        NaniteMeshes.Add(Mesh);
                        NaniteLod0Triangles += Triangles;
                    }
                    if (Mesh->GetNumLODs() <= 1)
                    {
                        SingleLodMeshes.Add(Mesh);
                    }
                    for (int32 Slot = 0; Slot < MeshComponent->GetNumMaterials(); ++Slot)
                    {
                        if (const UMaterialInterface* Material = MeshComponent->GetMaterial(Slot))
                        {
                            Materials.Add(Material);
                            TranslucentSections += IsTranslucentBlendMode(Material->GetBlendMode()) ? 1 : 0;
                        }
                    }
                }
            });
        }

        TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetStringField(TEXT("map"), World.GetMapName());
        TSharedRef<FJsonObject> LightsOut = MakeShared<FJsonObject>();
        for (const TPair<FString, FLightCount>& Pair : Lights)
        {
            TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetNumberField(TEXT("count"), Pair.Value.Count);
            Entry->SetNumberField(TEXT("shadow_casting"), Pair.Value.Shadowed);
            for (const TPair<FString, int32>& Mobility : Pair.Value.Mobility)
            {
                Entry->SetNumberField(Mobility.Key, Mobility.Value);
            }
            LightsOut->SetObjectField(Pair.Key, Entry);
        }
        Out->SetObjectField(TEXT("lights"), LightsOut);
        Out->SetArrayField(TEXT("directional_lights"), Directional);
        Out->SetArrayField(TEXT("sky_lights"), SkyLights);
        Out->SetArrayField(TEXT("height_fogs"), Fogs);
        Out->SetNumberField(TEXT("sky_atmospheres"), Atmospheres);
        Out->SetNumberField(TEXT("volumetric_clouds"), Clouds);
        Out->SetArrayField(TEXT("post_process"), PostProcess);

        TSharedRef<FJsonObject> MeshesOut = MakeShared<FJsonObject>();
        MeshesOut->SetNumberField(TEXT("components"), MeshComponents);
        MeshesOut->SetNumberField(TEXT("instanced_components"), InstancedComponents);
        MeshesOut->SetNumberField(TEXT("instances"), static_cast<double>(Instances));
        MeshesOut->SetNumberField(TEXT("unique_meshes"), Meshes.Num());
        MeshesOut->SetNumberField(TEXT("single_lod_meshes"), SingleLodMeshes.Num());
        MeshesOut->SetNumberField(TEXT("components_without_draw_distance"), NoDrawDistance);
        MeshesOut->SetNumberField(TEXT("lod0_triangles"), static_cast<double>(Lod0Triangles));
        MeshesOut->SetNumberField(TEXT("nanite_meshes"), NaniteMeshes.Num());
        MeshesOut->SetNumberField(TEXT("nanite_components"), NaniteComponents);
        MeshesOut->SetNumberField(TEXT("nanite_lod0_triangles"), static_cast<double>(NaniteLod0Triangles));
        MeshesOut->SetNumberField(TEXT("unique_materials"), Materials.Num());
        MeshesOut->SetNumberField(TEXT("translucent_sections"), TranslucentSections);
        MeshesOut->SetNumberField(TEXT("skeletal_components"), SkeletalComponents);
        Out->SetObjectField(TEXT("static_meshes"), MeshesOut);
        return Out;
    }
}

namespace MarburgumII::RenderAudit
{
    bool WriteJson(UWorld& World, const FString& Path)
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetNumberField(TEXT("schema"), 1);
        Root->SetObjectField(TEXT("rhi"), Rhi());
        Root->SetObjectField(TEXT("build"), Build());
        Root->SetObjectField(TEXT("scalability"), ScalabilityLevels());
        const TSharedRef<FJsonObject> Variables = CVars();
        Root->SetObjectField(TEXT("cvars"), Variables);
        const TSharedRef<FJsonObject> SceneOut = Scene(World);
        Root->SetObjectField(TEXT("scene"), SceneOut);

        FString Text;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
        if (!FJsonSerializer::Serialize(Root, Writer) || !FFileHelper::SaveStringToFile(Text, *Path))
        {
            UE_LOG(LogMarburgumRenderAudit, Error, TEXT("MB2RENDERAUDIT event=write_failed path=%s"), *Path);
            return false;
        }
        const TSharedPtr<FJsonObject> Meshes = SceneOut->GetObjectField(TEXT("static_meshes"));
        const auto Value = [&Variables](const TCHAR* Name)
        {
            FString Text;
            return Variables->TryGetStringField(Name, Text) ? Text : FString(TEXT("null"));
        };
        UE_LOG(LogMarburgumRenderAudit, Display,
            TEXT("MB2RENDERAUDIT rhi=%s adapter=\"%s\" gi=%s reflections=%s vsm=%s aa=%s screen_pct=%s nanite_meshes=%d/%d lod0_triangles=%.0f path=%s"),
            GDynamicRHI ? GDynamicRHI->GetName() : TEXT("none"), *GRHIAdapterName,
            *Value(TEXT("r.DynamicGlobalIlluminationMethod")), *Value(TEXT("r.ReflectionMethod")),
            *Value(TEXT("r.Shadow.Virtual.Enable")), *Value(TEXT("r.AntiAliasingMethod")), *Value(TEXT("r.ScreenPercentage")), static_cast<int32>(Meshes->GetNumberField(TEXT("nanite_meshes"))),
            static_cast<int32>(Meshes->GetNumberField(TEXT("unique_meshes"))), Meshes->GetNumberField(TEXT("lod0_triangles")), *Path);
        return true;
    }
}
