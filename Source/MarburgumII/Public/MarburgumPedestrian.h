#pragma once

#include "CoreMinimal.h"
#include "MarburgumCombatContract.h"
#include "MarburgumFighter.h"
#include "MarburgumStreetContract.h"
#include "MarburgumPedestrian.generated.h"

class UMarburgumStreetSubsystem;
struct FMarburgumStreetIncident;

/** What a street person is doing (ADR-044); the wire words are MarburgumII::Street::Intent*. */
UENUM()
enum class EMarburgumStreetMood : uint8
{
    Walk,
    Flee,
    Watch,
    Gather,
    Dodge,
    Fight,
    Down,
};

/**
 * One of the people on the street (ADR-044): the original's ambient pedestrian, carried into Marburgum II as a
 * fighter body that walks the walk graph. UMarburgumStreetSubsystem puts them on the pavement out of sight and
 * takes them away again; the rest is theirs.
 *
 * On the server, ten times a second, they follow the graph's edges at their own pace, keeping to the right and
 * picking at every branch the way closest to the one they were going, which drifts slowly. They jump clear of a
 * car about to hit them. A fight, a run-over or someone lying in the street close by is answered from their
 * personality's table (MarburgumII::Street::Decisions): they run along the graph away from it, stop and watch it
 * from a little way off, walk over to the body and stand by it, or carry on, though nobody walks into a fight.
 * Someone already running from or watching a fight scares the people near them into answering the same fight.
 * A player walking into them, or hitting them, is answered the same way; the ones who fight back become hostile
 * fighters against that player (the fighter rules of ADR-041), and walk on once it is over.
 *
 * A car strikes them as it does the Lent cast (KnockDownAuthority): down, then up and running from where it
 * happened, or out for good if it was fast enough, until nobody is about to see them taken away.
 *
 * Their looks are a population group's (npc_visual_profiles.json colours on the mannequin), replicated as indices.
 */
UCLASS()
class MARBURGUMII_API AMarburgumPedestrian : public AMarburgumFighter
{
    GENERATED_BODY()

public:
    AMarburgumPedestrian();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual bool IsHostileRole() const override;
    virtual const MarburgumII::Combat::FNpcDefinition* GetDefinition() const override;

    /** Server, before FinishSpawning: who they are and the edge they start on, walking from StartNode. */
    void InitStreet(UMarburgumStreetSubsystem& Street, int32 InSerial, int32 InGroup, int32 InLook, MarburgumII::Street::EPersonality InPersonality,
        float InPaceMetresPerSecond, int32 StartEdge, int32 StartNode, uint32 Seed);

    int32 GetSerial() const { return Serial; }
    int32 GetGroupIndex() const { return GroupIndex; }
    int32 GetLookIndex() const { return LookIndex; }
    MarburgumII::Street::EPersonality GetPersonality() const { return Personality; }
    EMarburgumStreetMood GetMood() const { return Mood; }
    static const TCHAR* MoodIntent(EMarburgumStreetMood Value);

    /** Server: running from or watching a fight or run-over, which spreads to the people round them. */
    bool IsSpreadingAlarm() const;
    /** Server: the incident that alarm is about. */
    int32 GetAlarmIncident() const { return AnsweredIncident; }
    /** Server: out (vitality gone) rather than merely knocked down. */
    bool IsOut() const { return IsIncapacitatedAuthority() && (bOut || GetVitalityPoints() == 0); }
    /** Server: walking into walls, cut off the graph. Nobody minds when they go. */
    bool IsLost() const { return bLost; }

    /**
     * Server, smoke tests only (-MB2StreetProbe): stand still watching At for Seconds, jumping clear of cars or
     * not, with this much vitality.
     */
    void MakeProbe(const FVector& At, double Seconds, bool bDodges, float InVitality);

