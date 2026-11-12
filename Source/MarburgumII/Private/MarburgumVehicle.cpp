#include "MarburgumVehicle.h"

#include "CollisionQueryParams.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "MarburgumCharacter.h"
#include "MarburgumGameState.h"
#include "MarburgumGround.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumVehicleContract.h"
#include "MarburgumVehicleDynamics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"

using namespace MarburgumII::Vehicles;

namespace
{
    constexpr double Cm = CentimetresPerMetre;
    /** Newtons to Unreal's force unit (kg cm / s^2). */
    constexpr double NewtonsToUnreal = CentimetresPerMetre;
    /** A rider has to be roughly level with the door, not on the bridge above it. */
    constexpr double EnterHeightToleranceCm = 250.0;
    /** How far above and below a door point the ground is looked for when someone steps out. */
    constexpr double ExitProbeUpCm = 150.0;
    constexpr double ExitProbeDownCm = 300.0;

    const TCHAR* const DoorSockets[WheelCount] = { TEXT("Door0"), TEXT("Door1"), TEXT("Door2"), TEXT("Door3") };

    void IgnoreRiders(const AActor& Vehicle, FCollisionQueryParams& Params)
    {
        Params.AddIgnoredActor(&Vehicle);
        TArray<AActor*> Attached;
        Vehicle.GetAttachedActors(Attached);
        Params.AddIgnoredActors(Attached);
    }
}

AMarburgumVehicle::AMarburgumVehicle()
{
    PrimaryActorTick.bCanEverTick = true;
    // Forces must be in before the physics step that integrates them.
    PrimaryActorTick.TickGroup = TG_PrePhysics;
    bReplicates = true;
    // The server's body state travels in Motion and clients interpolate it, as the original did; the
    // engine's own movement replication would fight that.
    SetReplicatingMovement(false);
    SetNetUpdateFrequency(30.f);
    NetPriority = 2.f;

    Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
    Body->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
    Body->SetLinearDamping(0.f);
    Body->SetAngularDamping(static_cast<float>(AngularDamping));
    Body->SetCanEverAffectNavigation(false);
    Body->SetShouldUpdatePhysicsVolume(false);
    RootComponent = Body;

    Shell = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shell"));
    Shell->SetupAttachment(Body);
    Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Shell->SetCanEverAffectNavigation(false);

    for (int32 Index = 0; Index < WheelCount; ++Index)
    {
        UStaticMeshComponent* Wheel = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Wheel%d"), Index));
        Wheel->SetupAttachment(Body);
        Wheel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Wheel->SetCanEverAffectNavigation(false);
        Wheels.Add(Wheel);

        USceneComponent* Hinge = CreateDefaultSubobject<USceneComponent>(*FString::Printf(TEXT("DoorHinge%d"), Index));
        Hinge->SetupAttachment(Shell);
        DoorHinges.Add(Hinge);

        UStaticMeshComponent* Door = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Door%d"), Index));
        Door->SetupAttachment(Hinge);
        Door->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Door->SetCanEverAffectNavigation(false);
        Doors.Add(Door);
    }
}

void AMarburgumVehicle::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMarburgumVehicle, Occupants);
    DOREPLIFETIME(AMarburgumVehicle, Motion);
}

const FVehicleDefinition* AMarburgumVehicle::GetDefinition() const
{
    return FindDefinition(DefinitionId.ToString());
}

FText AMarburgumVehicle::GetDisplayName() const
{
    const FVehicleDefinition* Definition = GetDefinition();
    return Definition ? FText::FromString(Definition->Name) : FText::FromName(DefinitionId);
}

void AMarburgumVehicle::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    ApplyDefinition();
}

