#include "MarburgumWeatherLookContent.h"

#include "Engine/StaticMesh.h"
#include "FileHelpers.h"
#include "MarburgumWeatherLook.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionEyeAdaptationInverse.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialParameterCollection.h"
#include "MeshDescription.h"
#include "MeshDescriptionBuilder.h"
#include "Misc/PackageName.h"
#include "AssetCompilingManager.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumWeatherLookContent, Log, All);

namespace MarburgumWeatherLookContent
{
    namespace Look = MarburgumII::WeatherLook;

    namespace
    {
        /** Round the curtain. 48 facets keep the polygon out of sight at 6 m. */
        constexpr int32 Segments = 48;

        /** Fixed so a regenerated collection keeps the ids the paving and landscape materials were compiled against. */
        const FGuid WetnessId(0x6D623257, 0x45545345, 0x53530001, 0x00000062);
        const FGuid SnowCoverId(0x6D623257, 0x45545345, 0x53530002, 0x00000062);

        /**
         * The streaks and flakes, from the curtain's UV alone; the original's, unchanged.
         *
         * x runs once round the curtain and y from its foot to its top. Rain is thin streaks in columns and snow is round
         * flakes in cells, both scrolling down, each column at its own speed so the fall never reads as one sheet. Column
         * counts are whole numbers, so the pattern closes on itself where x wraps from 1 to 0.
         *
         * The sizes are metres in disguise. The near curtain is 24 m tall and 2 pi 6 m round, so one unit of y is 0.6366
         * units of x; the far one is the same mesh 2.5 times larger and draws proportionally finer, so its drops look the
         * same size further away. Rain falls at about 8.5 m/s and snow at about 1 m/s, which is what those are in still air.
         */
        const TCHAR* const Code = TEXT(R"(
	float Far = saturate(Layer);
	float Height = 24.0 * lerp(1.0, 2.5, Far);
	float Seed = 1.0 + Far * 57.0;
	float RainSeen = 0.0;
	float SnowSeen = 0.0;

	if (Rain > 0.001)
	{
		float Columns = floor(lerp(260.0, 640.0, Far));
		float Rows = lerp(15.0, 36.0, Far);
		float C = UV.x * Columns;
		float Column = floor(C);
		float Across = frac(C);
		float H1 = frac(sin(Column * 12.9898 + Seed * 78.233) * 43758.5453);
		float H2 = frac(sin(Column * 39.3468 + Seed * 11.135) * 24634.6345);
		float Speed = 8.5 * (0.85 + 0.3 * H1) / Height;
		float Y = (UV.y + Time * Speed) * Rows + H2 * 17.0;
		float Cell = floor(Y);
		float Along = frac(Y);
		float H3 = frac(sin(Cell * 37.719 + Column * 4.581 + Seed) * 23421.631);
		float H4 = frac(sin(Cell * 91.173 + Column * 7.219 + Seed) * 31337.113);
		float Present = step(frac(H3 * 7.13), Rain * 0.75);
		float Centre = 0.25 + 0.5 * H4;
		float Width = lerp(0.07, 0.11, Far);
		float Line = 1.0 - smoothstep(Width * 0.35, Width, abs(Across - Centre));
		// The drop's path in one exposure: sharp at its head, which is the bottom, fading upwards.
		float Length = 0.3 + 0.15 * H1;
		float Start = (1.0 - Length) * H3;
		float Streak = smoothstep(Start, Start + 0.04, Along) * (1.0 - smoothstep(Start, Start + Length, Along));
		RainSeen = Line * Streak * Present * lerp(0.35, 0.22, Far) * (0.6 + 0.4 * Rain);
	}

	if (Snow > 0.001)
	{
		float Columns = floor(lerp(160.0, 400.0, Far));
		float Rows = Columns * 0.6366;
		float C = UV.x * Columns;
		float Column = floor(C);
		float Across = frac(C);
		float H1 = frac(sin(Column * 63.7264 + Seed * 10.873) * 43758.5453);
		float H2 = frac(sin(Column * 21.3517 + Seed * 51.191) * 15731.7431);
		float Speed = 1.0 * (0.7 + 0.6 * H1) / Height;
		float Y = (UV.y + Time * Speed) * Rows + H2 * 13.0;
		float Cell = floor(Y);
		float Along = frac(Y);
		float H3 = frac(sin(Cell * 27.619 + Column * 5.237 + Seed) * 29815.317);
		float H4 = frac(sin(Cell * 73.156 + Column * 3.917 + Seed) * 17351.873);
		float Present = step(frac(H4 * 5.71), Snow * 0.55);
		// Flakes drift from side to side as they fall, each on its own beat.
		float Sway = 0.18 * sin(Time * (0.9 + 0.8 * H4) + H3 * 6.2832);
		float2 Offset = float2(Across, Along) - float2(0.3 + 0.4 * H3 + Sway, 0.3 + 0.4 * H4);
		float Radius = lerp(0.09, 0.07, Far) * (0.7 + 0.6 * H3);
		float Flake = 1.0 - smoothstep(Radius * 0.5, Radius, length(Offset));
		SnowSeen = Flake * Present * lerp(0.85, 0.6, Far);
	}

	// Faded out towards the curtain's open ends, which would otherwise show as a hard edge.
	float Ends = smoothstep(0.0, 0.08, UV.y) * (1.0 - smoothstep(0.7, 1.0, UV.y));
	float Seen = saturate(RainSeen + SnowSeen) * Ends;
	float SnowShare = SnowSeen / max(RainSeen + SnowSeen, 0.0001);
	OutColour = lerp(float3(0.42, 0.45, 0.50), float3(0.86, 0.88, 0.92), SnowShare) * Brightness;
	return Seen;
)");

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

