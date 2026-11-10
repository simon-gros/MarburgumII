#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MarburgumRebuildFacadesCommandlet.generated.h"

/**
 * Rebuilds every Lent building façade in place.
 *
 *   UnrealEditor-Cmd MarburgumII.uproject -run=MarburgumRebuildFacades
 *
 * The original wall and roof triangles stay a closed shell. Windows and the door are recesses
 * with jambs and glass, so they do not open a hole through the building. A wall whose base is
 * above the landscape is extended down to it along the same bottom edge.
 */
UCLASS()
class UMarburgumRebuildFacadesCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UMarburgumRebuildFacadesCommandlet();
    virtual int32 Main(const FString& Params) override;
};