void AMarburgumVehicle::ApplyDefinition()
{
    const FVehicleDefinition* Definition = GetDefinition();
    if (!Definition)
    {
        UE_LOG(LogMarburgumNet, Warning, TEXT("%s role=%s event=unknown_definition vehicle=%s definition=%s"), LogPrefix,
            HasAuthority() ? TEXT("server") : TEXT("client"), *VehicleId.ToString(), *DefinitionId.ToString());
        return;
    }
    const FString Id = Definition->Id;
    Body->SetBoxExtent(CollisionHalfExtentCm(*Definition));

    // The bodywork's origin is the ground under the middle of the axles; on unloaded springs the ground
    // is the suspension's length and a tyre below the mounts (vehicle.cpp build_views).
    Shell->SetRelativeLocation(LocalPointCm(0.0, WheelAttachY - Definition->SuspensionRestLength - Definition->WheelRadius, 0.0));
    if (!Shell->GetStaticMesh())
    {
        Shell->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, *BodyMeshPath(Id), nullptr, LOAD_NoWarn | LOAD_Quiet));
    }
    for (int32 Index = 0; Index < WheelCount; ++Index)
    {
        Wheels[Index]->SetRelativeLocation(WheelAttachCm(*Definition, Index) - FVector(0.0, 0.0, Definition->SuspensionRestLength * Cm));
        if (!Wheels[Index]->GetStaticMesh())
        {
            Wheels[Index]->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, *WheelMeshPath(Id, Index), nullptr, LOAD_NoWarn | LOAD_Quiet));
        }
        // A door exists where the generated bodywork has a hinge socket for it (a lorry has only two).
        const UStaticMesh* BodyMesh = Shell->GetStaticMesh();
        const UStaticMeshSocket* Socket = BodyMesh ? BodyMesh->FindSocket(DoorSockets[Index]) : nullptr;
        if (Socket)
        {
            DoorHinges[Index]->SetRelativeLocation(Socket->RelativeLocation);
            if (!Doors[Index]->GetStaticMesh())
            {
                Doors[Index]->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, *DoorMeshPath(Id, Index), nullptr, LOAD_NoWarn | LOAD_Quiet));
            }
        }
        Doors[Index]->SetVisibility(Socket != nullptr);
    }
}

void AMarburgumVehicle::BeginPlay()
{
    Super::BeginPlay();
    ApplyDefinition();
    if (VehicleId.IsNone())
    {
        VehicleId = FName(GetName());
    }
    const FVehicleDefinition* Definition = GetDefinition();
    if (HasAuthority() && Definition)
    {
        // Only the server simulates (the original's "a car is frozen until the server claims it").
        Body->SetMassOverrideInKg(NAME_None, static_cast<float>(Definition->Mass), true);
        // A low centre of mass keeps an arcade car from tipping over in a turn it should hold.
        Body->SetCenterOfMass(LocalPointCm(Definition->CentreOfMass));
        Body->SetSimulatePhysics(true);
        // The vehicle smoke shrinks the distance a car is sent to players within, so that only the
        // occupants' rule in IsNetRelevantFor keeps it updating for them (ADR-056).
        float NetCullM = 0.f;
        if (FParse::Value(FCommandLine::Get(), TEXT("MB2VehicleNetCullM="), NetCullM) && NetCullM > 0.f)
        {
            SetNetCullDistanceSquared(FMath::Square(NetCullM * static_cast<float>(Cm)));
        }
        for (TActorIterator<APhysicsVolume> It(GetWorld()); It; ++It)
        {
            if (It->bWaterVolume)
            {
                WaterVolumes.Add(*It);
            }
        }
        Motion.Location = GetActorLocation();
        Motion.Rotation = GetActorQuat();
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=vehicle_ready vehicle=%s definition=%s location=%s water_volumes=%d net_cull_m=%.0f"),
            LogPrefix, *VehicleId.ToString(), Definition->Id, *GetActorLocation().ToString(), WaterVolumes.Num(),
            FMath::Sqrt(GetNetCullDistanceSquared()) / Cm);
    }
    else
    {
        // A client's car is placed where the server says; it blocks walkers but never simulates.
        Body->SetSimulatePhysics(false);
    }
}

void AMarburgumVehicle::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (HasAuthority())
    {
        PruneOccupantsAuthority();
        StepPhysicsAuthority(DeltaSeconds);
    }
    else
    {
        FollowServerMotion(DeltaSeconds);
    }
    if (GetNetMode() != NM_DedicatedServer)
    {
        UpdateWheels(DeltaSeconds);
        AnimateDoors(DeltaSeconds);
    }
}

bool AMarburgumVehicle::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const
{
    // A player's own car never goes stale under them, whatever the server takes their view to be: past the
    // cull distance it stopped updating while the server drove on, and getting out snapped it (ADR-056).
    const APlayerController* Viewer = Cast<APlayerController>(RealViewer);
    if (Viewer && Viewer->PlayerState && !FindSeatOf(Viewer->PlayerState).IsNone())
    {
        return true;
    }
    return Super::IsNetRelevantFor(RealViewer, ViewTarget, SrcLocation);
}

// ---- Server simulation ----

