#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MarburgumRebuildFacadesCommandlet.generated.h"

/**
 * Rebuilds every Lent building façade in place.
 *
 *   UnrealEditor-Cmd MarburgumII.uproject -run=MarburgumRebuildFacades
 *
 * Rebuilds every Lent building from its footprint. A nearly rectangular plan keeps a pitched
 * roof on that rectangle. A complex plan gets a roof whose eaves are the wall tops, instead of
 * a loose box roof. Footprints under 30 square metres, or with fewer than four corners, are
 * removed from the level.
 */
UCLASS()
class UMarburgumRebuildFacadesCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UMarburgumRebuildFacadesCommandlet();
    virtual int32 Main(const FString& Params) override;
};
