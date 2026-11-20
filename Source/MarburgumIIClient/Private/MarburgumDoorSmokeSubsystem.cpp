#include "MarburgumDoorSmokeSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "MarburgumCharacter.h"
#include "MarburgumDoor.h"
#include "MarburgumInteriorSubsystem.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "UnrealClient.h"

#define MB2_DOOR_SMOKE_LOG(Format, ...) UE_LOG(LogMarburgumNet, Log, TEXT("MB2DSMOKE role=%s " Format), *Role, ##__VA_ARGS__)

namespace MarburgumDoorSmoke
{
    /** Close enough to a route point to take the next leg. */
    constexpr double ArriveCm = 30.0;
    /** No more than this much progress for StuckSeconds is a stuck body. */
    constexpr double ProgressCm = 15.0;
    constexpr double StuckSeconds = 5.0;
    /** Below the floor by this much is a fall through it. */
    constexpr double FallCm = 60.0;
    /** A door that has not answered a request in this long has failed. */
    constexpr double DoorAnswerSeconds = 4.0;
    constexpr double ShotDelaySeconds = 0.6;
    constexpr double ShotTimeoutSeconds = 10.0;
    /** A capsule's distance from a door's doorway that puts it in the leaf's swing. */
    constexpr double InSwingCm = 70.0;
    /** Who counts as at the door before the walker and the rival start. */
    constexpr double AtDoorCm = 3000.0;
    /** The tourist is away once this far from its street door: past the 40 m interior radius, with margin. */
    constexpr double AwayCm = 6000.0;
    /** And back once this close. */
    constexpr double BackCm = 1500.0;
    /** Away, the building must leave the client within this long; back, stream in again within this long. */
    constexpr double StreamAnswerSeconds = 10.0;
    /** The server's move away and back, from the tourist's arrival (UMarburgumDoorTour waits less, dwells less). */
    constexpr double TourLegSeconds = 60.0;

    double DistanceToSegment2D(const FVector& P, const FVector& A, const FVector& B)
    {
        const FVector2D AB(B - A);
        const FVector2D AP(P - A);
        const double L2 = AB.SizeSquared();
        const double T = L2 > 1.0 ? FMath::Clamp(FVector2D::DotProduct(AP, AB) / L2, 0.0, 1.0) : 0.0;
        return (AP - AB * T).Size();
    }

    const TCHAR* MotionName(EMarburgumDoorMotion Motion)
    {
        switch (Motion)
        {
        case EMarburgumDoorMotion::Closed: return TEXT("closed");
        case EMarburgumDoorMotion::Opening: return TEXT("opening");
        case EMarburgumDoorMotion::Open: return TEXT("open");
        case EMarburgumDoorMotion::Closing: return TEXT("closing");
        }
        return TEXT("?");
    }
}

using namespace MarburgumDoorSmoke;

bool UMarburgumDoorSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
    FString Ignored;
    return GameInstance && !GameInstance->IsDedicatedServerInstance() && FParse::Value(FCommandLine::Get(), TEXT("MB2DoorSmoke="), Ignored);
}

void UMarburgumDoorSmokeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const TCHAR* CommandLine = FCommandLine::Get();
    FParse::Value(CommandLine, TEXT("MB2DoorSmoke="), Role);
    Building = TEXT("r1539756");
    FParse::Value(CommandLine, TEXT("MB2DoorBuilding="), Building);
    FParse::Value(CommandLine, TEXT("MB2DoorShot="), ShotPrefix);
    FParse::Value(CommandLine, TEXT("MB2DoorExpect="), ExpectStates);
    FParse::Value(CommandLine, TEXT("MB2DoorTimeout="), TimeoutSeconds);
    FParse::Value(CommandLine, TEXT("MB2DoorHold="), HoldSeconds);
    StartSeconds = FPlatformTime::Seconds();
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMarburgumDoorSmokeSubsystem::Tick), 0.f);
    MB2_DOOR_SMOKE_LOG("event=start building=%s shot=%d expect=%s", *Building, ShotPrefix.IsEmpty() ? 0 : 1, ExpectStates.IsEmpty() ? TEXT("-") : *ExpectStates);
}

void UMarburgumDoorSmokeSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    Super::Deinitialize();
}

