#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarburgumVehicleSmokeSubsystem.generated.h"

class AMarburgumCharacter;
class AMarburgumPlayerController;
class AMarburgumVehicle;

/**
 * Driving driver (ADR-039), active only with -MB2VehicleSmoke. Each client process plays one role against a
 * real dedicated server on /Game/World/lent, starting beside the authored lent_hatchback's driver door
 * (?MB2Start=lent_hatchback), and exits 0 (its expectations held) or 1. See scripts/vehicle-smoke-linux.sh.
 *
 *   D  the driver. The parked car rests level on four wheels without creeping. D gets in at the driver's
 *      door: the body plays the enter clip and is hidden in the seat, the HUD names the car. With a passenger
 *      aboard it drives forward on the throttle, steers, brakes to a stop and reverses (the HUD says REV),
 *      stops, waits for the passenger to get out, then gets out at its door on foot and walks.
 *   P  a passenger (-nullrhi). Asks for the driver's seat once D holds it (refused: seat_taken), gets into
 *      a passenger seat, rides while D drives (its own body moves with the car), and gets out once the car
 *      has stopped.
 *
 * Options: -MB2VehicleRole= -MB2VehicleShot=<path prefix> -MB2VehicleTimeout=<s>
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumVehicleSmokeSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    bool Tick(float DeltaTime);
    bool Bind();
    void TickD(double Now);
    void TickP(double Now);

    AMarburgumCharacter* Body() const;
    AMarburgumVehicle* Car() const;
    void OnVehicleResult(bool bOk, FName Action, FName Reason);
    void Expect(bool bCondition, const FString& What);
    void Shot(const TCHAR* Label);
    FString HudSpeed() const;
    void Advance(int32 Next, double Now);
    void Report(const TCHAR* Event, double Now);
    void Pass();
    void Fail(const FString& Reason);

    FTSTicker::FDelegateHandle TickerHandle;
    TWeakObjectPtr<AMarburgumPlayerController> Controller;
    TWeakObjectPtr<AMarburgumVehicle> Vehicle;

    FString Role = TEXT("D");
    FString ShotPrefix;
    double TimeoutSeconds = 300.0;
    double StartSeconds = 0.0;

    int32 Step = 0;
    double StepSeconds = 0.0;
    FVector Mark = FVector::ZeroVector;
    float MarkYaw = 0.f;
    double MaxSpeed = 0.0;
    double MinSpeed = 0.0;
    double StillSince = -1.0;
    double FarthestRideCm = 0.0;
    FVector CarRestAt = FVector::ZeroVector;
    FName RiddenSeat;

    // The last vehicle answer from the server.
    bool bAnswered = false;
    bool bAnswerOk = false;
    FName AnswerReason;

    FString PendingShot;
    double ShotRequestedSeconds = 0.0;
    bool bDone = false;
    TArray<FString> Problems;
};
