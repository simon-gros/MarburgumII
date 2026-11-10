#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MarburgumRebuildFacadesCommandlet.generated.h"

/**
 * Rebuilds every Lent building façade in place.
 *
 *   UnrealEditor-Cmd MarburgumII.uproject -run=MarburgumRebuildFacades
 *
 * Footprints, eaves and roofs stay. Each plaster wall quad becomes a 1990s façade: a cornice,
 * a row of windows on every floor, and one door on the building's longest wall. Meshes that
 * are already detailed are left alone, so the command can be repeated.
 */
UCLASS()
class UMarburgumRebuildFacadesCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UMarburgumRebuildFacadesCommandlet();
    virtual int32 Main(const FString& Params) override;
};
