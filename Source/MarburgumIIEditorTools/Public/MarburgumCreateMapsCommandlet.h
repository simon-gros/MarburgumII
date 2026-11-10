#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MarburgumCreateMapsCommandlet.generated.h"

/**
 * Regenerates the project's generated maps (scripts/create-maps.sh):
 *
 *   UnrealEditor-Cmd MarburgumII.uproject -run=MarburgumCreateMaps [-Maps=frontend,netsmoke,lent]
 *
 *   frontend  /Game/Maps/FrontEnd  the client's offline front-end (ADR-013)
 *   netsmoke  /Game/Maps/NetSmoke  network and Gate 1 test map with labeled fixtures
 *   lent      /Game/Maps/Lent      the Lent fallback blockout district (ADR-016), plus its flat-colour
 *                                  material instances under /Game/Blockout/Materials
 *
 * Each map is rebuilt from scratch and saved; "MB2MAP saved <path>" is logged per map. Returns nonzero
 * if any requested map could not be built.
 *
 *   -Describe=/Game/Maps/Lent   read-only: log each actor's class, transform and light settings
 *                               ("MB2DESCRIBE ..."), to compare a regenerated map with an earlier one.
 */
UCLASS()
class UMarburgumCreateMapsCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UMarburgumCreateMapsCommandlet();
    virtual int32 Main(const FString& Params) override;
};