int32 UMarburgumDoorSmokeSubsystem::PeopleAtDoor() const
{
    const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    int32 People = 0;
    if (World && !Route.IsEmpty())
    {
        for (TActorIterator<AMarburgumCharacter> It(World); It; ++It)
        {
            People += FVector::Dist2D(It->GetActorLocation(), Route[0]) < AtDoorCm ? 1 : 0;
        }
    }
    return People;
}

void UMarburgumDoorSmokeSubsystem::CheckExpected(const TCHAR* Event)
{
    const FString States = DoorStates();
    TArray<FString> Each;
    States.ParseIntoArray(Each, TEXT(","));
    bool bMatch = !Each.IsEmpty();
    if (ExpectStates == TEXT("open") || ExpectStates == TEXT("closed"))
    {
        for (const FString& State : Each)
        {
            bMatch &= State == ExpectStates;
        }
    }
    else if (!ExpectStates.IsEmpty())
    {
        bMatch = States == ExpectStates;
    }
    MB2_DOOR_SMOKE_LOG("event=%s states=%s expect=%s match=%d", Event, *States, ExpectStates.IsEmpty() ? TEXT("-") : *ExpectStates, bMatch ? 1 : 0);
    Expect(bMatch, FString::Printf(TEXT("%s_doors=%s"), Event, *States));
}

bool UMarburgumDoorSmokeSubsystem::TickTour(double Now)
{
    const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    const APawn* Body = Controller.IsValid() ? Controller->GetPawn() : nullptr;
    const UMarburgumInteriorSubsystem* Interiors = World ? World->GetSubsystem<UMarburgumInteriorSubsystem>() : nullptr;
    if (!Body || !Interiors)
    {
        return true; // between pawns, or the session is going
    }
    const double FromHome = FVector::Dist2D(Body->GetActorLocation(), HomeCentre);
    if (Step == EStep::TourAway)
    {
        if (FromHome < AwayCm)
        {
            if (Now - StepSeconds > TourLegSeconds)
            {
                Fail(FString::Printf(TEXT("tourist_never_moved_away distance_cm=%.0f"), FromHome));
            }
            return true;
        }
        if (TourSeconds <= 0.0)
        {
            TourSeconds = Now;
            MB2_DOOR_SMOKE_LOG("event=away distance_cm=%.0f doors_relevant=%d", FromHome, BuildingDoors().Num());
        }
        if (!Interiors->IsStreamed(Building))
        {
            MB2_DOOR_SMOKE_LOG("event=streamed_out seconds=%.2f doors_relevant=%d", Now - TourSeconds, BuildingDoors().Num());
            TourSeconds = 0.0;
            Advance(EStep::TourBack, Now);
        }
        else if (Now - TourSeconds > StreamAnswerSeconds)
        {
            Fail(TEXT("building_still_streamed_while_away"));
        }
        return true;
    }
    // TourBack: home again, the building streamed and its street door relevant, then the doors must be as left.
    if (FromHome > BackCm)
    {
        if (Now - StepSeconds > TourLegSeconds)
        {
            Fail(FString::Printf(TEXT("tourist_never_came_back distance_cm=%.0f"), FromHome));
        }
        return true;
    }
    if (TourSeconds <= 0.0)
    {
        TourSeconds = Now;
        MB2_DOOR_SMOKE_LOG("event=back distance_cm=%.0f", FromHome);
    }
    if (!Interiors->IsStreamed(Building) || !StreetDoor())
    {
        if (Now - TourSeconds > StreamAnswerSeconds)
        {
            Fail(FString::Printf(TEXT("building_not_back streamed=%d street_door=%d"), Interiors->IsStreamed(Building) ? 1 : 0, StreetDoor() ? 1 : 0));
        }
        return true;
    }
    // Let the doors' replicated states arrive, then compare.
    if (Now - TourSeconds < StreamAnswerSeconds * 0.4)
    {
        return true;
    }
    MB2_DOOR_SMOKE_LOG("event=streamed_in seconds=%.2f", Now - TourSeconds);
    CheckExpected(TEXT("tour_back"));
    Face(HomeCentre);
    Shot(TEXT("tourist-back"));
    Advance(EStep::Done, Now);
    return true;
}

