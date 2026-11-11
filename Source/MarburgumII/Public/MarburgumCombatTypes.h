#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "MarburgumCombatTypes.generated.h"

class UAnimInstance;
class UAnimMontage;

/** A swing the owning client asks for in one move (ADR-041). Carried as a move flag, never as a target. */
UENUM()
enum class EMarburgumSwing : uint8
{
    None,
    Jab,
    Heavy,
};

/**
 * How the server ruled one swing, sent to the player who threw it (the original's attack() result).
 * Refused swings carry a Reason (MarburgumII::Combat::Error*); accepted ones an Outcome (hit, blocked, miss)
 * and, when somebody was there, who it was and what it did.
 */
USTRUCT()
struct FMarburgumCombatResult
{
    GENERATED_BODY()

    UPROPERTY()
    bool bOk = false;

    UPROPERTY()
    bool bHeavy = false;

    UPROPERTY()
    FName Reason;

    UPROPERTY()
    FName Outcome;

    UPROPERTY()
    FName TargetId;

    UPROPERTY()
    FString TargetName;

    UPROPERTY()
    float Damage = 0.f;

    /** The target's vitality after the blow, in whole points; 0 is on the floor. */
    UPROPERTY()
    uint8 TargetVitality = 0;
};

/**
 * What other machines need to draw a fighting body (a player seen by someone else, or an NPC): which swing it
 * last threw, whether its hands are up and whether it is on the floor. Presentation only.
 */
USTRUCT()
struct FMarburgumFightPose
{
    GENERATED_BODY()

    /** Changes on every accepted swing, so two jabs in a row are two clips. */
    UPROPERTY()
    uint8 SwingSeq = 0;

    UPROPERTY()
    bool bHeavy = false;

    UPROPERTY()
    bool bGuarding = false;

    UPROPERTY()
    bool bDown = false;

    bool operator==(const FMarburgumFightPose& Other) const
    {
        return SwingSeq == Other.SwingSeq && bHeavy == Other.bHeavy && bGuarding == Other.bGuarding && bDown == Other.bDown;
    }
};

/**
 * How a fighting body looks (ADR-041), shared by players and fighting NPCs; never runs on a dedicated server.
 * The clips play through the template animation blueprint's DefaultSlot, as swimming and the car clips do: a
 * new swing is a one-shot that may cut the last one short (the original's character_view swing edge), the guard
 * loops only while the body stands still with its hands up (the original shows walking over a guard), and a body
 * on the floor plays the fall and holds its last frame until it gets up.
 */
struct FMarburgumFightAnimator
{
    /** Bring the body's montages in line with Pose. Speed is the visible ground speed, cm/s. */
    void Update(UAnimInstance* Anim, const FMarburgumFightPose& Pose, float Speed, bool bMayGuard);

    /** Presentation evidence: the fight clip this body is playing (guard, jab, heavy, down), or None. */
    FName GetPlaying(const UAnimInstance* Anim) const;

    /** Stop everything this animator started (the body got into a car). */
    void Stop(UAnimInstance* Anim);

private:
    TWeakObjectPtr<UAnimMontage> SwingMontage;
    TWeakObjectPtr<UAnimMontage> GuardMontage;
    TWeakObjectPtr<UAnimMontage> DownMontage;
    bool bSwingHeavy = false;
    bool bSeenFirst = false;
    uint8 SeenSwingSeq = 0;
};
