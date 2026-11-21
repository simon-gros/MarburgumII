#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MarburgumPartitionLevelCommandlet.generated.h"

/**
 * The World Partition settings of /Game/World/lent (ADR-091), once the engine's conversion has made it a partitioned
 * map with each actor in its own package:
 *
 *   UnrealEditor-Cmd MarburgumII.uproject -run=WorldPartitionConvertCommandlet /Game/World/lent
 *   UnrealEditor-Cmd MarburgumII.uproject -run=MarburgumPartitionLevel [-Map=/Game/World/lent] [-CheckOnly | -Ground]
 *
 * scripts/partition-world.sh runs both and then builds the HLODs. This commandlet sets:
 *
 *   grid     the main partition streams 128 m cells within 512 m of each player and blocks on slow streaming, so no
 *            player stands where the ground has not loaded; the HLOD partition draws the rest of the map from 256 m
 *            cells within 4 km, all of the 2,560 m square from anywhere in it;
 *   hlod     one HLOD layer, /Game/World/HLOD/HLOD_Lent, of merged meshes: each 256 m cell's buildings, bridges,
 *            paving and landscape as one Nanite mesh with the level's own materials, as a baked material loses the
 *            paving's world-aligned texture and the lit windows (the engine's simplifying builders are Win64-only). The conversion's copies of the engine's default layers are deleted;
 *   server   no server streaming: the dedicated server keeps the whole map, which placing content on the ground and
 *            the street people's walks across it need;
 *   actors   which actors stream (MarburgumPartition::ShouldStream); the rest are always loaded.
 *
 * -Ground changes none of that and instead writes the ground files (MarburgumGroundFile.h) of the street lamps, the plants
 * and the street furniture: where each stands, found on the whole level by the game's own rules, since a client holds
 * only the cells round its player. Run it after a placement file or the ground changes.
 *
 * Logs "MB2PARTITION ..." and saves unless -CheckOnly. Returns nonzero if the map is not partitioned or a setting
 * could not be made.
 */
UCLASS()
class UMarburgumPartitionLevelCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UMarburgumPartitionLevelCommandlet();
    virtual int32 Main(const FString& Params) override;
};
