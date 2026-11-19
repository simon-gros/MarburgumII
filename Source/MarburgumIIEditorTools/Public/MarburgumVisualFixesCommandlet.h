#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MarburgumVisualFixesCommandlet.generated.h"

/**
 * Idempotent corrections from the visual audit (Docs/VISUAL_QUALITY_BACKLOG.md) to the opening content of
 * /Game/World/lent, which the ground and façade rebuilds (ADR-040, ADR-045, ADR-053, ADR-054) left out of step:
 *
 *   UnrealEditor-Cmd MarburgumII.uproject -run=MarburgumVisualFixes [-Steps=font,signs,people,starts] [-DryRun]
 *
 *   font    /Game/Marburgum/Fonts/F_SignRoboto: an offline distance-field font from the engine's own Roboto Bold with
 *           Latin-1 and Latin Extended-A, so Slovene names (č, š, ž) on placed signs draw instead of a box. Every
 *           text sign in Marburgum/Opening uses it.
 *   signs   each shop sign goes onto the façade of the building in front of it: 3 cm proud of the wall, facing out,
 *           3.4 m above the street there, slid along the wall until the whole text is on one flat stretch of it and
 *           nothing stands between it and a passer-by 8 m out.
 *   people  a placed person (MB2Npc_*) stands with its feet on the first surface under it.
 *   starts  a player start more than 1.5 m above the surface under it goes 1 m above it, like the others.
 *
 * Logs "MB2FIX ..." per change and saves the map (and the font) unless -DryRun. Returns nonzero if a requested
 * step could not be carried out.
 */
UCLASS()
class UMarburgumVisualFixesCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UMarburgumVisualFixesCommandlet();
    virtual int32 Main(const FString& Params) override;
};
