#pragma once

#include "CoreMinimal.h"

// Vehicles carried forward from the original Marburgum (ADR-039). Engine-neutral: the definitions are the
// original project/data/vehicles.json (version 1) copied field for field into
// Data/Gameplay/vehicles.v0.1.json, and `mb2 validate vehicles` keeps this header in step with it.
//
// Every value is in the original's units and axes, so the two can be compared by eye: metres, newtons,
// kilograms, degrees for the steering lock, and in-vehicle offsets in the Godot vehicle frame (x right,
// y up, -z forward, origin at the suspension mounts' level). The conversions to Unreal centimetres and
// axes live in this header and nowhere else.
namespace MarburgumII::Vehicles
{
    /** A seat: where its occupant sits and the direction out of the car through its door. */
    struct FSeatDefinition
    {
        const TCHAR* Id = TEXT("");
        const TCHAR* Role = TEXT("passenger"); // "driver" or "passenger"
        double Offset[3] = { 0.0, 0.0, 0.0 };
        double Door[3] = { -1.0, 0.0, 0.0 };
    };

    inline constexpr int32 MaxSeats = 4;
    inline constexpr int32 WheelCount = 4; // front left, front right, rear left, rear right

    /**
     * One driveable vehicle. Values are real where the real car settles the question (dimensions,
     * wheelbase, track, wheel size, top speed) and tuned arcade-believable where the original roadmap asked
     * for that (engine force, grip, suspension). The bodywork's side-elevation profile and paint stay in
     * the data file; only the editor commandlet that builds the meshes reads them.
     */
    struct FVehicleDefinition
    {
        const TCHAR* Id = TEXT("");
        const TCHAR* Name = TEXT("");
        const TCHAR* Class = TEXT("car");
        double Mass = 0.0;
        double CentreOfMass[3] = { 0.0, 0.0, 0.0 };
        double BodyLength = 0.0;
        double BodyWidth = 0.0;
        double BodyHeight = 0.0;
        double Wheelbase = 0.0;
        double TrackWidth = 0.0;
        double WheelRadius = 0.0;
        bool bFrontWheelDrive = true;
        double EngineForce = 0.0;
        double MaxSpeed = 0.0;
        double ReverseSpeed = 0.0;
        double BrakeForce = 0.0;
        double HandbrakeForce = 0.0;
        double SteeringAngle = 0.0;
        double SteeringSpeed = 0.0;
        double Grip = 0.0;
        double WetGrip = 0.0;
        double HandbrakeGrip = 0.0;
        double SuspensionRestLength = 0.0;
        double SuspensionStrength = 0.0;
        double SuspensionDamping = 0.0;
        double SuspensionTravel = 0.0;
        double Drag = 0.0;
        double RollingResistance = 0.0;
        double EnterRange = 0.0;
        int32 SeatCount = 0;
        FSeatDefinition Seats[MaxSeats];
    };