void AMarburgumVehicle::StepPhysicsAuthority(float DeltaSeconds)
{
    using namespace MarburgumII::Vehicles::Dynamics;
    const FVehicleDefinition* Definition = GetDefinition();
    if (!Definition || DeltaSeconds <= 0.f || !Body->IsSimulatingPhysics())
    {
        return;
    }

    // Held intent lapses: a driver who stopped sending coasts; a car with nobody at the wheel rests on its brake.
    const double Now = GetWorld()->GetTimeSeconds();
    if (bInputLive && Now - InputAt > InputExpirySeconds)
    {
        bInputLive = false;
        Throttle = 0.f;
        Steer = 0.f;
        bHandbrake = false;
        Brake = GetOccupant(GetDriverSeatId()) ? 0.f : 1.f;
    }

    const FTransform Transform = Body->GetComponentTransform();
    const FVector Velocity = Body->GetPhysicsLinearVelocity();
    const FVector Spin = Body->GetPhysicsAngularVelocityInRadians();
    const FVector Centre = Body->GetCenterOfMass();
    const FVector Forward = Transform.GetUnitAxis(EAxis::X);
    const FVector Right = Transform.GetUnitAxis(EAxis::Y);
    const FVector Up = Transform.GetUnitAxis(EAxis::Z);

    // A crash: the body's velocity jumped further than the wheels can move it.
    ImpactQuiet = FMath::Max(0.0, ImpactQuiet - DeltaSeconds);
    if (bHasLastVelocity && ImpactQuiet <= 0.0 && Motion.WheelsInWater == 0
        && IsImpact(FVector::Distance(Velocity, LastVelocity) / Cm))
    {
        ++Motion.ImpactCount;
        ImpactQuiet = ImpactQuietSeconds;
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=impact vehicle=%s count=%d speed_change=%.2f"), LogPrefix,
            *VehicleId.ToString(), Motion.ImpactCount, FVector::Distance(Velocity, LastVelocity) / Cm);
    }
    LastVelocity = Velocity;
    bHasLastVelocity = true;

    const double ForwardSpeed = FVector::DotProduct(Velocity, Forward) / Cm;
    const double SteerAngle = StepSteerAngle(*Definition, Motion.SteerAngle, Steer, ForwardSpeed, DeltaSeconds);
    const double AppliedBrake = HeldBrake(Throttle, Brake, ForwardSpeed);
    // Stopped on the brake, the wheels hold the car on a slope rather than letting it creep (HoldForce).
    const bool bHeld = IsHeld(Throttle, AppliedBrake, ForwardSpeed);
    const FVector Pull(0.0, 0.0, -Gravity);
    const double Share = Definition->Mass / WheelCount;
    const double Reach = (Definition->SuspensionRestLength + Definition->WheelRadius) * Cm;

    // The server's weather (ADR-061): a wet road keeps only part of its grip (SurfaceGrip, SurfaceTraction).
    const FMarburgumWeather* Weather = AMarburgumGameState::WeatherOf(GetWorld());
    const double Wetness = Weather ? Weather->Wetness : 0.0;

    FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumVehicleWheel), false);
    Params.bReturnPhysicalMaterial = true;
    IgnoreRiders(*this, Params);

    int32 OnGround = 0;
    int32 InWater = 0;
    float WaterLine = -TNumericLimits<float>::Max();
    for (int32 Wheel = 0; Wheel < WheelCount; ++Wheel)
    {
        const FVector Attach = Transform.TransformPosition(WheelAttachCm(*Definition, Wheel));
        const FVector End = Attach - Up * Reach;
        FHitResult Hit;
        const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Attach, End, ECC_Visibility, Params);
        float Surface = 0.f;
        // The ray met the river: there is no road here to spring off or drive along.
        if (IsInWater(bHit ? Hit.ImpactPoint : End, Surface))
        {
            ++InWater;
            WaterLine = FMath::Max(WaterLine, Surface);
            continue;
        }
        if (!bHit)
        {
            continue;
        }
        ++OnGround;
        const FVector Contact = Hit.ImpactPoint;
        const double Compression = FMath::Max(0.0, Reach - FVector::Distance(Attach, Contact)) / Cm;
        const FVector WheelVelocity = Velocity + FVector::CrossProduct(Spin, Contact - Centre);
        const double Load = SuspensionLoad(*Definition, Compression, FVector::DotProduct(WheelVelocity, Up) / Cm);
        if (Load > 0.0)
        {
            Body->AddForceAtLocation(Up * Load * NewtonsToUnreal, Contact);
        }

        // Tyre forces are pulled up toward the centre of mass, so the body rolls and pitches less.
        const double CentreHeight = FVector::DotProduct(Up, Centre - Contact);
        const FVector GripPoint = Contact + Up * CentreHeight * (1.0 - RollLever);
        const FVector DrivePoint = Contact + Up * CentreHeight * (1.0 - PitchLever);
        FVector WheelForward = Forward;
        FVector WheelRight = Right;
        if (IsFrontWheel(Wheel) && SteerAngle != 0.0)
        {
            const double Degrees = FMath::RadiansToDegrees(SteerAngle);
            WheelForward = Forward.RotateAngleAxis(Degrees, Up).GetSafeNormal();
            WheelRight = Right.RotateAngleAxis(Degrees, Up).GetSafeNormal();
        }
        const FString Kind = MarburgumII::Ground::SurfaceOf(Hit);
        const double Sideways = FVector::DotProduct(WheelVelocity, WheelRight) / Cm;
        const double Rolling = FVector::DotProduct(WheelVelocity, WheelForward) / Cm;
        const double Grip = SurfaceGrip(Kind, Wetness);
        const double Traction = SurfaceTraction(Kind, Wetness);
        const double Lateral = bHeld
            ? HoldForce(Sideways, FVector::DotProduct(Pull, WheelRight), Share, DeltaSeconds, LateralFrictionLimit * Grip * Load)
            : LateralForce(*Definition, Wheel, Sideways, Load, bHandbrake, Grip);
        if (Lateral != 0.0)
        {
            Body->AddForceAtLocation(WheelRight * Lateral * NewtonsToUnreal, GripPoint);
        }
        const double Longitudinal = bHeld
            ? HoldForce(Rolling, FVector::DotProduct(Pull, WheelForward), Share, DeltaSeconds,
                BrakeLimit(*Definition, Wheel, AppliedBrake, bHandbrake, Traction))
            : LongitudinalForce(*Definition, Wheel, ForwardSpeed, Rolling, Throttle, AppliedBrake, bHandbrake, Traction, DeltaSeconds);
        if (Longitudinal != 0.0)
        {
            Body->AddForceAtLocation(WheelForward * Longitudinal * NewtonsToUnreal, DrivePoint);
        }
    }

    if (InWater > 0)
    {
        // The body displaces the river: deep enough and it is pushed back up; fast and the water takes the
        // speed. Depth is measured from the original's origin, CollisionLift below this actor's.
        const double OriginZ = Transform.GetLocation().Z - Up.Z * CollisionLift * Cm;
        const double Depth = (WaterLine + WaterBuoyancyOffset * Cm - OriginZ) / Cm;
        const double Lift = Buoyancy(*Definition, Depth);
        if (Lift > 0.0)
        {
            Body->AddForce(FVector(0.0, 0.0, Lift * NewtonsToUnreal));
        }
        // m/s * kg * 1/s is newtons; cm/s in gives Unreal's unit directly.
        Body->AddForce(-Velocity * Definition->Mass * WaterDrag);
    }
    else
    {
        const double Drag = Resistance(*Definition, Velocity.Size() / Cm, OnGround);
        if (Drag > 0.0)
        {
            Body->AddForce(-Velocity.GetSafeNormal() * Drag * NewtonsToUnreal);
        }
    }

    Motion.Location = Transform.GetLocation();
    Motion.Rotation = Transform.GetRotation();
    Motion.ForwardSpeed = static_cast<float>(ForwardSpeed);
    Motion.SteerAngle = static_cast<float>(SteerAngle);
    Motion.WheelsOnGround = static_cast<uint8>(OnGround);
    Motion.WheelsInWater = static_cast<uint8>(InWater);
}

