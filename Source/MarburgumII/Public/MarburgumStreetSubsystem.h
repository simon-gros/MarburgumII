#pragma once

#include "CoreMinimal.h"
#include "MarburgumStreetContract.h"
#include "MarburgumWalkGraph.h"
#include "Subsystems/WorldSubsystem.h"
#include "MarburgumStreetSubsystem.generated.h"

class AMarburgumCharacter;
class AMarburgumFighter;
class AMarburgumPedestrian;
class AMarburgumVehicle;

/** Something street people notice (ADR-044): a fight going on, a run-over, someone on the ground. */
struct FMarburgumStreetIncident
{
    int32 Id = INDEX_NONE;
    MarburgumII::Street::EEvent Event = MarburgumII::Street::EEvent::Fight;
    FVector Location = FVector::ZeroVector;
    /** A player is the one fighting, driving or lying there: the table's player column. */
    bool bPlayer = false;
    double UntilSeconds = 0.0;
    /** Who it is about (the fighter, the body), when there is one. */
    TWeakObjectPtr<AActor> Who;
};

/** A car moving in the street, for jumping clear of it. */
struct FMarburgumStreetMover
{
    FVector Location = FVector::ZeroVector;
    FVector Velocity = FVector::ZeroVector;
};

/** A player, as the street sees them: where they stand, which way they look, and whether they are on foot. */
struct FMarburgumStreetViewer
{
    TWeakObjectPtr<AMarburgumCharacter> Character;
    FVector Location = FVector::ZeroVector;
    FVector Velocity = FVector::ZeroVector;
    FVector2D Facing = FVector2D(1.0, 0.0);
    bool bOnFoot = true;
};

/**
 * The street population of the original Marburgum (ADR-044), on the server of a map with a walk graph (Lent):
 * people appear on the pavements round every player, out of sight, and go once nobody is near or has seen them
 * for a while (AMarburgumPedestrian is the rest). It also keeps what they notice: fights (players and fighters
 * report their swings), people on the ground, run-overs, cars on the move. And it is where cars strike people:
 * every frame, a car at 2.5 m/s or more knocks down anyone standing inside its body (swept back over the frame
 * and a little ahead), street person or Lent cast alike.
 *
 * How many are out, and who, follows the world clock and the weather (ADR-067, StreetHourAt): busy by day, thin in
 * the small hours, night owls in the late evening, an hour later on Friday and Saturday nights. It also follows the
 * street (ADR-068): each player brings the number their own district keeps at that hour, and each newcomer is one of
 * those who walk the district they appear in, on its busier streets first. Those numbers are kept apart (CrowdBudgets):
 * a quiet street is not filled with the busy one's share, and the cap of 48 is taken from the fullest streets first.
 * A player's district is held until they have walked ZoneHoldCm, so standing on a border does not retarget the crowd.
 * With more out than a street wants, people nobody is looking at go home, from the street that is over first.
 *
 * Bus stops, cafés, restaurants, bars, fast food, shops and sights draw people in (ADR-069, the walk graph's attractors),
 * by their hours: passers-by wait at a stop, look at a sight or go in at a door, and a share of the newcomers come out of
 * an open door, or are already waiting at a stop, rather than walking in from out of sight. Whoever went in is taken away.
 *
 * Off with -MB2Street=0 (or MB2_STREET=0). Logs carry MB2_STREET.
 */
UCLASS()
class MARBURGUMII_API UMarburgumStreetSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickable() const override { return bEnabled; }

    bool IsEnabled() const { return bEnabled; }
    const FMarburgumWalkGraph& GetGraph() const { return Graph; }

    /** Server: someone threw a punch here (a player's swing, hit or not, or a fighter's strike). */
    void ReportFight(AActor* Who, const FVector& At, bool bPlayer);

    const TArray<FMarburgumStreetIncident>& GetIncidents() const { return Incidents; }
    const TArray<FMarburgumStreetMover>& GetMovers() const { return Movers; }
    const TArray<FMarburgumStreetViewer>& GetViewers() const { return Viewers; }
    const FMarburgumStreetIncident* FindIncident(int32 Id) const;
    /** People spreading the alarm of an incident, as of the start of this step (one hop a step). */
    struct FAlarm
    {
        FVector Location = FVector::ZeroVector;
        int32 Incident = INDEX_NONE;
    };
    const TArray<FAlarm>& GetAlarms() const { return Alarms; }

    double Now() const;
    int32 CountPeople() const;
    /** The street's hour where the first player stands, and the people it keeps now (ADR-067, ADR-068). */
    const MarburgumII::Street::FStreetHour& GetHour() const { return Hour; }
    /** The zone of the street nearest a place (ADR-068): its nearest walk-graph node's, or the countryside. */
    MarburgumII::Street::EZone ZoneAt(const FVector& At) const;
    int32 GetTargetPeople() const { return TargetPeople; }
    /** How open places of a kind are now (ADR-069, AttractorOpen on the world clock), 0..1. */
    float AttractorOpenNow(MarburgumII::Street::EAttractor Kind) const;
    /** The first free spot at a place, or INDEX_NONE when everyone's there it holds. */
    int32 FreeSlot(int32 Attractor) const;
    /** Whether places draw people in and let them out (ADR-069); -MB2StreetPlaces=0 leaves the street as before them. */
    bool PlacesOn() const { return bPlaces; }