    // The original's five definitions, in data-file order. The first is the prototype the original drove
    // first: the city hatchback, measured from a Renault 4 GTL assembled at IMV/Revoz in Novo mesto.
    inline constexpr FVehicleDefinition Definitions[] = {
        {
            .Id = TEXT("city_hatchback_a"),
            .Name = TEXT("City hatchback"),
            .Class = TEXT("car"),
            .Mass = 780.0,
            .CentreOfMass = { 0.0, -0.35, 0.0 },
            .BodyLength = 3.66,
            .BodyWidth = 1.49,
            .BodyHeight = 1.44,
            .Wheelbase = 2.4,
            .TrackWidth = 1.25,
            .WheelRadius = 0.29,
            .bFrontWheelDrive = true,
            .EngineForce = 3400.0,
            .MaxSpeed = 33.0,
            .ReverseSpeed = 8.0,
            .BrakeForce = 5200.0,
            .HandbrakeForce = 9000.0,
            .SteeringAngle = 34.0,
            .SteeringSpeed = 2.8,
            .Grip = 6.8,
            .WetGrip = 4.0,
            .HandbrakeGrip = 1.5,
            .SuspensionRestLength = 0.42,
            .SuspensionStrength = 28000.0,
            .SuspensionDamping = 3000.0,
            .SuspensionTravel = 0.22,
            .Drag = 0.46,
            .RollingResistance = 12.0,
            .EnterRange = 3.5,
            .SeatCount = 4,
            .Seats = {
                { TEXT("driver"), TEXT("driver"), { -0.33, 0.1, -0.25 }, { -1.0, 0.0, -0.25 } },
                { TEXT("front_passenger"), TEXT("passenger"), { 0.33, 0.1, -0.25 }, { 1.0, 0.0, -0.25 } },
                { TEXT("rear_left"), TEXT("passenger"), { -0.33, 0.1, 0.65 }, { -1.0, 0.0, 0.65 } },
                { TEXT("rear_right"), TEXT("passenger"), { 0.33, 0.1, 0.65 }, { 1.0, 0.0, 0.65 } },
            },
        },
        {
            .Id = TEXT("city_sedan_a"),
            .Name = TEXT("City saloon"),
            .Class = TEXT("car"),
            .Mass = 1180.0,
            .CentreOfMass = { 0.0, -0.38, -0.18 },
            .BodyLength = 4.3,
            .BodyWidth = 1.68,
            .BodyHeight = 1.4,
            .Wheelbase = 2.55,
            .TrackWidth = 1.42,
            .WheelRadius = 0.3,
            .bFrontWheelDrive = true,
            .EngineForce = 4800.0,
            .MaxSpeed = 42.0,
            .ReverseSpeed = 7.5,
            .BrakeForce = 7800.0,
            .HandbrakeForce = 11000.0,
            .SteeringAngle = 28.0,
            .SteeringSpeed = 2.1,
            .Grip = 7.6,
            .WetGrip = 4.6,
            .HandbrakeGrip = 1.8,
            .SuspensionRestLength = 0.4,
            .SuspensionStrength = 40000.0,
            .SuspensionDamping = 4200.0,
            .SuspensionTravel = 0.18,
            .Drag = 0.36,
            .RollingResistance = 11.0,
            .EnterRange = 3.6,
            .SeatCount = 4,
            .Seats = {
                { TEXT("driver"), TEXT("driver"), { -0.36, 0.1, -0.2 }, { -1.05, 0.0, -0.2 } },
                { TEXT("front_passenger"), TEXT("passenger"), { 0.36, 0.1, -0.2 }, { 1.05, 0.0, -0.2 } },
                { TEXT("rear_left"), TEXT("passenger"), { -0.36, 0.1, 0.85 }, { -1.05, 0.0, 0.85 } },
                { TEXT("rear_right"), TEXT("passenger"), { 0.36, 0.1, 0.85 }, { 1.05, 0.0, 0.85 } },
            },
        },
        {
            .Id = TEXT("city_estate_a"),
            .Name = TEXT("City estate"),
            .Class = TEXT("car"),
            .Mass = 1280.0,
            .CentreOfMass = { 0.0, -0.3, 0.28 },
            .BodyLength = 4.45,
            .BodyWidth = 1.7,
            .BodyHeight = 1.45,
            .Wheelbase = 2.62,
            .TrackWidth = 1.42,
            .WheelRadius = 0.3,
            .bFrontWheelDrive = true,
            .EngineForce = 4300.0,
            .MaxSpeed = 36.0,
            .ReverseSpeed = 7.0,
            .BrakeForce = 7600.0,
            .HandbrakeForce = 11500.0,
            .SteeringAngle = 27.0,
            .SteeringSpeed = 1.9,
            .Grip = 6.9,
            .WetGrip = 3.9,
            .HandbrakeGrip = 1.4,
            .SuspensionRestLength = 0.46,
            .SuspensionStrength = 32000.0,
            .SuspensionDamping = 3300.0,
            .SuspensionTravel = 0.24,
            .Drag = 0.45,
            .RollingResistance = 15.0,
            .EnterRange = 3.7,
            .SeatCount = 4,
            .Seats = {
                { TEXT("driver"), TEXT("driver"), { -0.36, 0.1, -0.2 }, { -1.08, 0.0, -0.2 } },
                { TEXT("front_passenger"), TEXT("passenger"), { 0.36, 0.1, -0.2 }, { 1.08, 0.0, -0.2 } },
                { TEXT("rear_left"), TEXT("passenger"), { -0.36, 0.1, 0.9 }, { -1.08, 0.0, 0.9 } },
                { TEXT("rear_right"), TEXT("passenger"), { 0.36, 0.1, 0.9 }, { 1.08, 0.0, 0.9 } },
            },
        },
        {
            .Id = TEXT("city_van_a"),
            .Name = TEXT("City van"),
            .Class = TEXT("van"),
            .Mass = 1680.0,
            .CentreOfMass = { 0.0, -0.18, -0.55 },
            .BodyLength = 4.6,
            .BodyWidth = 1.85,
            .BodyHeight = 1.95,
            .Wheelbase = 2.5,
            .TrackWidth = 1.58,
            .WheelRadius = 0.32,
            .bFrontWheelDrive = true,
            .EngineForce = 3600.0,
            .MaxSpeed = 26.0,
            .ReverseSpeed = 5.5,
            .BrakeForce = 8200.0,
            .HandbrakeForce = 13000.0,
            .SteeringAngle = 24.0,
            .SteeringSpeed = 1.5,
            .Grip = 5.9,
            .WetGrip = 3.2,
            .HandbrakeGrip = 1.3,
            .SuspensionRestLength = 0.5,
            .SuspensionStrength = 38000.0,
            .SuspensionDamping = 3600.0,
            .SuspensionTravel = 0.26,
            .Drag = 0.62,
            .RollingResistance = 20.0,
            .EnterRange = 4.0,
            .SeatCount = 3,
            .Seats = {
                { TEXT("driver"), TEXT("driver"), { -0.4, 0.25, -1.2 }, { -1.15, 0.0, -1.2 } },
                { TEXT("front_passenger"), TEXT("passenger"), { 0.4, 0.25, -1.2 }, { 1.15, 0.0, -1.2 } },
                { TEXT("rear_left"), TEXT("passenger"), { -0.4, 0.25, 0.4 }, { -1.15, 0.0, 0.4 } },
            },
        },
        {
            .Id = TEXT("city_truck_a"),
            .Name = TEXT("City lorry"),
            .Class = TEXT("truck"),
            .Mass = 3800.0,
            .CentreOfMass = { 0.0, -0.12, 0.7 },
            .BodyLength = 6.8,
            .BodyWidth = 2.3,
            .BodyHeight = 3.0,
            .Wheelbase = 3.5,
            .TrackWidth = 1.95,
            .WheelRadius = 0.45,
            .bFrontWheelDrive = false,
            .EngineForce = 5600.0,
            .MaxSpeed = 20.0,
            .ReverseSpeed = 4.0,
            .BrakeForce = 14000.0,
            .HandbrakeForce = 18000.0,
            .SteeringAngle = 22.0,
            .SteeringSpeed = 1.2,
            .Grip = 5.2,
            .WetGrip = 2.8,
            .HandbrakeGrip = 1.1,
            .SuspensionRestLength = 0.64,
            .SuspensionStrength = 78000.0,
            .SuspensionDamping = 8200.0,
            .SuspensionTravel = 0.3,
            .Drag = 0.78,
            .RollingResistance = 32.0,
            .EnterRange = 4.5,
            .SeatCount = 2,
            .Seats = {
                { TEXT("driver"), TEXT("driver"), { -0.5, 0.5, -2.2 }, { -1.4, 0.0, -2.2 } },
                { TEXT("front_passenger"), TEXT("passenger"), { 0.5, 0.5, -2.2 }, { 1.4, 0.0, -2.2 } },
            },
        },
    };

