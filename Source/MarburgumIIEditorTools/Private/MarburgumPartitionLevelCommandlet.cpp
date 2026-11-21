#include "MarburgumPartitionLevelCommandlet.h"
#include "MarburgumEditorMaps.h"

#include "Algo/Transform.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "MarburgumGroundFile.h"
#include "MarburgumPlants.h"
#include "MarburgumStreetFurniture.h"
#include "MarburgumStreetFurnitureActor.h"
#include "MarburgumStreetLamps.h"
#include "MarburgumStreetLights.h"
#include "MarburgumVegetation.h"
#include "MarburgumWorldLoading.h"
#include "MarburgumWholeLevel.h"
#include "MeshMerge/MeshMergingSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "WorldPartition/HLOD/HLODBuilder.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/RuntimeHashSet/RuntimePartition.h"
#include "WorldPartition/RuntimeHashSet/WorldPartitionRuntimeHashSet.h"
#include "WorldPartition/WorldPartition.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumPartitionLevel, Log, All);

namespace MarburgumPartitionLevel
{
    constexpr const TCHAR* HLODLayerPackage = TEXT("/Game/World/HLOD/HLOD_Lent");
    // Unreal centimetres.
    constexpr uint32 MainCellSize = 12800;
    constexpr int32 MainLoadingRange = 51200;
    constexpr uint32 HLODCellSize = 25600;
    constexpr int32 HLODLoadingRange = 400000;

    // The runtime hash's partitions, the grid's cell size and the server streaming mode are private engine properties
    // without setters. They are reflected, so reach them by name, and fail loudly if an engine update renames one.
    template <typename TProperty>
    TProperty* Find(const UStruct* Struct, const TCHAR* Name)
    {
        TProperty* Property = Struct ? CastField<TProperty>(Struct->FindPropertyByName(Name)) : nullptr;
        if (!Property)
        {
            UE_LOG(LogMarburgumPartitionLevel, Error, TEXT("MB2PARTITION engine property %s.%s not found"),
                Struct ? *Struct->GetName() : TEXT("?"), Name);
        }
        return Property;
    }

    URuntimePartition* PartitionObject(const UStruct* Struct, void* Container, const TCHAR* Name)
    {
        const FObjectProperty* Property = Find<FObjectProperty>(Struct, Name);
        return Property ? Cast<URuntimePartition>(Property->GetObjectPropertyValue_InContainer(Container)) : nullptr;
    }

    bool SetGrid(URuntimePartition* Partition, uint32 CellSize, int32 LoadingRange, bool bBlockOnSlowStreaming)
    {
        const FUInt32Property* Cell = Partition ? Find<FUInt32Property>(Partition->GetClass(), TEXT("CellSize")) : nullptr;
        if (!Cell)
        {
            return false;
        }
        Partition->Modify();
        Cell->SetPropertyValue_InContainer(Partition, CellSize);
        Partition->LoadingRange = LoadingRange;
        Partition->bBlockOnSlowStreaming = bBlockOnSlowStreaming;
        return true;
    }

    // Each cell's meshes merged at their lowest LOD into one Nanite mesh that keeps the level's own materials, one
    // section per material (the whole level has about thirty). A baked material does not survive these: the paving's
    // texture follows the world position and came out as a checkerboard, and the lit windows read their household from
    // texture coordinates 2 and 3 (ADR-066). The engine's approximating and simplifying builders would make lighter
    // meshes, but they need GeometryProcessingAdapters and ProxyLODMeshReduction, which their plugins build for Win64
    // only; on Linux UHLODBuilderMeshApproximate dereferences the missing implementation and the HLOD build crashes.
    // Returns false if the engine's merge settings are no longer where they were.
    bool SetMergeSettings(UHLODLayer& Layer, TArray<UPackage*>& Dirty)
    {
        const FObjectProperty* SettingsProperty = Find<FObjectProperty>(UHLODLayer::StaticClass(), *UHLODLayer::GetHLODBuilderSettingsPropertyName().ToString());
        UObject* Settings = SettingsProperty ? SettingsProperty->GetObjectPropertyValue_InContainer(&Layer) : nullptr;
        const FStructProperty* Property = Settings ? Find<FStructProperty>(Settings->GetClass(), TEXT("MeshMergeSettings")) : nullptr;
        if (!Property || Property->Struct != FMeshMergingSettings::StaticStruct())
        {
            return false;
        }
        void* Current = Property->ContainerPtrToValuePtr<void>(Settings);
        FMeshMergingSettings Wanted = *static_cast<const FMeshMergingSettings*>(Current);
        Wanted.LODSelectionType = EMeshLODSelectionType::LowestDetailLOD;
        Wanted.bMergeMaterials = false;
        Wanted.bMergeEquivalentMaterials = true;
        Wanted.NaniteSettings.bEnabled = true;
        // Lumen lights the city; nothing is static-lit or collides with an HLOD.
        Wanted.bGenerateLightMapUV = false;
        Wanted.bMergePhysicsData = false;
        if (!Property->Identical(Current, &Wanted, PPF_None))
        {
            Settings->Modify();
            Property->CopyCompleteValue(Current, &Wanted);
            Dirty.AddUnique(Layer.GetPackage());
        }
        return true;
    }

