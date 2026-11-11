#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MarburgumCombatTypes.h"
#include "MarburgumFighter.generated.h"

class AMarburgumCharacter;

namespace MarburgumII::Combat
{
    struct FNpcDefinition;
}

/** What a fighting NPC's body is doing (the original's combat states), for evidence and presentation. */
UENUM()
enum class EMarburgumFighterState : uint8
{
    Idle,
    Hostile,
    Staggered,
    Incapacitated,
};

/** What an NPC is doing with its feet inside those states (the original's intents). */
UENUM()
enum class EMarburgumFighterIntent : uint8
{
    Wait,
    Wander,
    Watch,
    Avoid,
    Return,
    Close,
    Strike,
};

/**
 * One of the original's people who can be in a fight (ADR-041): the Lent drinker who answers violence, and the
 * passers-by who step away from it. Spawned by the game mode at the original's spot, from the shared combat
 * contract (MarburgumII::Combat::Npcs).
 *
 * The server runs the original's NPC rules (world_simulation.cpp tick_combat) ten times a second: a hostile
 * fights whoever hit it, and only them, until they are down or have led it further from home than its pursuit
 * range; it closes in, steps back out of the other body, throws a jab every attack interval and stands with its
 * hands up between blows. A civilian never fights; it steps away from a hostile that is fighting, but no further
 * from home than the avoid limit. Otherwise everyone turns to watch a player close by, goes home when nobody is,
 * and strolls around home on the original's schedule. Blows take vitality; one with none is down, out of the
 * fight and not a target. Marburgum II gets it back up at home after NpcRecoverySeconds.
 *
 * The body is the template mannequin, tinted with the person's colours from npc_visual_profiles.json and moved by
 * the engine's character movement, which replicates it. Swings, the guard between blows and the fall play from
 * a replicated fight pose (FMarburgumFightAnimator).
 */
UCLASS()
class MARBURGUMII_API AMarburgumFighter : public ACharacter
{
    GENERATED_BODY()

public:
    AMarburgumFighter();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    /** The contract id (lent_drunk, ...); set before the actor finishes spawning. */
    UPROPERTY(EditAnywhere, Replicated, Category = "Marburgum|Fighter")
    FName NpcId;

    FName GetNpcId() const { return NpcId; }
    FString GetDisplayName() const;
    /** Whether it answers violence by fighting: the cast's hostile role, or a street person who chose to. */
    virtual bool IsHostileRole() const;
    /** The person's numbers and looks; null for an unknown id. */
    virtual const MarburgumII::Combat::FNpcDefinition* GetDefinition() const;
    bool IsDown() const { return FightPose.bDown; }
    uint8 GetVitalityPoints() const { return VitalityPoints; }
    EMarburgumFighterState GetFighterState() const { return State; }
    EMarburgumFighterIntent GetIntent() const { return Intent; }
    /** Server: the player this NPC is fighting, if any. */
    AMarburgumCharacter* GetTarget() const { return Target.Get(); }

    /**
     * Server only: a player's blow lands (apply_damage, never guarded: NPCs do not guard). A hostile that is still
     * standing turns on whoever threw it. Returns the outcome word.
     */
    FName ReceiveBlowAuthority(AMarburgumCharacter* Attacker, float Damage, float StaggerSeconds, bool bHeavy);

    /**
     * Server only: a car struck this body (ADR-044). It loses Damage vitality and goes down; with some left it
     * gets up again after DownSeconds where it lies and carries on, with none it is out like a beaten fighter.
     */
    void KnockDownAuthority(AActor* Vehicle, float Damage, float DownSeconds);

    /** Server: on the floor, out of the fight or knocked down by a car. */
    bool IsIncapacitatedAuthority() const { return State == EMarburgumFighterState::Incapacitated; }
    /** Server: world seconds since it went down (meaningful while incapacitated). */
    double GetDownSinceSeconds() const { return DownSinceSeconds; }

    /** Everyone: a blow landed on this body; play its sound here. */
    UFUNCTION(NetMulticast, Unreliable)
    void MulticastImpact(FVector_NetQuantize At, FName Outcome, bool bHeavy, bool bFloored);

