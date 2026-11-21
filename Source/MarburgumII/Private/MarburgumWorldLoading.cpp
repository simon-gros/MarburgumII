#include "MarburgumWorldLoading.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "MarburgumGroundFile.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumWorldLoading, Log, All);

namespace MarburgumII::WorldLoading
{
    namespace
    {
        const TCHAR* Role(const UWorld& World)
        {
            switch (World.GetNetMode())
            {
            case NM_DedicatedServer: return TEXT("server");
            case NM_ListenServer: return TEXT("listen");
            case NM_Client: return TEXT("client");
            default: return TEXT("standalone");
            }
        }
    }

    void LoadWholeWorldOnServer(UWorld& World, const TCHAR* Caller)
    {
        const UWorldPartition* Partition = World.GetWorldPartition();
        const ENetMode NetMode = World.GetNetMode();
        if (!Partition || Partition->IsServerStreamingEnabled() || (NetMode != NM_DedicatedServer && NetMode != NM_ListenServer))
        {
            return;
        }
        // Before the engine's partition subsystem has begun play the server would also load the client-only cells
        // (the HLODs), which begin play drops again: correct, only slower. Logged, so it shows if the order changes.
        UWorldPartitionSubsystem* Streaming = World.GetSubsystem<UWorldPartitionSubsystem>();
        const bool bPartitionBegun = Streaming && Streaming->HasCalledBeginPlay();
        const double Started = FPlatformTime::Seconds();
        World.BlockTillLevelStreamingCompleted();
        UE_LOG(LogMarburgumWorldLoading, Log, TEXT("MB2WORLD role=server event=whole_world caller=%s partition_begun=%d streaming_complete=%d seconds=%.2f"),
            Caller, bPartitionBegun ? 1 : 0, Streaming && Streaming->IsAllStreamingCompleted() ? 1 : 0, FPlatformTime::Seconds() - Started);
    }

    TArray<TOptional<double>> GroundOf(UWorld& World, const TCHAR* PlacementFile, const FString& PlacementText, const TArray<FVector>& Points,
        FFindGround FindGround, const TCHAR* Caller, bool& bOutFromFile)
    {
        TArray<TOptional<double>> Ground;
        FString Text;
        FString Error = TEXT("none");
        const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), GroundFile::PathFor(PlacementFile));
        bOutFromFile = FFileHelper::LoadFileToString(Text, *Path, FFileHelper::EHashOptions::None, FILEREAD_Silent)
            && GroundFile::Parse(Text, GroundFile::Fingerprint(PlacementText), Points.Num(), Ground, Error);
        if (bOutFromFile)
        {
            return Ground;
        }
        if (World.IsPartitionedWorld())
        {
            UE_LOG(LogMarburgumWorldLoading, Warning, TEXT("MB2WORLD role=%s event=no_ground_file caller=%s reason=\"%s\" (scripts/partition-world.sh ground)"),
                Role(World), Caller, *Error);
        }
        // Tracing the whole level is only possible where all of it is in.
        LoadWholeWorldOnServer(World, Caller);
        Ground.Reset(Points.Num());
        for (const FVector& Point : Points)
        {
            Ground.Add(FindGround(World, Point));
        }
        return Ground;
    }
}
