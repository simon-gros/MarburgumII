#include "MarburgumWorldLoading.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumWorldLoading, Log, All);

namespace MarburgumII::WorldLoading
{
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
}
