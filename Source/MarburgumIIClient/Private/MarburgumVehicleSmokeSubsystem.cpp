#include "MarburgumVehicleSmokeSubsystem.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "MarburgumCharacter.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlayerController.h"
#include "MarburgumVehicle.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "UI/MarburgumUISubsystem.h"
#include "UI/MarburgumVehicleStatus.h"
#include "UnrealClient.h"

#define MB2_VEHICLE_SMOKE_LOG(Format, ...) \
    UE_LOG(LogMarburgumNet, Log, TEXT("MB2VSMOKE role=client tag=%s " Format), *Role, ##__VA_ARGS__)

namespace MarburgumVehicleSmoke
{
    const FName CarId(TEXT("lent_hatchback"));
    const FName DriverSeat(TEXT("driver"));
    constexpr double SettleSeconds = 2.0;
    constexpr double ShotTimeoutSeconds = 10.0;
    constexpr double AnswerTimeoutSeconds = 5.0;
    // A parked car settles on its springs; it must not roll or lean once it has.
    constexpr double RestCreepCm = 5.0;
    constexpr double RestTiltDegrees = 3.0;
    // Pedal phases (seconds) and what they must achieve on the hatchback (3400 N on 780 kg).
    constexpr double ThrottleSeconds = 3.5;
    constexpr double ThrottleMinSpeed = 3.0;
    constexpr double ThrottleMinDistanceCm = 300.0;
    constexpr double SteerSeconds = 2.5;
    constexpr double SteerMinYaw = 15.0;
    constexpr double ReverseSeconds = 2.5;
    constexpr double ReverseMinSpeed = -1.5;
    constexpr double StoppedSpeed = 0.2;
    constexpr double StoppedHoldSeconds = 1.5;
    // Where a body that stepped out may stand from its door, and how far it walks afterwards.
    constexpr double ExitNearDoorCm = 250.0;
    constexpr double WalkSeconds = 2.0;
    constexpr double WalkMinCm = 200.0;
    // The door clips sit the body down with its feet ahead (ADR-043): about 45 cm ahead of the pelvis in the
    // seat, more while ducking under the roof. A standing stance keeps them under it.
    constexpr double DoorClipFeetAheadMinCm = 25.0;
    constexpr double NoLegsSeenCm = -1000.0;
    constexpr double EnteringShotFeetAheadCm = 45.0;
}

using namespace MarburgumVehicleSmoke;

bool UMarburgumVehicleSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
    return GameInstance && !GameInstance->IsDedicatedServerInstance() && FParse::Param(FCommandLine::Get(), TEXT("MB2VehicleSmoke"));
}

void UMarburgumVehicleSmokeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const TCHAR* CommandLine = FCommandLine::Get();
    FParse::Value(CommandLine, TEXT("MB2VehicleRole="), Role);
    FParse::Value(CommandLine, TEXT("MB2VehicleShot="), ShotPrefix);
    FParse::Value(CommandLine, TEXT("MB2VehicleTimeout="), TimeoutSeconds);
    Role = Role.ToUpper();
    StartSeconds = FPlatformTime::Seconds();
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMarburgumVehicleSmokeSubsystem::Tick), 0.f);
    MB2_VEHICLE_SMOKE_LOG("event=start shot=%d", ShotPrefix.IsEmpty() ? 0 : 1);
}

void UMarburgumVehicleSmokeSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    Super::Deinitialize();
}

AMarburgumCharacter* UMarburgumVehicleSmokeSubsystem::Body() const
{
    const AMarburgumPlayerController* Local = Controller.Get();
    return Local ? Cast<AMarburgumCharacter>(Local->GetPawn()) : nullptr;
}

AMarburgumVehicle* UMarburgumVehicleSmokeSubsystem::Car() const
{
    return Vehicle.Get();
}

