#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MarburgumRebuildFacadesCommandlet.generated.h"

/**
 * Rebuilds every building of Lent and its eight neighbouring windows as one closed shell (ADR-034,
 * ADR-045), from Data/World/lent_ground/buildings.json (`lent-ground merge-buildings`).
 *
 *   UnrealEditor-Cmd MarburgumII.uproject -run=MarburgumRebuildFacades [-CheckOnly | -Only=<id> | -Inspect=<id> [-Window=<w>]]
 *
 * Each footprint becomes a closed solid (MarburgumBuildingMesher): walls, soffit and fascia, and a
 * straight-skeleton roof whose faces start on the wall lines. Gable ends and walls shared with a
 * neighbour stay vertical. Every shell must pass the closed-mesh check or the run fails without
 * saving it. -CheckOnly builds and checks every building without saving anything. Footprints under
 * 30 square metres, with fewer than four corners, or that cannot be kept simple are removed from
 * the level. Buildings the level does not place yet are placed, with the neighbouring windows'
 * bridges and Drava sheets, and the placed ids are written to placed_buildings.txt for the ground
 * tool.
 */
UCLASS()
class UMarburgumRebuildFacadesCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UMarburgumRebuildFacadesCommandlet();
    virtual int32 Main(const FString& Params) override;
};