        UMaterialExpressionScalarParameter* Parameter(UMaterial* Material, const TCHAR* Name, float Default)
        {
            UMaterialExpressionScalarParameter* Node = Expression<UMaterialExpressionScalarParameter>(Material);
            Node->ParameterName = FName(Name);
            Node->DefaultValue = Default;
            return Node;
        }

        UMaterial* WriteCurtainMaterial(TArray<UPackage*>& Dirty, FString& OutError)
        {
            UPackage* Package = OpenPackage(Look::CurtainMaterialPackage);
            const FString Name = FPackageName::GetShortName(Look::CurtainMaterialPackage);
            UMaterial* Material = Package ? FindObject<UMaterial>(Package, *Name) : nullptr;
            if (Material)
            {
                Material->Modify();
                Material->GetExpressionCollection().Empty();
                UMaterialEditorOnlyData* Ed = Material->GetEditorOnlyData();
                Ed->EmissiveColor = FColorMaterialInput();
                Ed->Opacity = FScalarMaterialInput();
            }
            else if (Package)
            {
                Material = NewObject<UMaterial>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
            }
            if (!Material)
            {
                OutError = FString::Printf(TEXT("could not create %s"), Look::CurtainMaterialPackage);
                return nullptr;
            }

            // Unlit, and bright in screen terms rather than scene terms: the colour goes through EyeAdaptationInverse, so
            // a streak shows at the same brightness on screen whatever the exposure. The original tried lit translucency
            // first and drew snow as black specks: under a closed sky the translucency volume holds almost no light,
            // though the eye has opened up to it.
            Material->MaterialDomain = MD_Surface;
            Material->BlendMode = BLEND_Translucent;
            Material->SetShadingModel(MSM_Unlit);
            Material->TwoSided = true;
            // Thin moving lines are what temporal upsampling smears into a haze. Responsive AA keeps them as lines.
            Material->bEnableResponsiveAA = true;

            UMaterialExpressionTextureCoordinate* Coordinates = Expression<UMaterialExpressionTextureCoordinate>(Material);
            Coordinates->CoordinateIndex = 0;
            UMaterialExpressionTime* Time = Expression<UMaterialExpressionTime>(Material);
            UMaterialExpression* Feeds[] = {
                Coordinates,
                Time,
                Parameter(Material, Look::RainParameter, 0.0f),
                Parameter(Material, Look::SnowParameter, 0.0f),
                Parameter(Material, Look::LayerParameter, 0.0f),
                Parameter(Material, Look::BrightnessParameter, 1.0f),
            };
            const TCHAR* const Inputs[] = { TEXT("UV"), TEXT("Time"), TEXT("Rain"), TEXT("Snow"), TEXT("Layer"), TEXT("Brightness") };

            UMaterialExpressionCustom* Node = Expression<UMaterialExpressionCustom>(Material);
            Node->Description = TEXT("WeatherCurtain");
            Node->Code = Code;
            Node->OutputType = CMOT_Float1;
            Node->Inputs.Empty();
            for (int32 Index = 0; Index < UE_ARRAY_COUNT(Inputs); ++Index)
            {
                FCustomInput& Input = Node->Inputs.AddDefaulted_GetRef();
                Input.InputName = FName(Inputs[Index]);
                Input.Input.Connect(0, Feeds[Index]);
            }
            FCustomOutput& Colour = Node->AdditionalOutputs.AddDefaulted_GetRef();
            Colour.OutputName = FName(TEXT("OutColour"));
            Colour.OutputType = CMOT_Float3;
            // Without this the extra output is asked for but not offered.
            Node->RebuildOutputs();

            // Its first input is the light value; the alpha is left at its default of full conversion. Output 1 of the
            // custom node is its first additional output.
            UMaterialExpressionEyeAdaptationInverse* OnScreen = Expression<UMaterialExpressionEyeAdaptationInverse>(Material);
            OnScreen->LightValueInput.Connect(1, Node);

            UMaterialEditorOnlyData* Ed = Material->GetEditorOnlyData();
            Ed->Opacity.Connect(0, Node);
            Ed->EmissiveColor.Connect(0, OnScreen);
            Material->PreEditChange(nullptr);
            Material->PostEditChange();
            Material->MarkPackageDirty();
            Dirty.AddUnique(Package);
            return Material;
        }