bool UMarburgumVehicleSmokeSubsystem::Bind()
{
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (!UMarburgumSessionSubsystem::IsAdmitted(World))
    {
        return false;
    }
    AMarburgumPlayerController* Local = Cast<AMarburgumPlayerController>(World->GetFirstPlayerController());
    const AMarburgumCharacter* Character = Local ? Cast<AMarburgumCharacter>(Local->GetPawn()) : nullptr;
    if (!Character || !Local->PlayerState)
    {
        return false;
    }
    if (!Vehicle.IsValid())
    {
        TArray<AMarburgumVehicle*> Cars;
        AMarburgumVehicle::GetAll(World, Cars);
        for (AMarburgumVehicle* Candidate : Cars)
        {
            if (Candidate->VehicleId == CarId)
            {
                Vehicle = Candidate;
            }
        }
        if (!Vehicle.IsValid())
        {
            return false;
        }
    }
    if (Controller.Get() != Local)
    {
        Controller = Local;
        Local->OnVehicleResult.AddUObject(this, &UMarburgumVehicleSmokeSubsystem::OnVehicleResult);
        StepSeconds = FPlatformTime::Seconds();
        // Face the car's driver side from behind the body.
        Local->SetControlRotation(FRotator(-12.f, Character->GetActorRotation().Yaw, 0.f));
        const FVector At = Character->GetActorLocation();
        MB2_VEHICLE_SMOKE_LOG("event=ready player=%d at=%.0f,%.0f,%.0f car=%s car_at=%s door_cm=%.0f", Local->PlayerState->GetPlayerId(),
            At.X, At.Y, At.Z, *Vehicle->DefinitionId.ToString(), *Vehicle->GetActorLocation().ToString(),
            FVector::Dist2D(At, Vehicle->GetDoorLocation(DriverSeat)));
    }
    return true;
}

void UMarburgumVehicleSmokeSubsystem::OnVehicleResult(bool bOk, FName Action, FName Reason)
{
    bAnswered = true;
    bAnswerOk = bOk;
    AnswerReason = Reason;
    MB2_VEHICLE_SMOKE_LOG("event=answer action=%s ok=%d reason=%s", *Action.ToString(), bOk ? 1 : 0, Reason.IsNone() ? TEXT("-") : *Reason.ToString());
}

void UMarburgumVehicleSmokeSubsystem::Expect(bool bCondition, const FString& What)
{
    if (!bCondition)
    {
        Problems.Add(What);
        MB2_VEHICLE_SMOKE_LOG("event=expectation_failed what=%s", *What);
    }
}

void UMarburgumVehicleSmokeSubsystem::Shot(const TCHAR* Label)
{
    if (ShotPrefix.IsEmpty())
    {
        return;
    }
    PendingShot = FString::Printf(TEXT("%s-%s.png"), *ShotPrefix, Label);
    IFileManager::Get().Delete(*PendingShot, false, true, true);
    FScreenshotRequest::RequestScreenshot(PendingShot, true, false);
    ShotRequestedSeconds = FPlatformTime::Seconds();
}

FString UMarburgumVehicleSmokeSubsystem::HudSpeed() const
{
    const UMarburgumUISubsystem* UI = GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>();
    const UMarburgumVehicleStatus* Hud = UI ? UI->GetVehicleStatus() : nullptr;
    return Hud ? Hud->GetShownSpeed() : TEXT("-");
}

void UMarburgumVehicleSmokeSubsystem::Advance(int32 Next, double Now)
{
    Step = Next;
    StepSeconds = Now;
}

