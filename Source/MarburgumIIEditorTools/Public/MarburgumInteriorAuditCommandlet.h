#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MarburgumInteriorAuditCommandlet.generated.h"

/**
 * Read-only audit of every building's ground floor (ADR-076) in the real level, against its real collision:
 *
 *   UnrealEditor-Cmd MarburgumII.uproject -run=MarburgumInteriorAudit [-Map=/Game/World/lent] [-Out=<dir>] [-Only=<id,id>]
 *
 * For each of the city's footprints: whether it has a shell in the level, its layout or the reason it has none,
 * and for each eligible one:
 *
 * - entrance as built: a player's capsule swept from the street through the door against the level's static
 *   collision (the game's own probe, UMarburgumInteriorSubsystem::ProbeEntrance), and the step from the street;
 * - the same with the building's own shell ignored, which says whether cutting the door would be enough;
 * - what else stands in each room (ground, terrain, another building, a prop) between floor and ceiling;
 * - a walk with the rooms built: street, doorway, vestibule, every room and back out, each leg a capsule sweep,
 *   with the leaves open; and whether each closed and fully open leaf is clear of everything but its own walls.
 *
 * Writes buildings.tsv, rooms.tsv and summary.txt to -Out and logs "MB2INTERIOR_AUDIT ..." lines. Spawns the
 * rooms in the loaded editor world for the walk and removes them; never saves a package.
 */
UCLASS()
class UMarburgumInteriorAuditCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UMarburgumInteriorAuditCommandlet();
    virtual int32 Main(const FString& Params) override;
};
