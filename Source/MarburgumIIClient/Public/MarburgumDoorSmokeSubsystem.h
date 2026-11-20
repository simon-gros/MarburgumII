#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarburgumDoorSmokeSubsystem.generated.h"

class AMarburgumDoor;
class AMarburgumPlayerController;

/**
 * Door and interior driver (ADR-076), active only with -MB2DoorSmoke=<role>. Clients against a real dedicated server
 * on /Game/World/lent started with -MB2DoorStarts, joining with ?MB2Start=door_<building>. See
 * scripts/door-smoke-linux.sh.
 *
 * walker   opens the street door, stands in its swing while the rival tries to shut it, then walks the building's
 *          route (UMarburgumInteriorSubsystem::GetWalkRoute) with ordinary movement input, opening each room door
 *          with E, photographing each room, and out to the street again. Fails when it is stuck, falls below the
 *          floor or a door does not answer. Then stays connected for -MB2DoorHold seconds.
 * rival    once the walker has the street door open: shuts it and reverses the swing before it ends; tries to shut
 *          it while the walker stands in the swing (the server must refuse); asks for a room door from outside
 *          its reach (refused); leaves.
 * observer joins later and reports every door of the building, which must match -MB2DoorExpect=open|closed|<list>.
 *
 * Options: -MB2DoorBuilding=<id> -MB2DoorShot=<path prefix> -MB2DoorTimeout=<s> -MB2DoorHold=<s> -MB2DoorExpect=...
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumDoorSmokeSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    enum class EStep : uint8
    {
        Bind,
        // walker
        OpenStreet,
        SeeReversal,
        HoldInSwing,
        Walk,
        Hold,
        // rival
        AwaitOpen,
        Approach,
        CloseAndReverse,
        AwaitWalkerInSwing,
        CloseOnWalker,
        FarDoor,
        // observer
        Observe,
        Done
    };

    bool Tick(float DeltaTime);
    bool Bind();
    AMarburgumDoor* StreetDoor() const;
    TArray<AMarburgumDoor*> BuildingDoors() const;
    FString DoorStates() const;
    /** Walk towards Target at floor level. True on arrival; fails the run when stuck. */
    bool MoveTo(const FVector& Target, double Now);
    void Face(const FVector& Target, float Pitch = -6.f);
    void Shot(const FString& Label);
    void Expect(bool bCondition, const FString& What);
    void Advance(EStep Next, double Now);
    void Pass();
    void Fail(const FString& Reason);

    FTSTicker::FDelegateHandle TickerHandle;
    TWeakObjectPtr<AMarburgumPlayerController> Controller;

    FString Role;
    FString Building;
    FString ShotPrefix;
    FString ExpectStates;
    double TimeoutSeconds = 240.0;
    double HoldSeconds = 0.0;
    double StartSeconds = 0.0;

    EStep Step = EStep::Bind;
    double StepSeconds = 0.0;
    int32 Phase = 0;

    TArray<FVector> Route;
    int32 RouteIndex = 0;
    double FloorZ = 0.0;
    FVector LastProgressAt = FVector::ZeroVector;
    double LastProgressSeconds = 0.0;
    TWeakObjectPtr<AMarburgumDoor> WaitingDoor;
    double DoorRequestedSeconds = 0.0;
    int32 RoomShots = 0;
    bool bSawClosing = false;
    double MaxStepUpCm = 0.0;

    /** A capture asked for and not yet taken, and when to take it. */
    FString ShotLabel;
    double ShotDueSeconds = 0.0;
    FString PendingShot;
    double ShotRequestedSeconds = 0.0;
    TArray<FString> Problems;
    bool bDone = false;
};
