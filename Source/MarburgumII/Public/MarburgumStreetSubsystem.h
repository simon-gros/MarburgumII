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

private:
    void Step(double Now);
    void GatherViewers();
    void GatherIncidents(double Now);
    void MarkSeenAndCull(double Now);
    void SpawnPeople(double Now, float DeltaSeconds);
    bool TrySpawnFor(const FMarburgumStreetViewer& Viewer, bool bInView);
    /** A new street person at a walk-graph node, or (Override) somewhere off it, walking away from the node. */
    AMarburgumPedestrian* SpawnAt(int32 Node, const FVector* Override, const TCHAR* Why, double PlayerDistanceCm);
    /** -MB2StreetProbe: once a player drives, a person who will not jump clear stands in the car's way. */
    void StepProbe();
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
    bool bProbe = false;
    bool bProbeDone = false;
};
