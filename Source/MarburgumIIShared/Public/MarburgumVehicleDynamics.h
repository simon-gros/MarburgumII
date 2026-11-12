#pragma once

#include "CoreMinimal.h"
#include "MarburgumVehicleContract.h"

// The original game's car handling (ADR-039): a rigid chassis on four suspension raycasts. Each wheel
// casts down; a spring and damper hold the body up at the contact; the tyre pushes the car along its own
// forward direction and resists sliding sideways. Body roll, weight transfer under braking and the nose
// lifting under power come out of that arrangement rather than being animated.
//
// Ported from the original src/shared/vehicle.cpp (Vehicle::_integrate_forces, apply_wheel) through its
// Unreal port UnrealMarburgum MarburgumVehicleRules.cpp (Dynamics), which added the idle hold. Pure
// functions in SI units (metres, seconds, newtons), so the server's force step and the tests share them.
namespace MarburgumII::Vehicles::Dynamics
{
    /** A tyre can hold at most this multiple of the load pressing it onto the road sideways. */
    inline constexpr double LateralFrictionLimit = 1.4;
    /** Tyre forces are applied this share of the way down from the centre of mass, so the body rolls less. */
    inline constexpr double RollLever = 0.35;
    /** And drive and braking forces this share, so it pitches less. */
    inline constexpr double PitchLever = 0.15;
    /** Off the throttle and slower than this (m/s), the car holds where it is on the brake. */
    inline constexpr double IdleHoldSpeed = 0.5;
    /** In the river: buoyancy from this far above the origin, at most this deep, and the water's drag. */
    inline constexpr double WaterBuoyancyOffset = 0.35;
    inline constexpr double WaterMaxDepth = 1.4;
    inline constexpr double WaterDrag = 2.8;
    inline constexpr double Gravity = 9.81;
    /** A crash changes the body's velocity by at least this much between two reads (m/s); then 0.3 s of quiet. */
    inline constexpr double ImpactMinSpeedChange = 1.5;
    inline constexpr double ImpactQuietSeconds = 0.3;

    inline bool IsFrontWheel(int32 Wheel)
    {
        return Wheel == 0 || Wheel == 1;
    }

    inline double Toward(double From, double To, double Step)
    {
        return FMath::Abs(To - From) <= Step ? To : From + FMath::Sign(To - From) * Step;
    }

    /**
     * Where the front wheels point next, radians, positive to the right. They turn toward the input rather
     * than snapping to it, and turn less at speed: full lock at 100 km/h is not steering, it is a spin.
     */
    inline double StepSteerAngle(const FVehicleDefinition& Definition, double Current, double SteerInput, double ForwardSpeed,
        double DeltaSeconds)
    {
        if (!FMath::IsFinite(Current) || !FMath::IsFinite(SteerInput) || !FMath::IsFinite(ForwardSpeed)
            || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0)
        {
            return FMath::IsFinite(Current) ? Current : 0.0;
        }
        const double Lock = FMath::DegreesToRadians(Definition.SteeringAngle);
        const double SpeedFactor = 1.0 / (1.0 + FMath::Abs(ForwardSpeed) / 18.0);
        const double Wanted = FMath::Clamp(SteerInput, -1.0, 1.0) * Lock * (0.35 + 0.65 * SpeedFactor);
        return Toward(Current, Wanted, Definition.SteeringSpeed * Lock * DeltaSeconds);
    }

    /** Spring and damper along the body's up, never pulling the tyre onto the road. */
    inline double SuspensionLoad(const FVehicleDefinition& Definition, double Compression, double WheelSpeedAlongUp)
    {
        if (!FMath::IsFinite(Compression) || !FMath::IsFinite(WheelSpeedAlongUp))
        {
            return 0.0;
        }
        const double Spring = FMath::Max(0.0, Compression) * Definition.SuspensionStrength;
        const double Damper = -WheelSpeedAlongUp * Definition.SuspensionDamping;
        return FMath::Max(0.0, Spring + Damper);
    }

    /**
     * Sideways tyre force, newtons along the wheel's right. The handbrake takes the grip out of the back
     * wheels, which is what makes it a handbrake; past the load-dependent limit the tyre slides.
     */
    inline double LateralForce(const FVehicleDefinition& Definition, int32 Wheel, double SidewaysSpeed, double Load, bool bHandbrake,
        double SurfaceGrip)
    {
        if (!FMath::IsFinite(SidewaysSpeed) || !FMath::IsFinite(Load) || !FMath::IsFinite(SurfaceGrip))
        {
            return 0.0;
        }
        const double Surface = FMath::Max(0.0, SurfaceGrip);
        const double Grip = (bHandbrake && !IsFrontWheel(Wheel) ? Definition.HandbrakeGrip : Definition.Grip) * Surface;
        const double Share = Definition.Mass / WheelCount;
        const double Limit = LateralFrictionLimit * Surface * FMath::Max(0.0, Load);
        return FMath::Clamp(-SidewaysSpeed * Grip * Share, -Limit, Limit);
    }