private:
    void Step(double Now);
    void GatherViewers();
    void GatherIncidents(double Now);
    void MarkSeenAndCull(double Now);
    /** The hour on the world clock and what falls from the sky, worked into who is out and how many (ADR-067). */
    void UpdateHour(double Now);
    /** The walk-graph node whose zone this player brings, held until they have walked ZoneHoldCm. */
    int32 ZoneNodeFor(const FMarburgumStreetViewer& Viewer);
    /** Over a street's number: the furthest unseen person on an over-full street goes home, one every HomeEverySeconds. */
    void SendSomeoneHome(double Now);
    void SpawnPeople(double Now, float DeltaSeconds);
    bool TrySpawnFor(const FMarburgumStreetViewer& Viewer, bool bInView);
    /** A point to look for somebody new round a player: in view 40-55 m ahead, or out of view 22-35 m behind. */
    FVector SpawnProbe(const FMarburgumStreetViewer& Viewer, bool bInView);
    /** Whether a newcomer at At is somewhere they are kept, on the side of the view they were picked for. */
    bool InSpawnBand(const FMarburgumStreetViewer& Viewer, const FVector& At, bool bInView) const;
    /** ADR-069: somebody out of an open door near a probe, or already waiting at an open stop there. */
    bool TrySpawnAtPlace(const FMarburgumStreetViewer& Viewer, bool bInView);
    /** A new street person at a walk-graph node, or (Override) somewhere off it, walking away from the node. */
    AMarburgumPedestrian* SpawnAt(int32 Node, const FVector* Override, const TCHAR* Why, double PlayerDistanceCm);
    /**
     * -MB2StreetProbe: once a player drives, a person who will not jump clear stands in the car's way. Once the
     * run-over has faded and only the body is left, one passer-by is put on the pavement within notice of it.
     */
    void StepProbe();
    void StepProbeFinder();
    /** -MB2BridgeCross: one person walks an existing bridge route from one bank to the other. */
    void StepBridgeCross(double Now);
    /**
     * -MB2StreetProbe, ADR-046: once a player is about, a person on the pavement nearest the river is scared
     * from the land side, so running straight away would be into the Drava, and is watched for a while.
     */
    void StepWaterProbe(double Now);
    /** Far enough from every player for the band (unless bNearPlayers), and not on top of anybody. */
    bool SpawnPointOk(const FVector& At, bool bInView, bool bNearPlayers = false) const;
    /** Within the view half-angle, narrowed by InsetDegrees (spawns use an inset so walking does not take them out of it). */
    bool InViewOf(const FMarburgumStreetViewer& Viewer, const FVector& At, double InsetDegrees = 0.0) const;
    void RunPeopleOver(float DeltaSeconds);
    int32 AddIncident(MarburgumII::Street::EEvent Event, const FVector& At, bool bPlayer, double Until, AActor* Who);

    bool bEnabled = false;
    FMarburgumWalkGraph Graph;
    FRandomStream Random;
    double StepAccumulator = 0.0;
    double SpawnBudget = 0.0;
    int32 NextSerial = 1;
    int32 NextIncident = 1;
    int32 ViewerCursor = 0;

    TArray<TWeakObjectPtr<AMarburgumPedestrian>> People;
    TArray<FMarburgumStreetViewer> Viewers;
    TArray<FMarburgumStreetIncident> Incidents;
    TArray<FMarburgumStreetMover> Movers;
    TArray<FAlarm> Alarms;
    /** Where each car's body was last frame, for sweeping it back over the frame. */
    TMap<TWeakObjectPtr<AMarburgumVehicle>, FVector> LastCarCentres;
    /** Bodies on the ground and their incident ids, kept while they stay down. */
    TMap<TWeakObjectPtr<AActor>, int32> BodyIncidents;
    double LastSummarySeconds = 0.0;
    MarburgumII::Street::FStreetHour Hour;
    /** The Marburgum clock's weekday and hour at the last update (Tuesday noon without a clock). */
    int32 ClockWeekday = 1;
    double ClockHour = 12.0;
    float Weather = 1.f;
    int32 TargetPeople = 0;
    /** Per player, after CrowdBudgets, and how many of the living crowd stand nearer that player. */
    TArray<int32> ViewerWants;
    TArray<int32> ViewerHaves;
    /** Last zone reading per player, so a border is not sampled again inside ZoneHoldCm. */
    struct FZoneHold
    {
        TWeakObjectPtr<AMarburgumCharacter> Character;
        FVector Location = FVector::ZeroVector;
        int32 Node = INDEX_NONE;
    };
    TArray<FZoneHold> ZoneHolds;
    /** Whether the world clock was known at the last update, and the hour's key last logged. */
    bool bClockKnown = false;
    FString LoggedHour;
    double LastHomeSeconds = -1000.0;
    /** Since the street began (ADR-069): people who went in at a door, came out of one, or were waiting at a stop. */
    int32 WentIn = 0;
    int32 CameOut = 0;
    int32 FoundWaiting = 0;
    bool bPlaces = true;
    bool bProbe = false;
    bool bProbeDone = false;
    /** The probe who goes down, the time their body was first an incident, and whether a finder has been sent to it. */
    TWeakObjectPtr<AMarburgumPedestrian> ProbeVictim;
    double ProbeBodySince = -1.0;
    bool bProbeFinderDone = false;
    bool bWaterProbeDone = false;
    TWeakObjectPtr<AMarburgumPedestrian> WaterProbe;
    double WaterProbeUntil = 0.0;
    bool bWaterProbeSwam = false;
    bool bBridgeCross = false;
    bool bBridgeCrossDone = false;
    TWeakObjectPtr<AMarburgumPedestrian> BridgeCross;
    TArray<int32> BridgeRoute;
    int32 BridgeLeg = 0;
    double BridgeWaitUntil = -1.0;
    double BridgeCrossStart = 0.0;
    double BridgeCrossLog = 0.0;
    int32 ZoneLookups = 0;
    int32 ZoneCacheHits = 0;
    double ZoneLookupSeconds = 0.0;
    FVector WaterProbeStart = FVector::ZeroVector;
};
