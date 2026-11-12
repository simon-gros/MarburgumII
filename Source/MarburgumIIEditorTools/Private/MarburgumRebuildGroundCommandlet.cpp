#include "MarburgumRebuildGroundCommandlet.h"

#include "AssetCompilingManager.h"
#include "Components/TextRenderComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/TextRenderActor.h"
#include "EngineUtils.h"
#include "Factories/TextureFactory.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/FileManager.h"
#include "Landscape.h"
#include "LandscapeDataAccess.h"
#include "LandscapeInfo.h"
#include "LandscapeProxy.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSingleLayerWaterMaterialOutput.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionUtils.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "AssetToolsModule.h"
#include "MeshDescription.h"
#include "MeshDescriptionBuilder.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonObject.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumGround, Log, All);

namespace
{
    const TCHAR* LentMap = TEXT("/Game/World/lent");
    const TCHAR* GroundDir = TEXT("/Game/World/Ground");
    const TCHAR* ActorPrefix = TEXT("MB2Ground_");

    // The data grid (tools/lent-ground): n × n points over plan −half..half m, row 0 north. Read from
    // ground.json: Lent and its eight neighbouring windows are 3025 points over ±768 m (ADR-045).
    int32 GridPoints = 0;
    double HalfExtent = 0.0;
    // The landscape: components of 2 × 63 quads, one quad per grid step.
    constexpr int32 SubsectionQuads = 63;
    constexpr int32 Subsections = 2;

    // Plan metres (x east, y south of Stari most, z up) to Unreal centimetres.
    FVector Ue(double X, double Y, double Z)
    {
        return FVector(-Y * 100.0, X * 100.0, Z * 100.0);
    }

    struct FGrid
    {
        int32 N = 0;
        TArray<float> V;

        float At(int32 R, int32 C) const { return V[R * N + C]; }
        double Sample(double X, double Y) const
        {
            const double Step = 2.0 * HalfExtent / (N - 1);
            const double R = FMath::Clamp((Y + HalfExtent) / Step, 0.0, N - 1.0);
            const double C = FMath::Clamp((X + HalfExtent) / Step, 0.0, N - 1.0);
            const int32 R0 = FMath::Min(FMath::FloorToInt(R), N - 2);
            const int32 C0 = FMath::Min(FMath::FloorToInt(C), N - 2);
            const double FR = R - R0;
            const double FC = C - C0;
            const double A = At(R0, C0) * (1.0 - FC) + At(R0, C0 + 1) * FC;
            const double B = At(R0 + 1, C0) * (1.0 - FC) + At(R0 + 1, C0 + 1) * FC;
            return A * (1.0 - FR) + B * FR;
        }
    };

    bool ReadExtent(const FString& Path)
    {
        FString Text;
        TSharedPtr<FJsonObject> Root;
        if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
        {
            return false;
        }
        const TSharedPtr<FJsonObject>* Landscape = nullptr;
        double Points = 0.0;
        if (!Root->TryGetObjectField(TEXT("landscape"), Landscape) || !(*Landscape)->TryGetNumberField(TEXT("points"), Points)
            || !(*Landscape)->TryGetNumberField(TEXT("half_extent_m"), HalfExtent))
        {
            return false;
        }
        GridPoints = static_cast<int32>(Points);
        // Whole components of 2 × 63 quads.
        return GridPoints > 1 && (GridPoints - 1) % (Subsections * SubsectionQuads) == 0 && HalfExtent > 0.0;
    }