TArray<AMarburgumDoor*> UMarburgumDoorSmokeSubsystem::BuildingDoors() const
{
    TArray<AMarburgumDoor*> Doors;
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (!World)
    {
        return Doors;
    }
    for (TActorIterator<AMarburgumDoor> It(World); It; ++It)
    {
        if (It->GetBuildingId().ToString() == Building)
        {
            Doors.Add(*It);
        }
    }
    // Street door first, then by where they hang, so two clients list them in the same order.
    Doors.Sort([](const AMarburgumDoor& A, const AMarburgumDoor& B)
    {
        if (A.IsStreetDoor() != B.IsStreetDoor())
        {
            return A.IsStreetDoor();
        }
        const FVector PA = A.GetDoorwayCentre();
        const FVector PB = B.GetDoorwayCentre();
        return PA.X != PB.X ? PA.X < PB.X : PA.Y < PB.Y;
    });
    return Doors;
}

AMarburgumDoor* UMarburgumDoorSmokeSubsystem::StreetDoor() const
{
    const TArray<AMarburgumDoor*> Doors = BuildingDoors();
    return !Doors.IsEmpty() && Doors[0]->IsStreetDoor() ? Doors[0] : nullptr;
}

FString UMarburgumDoorSmokeSubsystem::DoorStates() const
{
    TArray<FString> States;
    for (const AMarburgumDoor* Door : BuildingDoors())
    {
        States.Add(MotionName(Door->GetMotion()));
    }
    return FString::Join(States, TEXT(","));
}

bool UMarburgumDoorSmokeSubsystem::Bind()
{
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (!UMarburgumSessionSubsystem::IsAdmitted(World))
    {
        return false;
    }
    AMarburgumPlayerController* Local = Cast<AMarburgumPlayerController>(World->GetFirstPlayerController());
    const UMarburgumInteriorSubsystem* Interiors = World->GetSubsystem<UMarburgumInteriorSubsystem>();
    if (!Local || !Local->GetPawn() || !Local->PlayerState || !Interiors || !Interiors->IsStreamed(Building) || !StreetDoor())
    {
        return false;
    }
    if (Route.IsEmpty() && !Interiors->GetWalkRoute(Building, Route))
    {
        Fail(TEXT("no_route"));
        return false;
    }
    if (Controller.Get() != Local)
    {
        Controller = Local;
        FloorZ = Route[0].Z;
        int32 People = 0;
        for (TActorIterator<AMarburgumCharacter> It(World); It; ++It)
        {
            ++People;
        }
        MB2_DOOR_SMOKE_LOG("event=ready player=%d at=%s route_points=%d doors=%d states=%s people=%d", Local->PlayerState->GetPlayerId(),
            *Local->GetPawn()->GetActorLocation().ToString(), Route.Num(), BuildingDoors().Num(), *DoorStates(), People);
    }
    return true;
}

bool UMarburgumDoorSmokeSubsystem::MoveTo(const FVector& Target, double Now)
{
    APawn* Body = Controller.IsValid() ? Controller->GetPawn() : nullptr;
    if (!Body)
    {
        return false;
    }
    const FVector At = Body->GetActorLocation();
    const FVector Feet = At - FVector(0, 0, UMarburgumInteriorSubsystem::PlayerHalfHeightCm);
    if (Feet.Z < FloorZ - FallCm)
    {
        Fail(FString::Printf(TEXT("fell_below_floor_at_leg_%d z=%.0f floor=%.0f"), RouteIndex, Feet.Z, FloorZ));
        return false;
    }
    const FVector2D To(Target - At);
    if (To.Size() <= ArriveCm)
    {
        LastProgressSeconds = Now;
        LastProgressAt = At;
        return true;
    }
    if (FVector::Dist2D(At, LastProgressAt) > ProgressCm)
    {
        LastProgressAt = At;
        LastProgressSeconds = Now;
    }
    else if (Now - LastProgressSeconds > StuckSeconds)
    {
        Fail(FString::Printf(TEXT("stuck_at_leg_%d at=%s target=%s"), RouteIndex, *At.ToString(), *Target.ToString()));
        return false;
    }
    const FVector Direction(To.GetSafeNormal(), 0.0);
    Controller->SetControlRotation(FRotator(-6.f, Direction.Rotation().Yaw, 0.f));
    // Slow for the last metre so the body stops on the point instead of orbiting it.
    Body->AddMovementInput(Direction, To.Size() < 100.0 ? 0.45f : 1.f);
    return false;
}

void UMarburgumDoorSmokeSubsystem::Face(const FVector& Target, float Pitch)
{
    if (Controller.IsValid() && Controller->GetPawn())
    {
        const FVector To = Target - Controller->GetPawn()->GetActorLocation();
        Controller->SetControlRotation(FRotator(Pitch, To.Rotation().Yaw, 0.f));
    }
}