bool AMarburgumVehicle::IsInWater(const FVector& Point, float& OutSurfaceZ) const
{
    for (const TWeakObjectPtr<APhysicsVolume>& Weak : WaterVolumes)
    {
        const APhysicsVolume* Volume = Weak.Get();
        if (Volume && Volume->EncompassesPoint(Point))
        {
            FVector Origin;
            FVector Extent;
            Volume->GetActorBounds(false, Origin, Extent);
            OutSurfaceZ = static_cast<float>(Origin.Z + Extent.Z);
            return true;
        }
    }
    return false;
}

bool AMarburgumVehicle::SetDriverInputAuthority(const APlayerState* Who, float InThrottle, float InBrake, float InSteer, bool bInHandbrake)
{
    if (!HasAuthority() || !IsDriver(Who) || !FMath::IsFinite(InThrottle) || !FMath::IsFinite(InBrake) || !FMath::IsFinite(InSteer))
    {
        return false;
    }
    // Clamped, so a client cannot ask for more than full lock or full throttle.
    Throttle = FMath::Clamp(InThrottle, -1.f, 1.f);
    Brake = FMath::Clamp(InBrake, 0.f, 1.f);
    Steer = FMath::Clamp(InSteer, -1.f, 1.f);
    bHandbrake = bInHandbrake;
    InputAt = GetWorld()->GetTimeSeconds();
    bInputLive = true;
    return true;
}