    bool LoadGrid(const FString& Path, FGrid& Out)
    {
        TArray<uint8> Bytes;
        if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() != GridPoints * GridPoints * 4)
        {
            return false;
        }
        Out.N = GridPoints;
        Out.V.SetNumUninitialized(GridPoints * GridPoints);
        FMemory::Memcpy(Out.V.GetData(), Bytes.GetData(), Bytes.Num());
        return true;
    }

    UPackage* OpenPackage(const FString& PackageName)
    {
        UPackage* Package = FPackageName::DoesPackageExist(PackageName) ? LoadPackage(nullptr, *PackageName, LOAD_None) : CreatePackage(*PackageName);
        if (Package)
        {
            Package->FullyLoad();
        }
        return Package;
    }

    bool Save(const TArray<UPackage*>& Packages)
    {
        return UEditorLoadingAndSavingUtils::SavePackages(Packages, false);
    }

    // ---------------------------------------------------------------------------------------------
    // Textures

    UTexture2D* ImportTexture(const FString& File, const FString& PackageName, bool bSRGB, TextureCompressionSettings Compression, TextureAddress Address, TArray<UPackage*>& Dirty)
    {
        UPackage* Package = OpenPackage(PackageName);
        const FString Name = FPackageName::GetShortName(PackageName);
        UTextureFactory* Factory = NewObject<UTextureFactory>();
        Factory->AddToRoot();
        UTextureFactory::SuppressImportOverwriteDialog();
        bool bCancelled = false;
        UObject* Object = Factory->FactoryCreateFile(UTexture2D::StaticClass(), Package, FName(*Name), RF_Public | RF_Standalone, File, nullptr, GWarn, bCancelled);
        Factory->RemoveFromRoot();
        UTexture2D* Texture = Cast<UTexture2D>(Object);
        if (!Texture)
        {
            UE_LOG(LogMarburgumGround, Error, TEXT("MB2GROUND could not import %s"), *File);
            return nullptr;
        }
        // Let the import's own build finish before its settings change under it.
        FAssetCompilingManager::Get().FinishAllCompilation();
        Texture->PreEditChange(nullptr);
        Texture->SRGB = bSRGB;
        Texture->CompressionSettings = Compression;
        Texture->AddressX = Address;
        Texture->AddressY = Address;
        Texture->LODGroup = TEXTUREGROUP_World;
        Texture->PostEditChange();
        Package->MarkPackageDirty();
        Dirty.Add(Package);
        return Texture;
    }

    struct FTextureSet
    {
        const TCHAR* Id;
        double TileMetres;
        // The roughness map's 0..1 is remapped to this range: the scans' own values are too glossy
        // for dry ground (the grass averages 0.26).
        float RoughMin = 0.6f;
        float RoughMax = 0.9f;
        UTexture2D* Color = nullptr;
        UTexture2D* Normal = nullptr;
        UTexture2D* Roughness = nullptr;
    };

    bool ImportSet(FTextureSet& Set, TArray<UPackage*>& Dirty)
    {
        const FString Source = FPaths::Combine(FPaths::ProjectDir(), TEXT("SourceArt/Textures/Ground"), Set.Id);
        const FString Dir = FString::Printf(TEXT("%s/Textures"), GroundDir);
        Set.Color = ImportTexture(FPaths::Combine(Source, FString::Printf(TEXT("%s_Color.jpg"), Set.Id)), FString::Printf(TEXT("%s/T_%s_C"), *Dir, Set.Id), true, TC_Default, TA_Wrap, Dirty);
        Set.Normal = ImportTexture(FPaths::Combine(Source, FString::Printf(TEXT("%s_NormalDX.jpg"), Set.Id)), FString::Printf(TEXT("%s/T_%s_N"), *Dir, Set.Id), false, TC_Normalmap, TA_Wrap, Dirty);
        Set.Roughness = ImportTexture(FPaths::Combine(Source, FString::Printf(TEXT("%s_Roughness.jpg"), Set.Id)), FString::Printf(TEXT("%s/T_%s_R"), *Dir, Set.Id), false, TC_Masks, TA_Wrap, Dirty);
        return Set.Color && Set.Normal && Set.Roughness;
    }

    // ---------------------------------------------------------------------------------------------
    // Material graphs

    struct FGraph
    {
        UMaterial* Material = nullptr;
        int32 Row = 0;

        template <typename T>
        T* Node()
        {
            T* Expression = NewObject<T>(Material, NAME_None, RF_Transactional);
            Expression->MaterialExpressionEditorX = -400 - 300 * (Row % 6);
            Expression->MaterialExpressionEditorY = 120 * (Row / 6);
            ++Row;
            Material->GetExpressionCollection().AddExpression(Expression);
            return Expression;
        }
        UMaterialExpression* Const(float Value)
        {
            UMaterialExpressionConstant* E = Node<UMaterialExpressionConstant>();
            E->R = Value;
            return E;
        }
        UMaterialExpression* Scalar(const TCHAR* Name, float Value)
        {
            UMaterialExpressionScalarParameter* E = Node<UMaterialExpressionScalarParameter>();
            E->ParameterName = Name;
            E->DefaultValue = Value;
            return E;
        }
        UMaterialExpression* Mul(UMaterialExpression* A, UMaterialExpression* B, int32 OutA = 0, int32 OutB = 0)
        {
            UMaterialExpressionMultiply* E = Node<UMaterialExpressionMultiply>();
            E->A.Connect(OutA, A);
            E->B.Connect(OutB, B);
            return E;
        }
        UMaterialExpression* Add(UMaterialExpression* A, UMaterialExpression* B, int32 OutA = 0, int32 OutB = 0)
        {
            UMaterialExpressionAdd* E = Node<UMaterialExpressionAdd>();
            E->A.Connect(OutA, A);
            E->B.Connect(OutB, B);
            return E;
        }
        UMaterialExpression* Sub(UMaterialExpression* A, UMaterialExpression* B, int32 OutA = 0, int32 OutB = 0)
        {
            UMaterialExpressionSubtract* E = Node<UMaterialExpressionSubtract>();
            E->A.Connect(OutA, A);
            E->B.Connect(OutB, B);
            return E;
        }
        UMaterialExpression* Lerp(UMaterialExpression* A, UMaterialExpression* B, UMaterialExpression* Alpha, int32 OutA = 0, int32 OutB = 0, int32 OutAlpha = 0)
        {
            UMaterialExpressionLinearInterpolate* E = Node<UMaterialExpressionLinearInterpolate>();
            E->A.Connect(OutA, A);
            E->B.Connect(OutB, B);
            E->Alpha.Connect(OutAlpha, Alpha);
            return E;
        }
        UMaterialExpression* Abs(UMaterialExpression* A)
        {
            UMaterialExpressionAbs* E = Node<UMaterialExpressionAbs>();
            E->Input.Connect(0, A);
            return E;
        }
        UMaterialExpression* Vector(const FLinearColor& Value)
        {
            UMaterialExpressionConstant3Vector* E = Node<UMaterialExpressionConstant3Vector>();
            E->Constant = Value;
            return E;
        }
        UMaterialExpression* Saturate(UMaterialExpression* A)
        {
            UMaterialExpressionSaturate* E = Node<UMaterialExpressionSaturate>();
            E->Input.Connect(0, A);
            return E;
        }
        UMaterialExpression* Mask(UMaterialExpression* A, bool R, bool G, bool B = false, bool Alpha = false)
        {
            UMaterialExpressionComponentMask* E = Node<UMaterialExpressionComponentMask>();
            E->Input.Connect(0, A);
            E->R = R;
            E->G = G;
            E->B = B;
            E->A = Alpha;
            return E;
        }
        UMaterialExpression* Append(UMaterialExpression* A, UMaterialExpression* B)
        {
            UMaterialExpressionAppendVector* E = Node<UMaterialExpressionAppendVector>();
            E->A.Connect(0, A);
            E->B.Connect(0, B);
            return E;
        }
        UMaterialExpression* WorldPosition()
        {
            return Node<UMaterialExpressionWorldPosition>();
        }
        /** Gradient noise over world position: 0..1, features about 1/Scale centimetres. */
        UMaterialExpression* Noise(float Scale, int32 Levels)
        {
            UMaterialExpressionNoise* E = Node<UMaterialExpressionNoise>();
            E->Position.Connect(0, WorldPosition());
            E->Scale = Scale;
            E->Levels = Levels;
            E->Quality = 1;
            E->NoiseFunction = NOISEFUNCTION_GradientTex;
            E->bTurbulence = false;
            E->OutputMin = 0.0f;
            E->OutputMax = 1.0f;
            return E;
        }
        UMaterialExpressionTextureSample* Sample(UTexture* Texture, EMaterialSamplerType Type, UMaterialExpression* UV, ESamplerSourceMode Source)
        {
            UMaterialExpressionTextureSample* E = Node<UMaterialExpressionTextureSample>();
            E->Texture = Texture;
            E->SamplerType = Type;
            E->SamplerSource = Source;
            E->Coordinates.Connect(0, UV);
            return E;
        }
        UMaterialExpressionTextureSampleParameter2D* SampleParam(const TCHAR* Name, UTexture* Texture, EMaterialSamplerType Type, UMaterialExpression* UV)
        {
            UMaterialExpressionTextureSampleParameter2D* E = Node<UMaterialExpressionTextureSampleParameter2D>();
            E->ParameterName = Name;
            E->Texture = Texture;
            E->SamplerType = Type;
            E->SamplerSource = SSM_Wrap_WorldGroupSettings;
            E->Coordinates.Connect(0, UV);
            return E;
        }
    };

    UMaterial* BeginMaterial(const FString& PackageName, TArray<UPackage*>& Dirty)
    {
        UPackage* Package = OpenPackage(PackageName);
        const FString Name = FPackageName::GetShortName(PackageName);
        UMaterial* Material = FindObject<UMaterial>(Package, *Name);
        if (Material)
        {
            Material->Modify();
            Material->GetExpressionCollection().Empty();
            UMaterialEditorOnlyData* Ed = Material->GetEditorOnlyData();
            Ed->BaseColor = FColorMaterialInput();
            Ed->Normal = FVectorMaterialInput();
            Ed->Roughness = FScalarMaterialInput();
            Ed->Specular = FScalarMaterialInput();
        }
        else
        {
            Material = NewObject<UMaterial>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
        }
        Dirty.Add(Package);
        return Material;
    }

    void FinishMaterial(UMaterial* Material)
    {
        Material->PreEditChange(nullptr);
        Material->PostEditChange();
        Material->MarkPackageDirty();
    }

    /**
     * M_LentPaved: one texture set on the mesh's UV (metres), scaled by InvTile, with a tint, a
     * detail amount (0 for flat paint) and a large gradient-noise brightness change so the
     * repeat does not show.
     */
    UMaterial* BuildPavedMaster(const FTextureSet& Default, TArray<UPackage*>& Dirty)
    {
        FGraph G{ BeginMaterial(FString::Printf(TEXT("%s/Materials/M_LentPaved"), GroundDir), Dirty) };
        UMaterialExpressionTextureCoordinate* UV0 = G.Node<UMaterialExpressionTextureCoordinate>();
        UMaterialExpression* UV = G.Mul(UV0, G.Scalar(TEXT("InvTile"), 0.5f));
        UMaterialExpression* Color = G.SampleParam(TEXT("Color"), Default.Color, SAMPLERTYPE_Color, UV);
        UMaterialExpression* Normal = G.SampleParam(TEXT("Normal"), Default.Normal, SAMPLERTYPE_Normal, UV);
        UMaterialExpression* Rough = G.SampleParam(TEXT("Roughness"), Default.Roughness, SAMPLERTYPE_Masks, UV);
        UMaterialExpressionVectorParameter* Tint = G.Node<UMaterialExpressionVectorParameter>();
        Tint->ParameterName = TEXT("Tint");
        Tint->DefaultValue = FLinearColor::White;
        UMaterialExpression* Detail = G.Lerp(G.Const(1.0f), Color, G.Scalar(TEXT("Detail"), 1.0f));
        UMaterialExpression* Macro = G.Lerp(G.Const(0.86f), G.Const(1.08f), G.Noise(0.0035f, 2));
        UMaterialExpression* Base = G.Mul(G.Mul(Detail, G.Mask(Tint, true, true, true)), Macro);
        // Paint has almost no relief: blend the normal map towards flat.
        UMaterialExpressionVectorParameter* Flat = G.Node<UMaterialExpressionVectorParameter>();
        Flat->ParameterName = TEXT("FlatNormal");
        Flat->DefaultValue = FLinearColor(0.0f, 0.0f, 1.0f);
        UMaterialExpression* NormalOut = G.Lerp(G.Mask(Flat, true, true, true), Normal, G.Scalar(TEXT("NormalStrength"), 1.0f));
        // Output 1 of a texture sample is its red channel.
        UMaterialExpression* RoughOut = G.Saturate(G.Add(G.Mul(Rough, G.Scalar(TEXT("RoughnessScale"), 1.0f), 1), G.Scalar(TEXT("RoughnessBias"), 0.0f)));
        UMaterialEditorOnlyData* Ed = G.Material->GetEditorOnlyData();
        Ed->BaseColor.Connect(0, Base);
        Ed->Normal.Connect(0, NormalOut);
        Ed->Roughness.Connect(0, RoughOut);
        FinishMaterial(G.Material);
        return G.Material;
    }

    UMaterialInstanceConstant* PavedInstance(const TCHAR* Name, UMaterial* Parent, const FTextureSet& Set, const FLinearColor& Tint, float Detail, float RoughnessScale, float RoughnessBias, float NormalStrength, TArray<UPackage*>& Dirty)
    {
        const FString Directory = FString::Printf(TEXT("%s/Materials"), GroundDir);
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
        Instance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Color")), Set.Color);
        Instance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Normal")), Set.Normal);
        Instance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Roughness")), Set.Roughness);
        Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("InvTile")), 1.0f / Set.TileMetres);
        Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Tint")), Tint);
        Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Detail")), Detail);
        Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("RoughnessScale")), RoughnessScale);
        Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("RoughnessBias")), RoughnessBias);
        Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("NormalStrength")), NormalStrength);
        Instance->PostEditChange();
        Instance->MarkPackageDirty();
        Dirty.Add(Instance->GetOutermost());
        return Instance;
    }

    /**
     * M_LentTerrain: five world-aligned layers (hard standing, earth, gravel, grass, river
     * stones) weighted by the surface texture from tools/lent-ground, with noise on the
     * weights so edges are ragged rather than blurred, a large brightness variation, and stone
     * retaining wall projected sideways wherever the ground is steeper than about 45 degrees.
     */
    UMaterial* BuildTerrainMaterial(const TArray<FTextureSet*>& Layers, const FTextureSet& Wall, UTexture2D* Surface, TArray<UPackage*>& Dirty)
    {
        FGraph G{ BeginMaterial(FString::Printf(TEXT("%s/Materials/M_LentTerrain"), GroundDir), Dirty) };
        UMaterialExpression* World = G.WorldPosition();
        UMaterialExpression* XY = G.Mask(World, true, true);
        // Surface weights: texture u runs east (world Y), v runs south (−world X), over ±256 m.
        const float Inv = 1.0f / (2.0f * HalfExtent * 100.0f);
        UMaterialExpression* U = G.Add(G.Mul(G.Mask(World, false, true), G.Const(Inv)), G.Const(0.5f));
        UMaterialExpression* V = G.Sub(G.Const(0.5f), G.Mul(G.Mask(World, true, false), G.Const(Inv)));
        UMaterialExpression* Weights = G.Sample(Surface, SAMPLERTYPE_LinearColor, G.Append(U, V), SSM_Clamp_WorldGroupSettings);
        UMaterialExpression* Ragged = G.Noise(0.012f, 2);

        UMaterialExpression* Color = nullptr;
        UMaterialExpression* Normal = nullptr;
        UMaterialExpression* Rough = nullptr;
        // Layers[0] is the base; Layers[1..4] take the weight channels in the order the blend
        // applies them: earth (G), gravel (B), grass (R), river stones (A).
        const int32 Channel[5] = { -1, 2, 3, 1, 4 };
        for (int32 Index = 0; Index < Layers.Num(); ++Index)
        {
            const FTextureSet& Set = *Layers[Index];
            UMaterialExpression* UV = G.Mul(XY, G.Const(1.0f / (Set.TileMetres * 100.0f)));
            UMaterialExpression* C = G.Sample(Set.Color, SAMPLERTYPE_Color, UV, SSM_Wrap_WorldGroupSettings);
            UMaterialExpression* N = G.Sample(Set.Normal, SAMPLERTYPE_Normal, UV, SSM_Wrap_WorldGroupSettings);
            UMaterialExpression* R = G.Add(G.Mul(G.Sample(Set.Roughness, SAMPLERTYPE_Masks, UV, SSM_Wrap_WorldGroupSettings), G.Const(Set.RoughMax - Set.RoughMin), 1), G.Const(Set.RoughMin));
            if (Index == 3)
            {
                // Grass repeats most visibly: blend in the same texture at a quarter of the frequency.
                UMaterialExpression* Far = G.Sample(Set.Color, SAMPLERTYPE_Color, G.Mul(UV, G.Const(0.23f)), SSM_Wrap_WorldGroupSettings);
                C = G.Lerp(C, Far, G.Const(0.45f));
            }
            if (!Color)
            {
                // The base layer covers large unmapped yards: keep it a little darker than the
                // paved car parks so the two read apart.
                Color = G.Mul(C, G.Const(0.8f));
                Normal = N;
                Rough = R;
                continue;
            }
            // w' = saturate((w − 0.5 + (noise − 0.5) · 0.6) · 4 + 0.5); outputs 1–4 of a texture
            // sample are its R, G, B and A channels.
            UMaterialExpression* Jitter = G.Mul(G.Sub(Ragged, G.Const(0.5f)), G.Const(0.6f));
            UMaterialExpression* Alpha = G.Saturate(G.Add(G.Mul(G.Add(G.Sub(Weights, G.Const(0.5f), Channel[Index]), Jitter), G.Const(4.0f)), G.Const(0.5f)));
            Color = G.Lerp(Color, C, Alpha);
            Normal = G.Lerp(Normal, N, Alpha);
            Rough = G.Lerp(Rough, R, Alpha);
        }
        UMaterialExpression* Macro = G.Lerp(G.Const(0.84f), G.Const(1.1f), G.Noise(0.0012f, 3));
        Color = G.Mul(Color, Macro);

        // Steep ground: slope from the vertex normal, full wall by about 57 degrees. The wall
        // texture runs along whichever horizontal axis the face looks across, with V down.
        UMaterialExpression* NormalWS = G.Node<UMaterialExpressionVertexNormalWS>();
        UMaterialExpression* Steep = G.Saturate(G.Mul(G.Sub(G.Sub(G.Const(1.0f), G.Mask(NormalWS, false, false, true)), G.Const(0.25f)), G.Const(5.0f)));
        UMaterialExpression* Down = G.Mul(G.Mask(World, false, false, true), G.Const(-1.0f));
        UMaterialExpression* WallScale = G.Const(1.0f / (Wall.TileMetres * 100.0f));
        UMaterialExpression* AcrossX = G.Mul(G.Append(G.Mask(World, false, true), Down), WallScale);
        UMaterialExpression* AcrossY = G.Mul(G.Append(G.Mask(World, true, false), Down), WallScale);
        UMaterialExpression* FacingX = G.Saturate(G.Add(G.Mul(G.Sub(G.Abs(G.Mask(NormalWS, true, false)), G.Abs(G.Mask(NormalWS, false, true))), G.Const(4.0f)), G.Const(0.5f)));
        UMaterialExpression* WallColor = G.Lerp(G.Sample(Wall.Color, SAMPLERTYPE_Color, AcrossY, SSM_Wrap_WorldGroupSettings), G.Sample(Wall.Color, SAMPLERTYPE_Color, AcrossX, SSM_Wrap_WorldGroupSettings), FacingX);
        UMaterialExpression* WallRough = G.Add(G.Mul(G.Lerp(G.Sample(Wall.Roughness, SAMPLERTYPE_Masks, AcrossY, SSM_Wrap_WorldGroupSettings), G.Sample(Wall.Roughness, SAMPLERTYPE_Masks, AcrossX, SSM_Wrap_WorldGroupSettings), FacingX, 1, 1), G.Const(Wall.RoughMax - Wall.RoughMin)), G.Const(Wall.RoughMin));
        // The landscape's tangent frame is horizontal, so the wall's normal map is only a hint.
        UMaterialExpression* WallNormal = G.Lerp(G.Vector(FLinearColor(0.0f, 0.0f, 1.0f)), G.Sample(Wall.Normal, SAMPLERTYPE_Normal, AcrossX, SSM_Wrap_WorldGroupSettings), G.Const(0.35f));
        Color = G.Lerp(Color, WallColor, Steep);
        Normal = G.Lerp(Normal, WallNormal, Steep);
        Rough = G.Lerp(Rough, WallRough, Steep);

        UMaterialEditorOnlyData* Ed = G.Material->GetEditorOnlyData();
        Ed->BaseColor.Connect(0, Color);
        Ed->Normal.Connect(0, Normal);
        Ed->Roughness.Connect(0, G.Saturate(Rough));
        FinishMaterial(G.Material);
        return G.Material;
    }

    /**
     * M_DravaWater: single-layer water for the river sheets. Two scales of the engine's tiling
     * water normal drift downstream (plan east) at different speeds; the Drava's green comes from
     * absorbing red and scattering a little green, not from a painted colour, so depth and the
     * bed show through near the banks.
     */
    UMaterial* BuildWaterMaterial(TArray<UPackage*>& Dirty)
    {
        UTexture* Ripples = LoadObject<UTexture>(nullptr, TEXT("/Engine/Functions/Engine_MaterialFunctions02/ExampleContent/Textures/water_n.water_n"));
        if (!Ripples)
        {
            UE_LOG(LogMarburgumGround, Error, TEXT("MB2GROUND engine water normal texture missing"));
            return nullptr;
        }
        FGraph G{ BeginMaterial(FString::Printf(TEXT("%s/Materials/M_DravaWater"), GroundDir), Dirty) };
        G.Material->SetShadingModel(MSM_SingleLayerWater);
        G.Material->BlendMode = BLEND_Opaque;
        UMaterialExpression* XY = G.Mask(G.WorldPosition(), true, true);
        UMaterialExpression* Time = G.Node<UMaterialExpressionTime>();
        const EMaterialSamplerType Type = MaterialExpressionUtils::GetSamplerTypeForTexture(Ripples);
        auto Layer = [&](float TileMetres, float Downstream, float Across) {
            UMaterialExpression* Drift = G.Mask(G.Mul(Time, G.Vector(FLinearColor(Across, Downstream, 0.0f))), true, true);
            UMaterialExpression* N = G.Sample(Ripples, Type, G.Add(G.Mul(XY, G.Const(1.0f / (TileMetres * 100.0f))), Drift), SSM_Wrap_WorldGroupSettings);
            // A texture not compressed as a normal map comes back 0..1.
            return Type == SAMPLERTYPE_Normal ? N : G.Sub(G.Mul(N, G.Const(2.0f)), G.Const(1.0f));
        };
        UMaterialExpression* Ripple = G.Mul(G.Add(Layer(12.0f, 0.020f, 0.004f), Layer(4.5f, 0.034f, -0.006f)), G.Const(0.5f));
        UMaterialExpression* Normal = G.Lerp(G.Vector(FLinearColor(0.0f, 0.0f, 1.0f)), G.Mask(Ripple, true, true, true), G.Const(0.4f));
        UMaterialExpressionSingleLayerWaterMaterialOutput* Out = G.Node<UMaterialExpressionSingleLayerWaterMaterialOutput>();
        // Per centimetre (per metre: scattering 0.01 0.03 0.022, absorption 0.65 0.2 0.28): the
        // Drava's dark jade green, with the bed showing through only near the banks.
        Out->ScatteringCoefficients.Connect(0, G.Vector(FLinearColor(0.0001f, 0.0003f, 0.00022f)));
        Out->AbsorptionCoefficients.Connect(0, G.Vector(FLinearColor(0.0065f, 0.002f, 0.0028f)));
        Out->PhaseG.Connect(0, G.Const(0.2f));
        Out->ColorScaleBehindWater.Connect(0, G.Const(1.0f));
        UMaterialEditorOnlyData* Ed = G.Material->GetEditorOnlyData();
        // On single-layer water, base colour and opacity describe a layer on top of the water (foam):
        // none here. Left unconnected, opacity is 1 and the river is an opaque black sheet.
        Ed->BaseColor.Connect(0, G.Vector(FLinearColor(0.0f, 0.0f, 0.0f)));
        Ed->Opacity.Connect(0, G.Const(0.0f));
        Ed->Normal.Connect(0, Normal);
        Ed->Roughness.Connect(0, G.Const(0.05f));
        Ed->Specular.Connect(0, G.Const(0.5f));
        FinishMaterial(G.Material);
        return G.Material;
    }

    // ---------------------------------------------------------------------------------------------
    // Paved meshes

    struct FChunk
    {
        int32 Col = 0;
        int32 Row = 0;
        TArray<FVector3f> Positions;
        TArray<FVector2f> UVs;
        TArray<FIntVector3> Triangles;
        TArray<uint8> Materials;
    };

    constexpr int32 SurfaceCount = 6;
    const TCHAR* SurfaceNames[SurfaceCount] = { TEXT("asphalt"), TEXT("yard_asphalt"), TEXT("pavers"), TEXT("setts"), TEXT("kerb"), TEXT("paint") };

    bool ReadPaved(const FString& Path, TArray<FChunk>& Out)
    {
        TArray<uint8> Bytes;
        if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() < 12 || FMemory::Memcmp(Bytes.GetData(), "MBGM", 4) != 0)
        {
            return false;
        }
        int64 At = 4;
        auto U32 = [&]() { uint32 V; FMemory::Memcpy(&V, Bytes.GetData() + At, 4); At += 4; return V; };
        auto I32 = [&]() { int32 V; FMemory::Memcpy(&V, Bytes.GetData() + At, 4); At += 4; return V; };
        auto F32 = [&]() { float V; FMemory::Memcpy(&V, Bytes.GetData() + At, 4); At += 4; return V; };
        if (U32() != 1)
        {
            return false;
        }
        const uint32 Count = U32();
        for (uint32 Index = 0; Index < Count; ++Index)
        {
            FChunk Chunk;
            Chunk.Col = I32();
            Chunk.Row = I32();
            const uint32 Vertices = U32();
            for (uint32 V = 0; V < Vertices; ++V)
            {
                const float X = F32(), Y = F32(), Z = F32();
                Chunk.Positions.Add(FVector3f(Ue(X, Y, Z)));
            }
            for (uint32 V = 0; V < Vertices; ++V)
            {
                const float A = F32(), B = F32();
                Chunk.UVs.Add(FVector2f(A, B));
            }
            const uint32 Tris = U32();
            for (uint32 T = 0; T < Tris; ++T)
            {
                const int32 A = U32(), B = U32(), C = U32();
                Chunk.Triangles.Add(FIntVector3(A, B, C));
            }
            Chunk.Materials.Append(Bytes.GetData() + At, Tris);
            At += Tris;
            if (At > Bytes.Num())
            {
                return false;
            }
            Out.Add(MoveTemp(Chunk));
        }
        return At == Bytes.Num();
    }

    // The distant ring's material slots (tools/lent-ground ring.rs, RING_SLOTS).
    constexpr int32 RingSlotCount = 8;
    const TCHAR* RingSlotNames[RingSlotCount] = { TEXT("ground"), TEXT("trees"), TEXT("roof_tile"), TEXT("roof_flat"), TEXT("wall_light"), TEXT("wall_warm"), TEXT("wall_grey"), TEXT("water") };

    /** One chunk of paved.bin (Kind "Paved", collision from its own triangles) or ring.bin (Kind
     *  "Ring", no collision: nothing out there can be reached). */
    UStaticMesh* BuildChunkMesh(const FChunk& Chunk, const TCHAR* Kind, TArrayView<const TCHAR* const> SlotNames, const TArray<UMaterialInterface*>& Materials, bool bCollision, TArray<UPackage*>& Dirty)
    {
        const int32 SlotCount = SlotNames.Num();
        const FString Name = FString::Printf(TEXT("SM_Lent%s_%d_%d"), Kind, Chunk.Col, Chunk.Row);
        const FString PackageName = FString::Printf(TEXT("%s/%s/%s"), GroundDir, Kind, *Name);
        UPackage* Package = OpenPackage(PackageName);
        UStaticMesh* Mesh = FindObject<UStaticMesh>(Package, *Name);
        if (Mesh)
        {
            FAssetCompilingManager::Get().FinishAllCompilation();
            Mesh->Modify();
        }
        else
        {
            Mesh = NewObject<UStaticMesh>(Package, *Name, RF_Public | RF_Standalone);
        }
        FMeshDescription Description;
        FStaticMeshAttributes(Description).Register();
        FMeshDescriptionBuilder Builder;
        Builder.SetMeshDescription(&Description);
        Builder.EnablePolyGroups();
        Builder.SetNumUVLayers(1);
        TArray<FPolygonGroupID> Groups;
        for (int32 Index = 0; Index < SlotCount; ++Index)
        {
            Groups.Add(Builder.AppendPolygonGroup(SlotNames[Index]));
        }
        // One instance per vertex: shared vertices give smooth normals across the surface; the
        // tool writes separate vertices where kerb faces meet it, which keeps those edges hard.
        TArray<FVertexInstanceID> Instances;
        for (int32 Index = 0; Index < Chunk.Positions.Num(); ++Index)
        {
            const FVertexID Vertex = Builder.AppendVertex(FVector(Chunk.Positions[Index]));
            const FVertexInstanceID Instance = Builder.AppendInstance(Vertex);
            Builder.SetInstanceUV(Instance, FVector2D(Chunk.UVs[Index]), 0);
            Instances.Add(Instance);
        }
        for (int32 Index = 0; Index < Chunk.Triangles.Num(); ++Index)
        {
            const FIntVector3& T = Chunk.Triangles[Index];
            Builder.AppendTriangle(Instances[T.X], Instances[T.Y], Instances[T.Z], Groups[FMath::Min<int32>(Chunk.Materials[Index], SlotCount - 1)]);
        }
        Mesh->SetNumSourceModels(1);
        FStaticMeshSourceModel& Source = Mesh->GetSourceModel(0);
        Source.BuildSettings.bRecomputeNormals = true;
        Source.BuildSettings.bRecomputeTangents = true;
        Source.BuildSettings.bRemoveDegenerates = true;
        Source.BuildSettings.bUseFullPrecisionUVs = true;
        Source.BuildSettings.bGenerateLightmapUVs = false;
        Mesh->CreateMeshDescription(0, MoveTemp(Description));
        Mesh->CommitMeshDescription(0);
        TArray<FStaticMaterial> Slots;
        for (int32 Index = 0; Index < SlotCount; ++Index)
        {
            Slots.Add(FStaticMaterial(Materials[Index], SlotNames[Index], SlotNames[Index]));
        }
        Mesh->SetStaticMaterials(Slots);
        Mesh->Build(true);
        Mesh->PostEditChange();
        if (bCollision)
        {
            Mesh->CreateBodySetup();
            // Collision is the visible surface itself: you stand on the road, the kerb and the pavement.
            Mesh->GetBodySetup()->CollisionTraceFlag = CTF_UseComplexAsSimple;
            Mesh->GetBodySetup()->bDoubleSidedGeometry = false;
        }
        FAssetCompilingManager::Get().FinishAllCompilation();
        Package->MarkPackageDirty();
        Dirty.Add(Package);
        return Mesh;
    }

    /** Height of the first blocking surface under an actor, ignoring the actor itself. The trace
     *  starts 1.5 m above the actor, or just above `FloorCm` if that is higher. */
    TOptional<double> SurfaceBelow(UWorld* World, const AActor* Actor, double FloorCm = -1e9)
    {
        const FVector At = Actor->GetActorLocation();
        FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumGroundSeat), true, Actor);
        FHitResult Hit;
        const double StartZ = FMath::Max(At.Z + 150.0, FloorCm + 60.0);
        if (World->LineTraceSingleByChannel(Hit, FVector(At.X, At.Y, StartZ), At - FVector(0.0, 0.0, 3000.0), ECC_WorldStatic, Params))
        {
            return Hit.ImpactPoint.Z;
        }
        return {};
    }

    bool IsReseated(const AActor* Actor)
    {
        const FString Label = Actor->GetActorLabel();
        return Actor->IsA<APlayerStart>() || Actor->IsA<ATextRenderActor>() || Label.StartsWith(TEXT("MB2Npc_"));
    }
}

