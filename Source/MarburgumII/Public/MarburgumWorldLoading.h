#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * The server's whole World Partition map (ADR-091). /Game/World/lent streams on clients, but the server keeps every
 * cell (no server streaming), and the street people, the lamps and the authored content find their ground by tracing
 * at begin play. The cells only start to load once the world is running, and world subsystems begin play in no
 * promised order, so each of them calls LoadWholeWorldOnServer first.
 */
namespace MarburgumII::WorldLoading
{
    /**
     * On a server (dedicated or listen) running a World Partition map without server streaming, blocks until every
     * cell is in; elsewhere returns at once. Cheap once the cells are in. Logs "MB2WORLD ... event=whole_world" with
     * Caller and how long it waited.
     */
    MARBURGUMII_API void LoadWholeWorldOnServer(UWorld& World, const TCHAR* Caller);
}