        UStaticMesh* WriteCurtainMesh(UMaterial* Material, TArray<UPackage*>& Dirty, FString& OutError)
        {
            UPackage* Package = OpenPackage(Look::CurtainMeshPackage);
            const FString Name = FPackageName::GetShortName(Look::CurtainMeshPackage);
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
                OutError = FString::Printf(TEXT("could not create %s"), Look::CurtainMeshPackage);
                return nullptr;
            }

            // An open cylinder about the camera, Z up, its foot below the eye. Faces look inwards, at the camera; the
            // material is two-sided anyway, so this only decides which way the normals point.
            const double Radius = Look::CurtainRadiusMetres * 100.0;
            const double Bottom = -Look::CurtainBelowMetres * 100.0;
            const double Top = Bottom + Look::CurtainHeightMetres * 100.0;

            FMeshDescription Description;
            FStaticMeshAttributes(Description).Register();
            FMeshDescriptionBuilder Builder;
            Builder.SetMeshDescription(&Description);
            Builder.EnablePolyGroups();
            Builder.SetNumUVLayers(1);
            const FPolygonGroupID Group = Builder.AppendPolygonGroup(TEXT("Curtain"));
            for (int32 Index = 0; Index < Segments; ++Index)
            {
                const double A0 = UE_TWO_PI * Index / Segments;
                const double A1 = UE_TWO_PI * (Index + 1) / Segments;
                const double Middle = 0.5 * (A0 + A1);
                const FVector3f Inwards(-FMath::Cos(Middle), -FMath::Sin(Middle), 0.0f);
                const float U0 = static_cast<float>(Index) / Segments;
                const float U1 = static_cast<float>(Index + 1) / Segments;
                const FVector Corners[4] = {
                    FVector(Radius * FMath::Cos(A0), Radius * FMath::Sin(A0), Bottom),
                    FVector(Radius * FMath::Cos(A1), Radius * FMath::Sin(A1), Bottom),
                    FVector(Radius * FMath::Cos(A1), Radius * FMath::Sin(A1), Top),
                    FVector(Radius * FMath::Cos(A0), Radius * FMath::Sin(A0), Top),
                };
                const FVector2D UVs[4] = { FVector2D(U0, 0.0), FVector2D(U1, 0.0), FVector2D(U1, 1.0), FVector2D(U0, 1.0) };
                FVertexInstanceID Instances[4];
                for (int32 Corner = 0; Corner < 4; ++Corner)
                {
                    Instances[Corner] = Builder.AppendInstance(Builder.AppendVertex(Corners[Corner]));
                    Builder.SetInstanceUV(Instances[Corner], UVs[Corner], 0);
                    Builder.SetInstanceNormal(Instances[Corner], FVector(Inwards));
                }
                // Wound to face the axis.
                Builder.AppendTriangle(Instances[0], Instances[2], Instances[1], Group);
                Builder.AppendTriangle(Instances[0], Instances[3], Instances[2], Group);
            }

            Mesh->SetNumSourceModels(1);
            FStaticMeshSourceModel& Source = Mesh->GetSourceModel(0);
            Source.BuildSettings.bRecomputeNormals = false;
            Source.BuildSettings.bRecomputeTangents = true;
            Source.BuildSettings.bRemoveDegenerates = true;
            Source.BuildSettings.bUseFullPrecisionUVs = true;
            Source.BuildSettings.bGenerateLightmapUVs = false;
            Mesh->CreateMeshDescription(0, MoveTemp(Description));
            Mesh->CommitMeshDescription(0);
            Mesh->SetStaticMaterials({ FStaticMaterial(Material, TEXT("Curtain"), TEXT("Curtain")) });
            Mesh->GetNaniteSettings().bEnabled = false;
            Mesh->Build(true);
            Mesh->PostEditChange();
            FAssetCompilingManager::Get().FinishAllCompilation();
            Mesh->MarkPackageDirty();
            Dirty.AddUnique(Package);
            return Mesh;
        }