namespace
{
    /**
     * Walks a character-sized capsule down onto sample points of every paved surface in the saved
     * level and checks what it lands on: the paving itself, never the landscape, and with its
     * bottom on the surface, not sunk into it. This is the collision the character's movement
     * sweeps against.
     */
    int32 VerifyWalkable(UWorld* World, int32 Every)
    {
        constexpr float Radius = 34.0f;
        constexpr float HalfHeight = 88.0f;
        int32 Samples = 0, RayOnPaving = 0, RayOnLandscape = 0, RayOther = 0, RayOff = 0, Sunk = 0;
        double WorstSink = 0.0, WorstRay = 0.0;
        FString WorstSinkAt, WorstRayAt;
        const FCollisionShape Capsule = FCollisionShape::MakeCapsule(Radius, HalfHeight);
        for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
        {
            if (!It->GetActorLabel().StartsWith(ActorPrefix) || It->GetActorLabel().Contains(TEXT("LentRing")))
            {
                continue; // the distant ring is not walked on
            }
            const UStaticMesh* Mesh = It->GetStaticMeshComponent()->GetStaticMesh();
            const FMeshDescription* Description = Mesh ? Mesh->GetMeshDescription(0) : nullptr;
            if (!Description)
            {
                continue;
            }
            FStaticMeshConstAttributes Attributes(*Description);
            const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
            const TPolygonGroupAttributesConstRef<FName> Slots = Attributes.GetPolygonGroupMaterialSlotNames();
            int32 Index = 0;
            for (const FTriangleID Triangle : Description->Triangles().GetElementIDs())
            {
                if (Index++ % Every != 0)
                {
                    continue;
                }
                const FName Slot = Slots[Description->GetTrianglePolygonGroup(Triangle)];
                if (Slot == TEXT("paint"))
                {
                    continue; // 1 cm proud of the road and without collision of its own interest
                }
                const TArrayView<const FVertexID> V = Description->GetTriangleVertices(Triangle);
                const FVector A(Positions[V[0]]), B(Positions[V[1]]), C(Positions[V[2]]);
                const FVector Normal = FVector::CrossProduct(C - A, B - A).GetSafeNormal();
                if (Normal.Z < 0.9)
                {
                    continue; // kerb faces and edging
                }
                const FVector Centre = (A + B + C) / 3.0;
                ++Samples;
                FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumGroundVerify), true);

                // What you see from above is what you stand on: the first thing a ray meets
                // coming down must be this paving, at this height.
                FHitResult Ray;
                if (World->LineTraceSingleByChannel(Ray, Centre + FVector(0, 0, 250.0), Centre - FVector(0, 0, 250.0), ECC_Visibility, Params))
                {
                    const AActor* Under = Ray.GetActor();
                    if (Under && Under->IsA<ALandscapeProxy>() && Ray.ImpactPoint.Z - Centre.Z <= 1.0)
                    {
                        ++RayOther; // the landscape meeting a paving edge within a centimetre
                    }
                    else if (Under && Under->IsA<ALandscapeProxy>())
                    {
                        ++RayOnLandscape;
                        if (RayOnLandscape <= 12)
                        {
                            UE_LOG(LogMarburgumGround, Display, TEXT("MB2GROUND verify landscape over paving by %.1f cm at %s (%s)"), Ray.ImpactPoint.Z - Centre.Z, *Centre.ToString(), *Slot.ToString());
                        }
                    }
                    else if (Under && Under->GetActorLabel().StartsWith(ActorPrefix))
                    {
                        ++RayOnPaving;
                        // Road paint lies 1.2 cm over the asphalt, and up to 3 cm on a curving road.
                        const double Dz = Ray.ImpactPoint.Z - Centre.Z;
                        if (Dz > 3.0 || Dz < -1.0)
                        {
                            ++RayOff;
                            if (RayOff <= 12)
                            {
                                UE_LOG(LogMarburgumGround, Display, TEXT("MB2GROUND verify off %.1f cm at %s (%s)"), Dz, *Centre.ToString(), *Slot.ToString());
                            }
                            if (FMath::Abs(Dz) > FMath::Abs(WorstRay))
                            {
                                WorstRay = Dz;
                                WorstRayAt = Centre.ToString();
                            }
                        }
                    }
                    else
                    {
                        ++RayOther; // under a bridge deck or an eave
                    }
                }

                // A character-sized capsule let down onto the spot never ends up inside the paving.
                FHitResult Hit;
                if (World->SweepSingleByChannel(Hit, Centre + FVector(0, 0, HalfHeight + 120.0), Centre - FVector(0, 0, 300.0), FQuat::Identity, ECC_Pawn, Capsule, Params))
                {
                    const double Offset = Hit.Location.Z - HalfHeight - Centre.Z;
                    if (Offset < -1.0)
                    {
                        ++Sunk;
                        if (Offset < WorstSink)
                        {
                            WorstSink = Offset;
                            WorstSinkAt = Centre.ToString();
                        }
                    }
                }
            }
        }
        UE_LOG(LogMarburgumGround, Display, TEXT("MB2GROUND verify samples=%d ray_on_paving=%d ray_on_landscape=%d ray_other=%d ray_off_over_1cm=%d worst_ray_cm=%.1f %s capsule_sunk_over_1cm=%d worst_sink_cm=%.1f %s"),
            Samples, RayOnPaving, RayOnLandscape, RayOther, RayOff, WorstRay, *WorstRayAt, Sunk, WorstSink, *WorstSinkAt);
        return (Samples > 0 && RayOnLandscape == 0 && RayOff == 0 && Sunk == 0) ? 0 : 3;
    }
}