    /** Presentation evidence: the fight clip this body is playing (guard, jab, heavy, down), or None. */
    FName GetPlayingFightClip() const;

    static const TCHAR* StateName(EMarburgumFighterState Value);
    static const TCHAR* IntentName(EMarburgumFighterIntent Value);

protected:
    /** One of the original's 100 ms ticks for this NPC. */
    virtual void Think();
    void ThinkHostile(AMarburgumCharacter& Person);
    void SetStateAndIntent(EMarburgumFighterState NewState, EMarburgumFighterIntent NewIntent);
    virtual void GiveUp();
    void Strike(AMarburgumCharacter& Person);
    /** Up again after RecoverRemaining: whole and at home after a beating, where it lies after a knock-down. */
    virtual void Recover();
    /** Server: a player's blow landed and the body is still standing (bFloored false) or not. */
    virtual void OnBlowLanded(AMarburgumCharacter* Attacker, bool bFloored);
    /** Server: the body goes down: off its feet, out of everyone's way, not a target. */
    void FallDown(float Seconds);
    /**
     * Walk toward Goal until the next tick says otherwise; Stop() stands still. A step that would take the body
     * from dry ground into the water is not taken (ADR-046): it stands at the edge with bWaterAhead set.
     */
    void WalkTo(const FVector& Goal);
    void Stop();
    /** Where the feet are. */
    FVector FeetLocation() const;
    /** The straight way from here to Goal's plan position keeps out of the water (always, once swimming). */
    bool IsDryWay(const FVector& Goal) const;
    /**
     * A goal StepCm away, in Direction or as little off it as keeps the way dry (up to 150 degrees either side).
     * False when every way is wet.
     */
    bool FindDryStep(const FVector& Direction, double StepCm, FVector& OutGoal) const;
    void FaceToward(const FVector& Point);
    bool IsWalled(const AActor& Other) const;
    void Tint();
    /** Called once the stats are known (BeginPlay on the server). */
    void ApplyStats();
    virtual void LogEvent(const TCHAR* Event, const FString& Detail) const;

    // ---- Replicated ----

    UPROPERTY(Replicated)
    FMarburgumFightPose FightPose;

    UPROPERTY(Replicated)
    uint8 VitalityPoints = 0;

    UPROPERTY(Replicated)
    EMarburgumFighterState State = EMarburgumFighterState::Idle;

    UPROPERTY(Replicated)
    EMarburgumFighterIntent Intent = EMarburgumFighterIntent::Wait;

    // ---- Server ----

    TWeakObjectPtr<AMarburgumCharacter> Target;
    FVector Home = FVector::ZeroVector;
    double HomeYaw = 0.0;
    uint32 IdHash = 0;
    float Vitality = 0.f;
    float MaxVitality = 0.f;
    float Damage = 0.f;
    float ReachCm = 0.f;
    float PursuitRangeCm = 0.f;
    int64 AttackIntervalTicks = 1;
    float ThinkAccumulator = 0.f;
    int64 TickCount = 0;
    int64 StaggerUntilTick = 0;
    int64 SwingUntilTick = 0;
    int64 NextAttackTick = 0;
    /** 0 until the first pause is scheduled (the original's goal_until). */
    int64 GoalUntilTick = 0;
    FVector WanderGoal = FVector::ZeroVector;
    int32 Stuck = 0;
    FVector LastThinkLocation = FVector::ZeroVector;
    /** The walk asked for last tick covered almost none of its stride (the original's refused step). */
    bool bLastStepFailed = false;
    bool bWalking = false;
    FVector WalkGoal = FVector::ZeroVector;
    /** Walking was held back at the water's edge on the last frame. */
    bool bWaterAhead = false;
    bool bHasFacing = false;
    double FacingYaw = 0.0;
    float RecoverRemaining = 0.f;
    /** Down from a car, not beaten: gets up where it lies. */
    bool bKnockedDown = false;
    /** Where the car that knocked it down was. */
    FVector KnockedFrom = FVector::ZeroVector;
    double DownSinceSeconds = 0.0;

    // ---- Presentation ----

    FMarburgumFightAnimator FightAnimator;
};