        UMaterialInstanceConstant* WriteCloudMaterial(TArray<UPackage*>& Dirty, FString& OutError)
        {
            UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, Look::CloudParent);
            if (!Parent)
            {
                OutError = FString::Printf(TEXT("the engine has no %s"), Look::CloudParent);
                return nullptr;
            }
            for (const TCHAR* Name : { Look::CloudCoverageParameter, Look::CloudDensityParameter, Look::CloudStormParameter })
            {
                float Value = 0.0f;
                if (!Parent->GetScalarParameterValue(FHashedMaterialParameterInfo(FName(Name)), Value))
                {
                    OutError = FString::Printf(TEXT("%s has no scalar %s any more; the client's clouds would not follow the weather"), Look::CloudParent, Name);
                    return nullptr;
                }
            }
            UPackage* Package = OpenPackage(Look::CloudMaterialPackage);
            const FString Name = FPackageName::GetShortName(Look::CloudMaterialPackage);
            UMaterialInstanceConstant* Instance = Package ? FindObject<UMaterialInstanceConstant>(Package, *Name) : nullptr;
            if (!Instance && Package)
            {
                Instance = NewObject<UMaterialInstanceConstant>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
            }
            if (!Instance)
            {
                OutError = FString::Printf(TEXT("could not create %s"), Look::CloudMaterialPackage);
                return nullptr;
            }
            Instance->Modify();
            Instance->SetParentEditorOnly(Parent);
            Instance->PostEditChange();
            Instance->MarkPackageDirty();
            Dirty.AddUnique(Package);
            return Instance;
        }
    }

    UMaterialParameterCollection* EnsureCollection(TArray<UPackage*>& Dirty)
    {
        UPackage* Package = OpenPackage(Look::CollectionPackage);
        const FString Name = FPackageName::GetShortName(Look::CollectionPackage);
        UMaterialParameterCollection* Collection = Package ? FindObject<UMaterialParameterCollection>(Package, *Name) : nullptr;
        if (!Collection && Package)
        {
            Collection = NewObject<UMaterialParameterCollection>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
        }
        if (!Collection)
        {
            UE_LOG(LogMarburgumWeatherLookContent, Error, TEXT("MB2WEATHERCONTENT could not create %s"), Look::CollectionPackage);
            return nullptr;
        }
        struct FWanted
        {
            const TCHAR* Name;
            FGuid Id;
        };
        bool bChanged = false;
        for (const FWanted& Wanted : { FWanted{ Look::WetnessParameter, WetnessId }, FWanted{ Look::SnowCoverParameter, SnowCoverId } })
        {
            FCollectionScalarParameter* Existing = Collection->ScalarParameters.FindByPredicate(
                [&Wanted](const FCollectionScalarParameter& Parameter) { return Parameter.ParameterName == FName(Wanted.Name); });
            if (Existing && Existing->Id == Wanted.Id && Existing->DefaultValue == 0.0f)
            {
                continue;
            }
            if (!bChanged)
            {
                Collection->PreEditChange(nullptr);
                bChanged = true;
            }
            if (!Existing)
            {
                Existing = &Collection->ScalarParameters.AddDefaulted_GetRef();
                Existing->ParameterName = FName(Wanted.Name);
            }
            // A dry, snowless street until a weather says otherwise, so the editor and a world with no weather look as built.
            Existing->Id = Wanted.Id;
            Existing->DefaultValue = 0.0f;
        }
        if (bChanged)
        {
            Collection->PostEditChange();
            Collection->MarkPackageDirty();
            Dirty.AddUnique(Package);
        }
        return Collection;
    }

    const TCHAR* CurtainCode()
    {
        return Code;
    }

    bool Write(FString& OutError)
    {
        TArray<UPackage*> Dirty;
        if (!EnsureCollection(Dirty))
        {
            OutError = TEXT("no MPC_Weather");
            return false;
        }
        UMaterial* Material = WriteCurtainMaterial(Dirty, OutError);
        if (!Material || !WriteCurtainMesh(Material, Dirty, OutError) || !WriteCloudMaterial(Dirty, OutError))
        {
            return false;
        }
        FAssetCompilingManager::Get().FinishAllCompilation();
        if (!UEditorLoadingAndSavingUtils::SavePackages(Dirty, false))
        {
            OutError = TEXT("could not save the weather's look");
            return false;
        }
        UE_LOG(LogMarburgumWeatherLookContent, Display, TEXT("MB2WEATHERCONTENT look saved=%d curtain=%s clouds=%s collection=%s"), Dirty.Num(),
            Look::CurtainMesh, Look::CloudMaterial, Look::Collection);
        return true;
    }
}