    /** The prototype hatchback; the authored Lent car is one. */
    inline constexpr const TCHAR* PrototypeDefinitionId = TEXT("city_hatchback_a");

    inline const FVehicleDefinition* FindDefinition(const FString& Id)
    {
        for (const FVehicleDefinition& Definition : Definitions)
        {
            if (Id == Definition.Id)
            {
                return &Definition;
            }
        }
        return nullptr;
    }

    inline const FSeatDefinition* FindSeat(const FVehicleDefinition& Definition, const FString& SeatId)
    {
        for (int32 Index = 0; Index < Definition.SeatCount; ++Index)
        {
            if (SeatId == Definition.Seats[Index].Id)
            {
                return &Definition.Seats[Index];
            }
        }
        return nullptr;
    }

    /** The driver's seat (every definition has exactly one; the validator checks). */
    inline const FSeatDefinition* DriverSeat(const FVehicleDefinition& Definition)
    {
        for (int32 Index = 0; Index < Definition.SeatCount; ++Index)
        {
            if (FCString::Strcmp(Definition.Seats[Index].Role, TEXT("driver")) == 0)
            {
                return &Definition.Seats[Index];
            }
        }
        return nullptr;
    }

    // ---- Rules the original kept in code rather than data ----

    /** Where the suspension is mounted, in the vehicle frame: 10 cm below the origin (vehicle.cpp build). */
    inline constexpr double WheelAttachY = -0.1;

