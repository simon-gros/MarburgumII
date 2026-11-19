#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarburgumStreetSmokeSubsystem.generated.h"

class AMarburgumCharacter;
class AMarburgumPedestrian;
class AMarburgumPlayerController;
class AMarburgumVehicle;

/**
 * Street people driver (ADR-044), active only with -MB2StreetSmoke. One client on /Game/World/lent against a real
 * dedicated server started with -MB2StreetProbe, from the hatchback's driver door (?MB2Start=lent_hatchback);
 * exits 0 when its expectations held, else 1. See scripts/street-smoke-linux.sh.
 *
 *   1. People appear round the player, on the ground, and walk: most of them have moved several metres a few
 *      seconds later, and nobody has appeared within a few metres of the player.
 *   2. The player throws punches at the air: people close by flee or stop to watch (their replicated mood).
 *   3. The player gets into the car and drives off: the server's probe puts a person who will not jump clear
 *      in its way and one further on who will. The first goes down under the car; the second jumps aside.
 *   4. The car stops. Somebody gathers round, or runs from, the person lying in the road.
 *
 * With -MB2StreetCount=<s> (ADR-067) it only stands there instead, looking along the street, and counts the people
 * within 70 m by group every second for that long, then captures `count` and passes: the hours smoke reads how
 * full the street is at the server's hour (scripts/street-hours-smoke-linux.sh). With -MB2StreetShotAfterMove=<s>
 * (ADR-068) it also captures `stop<n>` that long after each time the server moves the player (the district smoke's
 * route, scripts/street-district-smoke-linux.sh), and logs `event=moved` when it sees the move.
 *
 * Options: -MB2StreetShot=<path prefix> -MB2StreetTimeout=<s> -MB2StreetCount=<s> -MB2StreetShotAfterMove=<s>
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumStreetSmokeSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    bool Tick(float DeltaTime);
    /** -MB2BridgeWatch: follow the server's bridge_cross pedestrian and photograph it once it has moved. */
    void WatchBridge(double Now);
    /** -MB2CrowdWatch: log the pedestrians this client can see. The script owns the lifetime. */
    void WatchCrowd(double Now);
    bool Bind();
    void Run(double Now);
    /** -MB2StreetCount: stand and count. */
    void RunCount(double Now);
    void Observe();
    void People(TArray<AMarburgumPedestrian*>& Out, double WithinCm) const;
    AMarburgumCharacter* Body() const;
    void Expect(bool bCondition, const FString& What);
    void Shot(const TCHAR* Label);
    void Advance(int32 Next, double Now);
    void Report(const TCHAR* Event, double Now);
    void Pass();
    void Fail(const FString& Reason);

    FTSTicker::FDelegateHandle TickerHandle;
    TWeakObjectPtr<AMarburgumPlayerController> Controller;
    TWeakObjectPtr<AMarburgumVehicle> Car;

    FString ShotPrefix;
    double TimeoutSeconds = 300.0;
    double CountSeconds = 0.0;
    /** -MB2StreetShotAfterMove: where the body last stood, when it was last moved and how many moves so far. */
    double ShotAfterMoveSeconds = 0.0;
    FVector LastBodyAt = FVector::ZeroVector;
    bool bBodySeen = false;
    double MovedSeconds = -1.0;
    int32 Moves = 0;
    double LastCountSeconds = 0.0;
    int32 FewestCounted = TNumericLimits<int32>::Max();
    int32 MostCounted = 0;
    int32 Samples = 0;
    int64 SampleTotal = 0;
    double StartSeconds = 0.0;
    int32 Step = 0;
    double StepSeconds = 0.0;
    double LastSwingSeconds = 0.0;
    bool bSeatAnswered = false;

    /** Where each person was when first counted. */
    TMap<TWeakObjectPtr<AMarburgumPedestrian>, FVector> Marks;
    /** Seen at any time: the most fleeing and watching at once, people down, and a gathering. */
    int32 MostFleeing = 0;
    int32 MostWatching = 0;
    int32 MostDown = 0;
    int32 MostGathered = 0;
    int32 MostDodging = 0;
    double NearestSpawnCm = TNumericLimits<double>::Max();
    TSet<TWeakObjectPtr<AMarburgumPedestrian>> Known;

    FString PendingShot;
    double PassAt = 0.0;
    bool bDone = false;
    bool bBridgeWatch = false;
    bool bBridgeSeen = false;
    bool bBridgeShot = false;
    int32 BridgeShots = 0;
    FVector BridgeFirstAt = FVector::ZeroVector;
    double BridgeWatchLog = 0.0;
    bool bCrowdWatch = false;
    double CrowdWatchLog = 0.0;
    TArray<FString> Problems;
};