// ---- Seats ----

APlayerState* AMarburgumVehicle::GetOccupant(FName SeatId) const
{
    for (const FMarburgumVehicleOccupant& Entry : Occupants)
    {
        if (Entry.SeatId == SeatId)
        {
            return Entry.Occupant;
        }
    }
    return nullptr;
}

FName AMarburgumVehicle::FindSeatOf(const APlayerState* Who) const
{
    for (const FMarburgumVehicleOccupant& Entry : Occupants)
    {
        if (Who && Entry.Occupant == Who)
        {
            return Entry.SeatId;
        }
    }
    return NAME_None;
}

FName AMarburgumVehicle::GetDriverSeatId() const
{
    const FVehicleDefinition* Definition = GetDefinition();
    const FSeatDefinition* Seat = Definition ? DriverSeat(*Definition) : nullptr;
    return Seat ? FName(Seat->Id) : NAME_None;
}

bool AMarburgumVehicle::IsDriver(const APlayerState* Who) const
{
    return Who && GetDriverSeatId() != NAME_None && GetOccupant(GetDriverSeatId()) == Who;
}

FVector AMarburgumVehicle::GetSeatLocation(FName SeatId) const
{
    const FVehicleDefinition* Definition = GetDefinition();
    const FSeatDefinition* Seat = Definition ? FindSeat(*Definition, SeatId.ToString()) : nullptr;
    return Seat ? GetActorTransform().TransformPosition(SeatPointCm(*Seat)) : GetActorLocation();
}

FVector AMarburgumVehicle::GetDoorLocation(FName SeatId) const
{
    const FVehicleDefinition* Definition = GetDefinition();
    const FSeatDefinition* Seat = Definition ? FindSeat(*Definition, SeatId.ToString()) : nullptr;
    return Seat ? GetActorTransform().TransformPosition(DoorPointCm(*Seat)) : GetActorLocation();
}

FName AMarburgumVehicle::FindReachableSeat(const FVector& Location) const
{
    const FVehicleDefinition* Definition = GetDefinition();
    if (!Definition)
    {
        return NAME_None;
    }
    const double Range = Definition->EnterRange * Cm;
    FName Best = NAME_None;
    double BestDistance = TNumericLimits<double>::Max();
    for (int32 Index = 0; Index < Definition->SeatCount; ++Index)
    {
        const FName Seat(Definition->Seats[Index].Id);
        if (GetOccupant(Seat))
        {
            continue;
        }
        const FVector Door = GetDoorLocation(Seat);
        const double Distance = FVector::Dist2D(Door, Location);
        if (Distance > Range || FMath::Abs(Door.Z - Location.Z) > EnterHeightToleranceCm)
        {
            continue;
        }
        // The original walked up to the driver's door; the wheel comes first whenever it is in reach.
        const bool bDriver = FCString::Strcmp(Definition->Seats[Index].Role, TEXT("driver")) == 0;
        const double Score = bDriver ? -1.0 : Distance;
        if (Score < BestDistance)
        {
            BestDistance = Score;
            Best = Seat;
        }
    }
    return Best;
}

