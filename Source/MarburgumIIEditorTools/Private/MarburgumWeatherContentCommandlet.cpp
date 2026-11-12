#include "MarburgumWeatherContentCommandlet.h"

#include "AssetToolsModule.h"
#include "AutomatedAssetImportData.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "MarburgumSurface.h"
#include "MarburgumWeatherLookContent.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumWeatherContent, Log, All);

namespace MarburgumWeatherContent
{
    namespace Surface = MarburgumII::Surface;

    constexpr const TCHAR* GroundMaterials = TEXT("/Game/World/Ground/Materials");
    constexpr const TCHAR* PackSource = TEXT("SourceArt/Audio/Footsteps/KenneyImpactSounds");
    constexpr const TCHAR* GeneratedSource = TEXT("SourceArt/Audio/Footsteps/Generated");

    /** Lent's paved materials (MarburgumRebuildGround) by the paved slot they dress. */
    const TPair<const TCHAR*, const TCHAR*> PavedMaterials[] = {
        { TEXT("MI_LentAsphalt"), TEXT("asphalt") },
        { TEXT("MI_LentYardAsphalt"), TEXT("yard_asphalt") },
        { TEXT("MI_LentPavers"), TEXT("pavers") },
        { TEXT("MI_LentSetts"), TEXT("setts") },
        { TEXT("MI_LentKerb"), TEXT("kerb") },
        { TEXT("MI_LentRoadPaint"), TEXT("paint") },
    };

    UPhysicalMaterial* EnsurePhysicalMaterial(const FString& SurfaceId, TArray<UPackage*>& Dirty)
    {
        const FString Name = FString(Surface::PhysicalMaterialPrefix) + SurfaceId;
        const FString PackageName = FString::Printf(TEXT("%s/%s"), Surface::PhysicalMaterialFolder, *Name);
        UPackage* Package = FPackageName::DoesPackageExist(PackageName) ? LoadPackage(nullptr, *PackageName, LOAD_None) : CreatePackage(*PackageName);
        if (!Package)
        {
            return nullptr;
        }
        Package->FullyLoad();
        UPhysicalMaterial* Material = FindObject<UPhysicalMaterial>(Package, *Name);
        if (!Material)
        {
            // Friction and restitution keep the engine defaults: tyres take grip from the surface tables
            // (MarburgumVehicleDynamics.h), feet from character movement; the material only names the surface.
            Material = NewObject<UPhysicalMaterial>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
            Material->MarkPackageDirty();
            Dirty.AddUnique(Package);
        }
        return Material;
    }

    bool AssignSurfaces()
    {
        TArray<UPackage*> Dirty;
        int32 Assigned = 0;
        for (const TPair<const TCHAR*, const TCHAR*>& Paved : PavedMaterials)
        {
            const FString SurfaceId = Surface::PavedSlotSurface(Paved.Value);
            UPhysicalMaterial* Physical = EnsurePhysicalMaterial(SurfaceId, Dirty);
            const FString Path = FString::Printf(TEXT("%s/%s.%s"), GroundMaterials, Paved.Key, Paved.Key);
            UMaterialInstanceConstant* Instance = LoadObject<UMaterialInstanceConstant>(nullptr, *Path);
            if (!Physical || !Instance)
            {
                UE_LOG(LogMarburgumWeatherContent, Error, TEXT("MB2WEATHERCONTENT missing %s"), Physical ? *Path : *SurfaceId);
                return false;
            }
            // An instance only uses its own physical material when it overrides its parent's.
            if (Instance->PhysMaterial != Physical || !Instance->bOverridePhysMaterial)
            {
                Instance->Modify();
                Instance->bOverridePhysMaterial = true;
                Instance->PhysMaterial = Physical;
                Instance->PostEditChange();
                Instance->MarkPackageDirty();
                Dirty.AddUnique(Instance->GetOutermost());
            }
            ++Assigned;
            UE_LOG(LogMarburgumWeatherContent, Display, TEXT("MB2WEATHERCONTENT surface %s slot=%s physical=%s"), Paved.Key, Paved.Value, *Physical->GetPathName());
        }
        if (Dirty.Num() > 0 && !UEditorLoadingAndSavingUtils::SavePackages(Dirty, false))
        {
            UE_LOG(LogMarburgumWeatherContent, Error, TEXT("MB2WEATHERCONTENT could not save the surfaces"));
            return false;
        }
        UE_LOG(LogMarburgumWeatherContent, Display, TEXT("MB2WEATHERCONTENT surfaces assigned=%d saved=%d"), Assigned, Dirty.Num());
        return true;
    }

