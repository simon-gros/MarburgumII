#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MarburgumVehicle.generated.h"

class AMarburgumCharacter;
class APhysicsVolume;
class APlayerState;
class UBoxComponent;
class UStaticMeshComponent;

namespace MarburgumII::Vehicles
{
    struct FVehicleDefinition;
    struct FSeatDefinition;
}

/** Who sits where. Replicated to everyone: a car's occupants are visible in the street. */
USTRUCT()
struct FMarburgumVehicleOccupant
{
    GENERATED_BODY()

    UPROPERTY()
    FName SeatId;

    UPROPERTY()
    TObjectPtr<APlayerState> Occupant = nullptr;
};

/** The server's body state as clients draw it, sent at the vehicle's net rate and interpolated (original set_server_state). */
USTRUCT()
struct FMarburgumVehicleMotion
{
    GENERATED_BODY()

    UPROPERTY()
    FVector_NetQuantize100 Location = FVector::ZeroVector;

    UPROPERTY()
    FQuat Rotation = FQuat::Identity;

    /** Along the car's own forward, m/s; negative when reversing. */
    UPROPERTY()
    float ForwardSpeed = 0.f;

    /** Front-wheel angle, radians, positive to the right. */
    UPROPERTY()
    float SteerAngle = 0.f;

    UPROPERTY()
    uint8 WheelsOnGround = 0;

    UPROPERTY()
    uint8 WheelsInWater = 0;

    /** Crashes this car has had; a client hears one when this goes up (presentation only). */
    UPROPERTY()
    int32 ImpactCount = 0;
};

/**
 * One driveable car from the vehicle catalogue (ADR-039, MarburgumVehicleContract.h), carried forward from
 * the original Marburgum's Vehicle (src/shared/vehicle.cpp).
 *
 * Authority: only the server simulates. The root is the collision box, a rigid body on four suspension
 * raycasts (MarburgumVehicleDynamics.h); the server applies the spring, tyre and engine forces every frame
 * from the driver's held intent, which expires after 0.4 s so a dropped client leaves a braking car. On a
 * client the body never simulates: it is kinematic, blocks walkers, and slides toward the transform the
 * server reports, as the original's frozen client car did. Seats are server state; a player gets in or out
 * only through AMarburgumPlayerController's requests, which the server checks (range, free seat, a body that
 * can move) and answers.
 *
 * Presentation (clients): the body shell and four wheels the MarburgumVehicleContent commandlet generated
 * from the original's vehicle_mesh.cpp, the wheels riding on their own client-side ray and spinning with the
 * road speed, the front pair steered by the server's angle; side doors that swing open while someone gets
 * in or out.
 */
UCLASS()
class MARBURGUMII_API AMarburgumVehicle : public AActor
{
    GENERATED_BODY()

public:
    AMarburgumVehicle();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    /** Catalogue id (MarburgumVehicleContract.h). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marburgum|Vehicle")
    FName DefinitionId = TEXT("city_hatchback_a");

    /** Stable world id, as the original's vehicle_instances.json named it. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marburgum|Vehicle")
    FName VehicleId;

    const MarburgumII::Vehicles::FVehicleDefinition* GetDefinition() const;
    FText GetDisplayName() const;

    // ---- Seats ----

    /** Where a seat's occupant sits, and where someone getting in or out of it stands (world space). */
    FVector GetSeatLocation(FName SeatId) const;
    FVector GetDoorLocation(FName SeatId) const;
    const TArray<FMarburgumVehicleOccupant>& GetOccupants() const { return Occupants; }
    APlayerState* GetOccupant(FName SeatId) const;
    FName FindSeatOf(const APlayerState* Who) const;
    FName GetDriverSeatId() const;
    bool IsDriver(const APlayerState* Who) const;

    /**
     * The free seat a pawn standing at Location could get into: the driver's when its door is within reach,
     * otherwise the nearest free seat's. NAME_None if none is in reach.
     */
    FName FindReachableSeat(const FVector& Location) const;