    /** Server: when some player last had them in view (UMarburgumStreetSubsystem keeps it). */
    double LastSeenSeconds = -1000.0;

protected:
    virtual void Think() override;
    virtual void Recover() override;
    virtual void OnBlowLanded(AMarburgumCharacter* Attacker, bool bFloored) override;
    virtual void GiveUp() override;
    virtual void LogEvent(const TCHAR* Event, const FString& Detail) const override;

private:
    // ---- Senses ----
    bool SenseCars(double Now);
    bool SenseIncidents(double Now);
    bool SenseBumps(double Now);
    /** Answers one incident from the table; true when it changed what they do. */
    bool Answer(const FMarburgumStreetIncident& Incident, MarburgumII::Street::EEvent Event, double Now, bool bSpread);

    // ---- Moods ----
    void SetMood(EMarburgumStreetMood NewMood, double Until);
    void BeginFlee(const FVector& From, double Now, double Seconds);
    void BeginWatch(const FVector& At, double Now);
    void BeginGather(const FVector& Body, double Now);
    void EndMood();
    void StepWalk(double Now, double DeltaSeconds);
    void StepFlee(double Now);
    void StepWatch(double Now);
    void StepGather(double Now);

    // ---- The graph ----
    /** Picks the next edge at Node: the way closest to Heading, or away from Threat when fleeing. */
    void TakeNextEdge(int32 Node, bool bFleeing);
    /** Back onto the graph from wherever they are: the nearest node they can walk to. */
    bool Rejoin();
    FVector EdgeGoal() const;
    /** Whether a point is in the clear zone of a fight going on now. */
    bool InFight(const FVector& Point) const;
    UMarburgumStreetSubsystem* Street() const;
    void SetPace(float CentimetresPerSecond);
    float RandomUnit();

    // ---- Replicated: who they are ----

    UPROPERTY(Replicated)
    uint8 GroupIndex = 0;

    UPROPERTY(Replicated)
    uint8 LookIndex = 0;

    /** Small per-person variation of the look's colours and size, so two of a look are not twins. */
    UPROPERTY(Replicated)
    uint8 Variation = 0;

    UPROPERTY(Replicated)
    EMarburgumStreetMood Mood = EMarburgumStreetMood::Walk;

    // ---- Server ----

    TWeakObjectPtr<UMarburgumStreetSubsystem> StreetRef;
    int32 Serial = 0;
    MarburgumII::Street::EPersonality Personality = MarburgumII::Street::EPersonality::Norm;
    float WalkPaceCm = 130.f;
    float FleePaceCm = 400.f;
    FRandomStream Random;
    /** Walking: the edge, and the node they are walking toward. */
    int32 Edge = INDEX_NONE;
    int32 ToNode = INDEX_NONE;
    /** Off the graph, walking to this node to get back on. */
    bool bRejoining = false;
    double HeadingYaw = 0.0;
    double MoodUntil = 0.0;
    FVector Threat = FVector::ZeroVector;
    FVector GatherSpot = FVector::ZeroVector;
    /** The incident being answered, and how loud it was. */
    int32 AnsweredIncident = INDEX_NONE;
    int32 AnsweredLoudness = 0;
    MarburgumII::Street::EEvent AnsweredEvent = MarburgumII::Street::EEvent::Count;
    /** Incidents already rolled for: never twice. */
    TSet<int32> Rolled;
    /** Answering an incident with "carry on": no new roll for it, but still not into the fight. */
    bool bUnbothered = false;
    bool bAnswersViolence = false;
    bool bOut = false;
    bool bLost = false;
    /** A smoke-test probe that does not jump clear of cars. */
    bool bNeverDodges = false;
    double LastProgressSeconds = 0.0;
    int32 TurnBacks = 0;
    bool bGatherArrived = false;
    /** Fleeing with no graph under them: straight away from the threat. */
    bool bFleeOffGraph = false;
    double NextBumpSeconds = 0.0;
    FVector LastProgressLocation = FVector::ZeroVector;

    /** Built from the replicated indices on every machine (GetDefinition). */
    mutable MarburgumII::Combat::FNpcDefinition StreetDefinition = {};
    mutable bool bDefinitionBuilt = false;
};
