#pragma once

#include "CoreMinimal.h"

class UAnimSequence;

/**
 * Carries one of the original game's procedural character clips (magna-raedarum-marburgum
 * tools/build_character.gd) onto the UE5 mannequin, key for key (ADR-039). The vehicle door
 * clips and the fight clips (ADR-041) use it; the swim commandlet (ADR-032, ADR-033, ADR-036) still carries its own copy of the same steps.
 *
 * The original keys model-space Euler angles in radians on its Godot humanoid: Y up, the body faces -Z, a
 * positive X rotation swings a limb forward and a positive Z rotation lifts the left arm out to the side.
 * Godot composes them Y, then X, then Z (Basis.from_euler, EULER_ORDER_YXZ), and each bone's turn applies
 * after its parent's (_bone_pose). The mannequin's reference pose is an A-pose with bent elbows, so each limb
 * is first turned onto the original's rest direction and the clip is applied from there.
 */
namespace MarburgumGodotClip
{
    /**
     * One Godot bone's keys, evenly spaced over the clip unless the clip lists KeyTimes; a looping clip's last
     * key equals its first.
     */
    struct FTrack
    {
        const TCHAR* GodotBone;
        TArray<FVector> Keys;
    };

    struct FClip
    {
        /** Long package name, e.g. /Game/Characters/Animations/A_Swim. */
        const TCHAR* Package;
        /** Transaction title in the animation model. */
        const TCHAR* Title;
        double LengthSeconds = 1.0;
        bool bLoop = false;
        TArray<FTrack> Tracks;
        /** Displacement of the hips from rest, Godot model-space metres, evenly spaced keys (may be empty). */
        TArray<FVector> HipsOffsets;
        /**
         * Seconds of each key, shared by every track, when they are not evenly spaced (the fight clips key at
         * 0, 28 % or 32 %, 50 % or 55 % and the end). Empty means evenly spaced. Hips offsets stay even.
         */
        TArray<double> KeyTimes;
    };

    inline constexpr int32 FramesPerSecond = 30;

    /** _shift(): the same loop started Steps keys later (the last key repeats the first). */
    TArray<FVector> Shift(const TArray<FVector>& Values, int32 Steps);
    /** _mirror(): the same motion on the other side of the body. */
    TArray<FVector> Mirror(TArray<FVector> Values);
    /** _cycle(values, Vector3.RIGHT): an X rotation per key, looping back to the first. */
    TArray<FVector> Cycle(std::initializer_list<double> Values);

    /**
     * Godot's cubic track sampled at T seconds: Catmull-Rom through the evenly spaced keys, wrapping round a
     * loop and holding the end keys of a one-shot.
     */
    FVector Sample(const TArray<FVector>& Keys, double T, double LengthSeconds, bool bLoop);

    /**
     * Godot's cubic track over keys at the given times (Animation::_interpolate with cubic_interpolate_in_time,
     * a one-shot: the end keys stand in for the missing neighbours, with no time between them).
     */
    FVector SampleTimed(const TArray<FVector>& Keys, const TArray<double>& Times, double T);

    /**
     * Keys ik_foot_l and ik_foot_r onto the feet the clip draws (ADR-042). The template animation blueprint's
     * foot IK (CR_Mannequin_FootIK) runs after the montage slot and pulls each foot to its IK bone; a clip
     * that does not key those bones leaves them at the reference pose, under a standing body, so the legs
     * are dragged down to a standing stance whatever the clip does. Positions, Rotations and Scales hold
     * the clip's local keys per bone, Frames + 1 of each; a bone without keys is at its reference pose.
     * Returns false (after logging under LogTag) when the skeleton lacks the bones.
     */
    bool FollowFeetWithIkBones(const FReferenceSkeleton& Reference, int32 Frames, TMap<FName, TArray<FVector>>& Positions,
        TMap<FName, TArray<FQuat>>& Rotations, TMap<FName, TArray<FVector>>& Scales, const TCHAR* LogTag);

    /**
     * Builds the clip on SK_Mannequin, with the IK foot bones following the drawn feet (FollowFeetWithIkBones,
     * ADR-043), and saves it; nullptr (after logging under LogTag) on failure.
     */
    UAnimSequence* Build(const FClip& Clip, const TCHAR* LogTag);

    /**
     * Evidence: limb directions read back from a saved clip at the given times, in the original's model axes
     * (x right, y up, z back), to compare with the same directions computed from the original's own clip.
     */
    void LogLimbCheck(const UAnimSequence& Sequence, const TArray<double>& Times, const TCHAR* LogTag);
}