void UMarburgumVehicleSmokeSubsystem::Report(const TCHAR* Event, double Now)
{
    const AMarburgumCharacter* Character = Body();
    const AMarburgumVehicle* Car = Vehicle.Get();
    const FVector At = Character ? Character->GetActorLocation() : FVector::ZeroVector;
    const FVector CarAt = Car ? Car->GetActorLocation() : FVector::ZeroVector;
    const FRotator CarRot = Car ? Car->GetActorRotation() : FRotator::ZeroRotator;
    MB2_VEHICLE_SMOKE_LOG("event=%s step_s=%.2f pos=%.0f,%.0f,%.0f seat=%s shown=%d car=%.0f,%.0f,%.0f yaw=%.1f pitch=%.1f roll=%.1f "
                          "speed=%.2f steer=%.2f wheels_ground=%d occupants=%d impacts=%d hud=\"%s\"",
        Event, Now - StepSeconds, At.X, At.Y, At.Z, Character ? *Character->GetSeatId().ToString() : TEXT("-"),
        Character && Character->IsBodyShown() ? 1 : 0, CarAt.X, CarAt.Y, CarAt.Z, CarRot.Yaw, CarRot.Pitch, CarRot.Roll,
        Car ? Car->GetForwardSpeed() : 0.f, Car ? Car->GetSteerAngle() : 0.f, Car ? Car->GetWheelsOnGround() : 0,
        Car ? Car->GetOccupants().Num() : 0, Car ? Car->GetImpactCount() : 0, *HudSpeed());
    if (Character && Character->IsBodyShown())
    {
        // The drawn pose, after the whole animation graph: mesh space, Z up from the ground and Y the body's forward.
        const USkeletalMeshComponent* Mesh = Character->GetMesh();
        const auto Bone = [Mesh](const TCHAR* Name) { return Mesh->GetBoneLocation(Name, EBoneSpaces::ComponentSpace); };
        MB2_VEHICLE_SMOKE_LOG("event=%s_pose pelvis_z=%.0f foot_l_z=%.0f foot_r_z=%.0f feet_ahead=%.0f", Event, Bone(TEXT("pelvis")).Z,
            Bone(TEXT("foot_l")).Z, Bone(TEXT("foot_r")).Z, FeetAheadOfPelvisCm());
    }
}

double UMarburgumVehicleSmokeSubsystem::FeetAheadOfPelvisCm() const
{
    // The mannequin faces +Y in its own component space (ADR-042, ADR-043).
    const AMarburgumCharacter* Character = Body();
    if (!Character)
    {
        return 0.0;
    }
    const USkeletalMeshComponent* Mesh = Character->GetMesh();
    const auto Bone = [Mesh](const TCHAR* Name) { return Mesh->GetBoneLocation(Name, EBoneSpaces::ComponentSpace); };
    return 0.5 * (Bone(TEXT("foot_l")).Y + Bone(TEXT("foot_r")).Y) - Bone(TEXT("pelvis")).Y;
}

bool UMarburgumVehicleSmokeSubsystem::Tick(float DeltaTime)
{
    if (bDone)
    {
        return true;
    }
    const double Now = FPlatformTime::Seconds();
    if (Now - StartSeconds > TimeoutSeconds)
    {
        Report(TEXT("timeout_state"), Now);
        Fail(FString::Printf(TEXT("timeout step=%d"), Step));
        return true;
    }
    if (!Bind())
    {
        return true;
    }
    if (const AMarburgumCharacter* Character = Body(); bWatchingLegs && Character && Character->IsBodyShown())
    {
        // While a door clip plays, every frame (a pending screenshot included): ducking in, sitting and
        // standing up put the drawn feet ahead of the pelvis when the legs follow the clip.
        MostFeetAheadCm = FMath::Max(MostFeetAheadCm, FeetAheadOfPelvisCm());
    }
    if (!PendingShot.IsEmpty())
    {
        const bool bSaved = IFileManager::Get().FileSize(*PendingShot) > 0;
        if (!bSaved && Now - ShotRequestedSeconds < ShotTimeoutSeconds)
        {
            return true;
        }
        MB2_VEHICLE_SMOKE_LOG("event=shot file=%s saved=%d", *FPaths::GetCleanFilename(PendingShot), bSaved ? 1 : 0);
        Expect(bSaved, FString::Printf(TEXT("shot_not_saved=%s"), *FPaths::GetCleanFilename(PendingShot)));
        PendingShot.Reset();
    }
    if (const AMarburgumVehicle* Car = Vehicle.Get())
    {
        MaxSpeed = FMath::Max(MaxSpeed, static_cast<double>(Car->GetForwardSpeed()));
        MinSpeed = FMath::Min(MinSpeed, static_cast<double>(Car->GetForwardSpeed()));
    }
    if (Role == TEXT("D"))
    {
        TickD(Now);
    }
    else if (Role == TEXT("P"))
    {
        TickP(Now);
    }
    else
    {
        Fail(FString::Printf(TEXT("unknown_role=%s"), *Role));
    }
    return true;
}