bool AMarburgumVehicle::TryEnterAuthority(AMarburgumCharacter& Who, FName SeatId, FName& OutReason)
{
    const FVehicleDefinition* Definition = GetDefinition();
    APlayerState* State = Who.GetPlayerState();
    OutReason = NAME_None;
    if (!HasAuthority() || !Definition || !State)
    {
        OutReason = TEXT("no_vehicle");
    }
    else if (!Who.CanEnterVehicle())
    {
        OutReason = TEXT("incapacitated");
    }
    else if (Who.GetSeatedVehicle())
    {
        OutReason = TEXT("already_seated");
    }
    else
    {
        if (SeatId.IsNone())
        {
            SeatId = GetDriverSeatId();
        }
        const FSeatDefinition* Seat = FindSeat(*Definition, SeatId.ToString());
        const FVector Door = Seat ? GetDoorLocation(SeatId) : FVector::ZeroVector;
        if (!Seat)
        {
            OutReason = TEXT("no_seat");
        }
        else if (GetOccupant(SeatId))
        {
            OutReason = TEXT("seat_taken");
        }
        else if (FVector::Dist2D(Door, Who.GetActorLocation()) > Definition->EnterRange * Cm
            || FMath::Abs(Door.Z - Who.GetActorLocation().Z) > EnterHeightToleranceCm)
        {
            OutReason = TEXT("too_far");
        }
    }
    if (!OutReason.IsNone())
    {
        LogAuthority(TEXT("enter_refused"), State, SeatId, OutReason);
        return false;
    }

    FMarburgumVehicleOccupant& Entry = Occupants.AddDefaulted_GetRef();
    Entry.SeatId = SeatId;
    Entry.Occupant = State;
    Who.SeatAuthority(this, SeatId);
    if (SeatId == GetDriverSeatId())
    {
        Throttle = 0.f;
        Brake = 0.f;
        Steer = 0.f;
        bHandbrake = false;
    }
    OnRep_Occupants();
    LogAuthority(TEXT("entered"), State, SeatId, NAME_None);
    return true;
}

bool AMarburgumVehicle::TryExitAuthority(AMarburgumCharacter& Who, FName& OutReason)
{
    APlayerState* State = Who.GetPlayerState();
    const FName SeatId = FindSeatOf(State);
    OutReason = NAME_None;
    FVector Spot;
    if (!HasAuthority() || SeatId.IsNone() || Who.GetSeatedVehicle() != this)
    {
        OutReason = TEXT("not_seated");
    }
    else if (!FindExitSpot(SeatId, Who, Spot))
    {
        OutReason = TEXT("no_room");
    }
    if (!OutReason.IsNone())
    {
        LogAuthority(TEXT("exit_refused"), State, SeatId, OutReason);
        return false;
    }
    // Where the server took this player's view to be, from the car: the camera behind it while seated, not
    // where the player got in (ADR-056).
    if (const APlayerController* Viewer = Cast<APlayerController>(Who.GetController()))
    {
        FVector ViewAt;
        FRotator ViewRotation;
        Viewer->GetPlayerViewPoint(ViewAt, ViewRotation);
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=exit_view vehicle=%s player=%d view_m=%.1f"), LogPrefix,
            *VehicleId.ToString(), State->GetPlayerId(), FVector::Dist(ViewAt, GetActorLocation()) / Cm);
    }
    ReleaseAuthority(State);
    // Step out facing away from the car, the way the door opens.
    const FVector Away = (Spot - GetSeatLocation(SeatId)).GetSafeNormal2D();
    Who.UnseatAuthority(Spot, Away.IsNearlyZero() ? GetActorRotation().Yaw : Away.Rotation().Yaw);
    LogAuthority(TEXT("exited"), State, SeatId, NAME_None);
    return true;
}

void AMarburgumVehicle::ReleaseAuthority(const APlayerState* Who)
{
    const FName SeatId = FindSeatOf(Who);
    if (SeatId.IsNone())
    {
        return;
    }
    Occupants.RemoveAll([Who](const FMarburgumVehicleOccupant& Entry) { return Entry.Occupant == Who; });
    if (SeatId == GetDriverSeatId())
    {
        // Nobody at the wheel: the car holds its brake rather than rolling off.
        Throttle = 0.f;
        Brake = 1.f;
        Steer = 0.f;
        bHandbrake = false;
        bInputLive = false;
    }
    OnRep_Occupants();
}

void AMarburgumVehicle::PruneOccupantsAuthority()
{
    TArray<const APlayerState*> Gone;
    for (const FMarburgumVehicleOccupant& Entry : Occupants)
    {
        const AMarburgumCharacter* Rider = Entry.Occupant ? Cast<AMarburgumCharacter>(Entry.Occupant->GetPawn()) : nullptr;
        if (!Rider || Rider->GetSeatedVehicle() != this)
        {
            Gone.Add(Entry.Occupant);
        }
    }
    for (const APlayerState* State : Gone)
    {
        LogAuthority(TEXT("seat_released"), State, FindSeatOf(State), TEXT("left_world"));
        if (State)
        {
            ReleaseAuthority(State);
        }
        else
        {
            Occupants.RemoveAll([](const FMarburgumVehicleOccupant& Entry) { return Entry.Occupant == nullptr; });
            OnRep_Occupants();
        }
    }
}