UMarburgumRebuildGroundCommandlet::UMarburgumRebuildGroundCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
}

int32 UMarburgumRebuildGroundCommandlet::Main(const FString& Params)
{
    const FString Data = FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/World/lent_ground"));
    if (!ReadExtent(FPaths::Combine(Data, TEXT("ground.json"))))
    {
        UE_LOG(LogMarburgumGround, Error, TEXT("MB2GROUND cannot read the landscape extent from %s/ground.json (run tools/lent-ground)"), *Data);
        return 1;
    }
    if (FParse::Param(*Params, TEXT("Verify")))
    {
        UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(LentMap);
        if (!World)
        {
            return 1;
        }
        World->UpdateWorldComponents(true, false);
        FAssetCompilingManager::Get().FinishAllCompilation();
        int32 Every = 7;
        FParse::Value(*Params, TEXT("Every="), Every);
        return VerifyWalkable(World, FMath::Max(1, Every));
    }
    FGrid Heights;
    FGrid Top;
    TArray<FChunk> Chunks;
    if (!LoadGrid(FPaths::Combine(Data, TEXT("heights.bin")), Heights) || !LoadGrid(FPaths::Combine(Data, TEXT("top.bin")), Top))
    {
        UE_LOG(LogMarburgumGround, Error, TEXT("MB2GROUND missing or wrong-sized heights.bin/top.bin in %s (run tools/lent-ground)"), *Data);
        return 1;
    }
    if (!ReadPaved(FPaths::Combine(Data, TEXT("paved.bin")), Chunks))
    {
        UE_LOG(LogMarburgumGround, Error, TEXT("MB2GROUND cannot read paved.bin"));
        return 1;
    }

    // Textures and materials.
    TArray<UPackage*> Dirty;
    FTextureSet Asphalt{ TEXT("Asphalt031"), 2.4, 0.62f, 0.92f };
    FTextureSet YardAsphalt{ TEXT("Asphalt023S"), 1.25, 0.65f, 0.95f };
    FTextureSet Pavers{ TEXT("PavingStones109"), 1.6, 0.6f, 0.9f };
    FTextureSet Setts{ TEXT("PavingStones070"), 1.15, 0.55f, 0.9f };
    FTextureSet Kerb{ TEXT("Concrete030"), 1.0, 0.65f, 0.9f };
    FTextureSet Wall{ TEXT("Bricks068"), 1.5, 0.6f, 0.9f };
    FTextureSet Grass{ TEXT("Grass004"), 1.6, 0.72f, 0.96f };
    FTextureSet Earth{ TEXT("Ground037"), 2.1, 0.7f, 0.95f };
    FTextureSet Gravel{ TEXT("Gravel040"), 1.5, 0.7f, 0.95f };
    FTextureSet Stones{ TEXT("Rocks022"), 0.8, 0.5f, 0.85f };
    for (FTextureSet* Set : { &Asphalt, &YardAsphalt, &Pavers, &Setts, &Kerb, &Wall, &Grass, &Earth, &Gravel, &Stones })
    {
        if (!ImportSet(*Set, Dirty))
        {
            return 1;
        }
    }
    UTexture2D* Surface = ImportTexture(FPaths::Combine(Data, TEXT("surface.png")), FString::Printf(TEXT("%s/Textures/T_LentSurface"), GroundDir), false, TC_VectorDisplacementmap, TA_Clamp, Dirty);
    if (!Surface)
    {
        return 1;
    }
    FAssetCompilingManager::Get().FinishAllCompilation();
    Surface->PreEditChange(nullptr);
    Surface->NeverStream = true;
    Surface->PostEditChange();
    UMaterial* PavedMaster = BuildPavedMaster(Asphalt, Dirty);
    TArray<UMaterialInterface*> PavedMaterials = {
        PavedInstance(TEXT("MI_LentAsphalt"), PavedMaster, Asphalt, FLinearColor(0.92f, 0.92f, 0.92f), 1.0f, Asphalt.RoughMax - Asphalt.RoughMin, Asphalt.RoughMin, 1.0f, Dirty),
        PavedInstance(TEXT("MI_LentYardAsphalt"), PavedMaster, YardAsphalt, FLinearColor(0.95f, 0.94f, 0.92f), 1.0f, YardAsphalt.RoughMax - YardAsphalt.RoughMin, YardAsphalt.RoughMin, 1.0f, Dirty),
        PavedInstance(TEXT("MI_LentPavers"), PavedMaster, Pavers, FLinearColor(1.0f, 0.98f, 0.95f), 1.0f, Pavers.RoughMax - Pavers.RoughMin, Pavers.RoughMin, 1.0f, Dirty),
        PavedInstance(TEXT("MI_LentSetts"), PavedMaster, Setts, FLinearColor(1.0f, 1.0f, 1.0f), 1.0f, Setts.RoughMax - Setts.RoughMin, Setts.RoughMin, 1.0f, Dirty),
        PavedInstance(TEXT("MI_LentKerb"), PavedMaster, Kerb, FLinearColor(1.05f, 1.04f, 1.02f), 1.0f, Kerb.RoughMax - Kerb.RoughMin, Kerb.RoughMin, 1.0f, Dirty),
        PavedInstance(TEXT("MI_LentRoadPaint"), PavedMaster, Kerb, FLinearColor(0.78f, 0.78f, 0.75f), 0.12f, 0.4f, 0.35f, 0.25f, Dirty),
    };
    UMaterial* Terrain = BuildTerrainMaterial({ &YardAsphalt, &Earth, &Gravel, &Grass, &Stones }, Wall, Surface, Dirty);
    UMaterial* Water = BuildWaterMaterial(Dirty);
    if (!Water)
    {
        return 1;
    }
    FAssetCompilingManager::Get().FinishAllCompilation();
    if (!Save(Dirty))
    {
        UE_LOG(LogMarburgumGround, Error, TEXT("MB2GROUND could not save textures and materials"));
        return 1;
    }
    Dirty.Reset();

    // Paved meshes.
    TArray<UStaticMesh*> Meshes;
    int32 PavedTriangles = 0;
    for (const FChunk& Chunk : Chunks)
    {
        UStaticMesh* Mesh = BuildChunkMesh(Chunk, TEXT("Paved"), MakeArrayView(SurfaceNames, SurfaceCount), PavedMaterials, true, Dirty);
        if (!Mesh || !Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.Num() == 0)
        {
            UE_LOG(LogMarburgumGround, Error, TEXT("MB2GROUND paved chunk %d,%d did not build"), Chunk.Col, Chunk.Row);
            return 1;
        }
        PavedTriangles += Mesh->GetRenderData()->LODResources[0].GetNumTriangles();
        Meshes.Add(Mesh);
    }
    if (!Save(Dirty))
    {
        UE_LOG(LogMarburgumGround, Error, TEXT("MB2GROUND could not save the paved meshes"));
        return 1;
    }
    Dirty.Reset();

    // The distant ring (ring.bin, tools/lent-ground ring): the survey round the square, so the
    // map ends in the rest of the city. Ground and canopies in the paved look, roofs and walls in
    // the buildings' own materials, and the Drava's water.
    TArray<UStaticMesh*> RingMeshes;
    int32 RingTriangles = 0;
    {
        TArray<FChunk> RingChunks;
        const FString RingPath = FPaths::Combine(Data, TEXT("ring.bin"));
        if (!FPaths::FileExists(RingPath))
        {
            UE_LOG(LogMarburgumGround, Warning, TEXT("MB2GROUND no ring.bin: the square keeps its bare edge (run lent-ground ring)"));
        }
        else if (!ReadPaved(RingPath, RingChunks))
        {
            UE_LOG(LogMarburgumGround, Error, TEXT("MB2GROUND cannot read ring.bin"));
            return 1;
        }
        auto Envelope = [](const TCHAR* Name) {
            return LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Marburgum/Materials/Instances/building_envelope/%s.%s"), Name, Name));
        };
        TArray<UMaterialInterface*> RingMaterials = {
            PavedInstance(TEXT("MI_LentRingGround"), PavedMaster, Grass, FLinearColor(0.78f, 0.80f, 0.70f), 1.0f, Grass.RoughMax - Grass.RoughMin, Grass.RoughMin, 0.6f, Dirty),
            PavedInstance(TEXT("MI_LentRingTrees"), PavedMaster, Grass, FLinearColor(0.42f, 0.52f, 0.36f), 1.0f, 0.1f, 0.85f, 0.4f, Dirty),
            Envelope(TEXT("MI_roof_tile_clay")), Envelope(TEXT("MI_roof_flat_grey")),
            Envelope(TEXT("MI_plaster_beige")), Envelope(TEXT("MI_plaster_ochre")), Envelope(TEXT("MI_plaster_render_grey")),
            Water,
        };
        if (RingChunks.Num() > 0 && RingMaterials.Contains(nullptr))
        {
            UE_LOG(LogMarburgumGround, Error, TEXT("MB2GROUND ring materials missing (run MarburgumRebuildFacades first)"));
            return 1;
        }
        for (const FChunk& Chunk : RingChunks)
        {
            UStaticMesh* Mesh = BuildChunkMesh(Chunk, TEXT("Ring"), MakeArrayView(RingSlotNames, RingSlotCount), RingMaterials, false, Dirty);
            if (!Mesh || !Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.Num() == 0)
            {
                UE_LOG(LogMarburgumGround, Error, TEXT("MB2GROUND ring chunk %d,%d did not build"), Chunk.Col, Chunk.Row);
                return 1;
            }
            RingTriangles += Mesh->GetRenderData()->LODResources[0].GetNumTriangles();
            RingMeshes.Add(Mesh);
        }
        if (!Save(Dirty))
        {
            UE_LOG(LogMarburgumGround, Error, TEXT("MB2GROUND could not save the ring"));
            return 1;
        }
        Dirty.Reset();
    }

    // The level.
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(LentMap);
    if (!World)
    {
        UE_LOG(LogMarburgumGround, Error, TEXT("MB2GROUND cannot load %s"), LentMap);
        return 1;
    }
    TArray<ALandscapeProxy*> OldLandscapes;
    TArray<AActor*> Remove;
    TArray<AActor*> Reseat;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (ALandscapeProxy* Landscape = Cast<ALandscapeProxy>(*It))
        {
            OldLandscapes.Add(Landscape);
        }
        else if (It->GetActorLabel() == TEXT("Roads_lent") || It->GetActorLabel().StartsWith(ActorPrefix))
        {
            Remove.Add(*It);
        }
        else if (IsReseated(*It))
        {
            Reseat.Add(*It);
        }
        // Lent's river sheet and the neighbouring windows' (Drava_pristan, Drava_titov_most).
        if (AStaticMeshActor* Sheet = Cast<AStaticMeshActor>(*It); Sheet && Sheet->GetActorLabel().StartsWith(TEXT("Drava")))
        {
            Sheet->GetStaticMeshComponent()->SetMaterial(0, Water);
        }
    }
    // What each placed actor stands on now: the first surface under it (the bridge deck counts).
    TArray<TOptional<double>> OldGround;
    for (AActor* Actor : Reseat)
    {
        OldGround.Add(SurfaceBelow(World, Actor));
    }
    for (ALandscapeProxy* Landscape : OldLandscapes)
    {
        World->EditorDestroyActor(Landscape, true);
    }
    for (AActor* Actor : Remove)
    {
        World->EditorDestroyActor(Actor, true);
    }

    // New landscape: one quad per grid step (about 0.5 m), heights in metres with a vertical scale of 100.
    const int32 Quads = GridPoints - 1;
    const double QuadCm = 2.0 * HalfExtent * 100.0 / Quads;
    TArray<uint16> HeightData;
    HeightData.SetNumUninitialized(GridPoints * GridPoints);
    for (int32 J = 0; J < GridPoints; ++J) // landscape Y: east
    {
        for (int32 I = 0; I < GridPoints; ++I) // landscape X: north
        {
            const float Metres = Heights.At(Quads - I, J);
            HeightData[J * GridPoints + I] = LandscapeDataAccess::GetTexHeight(Metres);
        }
    }
    ALandscape* Landscape = World->SpawnActor<ALandscape>(FVector(-HalfExtent * 100.0, -HalfExtent * 100.0, 0.0), FRotator::ZeroRotator);
    Landscape->SetActorRelativeScale3D(FVector(QuadCm, QuadCm, 100.0));
    Landscape->LandscapeMaterial = Terrain;
    TMap<FGuid, TArray<uint16>> HeightPerLayer;
    HeightPerLayer.Add(FGuid(), MoveTemp(HeightData));
    TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialPerLayer;
    MaterialPerLayer.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());
    Landscape->Import(FGuid::NewGuid(), 0, 0, Quads, Quads, Subsections, SubsectionQuads, HeightPerLayer, nullptr, MaterialPerLayer, ELandscapeImportAlphamapType::Additive, MakeArrayView<const FLandscapeLayer>({}));
    Landscape->CreateLandscapeInfo();
    Landscape->SetActorLabel(FString::Printf(TEXT("%sLandscape"), ActorPrefix));
    Landscape->SetFolderPath(TEXT("Ground"));
    // No edit-layer merge here: under -nullrhi the imported heights are already the final data.

    // Paved actors.
    for (UStaticMesh* Mesh : Meshes)
    {
        AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator);
        Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
        Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
        Actor->SetActorLabel(FString::Printf(TEXT("%s%s"), ActorPrefix, *Mesh->GetName().RightChop(3)));
        Actor->SetFolderPath(TEXT("Ground/Paved"));
    }
    for (UStaticMesh* Mesh : RingMeshes)
    {
        AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator);
        UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
        Component->SetStaticMesh(Mesh);
        Component->SetMobility(EComponentMobility::Static);
        Component->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
        Component->SetGenerateOverlapEvents(false);
        Component->bReceivesDecals = false;
        Actor->SetActorLabel(FString::Printf(TEXT("%s%s"), ActorPrefix, *Mesh->GetName().RightChop(3)));
        Actor->SetFolderPath(TEXT("Ground/Ring"));
    }

    // Collision for the new landscape and paving, so the seat traces below can hit it.
    World->UpdateWorldComponents(true, false);
    FAssetCompilingManager::Get().FinishAllCompilation();

    // Check the new surface where it matters, then move the placed actors.
    double WorstHeightError = 0.0;
    int32 Probes = 0;
    for (int32 R = 50; R < GridPoints - 50; R += 97)
    {
        for (int32 C = 50; C < GridPoints - 50; C += 97)
        {
            const double Step = 2.0 * HalfExtent / Quads;
            const double X = -HalfExtent + C * Step;
            const double Y = -HalfExtent + R * Step;
            const TOptional<float> Got = Landscape->GetHeightAtLocation(Ue(X, Y, 0.0), EHeightfieldSource::Complex);
            if (Got.IsSet())
            {
                WorstHeightError = FMath::Max(WorstHeightError, FMath::Abs(Got.GetValue() / 100.0 - Heights.At(R, C)));
                ++Probes;
            }
        }
    }
    // Each placed actor keeps its height above whatever it stands on.
    int32 Unseated = 0;
    for (int32 Index = 0; Index < Reseat.Num(); ++Index)
    {
        AActor* Actor = Reseat[Index];
        // The new ground can be higher than the actor was; start above it.
        const TOptional<double> NewGround = SurfaceBelow(World, Actor, Top.Sample(Actor->GetActorLocation().Y / 100.0, -Actor->GetActorLocation().X / 100.0) * 100.0);
        if (!OldGround[Index].IsSet() && Actor->IsA<APlayerStart>())
        {
            // A start that was below the old ground (the bridge start sat under Stari most's
            // deck) goes on top of whatever is highest there, a metre up like the others.
            const TOptional<double> Highest = SurfaceBelow(World, Actor, 4000.0);
            if (Highest.IsSet())
            {
                const FVector At = Actor->GetActorLocation();
                Actor->SetActorLocation(FVector(At.X, At.Y, Highest.GetValue() + 100.0));
                UE_LOG(LogMarburgumGround, Display, TEXT("MB2GROUND placed %s on the surface at %.0f cm"), *Actor->GetActorLabel(), Highest.GetValue());
                continue;
            }
        }
        if (!OldGround[Index].IsSet() || !NewGround.IsSet())
        {
            UE_LOG(LogMarburgumGround, Error, TEXT("MB2GROUND %s has no ground under it (at %s; before %s, after %s)"), *Actor->GetActorLabel(), *Actor->GetActorLocation().ToString(),
                OldGround[Index].IsSet() ? TEXT("found") : TEXT("none"), NewGround.IsSet() ? TEXT("found") : TEXT("none"));
            ++Unseated;
            continue;
        }
        const double Lift = NewGround.GetValue() - OldGround[Index].GetValue();
        Actor->SetActorLocation(Actor->GetActorLocation() + FVector(0.0, 0.0, Lift));
        UE_LOG(LogMarburgumGround, Display, TEXT("MB2GROUND moved %s by %.0f cm (ground %.0f cm)"), *Actor->GetActorLabel(), Lift, NewGround.GetValue());
    }
    if (!UEditorLoadingAndSavingUtils::SaveMap(World, LentMap))
    {
        UE_LOG(LogMarburgumGround, Error, TEXT("MB2GROUND could not save %s"), LentMap);
        return 1;
    }
    // Chunks of an earlier, smaller extent that no longer exist go with it; the level no longer
    // places them.
    TSet<FString> Current;
    for (const TArray<UStaticMesh*>* Set : { &Meshes, &RingMeshes })
    {
        for (const UStaticMesh* Mesh : *Set)
        {
            Current.Add(Mesh->GetName() + TEXT(".uasset"));
        }
    }
    int32 Stale = 0;
    for (const TCHAR* Kind : { TEXT("Paved"), TEXT("Ring") })
    {
        const FString KindFiles = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("World/Ground"), Kind);
        TArray<FString> Files;
        IFileManager::Get().FindFiles(Files, *FPaths::Combine(KindFiles, FString::Printf(TEXT("SM_Lent%s_*.uasset"), Kind)), true, false);
        for (const FString& File : Files)
        {
            if (!Current.Contains(File) && IFileManager::Get().Delete(*FPaths::Combine(KindFiles, File)))
            {
                ++Stale;
            }
        }
    }
    UE_LOG(LogMarburgumGround, Display, TEXT("MB2GROUND done landscape=%dx%d quad_cm=%.2f probes=%d worst_height_error_m=%.3f paved_chunks=%d paved_triangles=%d ring_chunks=%d ring_triangles=%d reseated=%d removed=%d stale_chunks_deleted=%d"),
        GridPoints, GridPoints, QuadCm, Probes, WorstHeightError, Meshes.Num(), PavedTriangles, RingMeshes.Num(), RingTriangles, Reseat.Num(), Remove.Num() + OldLandscapes.Num(), Stale);
    return (Probes > 0 && WorstHeightError < 0.05 && Unseated == 0) ? 0 : 2;
}