    /** What a wheel's brakes can give along it, newtons: its share of the brake, and the handbrake at the rear. */
    inline double BrakeLimit(const FVehicleDefinition& Definition, int32 Wheel, double AppliedBrake, bool bHandbrake, double SurfaceTraction)
    {
        const double Traction = FMath::IsFinite(SurfaceTraction) ? FMath::Max(0.0, SurfaceTraction) : 0.0;
        double Limit = FMath::Clamp(AppliedBrake, 0.0, 1.0) * Definition.BrakeForce * Traction / WheelCount;
        if (bHandbrake && !IsFrontWheel(Wheel))
        {
            Limit += Definition.HandbrakeForce * Traction / WheelCount;
        }
        return Limit;
    }

    /**
     * Engine, brake and handbrake along the wheel, newtons (positive pushes the wheel's forward). ForwardSpeed
     * is the body's, signed; RollingSpeed is this wheel's along its own forward. Thrust falls away toward the
     * speed the engine can hold; a brake never pulls harder than it takes to stop the wheel this step, or the
     * car would drive off backwards.
     */
    inline double LongitudinalForce(const FVehicleDefinition& Definition, int32 Wheel, double ForwardSpeed, double RollingSpeed,
        double Throttle, double Brake, bool bHandbrake, double SurfaceTraction, double DeltaSeconds)
    {
        if (!FMath::IsFinite(ForwardSpeed) || !FMath::IsFinite(RollingSpeed) || !FMath::IsFinite(Throttle) || !FMath::IsFinite(Brake)
            || !FMath::IsFinite(SurfaceTraction) || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0)
        {
            return 0.0;
        }
        const bool bFront = IsFrontWheel(Wheel);
        const bool bDriven = Definition.bFrontWheelDrive ? bFront : !bFront;
        const double Pedal = FMath::Clamp(Throttle, -1.0, 1.0);
        const double Traction = FMath::Max(0.0, SurfaceTraction);
        double Force = 0.0;
        if (bDriven && Pedal != 0.0)
        {
            const double Top = Pedal > 0.0 ? Definition.MaxSpeed : Definition.ReverseSpeed;
            const double Used = Pedal > 0.0 ? FMath::Max(0.0, ForwardSpeed) : FMath::Max(0.0, -ForwardSpeed);
            const double Headroom = Top > 0.0 ? FMath::Clamp(1.0 - Used / Top, 0.0, 1.0) : 0.0;
            Force = Pedal * Definition.EngineForce * Headroom * Traction / (WheelCount / 2);
        }
        const double Stopping = BrakeLimit(Definition, Wheel, Brake, bHandbrake, Traction);
        if (Stopping > 0.0 && RollingSpeed != 0.0)
        {
            const double Share = Definition.Mass / WheelCount;
            const double Most = FMath::Abs(RollingSpeed) * Share / DeltaSeconds;
            Force -= FMath::Sign(RollingSpeed) * FMath::Min(Stopping, Most);
        }
        return Force;
    }

    /** Air drag, and rolling resistance while any wheel is on the ground; the caller applies it against the velocity. */
    inline double Resistance(const FVehicleDefinition& Definition, double GroundSpeed, int32 WheelsOnGround)
    {
        const double Speed = FMath::IsFinite(GroundSpeed) ? FMath::Abs(GroundSpeed) : 0.0;
        if (Speed <= 0.01)
        {
            return 0.0;
        }
        return Definition.Drag * Speed * Speed + (WheelsOnGround > 0 ? Definition.RollingResistance * Speed : 0.0);
    }

    /** The brake the car actually gets: full when nobody is pressing the throttle and it is all but stopped. */
    inline double HeldBrake(double Throttle, double Brake, double ForwardSpeed)
    {
        return Throttle == 0.0 && FMath::Abs(ForwardSpeed) < IdleHoldSpeed ? 1.0 : FMath::Clamp(Brake, 0.0, 1.0);
    }

    /** Off the throttle, all but stopped and braking (the idle hold included): the wheels hold the car where it stands. */
    inline bool IsHeld(double Throttle, double AppliedBrake, double ForwardSpeed)
    {
        return Throttle == 0.0 && AppliedBrake > 0.0 && FMath::Abs(ForwardSpeed) < IdleHoldSpeed;
    }

