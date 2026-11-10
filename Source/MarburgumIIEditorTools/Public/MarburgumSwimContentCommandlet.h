#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MarburgumSwimContentCommandlet.generated.h"

/**
 * Builds the content swimming needs (ADR-032).
 *
 *   UnrealEditor-Cmd MarburgumII.uproject -run=MarburgumSwimContent -Steps=animation,water
 *
 * animation  /Game/Characters/Animations/A_Swim on the mannequin skeleton: the original game's swim
 *            cycle (tools/build_character.gd, _swim_animation), key for key, carried from the
 *            original Godot humanoid onto the UE5 mannequin.
 * water      water volumes over the Drava in /Game/World/lent, cut from the level's water mesh, with
 *            their tops at its surface, so the movement component knows where a body swims.
 * inspect    prints the water mesh, the landscape and existing physics volumes of /Game/World/lent.
 */
UCLASS()
class UMarburgumSwimContentCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UMarburgumSwimContentCommandlet();
    virtual int32 Main(const FString& Params) override;
};