bool AMarburgumVehicle::FindExitSpot(FName SeatId, const AMarburgumCharacter& Who, FVector& OutSpot) const
{
    const FVehicleDefinition* Definition = GetDefinition();
    const UCapsuleComponent* Capsule = Who.GetCapsuleComponent();
    if (!Definition || !Capsule)
    {
        return false;
    }
    // Out through this seat's door first, then the other doors, then off the nose and the tail.
    TArray<FVector> Tries;
    Tries.Add(GetDoorLocation(SeatId));
    for (int32 Index = 0; Index < Definition->SeatCount; ++Index)
    {
        if (SeatId != FName(Definition->Seats[Index].Id))
        {
            Tries.Add(GetDoorLocation(FName(Definition->Seats[Index].Id)));
        }
    }
    const double Clear = (Definition->BodyLength * 0.5 + DoorStepOut) * Cm;
    Tries.Add(GetActorLocation() + GetActorForwardVector() * Clear);
    Tries.Add(GetActorLocation() - GetActorForwardVector() * Clear);

    const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
    const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), HalfHeight);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumVehicleExit), false);
    IgnoreRiders(*this, Params);
    for (const FVector& Door : Tries)
    {
        FHitResult Ground;
        FVector Spot;
        float Surface = 0.f;
        if (GetWorld()->LineTraceSingleByChannel(Ground, Door + FVector(0.0, 0.0, ExitProbeUpCm), Door - FVector(0.0, 0.0, ExitProbeDownCm),
                ECC_Visibility, Params))
        {
            Spot = Ground.ImpactPoint + FVector(0.0, 0.0, HalfHeight + 2.0);
        }
        else if (IsInWater(Door, Surface))
        {
            Spot = FVector(Door.X, Door.Y, Surface);
        }
        else
        {
            continue;
        }
        if (!GetWorld()->OverlapBlockingTestByChannel(Spot, FQuat::Identity, ECC_Pawn, Shape, Params))
        {
            OutSpot = Spot;
            return true;
        }
    }
    return false;
}

int32 AMarburgumVehicle::DoorForSeat(FName SeatId) const
{
    // Doors: 0 front left, 1 front right, 2 rear left, 3 rear right. A seat without its own door uses the
    // front one on its side (the van's and lorry's cab doors).
    const FVehicleDefinition* Definition = GetDefinition();
    const FSeatDefinition* Seat = Definition ? FindSeat(*Definition, SeatId.ToString()) : nullptr;
    if (!Seat)
    {
        return INDEX_NONE;
    }
    const FVector Point = SeatPointCm(*Seat);
    const int32 Side = Point.Y < 0.0 ? 0 : 1;
    const int32 Rear = Point.X < 0.0 ? 2 : 0;
    const int32 Slot = Side + Rear;
    if (Rear && !Doors[Slot]->IsVisible())
    {
        return Side;
    }
    return Doors[Slot]->IsVisible() ? Slot : INDEX_NONE;
}

void AMarburgumVehicle::LogAuthority(const TCHAR* Event, const APlayerState* Who, FName SeatId, FName Reason) const
{
    // Seat names print as the catalogue spells them; an FName keeps whichever case the process saw first.
    const FVehicleDefinition* Definition = GetDefinition();
    const FSeatDefinition* Seat = Definition ? FindSeat(*Definition, SeatId.ToString()) : nullptr;
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=%s vehicle=%s player=%d seat=%s reason=%s location=%s speed=%.2f"), LogPrefix,
        Event, *VehicleId.ToString(), Who ? Who->GetPlayerId() : -1, Seat ? Seat->Id : *SeatId.ToString(),
        Reason.IsNone() ? TEXT("-") : *Reason.ToString(), *GetActorLocation().ToString(), Motion.ForwardSpeed);
}

void AMarburgumVehicle::GetAll(const UWorld* World, TArray<AMarburgumVehicle*>& Out)
{
    Out.Reset();
    if (World)
    {
        for (TActorIterator<AMarburgumVehicle> It(World); It; ++It)
        {
            Out.Add(*It);
        }
    }
}

// ---- Client presentation ----

