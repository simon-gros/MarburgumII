#include "MarburgumVehicleContract.h"
#include "MarburgumVehicleDynamics.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include <limits>

using namespace MarburgumII::Vehicles;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumVehicleDynamicsTest,
    "Marburgum.Vehicles.Dynamics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarburgumVehicleDynamicsTest::RunTest(const FString& Parameters)
{
    const FVehicleDefinition* Hatchback = FindDefinition(PrototypeDefinitionId);
    if (!TestNotNull(TEXT("Prototype hatchback defined"), Hatchback))
    {
        return false;
    }
    const FVehicleDefinition& Car = *Hatchback;
    constexpr double Step = 1.0 / 60.0;

    // Catalogue shape the actor relies on.
    for (const FVehicleDefinition& Definition : Definitions)
    {
        TestNotNull(*FString::Printf(TEXT("%s has a driver's seat"), Definition.Id), DriverSeat(Definition));
        TestTrue(*FString::Printf(TEXT("%s seat count in range"), Definition.Id), Definition.SeatCount >= 1 && Definition.SeatCount <= MaxSeats);
    }
    TestNull(TEXT("Unknown definition"), FindDefinition(TEXT("tram")));
    TestNull(TEXT("Unknown seat"), FindSeat(Car, TEXT("roof")));

    // Axes: Godot -z forward is Unreal +X; x right stays +Y; y up is +Z.
    TestTrue(TEXT("Forward maps to +X"), DirectionToUnreal(0.0, 0.0, -1.0).Equals(FVector(1, 0, 0)));
    TestTrue(TEXT("Right maps to +Y"), DirectionToUnreal(1.0, 0.0, 0.0).Equals(FVector(0, 1, 0)));
    TestTrue(TEXT("Up maps to +Z"), DirectionToUnreal(0.0, 1.0, 0.0).Equals(FVector(0, 0, 1)));
    const FSeatDefinition& Driver = *DriverSeat(Car);
    TestTrue(TEXT("Driver sits on the left"), SeatPointCm(Driver).Y < 0.0);
    TestTrue(TEXT("Driver's door is outside the body"), FMath::Abs(DoorPointCm(Driver).Y) > CollisionHalfExtentCm(Car).Y);
    TestTrue(TEXT("Front wheels ahead of rear"), WheelAttachCm(Car, 0).X > WheelAttachCm(Car, 2).X);
    TestTrue(TEXT("Left wheels on the left"), WheelAttachCm(Car, 0).Y < 0.0 && WheelAttachCm(Car, 1).Y > 0.0);
    TestTrue(TEXT("Ride height clears the wheel"), RideHeightCm(Car) > Car.WheelRadius * 100.0);

    // Steering: toward the input at the steering rate, less lock at speed, never NaN.
    const double OneStep = Dynamics::StepSteerAngle(Car, 0.0, 1.0, 0.0, Step);
    TestTrue(TEXT("Steering turns right gradually"), OneStep > 0.0 && OneStep < FMath::DegreesToRadians(Car.SteeringAngle));
    double Settled = 0.0;
    for (int32 Index = 0; Index < 120; ++Index)
    {
        Settled = Dynamics::StepSteerAngle(Car, Settled, 1.0, 0.0, Step);
    }
    TestEqual(TEXT("Full lock at rest"), Settled, FMath::DegreesToRadians(Car.SteeringAngle), 1e-9);
    double Fast = 0.0;
    for (int32 Index = 0; Index < 120; ++Index)
    {
        Fast = Dynamics::StepSteerAngle(Car, Fast, 1.0, 30.0, Step);
    }
    TestTrue(TEXT("Less lock at speed"), Fast < Settled * 0.7);
    TestEqual(TEXT("NaN input keeps the angle"), Dynamics::StepSteerAngle(Car, 0.1, std::numeric_limits<double>::quiet_NaN(), 0.0, Step), 0.1);

    // Suspension pushes, never pulls.
    TestTrue(TEXT("Compressed spring pushes"), Dynamics::SuspensionLoad(Car, 0.1, 0.0) > 0.0);
    TestEqual(TEXT("Extended spring does not pull"), Dynamics::SuspensionLoad(Car, -0.1, 0.0), 0.0);
    TestEqual(TEXT("Rebounding fast does not pull"), Dynamics::SuspensionLoad(Car, 0.01, 10.0), 0.0);

    // Tyres: grip against the slide, capped by the load; the handbrake frees only the rear.
    const double Load = Car.Mass * Dynamics::Gravity / WheelCount;
    TestTrue(TEXT("Lateral force opposes a slide"), Dynamics::LateralForce(Car, 0, 1.0, Load, false, 1.0) < 0.0);
    TestEqual(TEXT("Slide capped by load"), FMath::Abs(Dynamics::LateralForce(Car, 0, 50.0, Load, false, 1.0)),
        Dynamics::LateralFrictionLimit * Load, 1e-6);
    TestEqual(TEXT("Handbrake leaves front grip"), Dynamics::LateralForce(Car, 0, 0.2, Load, true, 1.0),
        Dynamics::LateralForce(Car, 0, 0.2, Load, false, 1.0));
    TestTrue(TEXT("Handbrake frees the rear"), FMath::Abs(Dynamics::LateralForce(Car, 2, 0.2, Load, true, 1.0))
        < FMath::Abs(Dynamics::LateralForce(Car, 2, 0.2, Load, false, 1.0)));

    // Engine: front-wheel drive, fading toward top speed, reverse limited to reverse speed.
    TestTrue(TEXT("Front wheel driven"), Dynamics::LongitudinalForce(Car, 0, 0.0, 0.0, 1.0, 0.0, false, 1.0, Step) > 0.0);
    TestEqual(TEXT("Rear wheel not driven"), Dynamics::LongitudinalForce(Car, 2, 0.0, 0.0, 1.0, 0.0, false, 1.0, Step), 0.0);
    TestEqual(TEXT("No thrust at top speed"), Dynamics::LongitudinalForce(Car, 0, Car.MaxSpeed, Car.MaxSpeed, 1.0, 0.0, false, 1.0, Step), 0.0);
    TestTrue(TEXT("Reverse pushes back"), Dynamics::LongitudinalForce(Car, 0, 0.0, 0.0, -1.0, 0.0, false, 1.0, Step) < 0.0);
    TestEqual(TEXT("No reverse thrust past reverse speed"),
        Dynamics::LongitudinalForce(Car, 0, -Car.ReverseSpeed, -Car.ReverseSpeed, -1.0, 0.0, false, 1.0, Step), 0.0);
    // A brake never reverses the wheel: at a crawl it takes exactly what stops it this step.
    const double Crawl = 0.01;
    const double Braking = Dynamics::LongitudinalForce(Car, 2, Crawl, Crawl, 0.0, 1.0, false, 1.0, Step);
    TestEqual(TEXT("Brake stops, does not reverse"), Braking, -Crawl * (Car.Mass / WheelCount) / Step, 1e-6);
    TestTrue(TEXT("Handbrake brakes the rear"), Dynamics::LongitudinalForce(Car, 2, 5.0, 5.0, 0.0, 0.0, true, 1.0, Step) < 0.0);
    TestEqual(TEXT("Handbrake leaves the front"), Dynamics::LongitudinalForce(Car, 0, 5.0, 5.0, 0.0, 0.0, true, 1.0, Step), 0.0);

    // Pedals: back brakes while rolling forward, reverses once stopped.
    double Throttle = 0.0;
    double Brake = 0.0;
    Dynamics::Pedals(1.0, 0.0, Throttle, Brake);
    TestTrue(TEXT("Forward is throttle"), Throttle == 1.0 && Brake == 0.0);
    Dynamics::Pedals(-1.0, 10.0, Throttle, Brake);
    TestTrue(TEXT("Back brakes while rolling"), Throttle == 0.0 && Brake == 1.0);
    Dynamics::Pedals(-1.0, 0.0, Throttle, Brake);
    TestTrue(TEXT("Back reverses when stopped"), Throttle == -1.0 && Brake == 0.0);

    // Idle hold: a parked car without throttle rests on its brake; with throttle it does not.
    TestEqual(TEXT("Idle hold at rest"), Dynamics::HeldBrake(0.0, 0.0, 0.1), 1.0);
    TestEqual(TEXT("No hold while rolling"), Dynamics::HeldBrake(0.0, 0.0, 5.0), 0.0);
    TestEqual(TEXT("No hold on throttle"), Dynamics::HeldBrake(0.5, 0.0, 0.1), 0.0);
    TestTrue(TEXT("Held at rest"), Dynamics::IsHeld(0.0, 1.0, 0.1));
    TestFalse(TEXT("Not held while rolling"), Dynamics::IsHeld(0.0, 1.0, 5.0));
    TestFalse(TEXT("Not held on throttle"), Dynamics::IsHeld(-0.6, 0.0, 0.0));

    // A held wheel cancels its share of the slope's pull and its own creep within the step, up to its limit.
    const double Share = Car.Mass / WheelCount;
    const double CrossFall = Dynamics::Gravity * FMath::Sin(FMath::DegreesToRadians(1.4));
    TestEqual(TEXT("Hold cancels the slope at rest"), Dynamics::HoldForce(0.0, CrossFall, Share, Step, 1.0e6), -CrossFall * Share, 1e-9);
    TestEqual(TEXT("Hold cancels creep too"), Dynamics::HoldForce(0.035, CrossFall, Share, Step, 1.0e6),
        -(0.035 / Step + CrossFall) * Share, 1e-9);
    TestEqual(TEXT("Hold is limited"), Dynamics::HoldForce(0.0, 9.81, Share, Step, 100.0), -100.0);
    TestEqual(TEXT("No hold without grip"), Dynamics::HoldForce(0.0, CrossFall, Share, Step, 0.0), 0.0);
    TestEqual(TEXT("Brake limit at the front"), Dynamics::BrakeLimit(Car, 0, 1.0, true, 1.0), Car.BrakeForce / WheelCount, 1e-9);
    TestEqual(TEXT("Handbrake adds at the rear"), Dynamics::BrakeLimit(Car, 2, 1.0, true, 1.0),
        (Car.BrakeForce + Car.HandbrakeForce) / WheelCount, 1e-9);
    TestTrue(TEXT("The brake holds the hatchback on a 10% grade"),
        Dynamics::BrakeLimit(Car, 0, 1.0, false, 1.0) > Share * Dynamics::Gravity * 0.1);

    // Resistance, river and crashes.
    TestEqual(TEXT("No resistance at rest"), Dynamics::Resistance(Car, 0.0, 4), 0.0);
    TestTrue(TEXT("Rolling adds resistance"), Dynamics::Resistance(Car, 10.0, 4) > Dynamics::Resistance(Car, 10.0, 0));
    TestEqual(TEXT("No buoyancy out of water"), Dynamics::Buoyancy(Car, -0.2), 0.0);
    TestEqual(TEXT("Buoyancy capped"), Dynamics::Buoyancy(Car, 10.0), Car.Mass * Dynamics::Gravity * Dynamics::WaterMaxDepth, 1e-6);
    TestTrue(TEXT("Crash threshold"), Dynamics::IsImpact(2.0) && !Dynamics::IsImpact(1.0) && !Dynamics::IsImpact(std::numeric_limits<double>::quiet_NaN()));

    // Surfaces: unknown stays neutral, wet asphalt loses grip, water barely drives.
    TestEqual(TEXT("Unknown surface neutral"), Dynamics::SurfaceGrip(TEXT("unknown"), 1.0), 1.0);
    TestTrue(TEXT("Wet asphalt slips"), Dynamics::SurfaceGrip(TEXT("asphalt"), 1.0) < Dynamics::SurfaceGrip(TEXT("asphalt"), 0.0));
    TestTrue(TEXT("Water barely drives"), Dynamics::SurfaceTraction(TEXT("water"), 0.0) < 0.1);
    TestEqual(TEXT("NaN wetness is dry"), Dynamics::SurfaceGrip(TEXT("asphalt"), std::numeric_limits<double>::quiet_NaN()), Dynamics::SurfaceGrip(TEXT("asphalt"), 0.0));
    return true;
}

#endif
