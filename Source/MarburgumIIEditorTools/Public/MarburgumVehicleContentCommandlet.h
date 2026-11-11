#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MarburgumVehicleContentCommandlet.generated.h"

/**
 * Builds the content vehicles need (ADR-039).
 *
 *   UnrealEditor-Cmd MarburgumII.uproject -run=MarburgumVehicleContent -Steps=mesh,animation,place
 *
 * mesh       /Game/Vehicles/<definition>/: the bodywork (SM_<id>_Body, with Door0..3 hinge sockets), four
 *            wheels centred on their hubs and the side doors, for every definition in
 *            Data/Gameplay/vehicles.v0.1.json. A port of the original's src/shared/vehicle_mesh.cpp: the same
 *            side-elevation profile, glasshouse, bumpers, lamps, blank plates and steel wheels, with each
 *            part's colour, roughness and metalness in the vertex data, painted through
 *            /Game/Vehicles/Materials/M_Vehicle and one MI_<id> per definition.
 * animation  /Game/Characters/Animations/A_EnterVehicle and A_ExitVehicle on the mannequin skeleton: the
 *            original's procedural door clips (tools/build_character.gd, _vehicle_animation).
 * place      the authored lent_hatchback in /Game/World/lent: at the original's position if that is clear,
 *            level carriageway (ADR-040), otherwise on the nearest such spot to it, parked along the river.
 */
UCLASS()
class UMarburgumVehicleContentCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UMarburgumVehicleContentCommandlet();
    virtual int32 Main(const FString& Params) override;
};