void AMarburgumVehicle::FollowServerMotion(float DeltaSeconds)
{
    if (Motion.Location.IsZero() && Motion.Rotation.Equals(FQuat::Identity))
    {
        return; // Nothing from the server yet: stay where the level put the car.
    }
    const FVector Target = Motion.Location;
    const FVector Current = GetActorLocation();
    if (!bHasMotion || FVector::Distance(Current, Target) > ServerStateSnap * Cm)
    {
        bHasMotion = true;
        SetActorLocationAndRotation(Target, Motion.Rotation, false, nullptr, ETeleportType::TeleportPhysics);
        return;
    }
    // A 30 Hz report should not show up as 30 Hz motion (the original's CharacterView-style blend).
    const float Blend = FMath::Min(1.f, static_cast<float>(ServerStateSharpness) * DeltaSeconds);
    const FQuat Rotation = FQuat::Slerp(GetActorQuat(), Motion.Rotation, Blend);
    SetActorLocationAndRotation(FMath::Lerp(Current, Target, Blend), Rotation, false, nullptr, ETeleportType::TeleportPhysics);
}

void AMarburgumVehicle::UpdateWheels(float DeltaSeconds)
{
    const FVehicleDefinition* Definition = GetDefinition();
    if (!Definition)
    {
        return;
    }
    const FTransform Transform = GetActorTransform();
    const FVector Up = Transform.GetUnitAxis(EAxis::Z);
    const double Reach = (Definition->SuspensionRestLength + Definition->WheelRadius) * Cm;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumVehicleWheelView), false);
    IgnoreRiders(*this, Params);

    // Rolling: the road speed over the tyre's radius.
    WheelSpin = FMath::Fmod(WheelSpin + static_cast<float>(Motion.ForwardSpeed / Definition->WheelRadius) * DeltaSeconds, 2.f * PI);
    const float SteerDegrees = FMath::RadiansToDegrees(Motion.SteerAngle);
    for (int32 Wheel = 0; Wheel < WheelCount; ++Wheel)
    {
        const FVector Mount = WheelAttachCm(*Definition, Wheel);
        const FVector Attach = Transform.TransformPosition(Mount);
        FHitResult Hit;
        double Drop = Definition->SuspensionRestLength * Cm; // hanging at full droop
        if (GetWorld()->LineTraceSingleByChannel(Hit, Attach, Attach - Up * Reach, ECC_Visibility, Params))
        {
            Drop = FMath::Max(0.0, Hit.Distance - Definition->WheelRadius * Cm);
        }
        Wheels[Wheel]->SetRelativeLocationAndRotation(Mount - FVector(0.0, 0.0, Drop),
            FRotator(-FMath::RadiansToDegrees(WheelSpin), Dynamics::IsFrontWheel(Wheel) ? SteerDegrees : 0.f, 0.f));
    }
}

void AMarburgumVehicle::OnRep_Occupants()
{
    if (GetNetMode() == NM_DedicatedServer)
    {
        return;
    }
    // A seat taken or left swings its door open while the occupant steps through (end_seat_pass).
    TSet<FName> Now;
    for (const FMarburgumVehicleOccupant& Entry : Occupants)
    {
        Now.Add(Entry.SeatId);
    }
    for (const FName& Seat : Now.Difference(ShownSeats).Union(ShownSeats.Difference(Now)))
    {
        const int32 Door = DoorForSeat(Seat);
        if (Door != INDEX_NONE)
        {
            DoorHold[Door] = static_cast<float>(DoorHoldSeconds);
        }
    }
    ShownSeats = MoveTemp(Now);
}

void AMarburgumVehicle::AnimateDoors(float DeltaSeconds)
{
    for (int32 Door = 0; Door < WheelCount; ++Door)
    {
        DoorHold[Door] = FMath::Max(0.f, DoorHold[Door] - DeltaSeconds);
        const float Target = DoorHold[Door] > 0.f ? static_cast<float>(DoorOpenRadians) : 0.f;
        const float Before = DoorAngle[Door];
        DoorAngle[Door] = FMath::FInterpConstantTo(DoorAngle[Door], Target, DeltaSeconds, static_cast<float>(DoorRadiansPerSecond));
        if (Before != DoorAngle[Door])
        {
            // Hinged on the forward edge: a left door's rear edge swings out to the left, a right door's to the right.
            const float Sign = Door % 2 == 0 ? 1.f : -1.f;
            DoorHinges[Door]->SetRelativeRotation(FRotator(0.f, Sign * FMath::RadiansToDegrees(DoorAngle[Door]), 0.f));
        }
    }
}
