#include "MarburgumWholeLevel.h"

#include "AssetCompilingManager.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "LandscapeStreamingProxy.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/LoaderAdapter/LoaderAdapterShape.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumPartition, Log, All);

FMarburgumWholeLevel::FMarburgumWholeLevel(const FString& InMapPath)
    : MapPath(InMapPath)
{
    World = UEditorLoadingAndSavingUtils::LoadMap(MapPath);
    if (!World || !World->IsPartitionedWorld())
    {
        return;
    }
    const FBox Everywhere(FVector(-HALF_WORLD_MAX), FVector(HALF_WORLD_MAX));
    Loader = MakeUnique<FLoaderAdapterShape>(World, Everywhere, TEXT("MarburgumWholeLevel"));
    Loader->Load();
    // Meshes still compiling have no collision yet, and most commandlets trace the ground next.
    FAssetCompilingManager::Get().FinishAllCompilation();
    int32 Actors = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        ++Actors;
    }
    UE_LOG(LogMarburgumPartition, Display, TEXT("MB2PARTITION loaded map=%s actors=%d"), *MapPath, Actors);
}

FMarburgumWholeLevel::~FMarburgumWholeLevel() = default;

bool FMarburgumWholeLevel::Save()
{
    if (!World)
    {
        return false;
    }
    if (World->IsPartitionedWorld())
    {
        const int32 Changed = MarburgumPartition::ApplyStreamingRules(*World);
        UE_LOG(LogMarburgumPartition, Display, TEXT("MB2PARTITION streaming_rules changed=%d"), Changed);
    }
    return UEditorLoadingAndSavingUtils::SaveMap(World, MapPath);
}

bool MarburgumPartition::ShouldStream(const UClass& Class, const FString& Label, const FString& Folder)
{
    if (Class.IsChildOf(ALandscapeStreamingProxy::StaticClass()))
    {
        return true;
    }
    if (Class.IsChildOf(AStaticMeshActor::StaticClass()))
    {
        return !Folder.StartsWith(TEXT("Ground/Ring")) && !Label.StartsWith(TEXT("Drava"));
    }
    return false;
}

int32 MarburgumPartition::ApplyStreamingRules(UWorld& World)
{
    int32 Changed = 0;
    for (TActorIterator<AActor> It(&World); It; ++It)
    {
        AActor* Actor = *It;
        if (!Actor->IsPackageExternal() || Actor->IsA<AWorldPartitionHLOD>() || !Actor->CanChangeIsSpatiallyLoadedFlag())
        {
            continue;
        }
        const bool bStream = ShouldStream(*Actor->GetClass(), Actor->GetActorLabel(), Actor->GetFolderPath().ToString());
        if (Actor->GetIsSpatiallyLoaded() != bStream)
        {
            Actor->Modify();
            Actor->SetIsSpatiallyLoaded(bStream);
            ++Changed;
        }
    }
    return Changed;
}
