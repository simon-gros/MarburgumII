#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MarburgumCombatContentCommandlet.generated.h"

/**
 * Builds the content fighting needs (ADR-041).
 *
 *   UnrealEditor-Cmd MarburgumII.uproject -run=MarburgumCombatContent -Steps=animation,audio
 *
 * animation  /Game/Characters/Animations/A_FightGuard, A_FightJab and A_FightHeavy on the mannequin skeleton:
 *            the original game's own guard loop, jab and committed swing (tools/build_character.gd,
 *            _guard_animation and _fight_animation), carried key for key at the original's key times.
 * audio      /Game/Audio/Combat: Kenney Impact Sounds 1.0 (CC0) from SourceArt/Audio/Combat/KenneyImpactSounds,
 *            imported under their own file names; the combat contract says which plays for what.
 */
UCLASS()
class UMarburgumCombatContentCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UMarburgumCombatContentCommandlet();
    virtual int32 Main(const FString& Params) override;
};