    /** Server: put a character in a seat or take it out. The reason is a wire name when refused. */
    bool TryEnterAuthority(AMarburgumCharacter& Who, FName SeatId, FName& OutReason);
    bool TryExitAuthority(AMarburgumCharacter& Who, FName& OutReason);

    /** Server: forget whatever this character held, without moving it (it is going away). */
    void ReleaseAuthority(const APlayerState* Who);

    /** Server: the driver's intent. Values are clamped; anyone but the driver is ignored. */
    bool SetDriverInputAuthority(const APlayerState* Who, float Throttle, float Brake, float Steer, bool bHandbrake);

    // ---- Motion ----

    /** Along the car's own forward, m/s, negative when reversing; the server's value on clients. */
    float GetForwardSpeed() const { return Motion.ForwardSpeed; }
    float GetSteerAngle() const { return Motion.SteerAngle; }
    int32 GetWheelsOnGround() const { return Motion.WheelsOnGround; }
    int32 GetWheelsInWater() const { return Motion.WheelsInWater; }
    int32 GetImpactCount() const { return Motion.ImpactCount; }

    /** Server: the held intent, for evidence. */
    float GetThrottleAuthority() const { return Throttle; }

    UBoxComponent* GetBody() const { return Body; }

    /** Every vehicle in a world (a handful per map until the city streams). */
    static void GetAll(const UWorld* World, TArray<AMarburgumVehicle*>& Out);

protected:
    UFUNCTION()
    void OnRep_Occupants();

private:
    void ApplyDefinition();
    void StepPhysicsAuthority(float DeltaSeconds);
    void FollowServerMotion(float DeltaSeconds);
    void UpdateWheels(float DeltaSeconds);
    void AnimateDoors(float DeltaSeconds);
    void PruneOccupantsAuthority();
    bool FindExitSpot(FName SeatId, const AMarburgumCharacter& Who, FVector& OutSpot) const;
    bool IsInWater(const FVector& Point, float& OutSurfaceZ) const;
    int32 DoorForSeat(FName SeatId) const;
    void LogAuthority(const TCHAR* Event, const APlayerState* Who, FName SeatId, FName Reason) const;

    UPROPERTY(VisibleAnywhere, Category = "Marburgum|Vehicle")
    TObjectPtr<UBoxComponent> Body;

    UPROPERTY(VisibleAnywhere, Category = "Marburgum|Vehicle")
    TObjectPtr<UStaticMeshComponent> Shell;

    UPROPERTY(VisibleAnywhere, Category = "Marburgum|Vehicle")
    TArray<TObjectPtr<UStaticMeshComponent>> Wheels;

    UPROPERTY(VisibleAnywhere, Category = "Marburgum|Vehicle")
    TArray<TObjectPtr<USceneComponent>> DoorHinges;

    UPROPERTY(VisibleAnywhere, Category = "Marburgum|Vehicle")
    TArray<TObjectPtr<UStaticMeshComponent>> Doors;

    UPROPERTY(ReplicatedUsing = OnRep_Occupants)
    TArray<FMarburgumVehicleOccupant> Occupants;

    UPROPERTY(Replicated)
    FMarburgumVehicleMotion Motion;

    // Server: the driver's held intent and when it was last renewed.
    float Throttle = 0.f;
    float Brake = 1.f;
    float Steer = 0.f;
    bool bHandbrake = false;
    double InputAt = -1000.0;
    bool bInputLive = false;

    // Server: crash detection against the body's previous velocity.
    FVector LastVelocity = FVector::ZeroVector;
    bool bHasLastVelocity = false;
    double ImpactQuiet = 0.0;

    // Server: the river (water physics volumes), found once.
    TArray<TWeakObjectPtr<APhysicsVolume>> WaterVolumes;

    // Client presentation.
    float WheelSpin = 0.f;
    float DoorAngle[4] = {};
    float DoorHold[4] = {};
    TSet<FName> ShownSeats;
    bool bHasMotion = false;
};