    // The ground under each item of a placement file, found on the whole level by its game rule and written beside the
    // file, for the clients that hold only the cells round their player. Returns false if it could not be written.
    bool WriteGround(const UWorld& World, const TCHAR* Name, const TCHAR* PlacementFile, const FString& Text, const TArray<FVector>& Surveyed,
        MarburgumII::WorldLoading::FFindGround FindGround)
    {
        TArray<TOptional<double>> Ground;
        Ground.Reserve(Surveyed.Num());
        int32 NoGround = 0;
        for (const FVector& Point : Surveyed)
        {
            Ground.Add(FindGround(World, Point));
            NoGround += Ground.Last().IsSet() ? 0 : 1;
        }
        const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), MarburgumII::GroundFile::PathFor(PlacementFile));
        if (!FFileHelper::SaveStringToFile(MarburgumII::GroundFile::Write(MarburgumII::GroundFile::Fingerprint(Text), Ground), *Path,
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {
            UE_LOG(LogMarburgumPartitionLevel, Error, TEXT("MB2PARTITION could not write %s"), *Path);
            return false;
        }
        UE_LOG(LogMarburgumPartitionLevel, Display, TEXT("MB2PARTITION ground of=%s items=%d grounded=%d no_ground=%d file=%s"), Name, Surveyed.Num(),
            Surveyed.Num() - NoGround, NoGround, *Path);
        return true;
    }

    bool ReadPlacements(const TCHAR* PlacementFile, FString& OutText)
    {
        const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), PlacementFile);
        if (!FFileHelper::LoadFileToString(OutText, *Path))
        {
            UE_LOG(LogMarburgumPartitionLevel, Error, TEXT("MB2PARTITION cannot read %s"), *Path);
            return false;
        }
        return true;
    }

    // The street lamps', the plants' and the street furniture's ground. Returns the process exit code.
    int32 WriteGroundFiles(const UWorld& World)
    {
        FString Error;
        FString Text;
        TArray<FVector> Surveyed;

        TArray<MarburgumII::StreetLights::FLamp> Lamps;
        if (!ReadPlacements(MarburgumII::StreetLights::LampsFile, Text) || !MarburgumII::StreetLights::Parse(Text, Lamps, Error))
        {
            UE_LOG(LogMarburgumPartitionLevel, Error, TEXT("MB2PARTITION street lamps: %s"), *Error);
            return 1;
        }
        Algo::Transform(Lamps, Surveyed, [](const MarburgumII::StreetLights::FLamp& Lamp) { return Lamp.Location; });
        if (!WriteGround(World, TEXT("street_lamps"), MarburgumII::StreetLights::LampsFile, Text, Surveyed, &UMarburgumStreetLampsSubsystem::FindGround))
        {
            return 1;
        }

        TArray<MarburgumII::Vegetation::FSpecies> Species;
        TArray<MarburgumII::Vegetation::FPlant> Plants;
        if (!ReadPlacements(MarburgumII::Vegetation::PlantsFile, Text) || !MarburgumII::Vegetation::Parse(Text, Species, Plants, Error))
        {
            UE_LOG(LogMarburgumPartitionLevel, Error, TEXT("MB2PARTITION plants: %s"), *Error);
            return 1;
        }
        Surveyed.Reset();
        Algo::Transform(Plants, Surveyed, [](const MarburgumII::Vegetation::FPlant& Plant) { return Plant.Location; });
        if (!WriteGround(World, TEXT("plants"), MarburgumII::Vegetation::PlantsFile, Text, Surveyed, &UMarburgumPlantsSubsystem::FindGround))
        {
            return 1;
        }

        TArray<MarburgumII::StreetFurniture::FPiece> Pieces;
        if (!ReadPlacements(MarburgumII::StreetFurniture::FurnitureFile, Text) || !MarburgumII::StreetFurniture::Parse(Text, Pieces, Error))
        {
            UE_LOG(LogMarburgumPartitionLevel, Error, TEXT("MB2PARTITION street furniture: %s"), *Error);
            return 1;
        }
        Surveyed.Reset();
        Algo::Transform(Pieces, Surveyed, [](const MarburgumII::StreetFurniture::FPiece& Piece) { return Piece.Location; });
        return WriteGround(World, TEXT("street_furniture"), MarburgumII::StreetFurniture::FurnitureFile, Text, Surveyed,
            &UMarburgumStreetFurnitureSubsystem::FindGround) ? 0 : 1;
    }

    // The one HLOD layer: merged meshes, no parent.
    UHLODLayer* MergedLayer(TArray<UPackage*>& Dirty)
    {
        const FString Name = FPackageName::GetLongPackageAssetName(HLODLayerPackage);
        UPackage* Package = FPackageName::DoesPackageExist(HLODLayerPackage) ? LoadPackage(nullptr, HLODLayerPackage, LOAD_None) : CreatePackage(HLODLayerPackage);
        if (!Package)
        {
            return nullptr;
        }
        UHLODLayer* Layer = FindObject<UHLODLayer>(Package, *Name);
        const bool bNew = !Layer;
        if (bNew)
        {
            Layer = NewObject<UHLODLayer>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
            FAssetRegistryModule::AssetCreated(Layer);
        }
        if (bNew || Layer->GetLayerType() != EHLODLayerType::MeshMerge || Layer->GetParentLayer())
        {
            Layer->Modify();
            Layer->SetLayerType(EHLODLayerType::MeshMerge);
            Layer->SetParentLayer(nullptr);
            // The builder's settings follow the layer type: the layer makes new ones when its type is edited.
            FPropertyChangedEvent TypeChanged(FindFProperty<FProperty>(UHLODLayer::StaticClass(), UHLODLayer::GetLayerTypePropertyName()));
            static_cast<UObject*>(Layer)->PostEditChangeProperty(TypeChanged);
            Dirty.Add(Package);
        }
        if (!SetMergeSettings(*Layer, Dirty))
        {
            UE_LOG(LogMarburgumPartitionLevel, Error, TEXT("MB2PARTITION %s has no mesh merge settings"), HLODLayerPackage);
            return nullptr;
        }
        return Layer;
    }
}

