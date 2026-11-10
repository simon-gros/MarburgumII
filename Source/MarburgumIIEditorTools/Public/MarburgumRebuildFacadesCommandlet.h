#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MarburgumRebuildFacadesCommandlet.generated.h"

/**
 * Rebuilds every Lent building in place as one closed shell.
 *
 *   UnrealEditor-Cmd MarburgumII.uproject -run=MarburgumRebuildFacades [-CheckOnly]
 *
 * Each footprint becomes a closed solid (MarburgumBuildingMesher): walls, soffit and fascia, and a
 * straight-skeleton roof whose faces start on the wall lines. Gable ends and walls shared with a
 * neighbour stay vertical. Every shell must pass the closed-mesh check or the run fails without
 * saving it. -CheckOnly builds and checks every building without saving anything. Footprints under
 * 30 square metres, with fewer than four corners, or that cannot be kept simple are removed from
 * the level.
 */
UCLASS()
class UMarburgumRebuildFacadesCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UMarburgumRebuildFacadesCommandlet();
    virtual int32 Main(const FString& Params) override;
};