void UMarburgumVehicleSmokeSubsystem::TickD(double Now)
{
    AMarburgumCharacter* Character = Body();
    AMarburgumVehicle* CarActor = Car();
    AMarburgumPlayerController* Local = Controller.Get();
    if (!Character || !CarActor || !Local)
    {
        return;
    }
    const bool bRendered = !ShotPrefix.IsEmpty();
    const double Speed = CarActor->GetForwardSpeed();
    switch (Step)
    {
    case 0:
        // The parked car: on four wheels, level, and still.
        if (Now - StepSeconds < SettleSeconds) return;
        CarRestAt = CarActor->GetActorLocation();
        Report(TEXT("parked"), Now);
        Advance(1, Now);
        return;
    case 1:
    {
        if (Now - StepSeconds < SettleSeconds) return;
        const FRotator Tilt = CarActor->GetActorRotation();
        Report(TEXT("parked_after"), Now);
        Expect(CarActor->GetWheelsOnGround() == 4, FString::Printf(TEXT("parked_wheels_on_ground=%d"), CarActor->GetWheelsOnGround()));
        Expect(FMath::Abs(Tilt.Pitch) < RestTiltDegrees && FMath::Abs(Tilt.Roll) < RestTiltDegrees,
            FString::Printf(TEXT("parked_tilt=%.1f,%.1f"), Tilt.Pitch, Tilt.Roll));
        Expect(FVector::Dist(CarActor->GetActorLocation(), CarRestAt) < RestCreepCm,
            FString::Printf(TEXT("parked_creep_cm=%.1f"), FVector::Dist(CarActor->GetActorLocation(), CarRestAt)));
        Shot(TEXT("parked"));
        FName Seat;
        AMarburgumVehicle* Candidate = Local->FindVehicleCandidate(Seat);
        Expect(Candidate == CarActor && Seat == DriverSeat, FString::Printf(TEXT("candidate=%s seat=%s"),
            Candidate ? *Candidate->VehicleId.ToString() : TEXT("none"), *Seat.ToString()));
        bAnswered = false;
        Local->RequestEnterVehicle(CarActor, Seat);
        Advance(2, Now);
        return;
    }
    case 2:
        // In at the driver's door: seated at once, the body still drawn while the enter clip plays.
        if (!bAnswered || Character->GetSeatedVehicle() != CarActor)
        {
            if (Now - StepSeconds > AnswerTimeoutSeconds) Fail(TEXT("never_seated"));
            return;
        }
        Report(TEXT("entered"), Now);
        Expect(bAnswerOk, FString::Printf(TEXT("enter_refused=%s"), *AnswerReason.ToString()));
        Expect(Character->IsDriving(), TEXT("not_driving"));
        Expect(Character->IsBodyShown(), TEXT("hidden_before_enter_clip"));
        MostFeetAheadCm = NoLegsSeenCm;
        bWatchingLegs = true;
        Advance(3, Now);
        return;
    case 3:
        // The clip ends with the door shut and the body hidden in its seat.
        if (Character->IsBodyShown())
        {
            // Drawn once the body ducks in (or late in the clip if it never does), for the evidence.
            if (bRendered && !bEnteringShot && (FeetAheadOfPelvisCm() > EnteringShotFeetAheadCm || Now - StepSeconds > 0.8))
            {
                bEnteringShot = true;
                Report(TEXT("entering"), Now);
                Shot(TEXT("entering"));
            }
            if (Now - StepSeconds > 5.0) Fail(TEXT("never_hidden_in_seat"));
            return;
        }
        Report(TEXT("hidden_in_seat"), Now);
        bWatchingLegs = false;
        MB2_VEHICLE_SMOKE_LOG("event=enter_legs feet_ahead_max=%.0f", MostFeetAheadCm);
        Expect(MostFeetAheadCm > DoorClipFeetAheadMinCm, FString::Printf(TEXT("enter_legs_standing=%.0f"), MostFeetAheadCm));
        Expect(Now - StepSeconds > 0.3, FString::Printf(TEXT("enter_clip_too_short=%.2f"), Now - StepSeconds));
        if (bRendered)
        {
            const UMarburgumUISubsystem* UI = GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>();
            const UMarburgumVehicleStatus* Hud = UI ? UI->GetVehicleStatus() : nullptr;
            const FString Name = Hud ? Hud->GetShownName() : FString();
            Expect(Name.Contains(TEXT("hatchback")), FString::Printf(TEXT("hud_name=%s"), *Name));
            Expect(HudSpeed() == TEXT("0 km/h"), FString::Printf(TEXT("hud_speed_parked=%s"), *HudSpeed()));
        }
        Shot(TEXT("seated"));
        Advance(4, Now);
        return;
    case 4:
        // Wait for the passenger.
        if (CarActor->GetOccupants().Num() < 2)
        {
            if (Now - StepSeconds > 90.0) Fail(TEXT("no_passenger"));
            return;
        }
        Report(TEXT("passenger_aboard"), Now);
        Mark = CarActor->GetActorLocation();
        MarkYaw = CarActor->GetActorRotation().Yaw;
        MaxSpeed = 0.0;
        Local->SetDrivingOverride(0.6f, 0.f, 0.f, false);
        Advance(5, Now);
        return;
    case 5:
    {
        // Throttle: away along the car's own forward.
        if (Now - StepSeconds < ThrottleSeconds) return;
        const FVector Moved = CarActor->GetActorLocation() - Mark;
        const double Along = FVector::DotProduct(Moved, FRotator(0.0, MarkYaw, 0.0).Vector());
        Report(TEXT("throttle"), Now);
        Expect(MaxSpeed >= ThrottleMinSpeed, FString::Printf(TEXT("throttle_max_speed=%.2f"), MaxSpeed));
        Expect(Along >= ThrottleMinDistanceCm, FString::Printf(TEXT("throttle_forward_cm=%.0f"), Along));
        Expect(FMath::Abs(FMath::FindDeltaAngleDegrees(MarkYaw, CarActor->GetActorRotation().Yaw)) < 10.0,
            FString::Printf(TEXT("throttle_yaw_drift=%.1f"), FMath::FindDeltaAngleDegrees(MarkYaw, CarActor->GetActorRotation().Yaw)));
        if (bRendered)
        {
            Expect(HudSpeed().EndsWith(TEXT(" km/h")) && !HudSpeed().StartsWith(TEXT("REV")) && HudSpeed() != TEXT("0 km/h"),
                FString::Printf(TEXT("hud_speed_driving=%s"), *HudSpeed()));
        }
        Shot(TEXT("driving"));
        MarkYaw = CarActor->GetActorRotation().Yaw;
        Local->SetDrivingOverride(0.4f, 0.f, 0.7f, false);
        Advance(6, Now);
        return;
    }
    case 6:
    {
        // Steering right turns the car right (yaw increases in Unreal's left-handed frame).
        if (Now - StepSeconds < SteerSeconds) return;
        const double Turned = FMath::FindDeltaAngleDegrees(MarkYaw, CarActor->GetActorRotation().Yaw);
        Report(TEXT("steered"), Now);
        Expect(Turned >= SteerMinYaw, FString::Printf(TEXT("steer_yaw=%.1f"), Turned));
        Local->SetDrivingOverride(0.f, 1.f, 0.f, false);
        StillSince = -1.0;
        Advance(7, Now);
        return;
    }
    case 7:
        // Brake to a stop.
        if (FMath::Abs(Speed) > StoppedSpeed)
        {
            StillSince = -1.0;
            if (Now - StepSeconds > 8.0) Fail(TEXT("never_stopped"));
            return;
        }
        if (StillSince < 0.0) StillSince = Now;
        if (Now - StillSince < 0.5) return;
        Report(TEXT("braked"), Now);
        MinSpeed = 0.0;
        Mark = CarActor->GetActorLocation();
        MarkYaw = CarActor->GetActorRotation().Yaw;
        Local->SetDrivingOverride(-0.6f, 0.f, 0.f, false);
        Advance(8, Now);
        return;
    case 8:
    {
        // Reverse: backward along the car's forward, REV on the HUD.
        if (Now - StepSeconds < ReverseSeconds) return;
        const double Along = FVector::DotProduct(CarActor->GetActorLocation() - Mark, FRotator(0.0, MarkYaw, 0.0).Vector());
        Report(TEXT("reversing"), Now);
        Expect(MinSpeed <= ReverseMinSpeed, FString::Printf(TEXT("reverse_min_speed=%.2f"), MinSpeed));
        Expect(Along < -100.0, FString::Printf(TEXT("reverse_along_cm=%.0f"), Along));
        if (bRendered)
        {
            Expect(HudSpeed().StartsWith(TEXT("REV")), FString::Printf(TEXT("hud_speed_reversing=%s"), *HudSpeed()));
        }
        Shot(TEXT("reversing"));
        Local->SetDrivingOverride(0.f, 1.f, 0.f, false);
        StillSince = -1.0;
        Advance(9, Now);
        return;
    }
    case 9:
        if (FMath::Abs(Speed) > StoppedSpeed)
        {
            StillSince = -1.0;
            if (Now - StepSeconds > 8.0) Fail(TEXT("never_stopped_after_reverse"));
            return;
        }
        if (StillSince < 0.0) StillSince = Now;
        if (Now - StillSince < StoppedHoldSeconds) return;
        Report(TEXT("stopped"), Now);
        Advance(10, Now);
        return;
    case 10:
        // The passenger gets out first.
        if (CarActor->GetOccupants().Num() > 1)
        {
            if (Now - StepSeconds > 30.0) Fail(TEXT("passenger_never_left"));
            return;
        }
        Report(TEXT("passenger_left"), Now);
        Local->ClearDrivingOverride();
        bAnswered = false;
        Local->RequestExitVehicle();
        Advance(11, Now);
        return;
    case 11:
        // Out at the driver's door, on foot, drawn.
        if (!bAnswered || Character->GetSeatedVehicle())
        {
            if (Now - StepSeconds > AnswerTimeoutSeconds) Fail(TEXT("never_got_out"));
            return;
        }
        Report(TEXT("exited"), Now);
        Expect(bAnswerOk, FString::Printf(TEXT("exit_refused=%s"), *AnswerReason.ToString()));
        Expect(Character->IsBodyShown(), TEXT("hidden_after_exit"));
        Expect(FVector::Dist2D(Character->GetActorLocation(), CarActor->GetDoorLocation(DriverSeat)) < ExitNearDoorCm,
            FString::Printf(TEXT("exit_from_door_cm=%.0f"), FVector::Dist2D(Character->GetActorLocation(), CarActor->GetDoorLocation(DriverSeat))));
        Expect(Character->GetCharacterMovement()->MovementMode != MOVE_None, TEXT("exit_without_movement"));
        if (bRendered)
        {
            const UMarburgumUISubsystem* UI = GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>();
            const UMarburgumVehicleStatus* Hud = UI ? UI->GetVehicleStatus() : nullptr;
            Expect(!Hud || Hud->GetShownName().IsEmpty(), TEXT("hud_still_shown_on_foot"));
        }
        Mark = Character->GetActorLocation();
        CarRestAt = CarActor->GetActorLocation();
        // Sampled from the next frame on: the pose drawn now is the one the hidden body was left in.
        MostFeetAheadCm = NoLegsSeenCm;
        bWatchingLegs = true;
        Shot(TEXT("exited"));
        Advance(13, Now);
        return;
    case 13:
        // Where it got out it stays: no stale walking move is replayed once the seat is left.
        // Meanwhile the exit clip stands the body up from the seat, feet ahead of the pelvis at first.
        if (Now - StepSeconds < 1.0) return;
        bWatchingLegs = false;
        MB2_VEHICLE_SMOKE_LOG("event=exit_legs feet_ahead_max=%.0f", MostFeetAheadCm);
        Expect(MostFeetAheadCm > DoorClipFeetAheadMinCm, FString::Printf(TEXT("exit_legs_standing=%.0f"), MostFeetAheadCm));
        Expect(FVector::Dist2D(Character->GetActorLocation(), Mark) < 50.0,
            FString::Printf(TEXT("moved_after_exit_cm=%.0f"), FVector::Dist2D(Character->GetActorLocation(), Mark)));
        Mark = Character->GetActorLocation();
        Advance(12, Now);
        return;
    case 12:
    {
        // Walk away from the car, the way the body faces.
        const FVector Away = (Character->GetActorLocation() - CarActor->GetActorLocation()).GetSafeNormal2D();
        Character->AddMovementInput(Away, 1.f);
        if (Now - StepSeconds < WalkSeconds) return;
        const double Walked = FVector::Dist2D(Character->GetActorLocation(), Mark);
        Report(TEXT("walked"), Now);
        Expect(Walked >= WalkMinCm, FString::Printf(TEXT("walked_cm=%.0f"), Walked));
        Expect(FVector::Dist(CarActor->GetActorLocation(), CarRestAt) < RestCreepCm,
            FString::Printf(TEXT("left_car_creep_cm=%.1f"), FVector::Dist(CarActor->GetActorLocation(), CarRestAt)));
        Pass();
        return;
    }
    default:
        return;
    }
}