    /** The collision box: this share of the body height, lifted this far above the origin (vehicle.cpp). */
    inline constexpr double CollisionHeightFraction = 0.6;
    inline constexpr double CollisionLift = 0.25;

    /** A door point is the seat stepped out this far along the door direction (Vehicle::get_door_position). */
    inline constexpr double DoorStepOut = 1.2;

    /** The driver's intent persists this long, then the car rests on its brake (protocol.h VEHICLE_INPUT_EXPIRY_MSEC). */
    inline constexpr double InputExpirySeconds = 0.4;
    /** The client resends held pedals this often (protocol.h DIRECTION_KEEPALIVE_MSEC). */
    inline constexpr double InputKeepaliveSeconds = 0.1;
    /** Pedal and wheel changes smaller than this are not worth sending (player_controller.cpp). */
    inline constexpr double InputEpsilon = 0.02;
    /** Back is one pedal: it brakes while the car rolls forward faster than this (m/s), and reverses below it. */
    inline constexpr double ReverseThreshold = 0.6;

    /** Clients slide the car toward the server's report at this rate per second, or snap past this many metres. */
    inline constexpr double ServerStateSharpness = 12.0;
    inline constexpr double ServerStateSnap = 8.0;

    /** Body damping the original set on the rigid body: none on translation, a little on rotation. */
    inline constexpr double AngularDamping = 0.3;

    // Driving camera (player_camera.cpp): farther and higher than on foot, swinging back behind the car
    // unless the player looked around in the last 2.5 s, and leading the car a little at speed.
    inline constexpr double CameraDistance = 11.0;
    inline constexpr double CameraPitchDegrees = 28.0;
    inline constexpr double CameraFocusHeight = 1.0;
    inline constexpr double CameraHeadingSharpness = 3.5;
    inline constexpr double CameraFreeLookSeconds = 2.5;
    inline constexpr double CameraLookAheadSeconds = 0.12;
    inline constexpr double CameraMaxLookAhead = 3.5;

    // Doors (Vehicle::animate_doors, end_seat_pass): a door swings 1.15 rad open at 2.4 rad/s when its seat
    // is taken or left, stays open 1.6 s while the occupant steps through, and shuts.
    inline constexpr double DoorOpenRadians = 1.15;
    inline constexpr double DoorRadiansPerSecond = 2.4;
    inline constexpr double DoorHoldSeconds = 1.6;

    // ---- Axes ----

    inline constexpr double CentimetresPerMetre = 100.0;