    /**
     * A held wheel grips like static friction, newtons along one direction: the force that stops its share
     * of the car within this step, the slope's pull included, up to Limit (what the brake gives along the
     * wheel, or the tyre across it). SlopeAccel is gravity along that direction, m/s^2.
     *
     * Marburgum II's addition (ADR-039). The original's brake and tyre are viscous, so a parked car on a
     * slope settles at the speed where they balance the pull: on Lent's 1.4 degree cross-fall it slid
     * sideways at about 3.5 cm/s.
     */
    inline double HoldForce(double Speed, double SlopeAccel, double Share, double DeltaSeconds, double Limit)
    {
        if (!FMath::IsFinite(Speed) || !FMath::IsFinite(SlopeAccel) || !FMath::IsFinite(Share) || !FMath::IsFinite(Limit)
            || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0 || Limit <= 0.0)
        {
            return 0.0;
        }
        return FMath::Clamp(-(Speed / DeltaSeconds + SlopeAccel) * Share, -Limit, Limit);
    }

    /** Upward push of the river on a car whose origin is Depth metres under WaterBuoyancyOffset above the surface. */
    inline double Buoyancy(const FVehicleDefinition& Definition, double Depth)
    {
        return Depth > 0.0 ? Definition.Mass * Gravity * FMath::Clamp(Depth, 0.0, WaterMaxDepth) : 0.0;
    }

    inline bool IsImpact(double SpeedChange)
    {
        return FMath::IsFinite(SpeedChange) && SpeedChange >= ImpactMinSpeedChange;
    }

    /**
     * Back is one pedal (player_controller.cpp update_driving): forward is the throttle; back brakes while
     * the car rolls forward and reverses once it has stopped. Out: throttle in [-1, 1], brake in [0, 1].
     */
    inline void Pedals(double Forward, double ForwardSpeed, double& OutThrottle, double& OutBrake)
    {
        OutThrottle = FMath::Max(0.0, Forward);
        OutBrake = 0.0;
        if (Forward < 0.0)
        {
            if (ForwardSpeed > ReverseThreshold)
            {
                OutBrake = -Forward;
            }
            else
            {
                OutThrottle = Forward;
            }
        }
    }

    // ---- Surface response (original src/shared/surface_response.cpp) ----
    //
    // Physical surfaces scale tyre grip and drive/brake traction; wetness (0..1, server-owned world state)
    // takes a share of each away. An unknown surface stays neutral, so missing semantic coverage never
    // silently changes handling. Surfaces come from MarburgumGround.h, wetness from the server's weather (ADR-061).

    inline double CleanWetness(double Wetness)
    {
        return FMath::Clamp(FMath::IsFinite(Wetness) ? Wetness : 0.0, 0.0, 1.0);
    }

    inline double WetRetention(const FString& Surface, bool bTraction)
    {
        if (Surface == TEXT("asphalt")) return bTraction ? 0.82 : 0.78;
        if (Surface == TEXT("concrete") || Surface == TEXT("paving")) return bTraction ? 0.84 : 0.82;
        if (Surface == TEXT("cobblestone")) return bTraction ? 0.76 : 0.74;
        if (Surface == TEXT("gravel") || Surface == TEXT("rail_ballast")) return bTraction ? 0.94 : 0.92;
        if (Surface == TEXT("grass") || Surface == TEXT("meadow")) return bTraction ? 0.66 : 0.68;
        if (Surface == TEXT("forest_floor")) return bTraction ? 0.70 : 0.72;
        if (Surface == TEXT("soil")) return bTraction ? 0.58 : 0.62;
        if (Surface == TEXT("sports_surface")) return bTraction ? 0.82 : 0.80;
        return 1.0;
    }

    inline double DrySurface(const FString& Surface, bool bTraction)
    {
        if (Surface == TEXT("cobblestone")) return bTraction ? 0.90 : 0.88;
        if (Surface == TEXT("gravel") || Surface == TEXT("rail_ballast")) return bTraction ? 0.68 : 0.72;
        if (Surface == TEXT("grass") || Surface == TEXT("meadow") || Surface == TEXT("forest_floor")) return bTraction ? 0.55 : 0.62;
        if (Surface == TEXT("soil")) return bTraction ? 0.45 : 0.55;
        if (Surface == TEXT("sports_surface")) return bTraction ? 0.95 : 0.90;
        if (Surface == TEXT("water")) return bTraction ? 0.05 : 0.08;
        return 1.0; // asphalt, concrete, paving and anything unknown
    }

    inline double SurfaceGrip(const FString& Surface, double Wetness)
    {
        return DrySurface(Surface, false) * (1.0 + (WetRetention(Surface, false) - 1.0) * CleanWetness(Wetness));
    }

    inline double SurfaceTraction(const FString& Surface, double Wetness)
    {
        return DrySurface(Surface, true) * (1.0 + (WetRetention(Surface, true) - 1.0) * CleanWetness(Wetness));
    }
}
