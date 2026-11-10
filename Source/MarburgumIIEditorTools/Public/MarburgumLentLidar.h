#pragma once

#include "CoreMinimal.h"

class UStaticMesh;

namespace MarburgumMaps
{
    /**
     * The original Lent building mesh: OSM footprints and LiDAR eaves/ridge from
     * Data/World/lent_buildings.json, seated on the promenade. Not a TRELLIS mesh.
     * On failure Mesh is null and Error says why.
     */
    struct FLentLidarMesh
    {
        UStaticMesh* Mesh = nullptr;
        int32 Buildings = 0;
        FString Error;
    };

    FLentLidarMesh BuildLentLidarMesh();
}