    /**
     * A direction in the Godot vehicle frame (x right, y up, -z forward) in Unreal's (X forward, Y right,
     * Z up). Forward stays forward and right stays right; the handedness flips.
     */
    inline FVector DirectionToUnreal(const double (&Godot)[3])
    {
        return FVector(-Godot[2], Godot[0], Godot[1]);
    }

    inline FVector DirectionToUnreal(double X, double Y, double Z)
    {
        return FVector(-Z, X, Y);
    }

    /**
     * A point in the vehicle, in the Unreal actor's frame: centimetres from the actor origin, which is the
     * centre of the collision box, CollisionLift above the original's origin.
     */
    inline FVector LocalPointCm(double X, double Y, double Z)
    {
        return DirectionToUnreal(X, Y - CollisionLift, Z) * CentimetresPerMetre;
    }

    inline FVector LocalPointCm(const double (&Godot)[3])
    {
        return LocalPointCm(Godot[0], Godot[1], Godot[2]);
    }

    /** Suspension mount of wheel 0..3 (front left, front right, rear left, rear right), actor frame, cm. */
    inline FVector WheelAttachCm(const FVehicleDefinition& Definition, int32 Wheel)
    {
        const double HalfTrack = Definition.TrackWidth * 0.5;
        const double HalfWheelbase = Definition.Wheelbase * 0.5;
        const double X = (Wheel % 2 == 0) ? -HalfTrack : HalfTrack;
        const double Z = Wheel < 2 ? -HalfWheelbase : HalfWheelbase;
        return LocalPointCm(X, WheelAttachY, Z);
    }

    /** How far the actor origin stands above level ground on unloaded springs, cm. */
    inline double RideHeightCm(const FVehicleDefinition& Definition)
    {
        return (Definition.SuspensionRestLength + Definition.WheelRadius - WheelAttachY + CollisionLift) * CentimetresPerMetre;
    }

    inline FVector SeatPointCm(const FSeatDefinition& Seat)
    {
        return LocalPointCm(Seat.Offset);
    }

    /** Where someone getting in or out of this seat stands, actor frame, cm. */
    inline FVector DoorPointCm(const FSeatDefinition& Seat)
    {
        const FVector Out = DirectionToUnreal(Seat.Door).GetSafeNormal();
        return SeatPointCm(Seat) + Out * DoorStepOut * CentimetresPerMetre;
    }

    /** Collision box half extents, cm (X length, Y width, Z height). */
    inline FVector CollisionHalfExtentCm(const FVehicleDefinition& Definition)
    {
        return FVector(Definition.BodyLength, Definition.BodyWidth, Definition.BodyHeight * CollisionHeightFraction)
            * 0.5 * CentimetresPerMetre;
    }

    // ---- Asset paths written by the MarburgumVehicleContent commandlet ----

    inline FString BodyMeshPath(const FString& Id)
    {
        return FString::Printf(TEXT("/Game/Vehicles/%s/SM_%s_Body.SM_%s_Body"), *Id, *Id, *Id);
    }

    inline FString WheelMeshPath(const FString& Id, int32 Wheel)
    {
        return FString::Printf(TEXT("/Game/Vehicles/%s/SM_%s_Wheel%d.SM_%s_Wheel%d"), *Id, *Id, Wheel, *Id, Wheel);
    }

    inline FString DoorMeshPath(const FString& Id, int32 Door)
    {
        return FString::Printf(TEXT("/Game/Vehicles/%s/SM_%s_Door%d.SM_%s_Door%d"), *Id, *Id, Door, *Id, Door);
    }

    inline constexpr const TCHAR* EnterClipPath = TEXT("/Game/Characters/Animations/A_EnterVehicle.A_EnterVehicle");
    inline constexpr const TCHAR* ExitClipPath = TEXT("/Game/Characters/Animations/A_ExitVehicle.A_ExitVehicle");

    /** Evidence log prefix for vehicle authority and smokes. */
    inline constexpr const TCHAR* LogPrefix = TEXT("MB2VEHICLE");
}