    bool ImportFootsteps()
    {
        TArray<FString> Files;
        for (const FName Set : { Surface::FootstepHard, Surface::FootstepHardWet, Surface::FootstepSoft, Surface::FootstepCrunch })
        {
            const TCHAR* Directory = Set == Surface::FootstepHardWet ? GeneratedSource : PackSource;
            for (int32 Variant = 0; Variant < Surface::FootstepVariants; ++Variant)
            {
                const FString File = FPaths::Combine(FPaths::ProjectDir(), Directory, FString::Printf(TEXT("%s_%03d.wav"), Surface::FootstepStem(Set), Variant));
                if (!IFileManager::Get().FileExists(*File))
                {
                    UE_LOG(LogMarburgumWeatherContent, Error, TEXT("MB2WEATHERCONTENT missing sound %s"), *File);
                    return false;
                }
                Files.Add(File);
            }
        }
        UAutomatedAssetImportData* Import = NewObject<UAutomatedAssetImportData>();
        Import->DestinationPath = Surface::FootstepFolder;
        Import->Filenames = Files;
        Import->bReplaceExisting = true;
        IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
        TArray<UPackage*> Packages;
        for (UObject* Asset : AssetTools.ImportAssetsAutomated(Import))
        {
            if (const USoundWave* Wave = Cast<USoundWave>(Asset))
            {
                Packages.AddUnique(Wave->GetOutermost());
                UE_LOG(LogMarburgumWeatherContent, Display, TEXT("MB2WEATHERCONTENT sound %s duration=%.3f channels=%d"), *Wave->GetPathName(), Wave->Duration, Wave->NumChannels);
            }
        }
        if (Packages.Num() != Files.Num())
        {
            UE_LOG(LogMarburgumWeatherContent, Error, TEXT("MB2WEATHERCONTENT imported %d of %d sounds"), Packages.Num(), Files.Num());
            return false;
        }
        if (!UEditorLoadingAndSavingUtils::SavePackages(Packages, false))
        {
            UE_LOG(LogMarburgumWeatherContent, Error, TEXT("MB2WEATHERCONTENT could not save the sounds"));
            return false;
        }
        UE_LOG(LogMarburgumWeatherContent, Display, TEXT("MB2WEATHERCONTENT saved sounds=%d under %s"), Packages.Num(), Surface::FootstepFolder);
        return true;
    }
}

UMarburgumWeatherContentCommandlet::UMarburgumWeatherContentCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UMarburgumWeatherContentCommandlet::Main(const FString& Params)
{
    FString Steps = TEXT("look,surfaces,audio");
    FParse::Value(*Params, TEXT("Steps="), Steps, false);
    TArray<FString> List;
    Steps.ParseIntoArray(List, TEXT(","));
    bool bOk = true;
    for (const FString& Step : List)
    {
        if (Step == TEXT("surfaces"))
        {
            bOk &= MarburgumWeatherContent::AssignSurfaces();
        }
        else if (Step == TEXT("audio"))
        {
            bOk &= MarburgumWeatherContent::ImportFootsteps();
        }
        else if (Step == TEXT("look"))
        {
            FString Error;
            if (!MarburgumWeatherLookContent::Write(Error))
            {
                UE_LOG(LogMarburgumWeatherContent, Error, TEXT("MB2WEATHERCONTENT look failed: %s"), *Error);
                bOk = false;
            }
        }
        else
        {
            UE_LOG(LogMarburgumWeatherContent, Error, TEXT("MB2WEATHERCONTENT unknown step %s"), *Step);
            bOk = false;
        }
    }
    UE_LOG(LogMarburgumWeatherContent, Display, TEXT("MB2WEATHERCONTENT result=%d"), bOk ? 0 : 1);
    return bOk ? 0 : 1;
}
