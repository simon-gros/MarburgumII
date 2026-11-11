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
 * Options: -MB2StreetShot=<path prefix> -MB2StreetTimeout=<s>
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
    bool Bind();
    void Run(double Now);
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
    TArray<FString> Problems;
};
