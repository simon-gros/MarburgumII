#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MarburgumNpcContentCommandlet.generated.h"

/**
 * Builds what an authored NPC's body needs beyond the template mannequin (ADR-081).
 *
 *   UnrealEditor-Cmd MarburgumII.uproject -run=MarburgumNpcContent -Steps=rod,animation
 *
 * rod        /Game/Characters/Props/SM_FishingRod: a 2.7 m two-piece spinning rod of the period (cork grips, screw reel
 *            seat, varnished glass blank, seven guides, a fixed-spool reel), built from code on M_Vehicle, origin at the
 *            grip, with a `tip` socket for the line.
 * animation  The fishing_rod_r socket on the mannequin skeleton, placed from the hold pose so the blank leaves the closed
 *            right hand forward and up, and /Game/Characters/Animations/A_FishHold, A_FishCast, A_FishWait, A_FishReel
 *            and A_NpcTalk, keyed in the original's convention (MarburgumGodotClip). The left arm is not keyed by eye:
 *            the right elbow is brought in until the left palm can rest on the reel handle, then for every key the left
 *            arm is searched so the palm lands on the handle (or the rod butt while casting) where the right hand has
 *            put the rod.
 */
UCLASS()
class UMarburgumNpcContentCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UMarburgumNpcContentCommandlet();
    virtual int32 Main(const FString& Params) override;
};
