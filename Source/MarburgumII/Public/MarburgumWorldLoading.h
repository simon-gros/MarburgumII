#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class UWorld;

/**
 * The server's whole World Partition map (ADR-091). /Game/World/lent streams on clients, but the server keeps every
 * cell (no server streaming), and the street people, the lamps, the plants, the street furniture and the authored
 * content find their ground by tracing at begin play. The cells only start to load once the world is running, and
 * world subsystems begin play in no promised order, so each of them calls LoadWholeWorldOnServer first.
 */
namespace MarburgumII::WorldLoading
{
    /**
     * On a server (dedicated or listen) running a World Partition map without server streaming, blocks until every
     * cell is in; elsewhere returns at once. Cheap once the cells are in. Logs "MB2WORLD ... event=whole_world" with
     * Caller and how long it waited.
     */
    MARBURGUMII_API void LoadWholeWorldOnServer(UWorld& World, const TCHAR* Caller);

    /** Where a thing surveyed at a point stands, by its own rule; unset where the world has no ground there. */
    using FFindGround = TFunctionRef<TOptional<double>(const UWorld& World, const FVector& Surveyed)>;

    /**
     * The ground under each of Points, the items of the placement file PlacementFile (relative to the Content directory)
     * whose text is PlacementText. A client holds only the cells round its player, so this is taken from the editor's
     * ground file (MarburgumGroundFile.h) when it was made for this text; otherwise by FindGround on the whole map,
     * loaded first on a server. A World Partition map without a matching ground file logs "MB2WORLD ...
     * event=no_ground_file". bOutFromFile says which way it went.
     */
    MARBURGUMII_API TArray<TOptional<double>> GroundOf(UWorld& World, const TCHAR* PlacementFile, const FString& PlacementText,
        const TArray<FVector>& Points, FFindGround FindGround, const TCHAR* Caller, bool& bOutFromFile);
}