void UMarburgumDoorSmokeSubsystem::Advance(EStep Next, double Now)
{
    Step = Next;
    StepSeconds = Now;
    Phase = 0;
    LastProgressSeconds = Now;
    LastProgressAt = Controller.IsValid() && Controller->GetPawn() ? Controller->GetPawn()->GetActorLocation() : FVector::ZeroVector;
}

bool UMarburgumDoorSmokeSubsystem::Tick(float DeltaTime)
{
    if (bDone)
    {
        return true;
    }
    const double Now = FPlatformTime::Seconds();
    if (Now - StartSeconds > TimeoutSeconds)
    {
        Fail(FString::Printf(TEXT("timeout_in_step_%d_phase_%d_leg_%d"), static_cast<int32>(Step), Phase, RouteIndex));
        return true;
    }
    // A capture waits for the body and the camera to come to rest, so it is not smeared by motion blur.
    if (!ShotLabel.IsEmpty())
    {
        if (Now < ShotDueSeconds)
        {
            return true;
        }
        PendingShot = FString::Printf(TEXT("%s-%s.png"), *ShotPrefix, *ShotLabel);
        ShotLabel.Reset();
        IFileManager::Get().Delete(*PendingShot, false, true, true);
        FScreenshotRequest::RequestScreenshot(PendingShot, false, false);
        ShotRequestedSeconds = Now;
        return true;
    }
    if (!PendingShot.IsEmpty())
    {
        const bool bSaved = IFileManager::Get().FileSize(*PendingShot) > 0;
        if (!bSaved && Now - ShotRequestedSeconds < ShotTimeoutSeconds)
        {
            return true;
        }
        MB2_DOOR_SMOKE_LOG("event=shot file=%s saved=%d", *FPaths::GetCleanFilename(PendingShot), bSaved ? 1 : 0);
        Expect(bSaved, FString::Printf(TEXT("shot_not_saved=%s"), *FPaths::GetCleanFilename(PendingShot)));
        PendingShot.Reset();
        // Standing still for a capture is not being stuck.
        LastProgressSeconds = Now;
    }
    if (Step == EStep::TourAway || Step == EStep::TourBack)
    {
        return TickTour(Now);
    }
    if (!Bind())
    {
        return true;
    }
    AMarburgumPlayerController* Local = Controller.Get();
    APawn* Body = Local->GetPawn();
    AMarburgumDoor* Street = StreetDoor();
    const FVector DoorCentre = Street->GetDoorwayCentre();
    const FVector Into = (Route[1] - Route[0]).GetSafeNormal2D();
    // Half a metre inside the street door: in the leaf's swing.
    const FVector SwingPoint = FVector(DoorCentre.X, DoorCentre.Y, FloorZ) + Into * 50.0;

    if (Step == EStep::Bind)
    {
        if (Role == TEXT("observer"))
        {
            Advance(EStep::Observe, Now);
        }
        else if (Role == TEXT("tourist"))
        {
            Advance(EStep::TourHome, Now);
        }
        else
        {
            // Both players at the door before anything moves, so each sees the other's requests.
            if (PeopleAtDoor() < 2)
            {
                return true;
            }
            if (Role == TEXT("walker"))
            {
                Expect(Street->GetMotion() == EMarburgumDoorMotion::Closed, FString::Printf(TEXT("street_door_not_closed_at_start=%s"), MotionName(Street->GetMotion())));
            }
            Advance(Role == TEXT("walker") ? EStep::OpenStreet : EStep::AwaitOpen, Now);
        }
        return true;
    }

    // ---- walker ----
    if (Step == EStep::OpenStreet)
    {
        if (Phase == 0)
        {
            // Walk to the door, photograph it shut, open it with E.
            if (!MoveTo(Route[0], Now))
            {
                return true;
            }
            Face(DoorCentre);
            Shot(TEXT("01-outside-closed"));
            Phase = 1;
            return true;
        }
        if (Phase == 1)
        {
            const AActor* Candidate = Local->FindUseCandidate();
            Expect(Candidate == Street, TEXT("street_door_not_the_use_candidate"));
            Local->RequestInteract();
            DoorRequestedSeconds = Now;
            MB2_DOOR_SMOKE_LOG("event=request door=street want=open states=%s", *DoorStates());
            Phase = 2;
            return true;
        }
        if (Street->GetMotion() == EMarburgumDoorMotion::Open)
        {
            MB2_DOOR_SMOKE_LOG("event=street_open seconds=%.2f", Now - DoorRequestedSeconds);
            Face(DoorCentre + Into * 200.0);
            Shot(TEXT("02-outside-open"));
            Advance(EStep::SeeReversal, Now);
        }
        else if (Now - DoorRequestedSeconds > DoorAnswerSeconds)
        {
            Fail(FString::Printf(TEXT("street_door_did_not_open=%s"), MotionName(Street->GetMotion())));
        }
        return true;
    }
    if (Step == EStep::SeeReversal)
    {
        // The rival shuts the door and reverses the swing. Wait to see the door closing and open again.
        bSawClosing |= Street->GetMotion() == EMarburgumDoorMotion::Closing;
        if (bSawClosing && Street->GetMotion() == EMarburgumDoorMotion::Open)
        {
            MB2_DOOR_SMOKE_LOG("event=saw_reversal states=%s", *DoorStates());
            Advance(EStep::HoldInSwing, Now);
        }
        else if (Now - StepSeconds > 30.0)
        {
            Expect(false, FString::Printf(TEXT("reversal_not_seen=%s"), MotionName(Street->GetMotion())));
            Advance(EStep::HoldInSwing, Now);
        }
        return true;
    }
    if (Step == EStep::HoldInSwing)
    {
        if (Phase == 0)
        {
            if (!MoveTo(SwingPoint, Now))
            {
                return true;
            }
            Face(SwingPoint - Into * 300.0);
            MB2_DOOR_SMOKE_LOG("event=in_swing at=%s", *Body->GetActorLocation().ToString());
            Phase = 1;
            StepSeconds = Now;
            return true;
        }
        // The rival tries to shut the door on us. It must stay open the whole time.
        Expect(Street->IsOpen(), FString::Printf(TEXT("street_door_moved_while_in_swing=%s"), MotionName(Street->GetMotion())));
        if (Now - StepSeconds > 8.0)
        {
            Shot(TEXT("03-in-doorway"));
            RouteIndex = 1;
            Advance(EStep::Walk, Now);
        }
        return true;
    }
    if (Step == EStep::Walk)
    {
        if (RouteIndex >= Route.Num())
        {
            Face(DoorCentre);
            Shot(TEXT("09-back-outside"));
            MB2_DOOR_SMOKE_LOG("event=route_done legs=%d rooms_shot=%d states=%s", Route.Num() - 1, RoomShots, *DoorStates());
            Advance(EStep::Hold, Now);
            return true;
        }
        const FVector From = Route[RouteIndex - 1];
        const FVector To = Route[RouteIndex];
        // A shut door on this leg: open it with E from where we stand, and wait for it.
        if (AMarburgumDoor* Waiting = WaitingDoor.Get())
        {
            if (Waiting->GetMotion() == EMarburgumDoorMotion::Open)
            {
                MB2_DOOR_SMOKE_LOG("event=room_door_open leg=%d seconds=%.2f", RouteIndex, Now - DoorRequestedSeconds);
                WaitingDoor.Reset();
                LastProgressSeconds = Now;
            }
            else if (Now - DoorRequestedSeconds > DoorAnswerSeconds)
            {
                Fail(FString::Printf(TEXT("room_door_did_not_open_leg_%d=%s"), RouteIndex, MotionName(Waiting->GetMotion())));
            }
            return true;
        }
        for (AMarburgumDoor* Door : BuildingDoors())
        {
            if (!Door->IsOpen() && DistanceToSegment2D(Door->GetDoorwayCentre(), From, To) < 60.0)
            {
                Expect(Local->FindUseCandidate() == Door, FString::Printf(TEXT("room_door_not_the_use_candidate_leg_%d"), RouteIndex));
                Local->RequestInteract();
                WaitingDoor = Door;
                DoorRequestedSeconds = Now;
                MB2_DOOR_SMOKE_LOG("event=request door=room leg=%d want=open", RouteIndex);
                return true;
            }
        }
        if (!MoveTo(To, Now))
        {
            return true;
        }
        // The room's middle comes after its doorway's near and far side: photograph it, looking on into the room.
        if (RouteIndex >= 4 && Route[RouteIndex - 1].Equals(Route[RouteIndex + 1 < Route.Num() ? RouteIndex + 1 : RouteIndex], 1.0))
        {
            ++RoomShots;
            Face(To + (To - From).GetSafeNormal2D() * 300.0, -4.f);
            Shot(FString::Printf(TEXT("%02d-room-%d"), 3 + RoomShots, RoomShots));
        }
        else if (RouteIndex == 1)
        {
            Face(To + (To - From).GetSafeNormal2D() * 300.0, -4.f);
            Shot(TEXT("04-front-room"));
        }
        ++RouteIndex;
        return true;
    }
    if (Step == EStep::Hold)
    {
        if (Now - StepSeconds > HoldSeconds)
        {
            MB2_DOOR_SMOKE_LOG("event=final states=%s", *DoorStates());
            Pass();
        }
        return true;
    }

    // ---- rival ----
    const FVector Beside = Route[0] + FVector::CrossProduct(Into, FVector::UpVector) * 70.0;
    if (Step == EStep::AwaitOpen)
    {
        if (!MoveTo(Beside, Now))
        {
            return true;
        }
        if (Street->GetMotion() == EMarburgumDoorMotion::Open)
        {
            Face(DoorCentre);
            Advance(EStep::CloseAndReverse, Now);
        }
        return true;
    }
    if (Step == EStep::CloseAndReverse)
    {
        if (Phase == 0)
        {
            // Wait out the request rate window from the approach, then shut the door.
            if (Now - StepSeconds < 1.5)
            {
                return true;
            }
            const AActor* Candidate = Local->FindUseCandidate();
            const AMarburgumDoor* CandidateDoor = Cast<AMarburgumDoor>(Candidate);
            MB2_DOOR_SMOKE_LOG("event=use_candidate actor=%s door=%s street=%d body_yaw=%.0f view_yaw=%.0f at=%s",
                Candidate ? *Candidate->GetName() : TEXT("none"),
                CandidateDoor ? *CandidateDoor->GetPlaceName() : TEXT("-"),
                CandidateDoor && CandidateDoor->IsStreetDoor() ? 1 : 0,
                Body ? Body->GetActorRotation().Yaw : 0.f,
                Local->GetControlRotation().Yaw,
                Body ? *Body->GetActorLocation().ToString() : TEXT("-"));
            Expect(Candidate == Street, TEXT("rival_street_door_not_the_use_candidate"));
            Local->RequestInteract();
            DoorRequestedSeconds = Now;
            MB2_DOOR_SMOKE_LOG("event=request door=street want=close states=%s", *DoorStates());
            Phase = 1;
            return true;
        }
        if (Phase == 1)
        {
            if (Street->GetMotion() == EMarburgumDoorMotion::Closing)
            {
                // Half way: change our mind.
                Local->RequestInteract();
                DoorRequestedSeconds = Now;
                MB2_DOOR_SMOKE_LOG("event=request door=street want=reverse states=%s", *DoorStates());
                Phase = 2;
            }
            else if (Now - DoorRequestedSeconds > DoorAnswerSeconds)
            {
                Fail(FString::Printf(TEXT("street_door_did_not_start_closing=%s"), MotionName(Street->GetMotion())));
            }
            return true;
        }
        if (Street->GetMotion() == EMarburgumDoorMotion::Open)
        {
            MB2_DOOR_SMOKE_LOG("event=reversed seconds=%.2f", Now - DoorRequestedSeconds);
            Advance(EStep::AwaitWalkerInSwing, Now);
        }
        else if (Street->GetMotion() == EMarburgumDoorMotion::Closed || Now - DoorRequestedSeconds > DoorAnswerSeconds)
        {
            Fail(FString::Printf(TEXT("swing_did_not_reverse=%s"), MotionName(Street->GetMotion())));
        }
        return true;
    }
    if (Step == EStep::AwaitWalkerInSwing)
    {
        for (TActorIterator<AMarburgumCharacter> It(Local->GetWorld()); It; ++It)
        {
            if (*It != Body && FVector::Dist2D(It->GetActorLocation(), SwingPoint) < InSwingCm)
            {
                Advance(EStep::CloseOnWalker, Now);
                return true;
            }
        }
        if (Now - StepSeconds > 30.0)
        {
            Fail(TEXT("walker_never_stood_in_the_swing"));
        }
        return true;
    }
    if (Step == EStep::CloseOnWalker)
    {
        if (Phase == 0)
        {
            if (Now - StepSeconds < 1.2)
            {
                return true;
            }
            Local->RequestInteract();
            DoorRequestedSeconds = Now;
            MB2_DOOR_SMOKE_LOG("event=request door=street want=close_on_walker states=%s", *DoorStates());
            Phase = 1;
            return true;
        }
        Expect(Street->IsOpen(), FString::Printf(TEXT("street_door_closed_on_walker=%s"), MotionName(Street->GetMotion())));
        if (Now - DoorRequestedSeconds > 2.5)
        {
            MB2_DOOR_SMOKE_LOG("event=close_on_walker_held states=%s", *DoorStates());
            Advance(EStep::FarDoor, Now);
        }
        return true;
    }
    if (Step == EStep::FarDoor)
    {
        if (Phase == 0)
        {
            if (Now - StepSeconds < 1.2)
            {
                return true;
            }
            // The room door farthest from us, well out of reach: a client that asks anyway is refused by the server.
            AMarburgumDoor* Far = nullptr;
            for (AMarburgumDoor* Door : BuildingDoors())
            {
                if (!Door->IsStreetDoor() && !Door->IsOpen()
                    && (!Far || FVector::Dist2D(Door->GetDoorwayCentre(), Body->GetActorLocation()) > FVector::Dist2D(Far->GetDoorwayCentre(), Body->GetActorLocation())))
                {
                    Far = Door;
                }
            }
            if (!Far)
            {
                MB2_DOOR_SMOKE_LOG("event=far_door_skipped reason=no_shut_room_door");
                Pass();
                return true;
            }
            WaitingDoor = Far;
            Local->SubmitDoorToggle(Far);
            DoorRequestedSeconds = Now;
            MB2_DOOR_SMOKE_LOG("event=request door=far distance_cm=%.0f", FVector::Dist2D(Far->GetDoorwayCentre(), Body->GetActorLocation()));
            Phase = 1;
            return true;
        }
        const AMarburgumDoor* Far = WaitingDoor.Get();
        Expect(Far && !Far->IsOpen(), TEXT("far_door_moved"));
        if (Now - DoorRequestedSeconds > 2.5)
        {
            MB2_DOOR_SMOKE_LOG("event=far_door_held states=%s", *DoorStates());
            Pass();
        }
        return true;
    }

    // ---- observer ----
    if (Step == EStep::Observe)
    {
        // Let replication settle, then report every door.
        if (Now - StepSeconds < 3.0)
        {
            return true;
        }
        CheckExpected(TEXT("observed"));
        Face(DoorCentre);
        Shot(TEXT("observer"));
        Advance(EStep::Done, Now);
        return true;
    }
    // ---- tourist ----
    if (Step == EStep::TourHome)
    {
        // At home first: the doors as the walker left them, before the server moves us away.
        if (Now - StepSeconds < 3.0)
        {
            return true;
        }
        HomeCentre = DoorCentre;
        CheckExpected(TEXT("tour_home"));
        Advance(EStep::TourAway, Now);
        return true;
    }
    if (Step == EStep::Done && PendingShot.IsEmpty() && ShotLabel.IsEmpty())
    {
        Pass();
    }
    return true;
}

void UMarburgumDoorSmokeSubsystem::Shot(const FString& Label)
{
    if (ShotPrefix.IsEmpty())
    {
        return;
    }
    ShotLabel = Label;
    ShotDueSeconds = FPlatformTime::Seconds() + ShotDelaySeconds;
}

void UMarburgumDoorSmokeSubsystem::Expect(bool bCondition, const FString& What)
{
    if (!bCondition && !Problems.Contains(What))
    {
        Problems.Add(What);
        MB2_DOOR_SMOKE_LOG("event=expectation_failed what=%s", *What);
    }
}

void UMarburgumDoorSmokeSubsystem::Pass()
{
    if (!Problems.IsEmpty())
    {
        Fail(TEXT("expectations"));
        return;
    }
    MB2_DOOR_SMOKE_LOG("event=pass");
    bDone = true;
    FPlatformMisc::RequestExitWithStatus(false, 0);
}

void UMarburgumDoorSmokeSubsystem::Fail(const FString& Reason)
{
    if (bDone)
    {
        return;
    }
    Problems.Add(Reason);
    MB2_DOOR_SMOKE_LOG("event=fail reason=%s", *FString::Join(Problems, TEXT(",")));
    bDone = true;
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

#undef MB2_DOOR_SMOKE_LOG
