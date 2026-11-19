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
    /** Going to, or at, a place that draws people in (ADR-069): a bus stop, a venue's door, a sight. */
    Visit,
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
 * Reaching a node by a bus stop, a café, a shop or a sight that is open (ADR-069), they may go to it, by their group's
 * visit chance for that kind of place times how open it is: they wait at the stop facing the road, or look at the sight
 * from a little way off, and walk on; or they go in at the door and leave the street.
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
    /** Server: the place they are going to or at (FMarburgumWalkGraph::GetAttractors()), and their spot there; else INDEX_NONE. */
    int32 GetAttractor() const { return Mood == EMarburgumStreetMood::Visit ? Attractor : INDEX_NONE; }
    int32 GetAttractorSlot() const { return AttractorSlot; }
    /** Server: went in at a door, off the street. The street takes them away. */
    bool HasGoneIn() const { return bGoneIn; }
    /** Server, before anyone sees them: already at a place, at this spot, part of the way through their visit. */
    void StartAt(int32 InAttractor, int32 Slot, double Now);
    /** Server, before anyone sees them: just out of a door, walking to the place's node. */
    void ComeOutAt(int32 InAttractor);

    /**
     * Server, smoke tests only (-MB2StreetProbe): stand still watching At for Seconds, jumping clear of cars or
     * not, with this much vitality.
     */
    void MakeProbe(const FVector& At, double Seconds, bool bDodges, float InVitality);
    /** Server, smoke tests only (ADR-046): run off the graph, straight away from From, for Seconds. */
    void MakeWaterProbe(const FVector& From, double Seconds);

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
    /** At a node: a place there that is open may draw them in (ADR-069). True when it did. */
    bool TryVisit(int32 Node, double Now);
    void BeginVisit(int32 InAttractor, int32 Slot, double Now);
    void StepVisit(double Now);

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
    /** The edge already reversed for a fight or the river. A second block holds instead of shuffling. */
    int32 ReversedEdge = INDEX_NONE;
    bool bGatherArrived = false;
    /** Fleeing with no graph under them: straight away from the threat. */
    bool bFleeOffGraph = false;
    double NextBumpSeconds = 0.0;
    FVector LastProgressLocation = FVector::ZeroVector;
    /** The place they are visiting, their spot at it, and whether they are there. */
    int32 Attractor = INDEX_NONE;
    int32 AttractorSlot = 0;
    bool bAtAttractor = false;
    bool bGoneIn = false;
    /** The node they last rolled for a place at, and the place they were last at: neither again straight away. */
    int32 LastVisitNode = INDEX_NONE;
    int32 LastAttractor = INDEX_NONE;

    /** Built from the replicated indices on every machine (GetDefinition). */
    mutable MarburgumII::Combat::FNpcDefinition StreetDefinition = {};
    mutable bool bDefinitionBuilt = false;
};
