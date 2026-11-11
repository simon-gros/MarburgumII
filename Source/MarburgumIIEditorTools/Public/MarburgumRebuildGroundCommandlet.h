#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MarburgumRebuildGroundCommandlet.generated.h"

/**
 * Rebuilds the ground of /Game/World/lent from Data/World/lent_ground (ADR-040).
 *
 *   UnrealEditor-Cmd MarburgumII.uproject -run=MarburgumRebuildGround -nullrhi [-Verify [-Every=N]]
 *
 * The data comes from tools/lent-ground: bare-earth heights from the ARSO 2014 LiDAR survey, road,
 * kerb, pavement and marking meshes from OpenStreetMap, and surface weights for the landscape.
 * The commandlet imports the CC0 ground textures, builds the paved and terrain materials, replaces
 * the landscape and the old flat road sheet, adds the paved meshes with collision, and moves the
 * player starts, the angler and the street signs by however much the ground under them moved.
 * Run `-run=MarburgumSwimContent -Steps=water` afterwards: the river volumes follow the new bed.
 * -Verify changes nothing: it drops a character-sized capsule onto every Nth paved triangle of the
 * saved level and fails if it lands on the landscape, misses, or sinks more than 1 cm into the
 * paving.
 */
UCLASS()
class UMarburgumRebuildGroundCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UMarburgumRebuildGroundCommandlet();
    virtual int32 Main(const FString& Params) override;
};