void UMarburgumVehicleSmokeSubsystem::TickP(double Now)
{
    AMarburgumCharacter* Character = Body();
    AMarburgumVehicle* CarActor = Car();
    AMarburgumPlayerController* Local = Controller.Get();
    if (!Character || !CarActor || !Local)
    {
        return;
    }
    const APlayerState* Driver = CarActor->GetOccupant(DriverSeat);
    switch (Step)
    {
    case 0:
        // Wait for D at the wheel, then ask for it anyway.
        if (Now - StepSeconds < SettleSeconds || !Driver || Driver == Local->PlayerState)
        {
            if (Now - StepSeconds > 90.0) Fail(TEXT("no_driver"));
            return;
        }
        Report(TEXT("driver_seated"), Now);
        bAnswered = false;
        Local->RequestEnterVehicle(CarActor, DriverSeat);
        Advance(1, Now);
        return;
    case 1:
    {
        if (!bAnswered)
        {
            if (Now - StepSeconds > AnswerTimeoutSeconds) Fail(TEXT("no_answer_for_driver_seat"));
            return;
        }
        Expect(!bAnswerOk && AnswerReason == TEXT("seat_taken"), FString::Printf(TEXT("driver_seat_answer=%d,%s"), bAnswerOk ? 1 : 0,
            *AnswerReason.ToString()));
        FName Seat;
        AMarburgumVehicle* Candidate = Local->FindVehicleCandidate(Seat);
        Expect(Candidate == CarActor && !Seat.IsNone() && Seat != DriverSeat, FString::Printf(TEXT("passenger_candidate=%s"), *Seat.ToString()));
        if (Candidate != CarActor || Seat.IsNone())
        {
            Fail(TEXT("no_passenger_seat_in_reach"));
            return;
        }
        Report(TEXT("asking_passenger_seat"), Now);
        bAnswered = false;
        Local->RequestEnterVehicle(CarActor, Seat);
        Advance(2, Now);
        return;
    }
    case 2:
        if (!bAnswered || Character->GetSeatedVehicle() != CarActor)
        {
            if (Now - StepSeconds > AnswerTimeoutSeconds) Fail(TEXT("passenger_never_seated"));
            return;
        }
        RiddenSeat = Character->GetSeatId();
        Report(TEXT("passenger_seated"), Now);
        Expect(bAnswerOk, FString::Printf(TEXT("passenger_enter_refused=%s"), *AnswerReason.ToString()));
        Expect(!Character->IsDriving(), TEXT("passenger_driving"));
        Mark = Character->GetActorLocation();
        MaxSpeed = 0.0;
        MinSpeed = 0.0;
        StillSince = -1.0;
        Advance(3, Now);
        return;
    case 3:
    {
        // Ride along: the body goes where the car goes. Out once D has driven, reversed and stopped.
        FarthestRideCm = FMath::Max(FarthestRideCm, FVector::Dist2D(Character->GetActorLocation(), Mark));
        if (Character->GetSeatedVehicle() != CarActor)
        {
            Fail(TEXT("dropped_from_seat"));
            return;
        }
        const bool bDriven = MaxSpeed >= ThrottleMinSpeed && MinSpeed <= ReverseMinSpeed;
        if (!bDriven || FMath::Abs(CarActor->GetForwardSpeed()) > StoppedSpeed)
        {
            StillSince = -1.0;
            if (Now - StepSeconds > 90.0) Fail(TEXT("ride_never_finished"));
            return;
        }
        if (StillSince < 0.0) StillSince = Now;
        if (Now - StillSince < StoppedHoldSeconds) return;
        Report(TEXT("ride_over"), Now);
        Expect(FarthestRideCm >= ThrottleMinDistanceCm, FString::Printf(TEXT("ride_carried_cm=%.0f"), FarthestRideCm));
        Expect(!Character->IsBodyShown(), TEXT("passenger_drawn_in_seat"));
        bAnswered = false;
        Local->RequestExitVehicle();
        Advance(4, Now);
        return;
    }
    case 4:
        if (!bAnswered || Character->GetSeatedVehicle())
        {
            if (Now - StepSeconds > AnswerTimeoutSeconds) Fail(TEXT("passenger_never_got_out"));
            return;
        }
        Report(TEXT("passenger_exited"), Now);
        Expect(bAnswerOk, FString::Printf(TEXT("passenger_exit_refused=%s"), *AnswerReason.ToString()));
        Expect(Character->IsBodyShown(), TEXT("passenger_hidden_after_exit"));
        Expect(FVector::Dist2D(Character->GetActorLocation(), CarActor->GetDoorLocation(RiddenSeat)) < ExitNearDoorCm,
            FString::Printf(TEXT("passenger_exit_from_door_cm=%.0f"), FVector::Dist2D(Character->GetActorLocation(), CarActor->GetDoorLocation(RiddenSeat))));
        Mark = Character->GetActorLocation();
        Advance(5, Now);
        return;
    case 5:
        // Where it got out it stays (no stale move replayed), until the driver is out too.
        if (Driver && Now - StepSeconds < 30.0) return;
        Expect(FVector::Dist2D(Character->GetActorLocation(), Mark) < 50.0,
            FString::Printf(TEXT("passenger_moved_after_exit_cm=%.0f"), FVector::Dist2D(Character->GetActorLocation(), Mark)));
        Pass();
        return;
    default:
        return;
    }
}

void UMarburgumVehicleSmokeSubsystem::Pass()
{
    if (Problems.IsEmpty())
    {
        MB2_VEHICLE_SMOKE_LOG("event=pass");
        bDone = true;
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }
    else
    {
        Fail(TEXT("expectations"));
    }
}

void UMarburgumVehicleSmokeSubsystem::Fail(const FString& Reason)
{
    if (bDone)
    {
        return;
    }
    Problems.Add(Reason);
    MB2_VEHICLE_SMOKE_LOG("event=fail reason=%s", *FString::Join(Problems, TEXT(",")));
    bDone = true;
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

#undef MB2_VEHICLE_SMOKE_LOG