UMarburgumPartitionLevelCommandlet::UMarburgumPartitionLevelCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UMarburgumPartitionLevelCommandlet::Main(const FString& Params)
{
    using namespace MarburgumPartitionLevel;
    FString Map = MarburgumEditorMaps::Lent;
    FParse::Value(*Params, TEXT("Map="), Map);
    const bool bCheckOnly = FParse::Param(*Params, TEXT("CheckOnly"));
    const bool bGround = FParse::Param(*Params, TEXT("Ground"));

    FMarburgumWholeLevel WholeLevel(Map);
    UWorld* World = WholeLevel.GetWorld();
    if (!World)
    {
        UE_LOG(LogMarburgumPartitionLevel, Error, TEXT("MB2PARTITION cannot load %s"), *Map);
        return 1;
    }
    UWorldPartition* Partition = World->GetWorldPartition();
    if (!Partition)
    {
        UE_LOG(LogMarburgumPartitionLevel, Error, TEXT("MB2PARTITION %s is not a World Partition map; convert it first (scripts/partition-world.sh)"), *Map);
        return 1;
    }
    if (bGround)
    {
        return WriteGroundFiles(*World);
    }

    TArray<UPackage*> Dirty;
    UHLODLayer* Layer = MergedLayer(Dirty);
    if (!Layer || !Layer->GetHLODBuilderSettings())
    {
        UE_LOG(LogMarburgumPartitionLevel, Error, TEXT("MB2PARTITION cannot make the HLOD layer %s"), HLODLayerPackage);
        return 1;
    }
    // The conversion's copies of the engine's default layers, deleted once nothing points at them.
    TArray<FString> Replaced;
    for (const UHLODLayer* Old = Partition->GetDefaultHLODLayer(); Old && Old != Layer; Old = Old->GetParentLayer())
    {
        if (Old->GetPackage()->GetName().StartsWith(TEXT("/Game/")))
        {
            Replaced.Add(Old->GetPackage()->GetName());
        }
    }
    Partition->Modify();
    Partition->SetDefaultHLODLayer(Layer);

    const FEnumProperty* ServerStreaming = Find<FEnumProperty>(UWorldPartition::StaticClass(), TEXT("ServerStreamingMode"));
    const FObjectProperty* HashProperty = Find<FObjectProperty>(UWorldPartition::StaticClass(), TEXT("RuntimeHash"));
    if (!ServerStreaming || !HashProperty)
    {
        return 1;
    }
    ServerStreaming->GetUnderlyingProperty()->SetIntPropertyValue(ServerStreaming->ContainerPtrToValuePtr<void>(Partition),
        static_cast<int64>(EWorldPartitionServerStreamingMode::Disabled));

    UWorldPartitionRuntimeHashSet* Hash = Cast<UWorldPartitionRuntimeHashSet>(HashProperty->GetObjectPropertyValue_InContainer(Partition));
    const FArrayProperty* PartitionsProperty = Find<FArrayProperty>(UWorldPartitionRuntimeHashSet::StaticClass(), TEXT("RuntimePartitions"));
    if (!Hash || !PartitionsProperty)
    {
        UE_LOG(LogMarburgumPartitionLevel, Error, TEXT("MB2PARTITION the runtime hash is not a hash set"));
        return 1;
    }
    // Start again from the engine's default partitions: the main grid and one HLOD grid for each layer of the default
    // HLOD layer's chain, here one. Then size them.
    Hash->Modify();
    FScriptArrayHelper(PartitionsProperty, PartitionsProperty->ContainerPtrToValuePtr<void>(Hash)).EmptyValues();
    Hash->SetDefaultValues();
    FScriptArrayHelper Partitions(PartitionsProperty, PartitionsProperty->ContainerPtrToValuePtr<void>(Hash));
    const UStruct* DescStruct = CastField<FStructProperty>(PartitionsProperty->Inner)->Struct;
    const FArrayProperty* SetupsProperty = Find<FArrayProperty>(DescStruct, TEXT("HLODSetups"));
    if (Partitions.Num() != 1 || !SetupsProperty)
    {
        UE_LOG(LogMarburgumPartitionLevel, Error, TEXT("MB2PARTITION expected one runtime partition, got %d"), Partitions.Num());
        return 1;
    }
    URuntimePartition* Main = PartitionObject(DescStruct, Partitions.GetRawPtr(0), TEXT("MainLayer"));
    FScriptArrayHelper Setups(SetupsProperty, SetupsProperty->ContainerPtrToValuePtr<void>(Partitions.GetRawPtr(0)));
    URuntimePartition* HLOD = Setups.Num() == 1
        ? PartitionObject(CastField<FStructProperty>(SetupsProperty->Inner)->Struct, Setups.GetRawPtr(0), TEXT("PartitionLayer"))
        : nullptr;
    if (!SetGrid(Main, MainCellSize, MainLoadingRange, true) || !SetGrid(HLOD, HLODCellSize, HLODLoadingRange, false))
    {
        UE_LOG(LogMarburgumPartitionLevel, Error, TEXT("MB2PARTITION cannot size the grids (HLOD setups: %d)"), Setups.Num());
        return 1;
    }

    MarburgumPartition::ApplyStreamingRules(*World);
    int32 Streaming = 0;
    int32 AlwaysLoaded = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->IsPackageExternal())
        {
            ++(It->GetIsSpatiallyLoaded() ? Streaming : AlwaysLoaded);
        }
    }
    UE_LOG(LogMarburgumPartitionLevel, Display,
        TEXT("MB2PARTITION map=%s main_cell_cm=%u main_range_cm=%d hlod_layer=%s hlod_builder=%s hlod_cell_cm=%u hlod_range_cm=%d server_streaming=0 streaming_actors=%d always_loaded=%d"),
        *Map, MainCellSize, MainLoadingRange, *Layer->GetPathName(), *Layer->GetHLODBuilderSettings()->GetClass()->GetName(), HLODCellSize,
        HLODLoadingRange, Streaming, AlwaysLoaded);
    if (bCheckOnly)
    {
        return 0;
    }
    if (!Dirty.IsEmpty() && !UEditorLoadingAndSavingUtils::SavePackages(Dirty, false))
    {
        UE_LOG(LogMarburgumPartitionLevel, Error, TEXT("MB2PARTITION could not save %s"), HLODLayerPackage);
        return 1;
    }
    if (!WholeLevel.Save())
    {
        UE_LOG(LogMarburgumPartitionLevel, Error, TEXT("MB2PARTITION could not save %s"), *Map);
        return 1;
    }
    for (const FString& Name : Replaced)
    {
        FString File;
        if (FPackageName::TryConvertLongPackageNameToFilename(Name, File, FPackageName::GetAssetPackageExtension()) && IFileManager::Get().Delete(*File))
        {
            UE_LOG(LogMarburgumPartitionLevel, Display, TEXT("MB2PARTITION deleted %s"), *Name);
        }
    }
    UE_LOG(LogMarburgumPartitionLevel, Display, TEXT("MB2PARTITION saved %s"), *Map);
    return 0;
}
