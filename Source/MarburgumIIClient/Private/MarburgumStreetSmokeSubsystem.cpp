#include "MarburgumStreetSmokeSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "MarburgumCharacter.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPedestrian.h"
#include "MarburgumPlayerController.h"
#include "MarburgumVehicle.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "UnrealClient.h"

#define MB2_STREET_SMOKE_LOG(Format, ...) UE_LOG(LogMarburgumNet, Log, TEXT("MB2SSMOKE role=client " Format), ##__VA_ARGS__)

namespace MarburgumStreetSmoke
{
    constexpr double SettleSeconds = 3.0;
    // People counted round the player, and how many must be there.
    constexpr double CountWithinCm = 7000.0;
    constexpr int32 MinPeople = 8;
    constexpr double PopulateTimeoutSeconds = 40.0;
    // A walker has moved this far after WalkSeconds; this many of them must have.
    constexpr double WalkSeconds = 8.0;
    constexpr double WalkedCm = 300.0;
    constexpr int32 MinWalked = 5;
    // Nobody is seen to appear this close (the spawn bands start at 22 m; replication lets them walk a little).
    constexpr double AppearMinCm = 1200.0;
    // The fight: a punch every SwingSpacing for FightSeconds.
    constexpr double FightSeconds = 7.0;
    constexpr double SwingSpacingSeconds = 0.6;
    // People within this distance count as having seen the fight.
    constexpr double FightWitnessCm = 3000.0;
    constexpr double CalmSeconds = 4.0;
    constexpr double SeatTimeoutSeconds = 8.0;
    constexpr double DriveSeconds = 7.0;
    constexpr double GatherWaitSeconds = 25.0;
    const FName DriverSeat(TEXT("driver"));
}

using namespace MarburgumStreetSmoke;

bool UMarburgumStreetSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
    return GameInstance && !GameInstance->IsDedicatedServerInstance() && FParse::Param(FCommandLine::Get(), TEXT("MB2StreetSmoke"));
}

void UMarburgumStreetSmokeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    FParse::Value(FCommandLine::Get(), TEXT("MB2StreetShot="), ShotPrefix);
    FParse::Value(FCommandLine::Get(), TEXT("MB2StreetTimeout="), TimeoutSeconds);
    StartSeconds = FPlatformTime::Seconds();
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMarburgumStreetSmokeSubsystem::Tick), 0.f);
    MB2_STREET_SMOKE_LOG("event=start shot=%d", ShotPrefix.IsEmpty() ? 0 : 1);
}

void UMarburgumStreetSmokeSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    Super::Deinitialize();
}

AMarburgumCharacter* UMarburgumStreetSmokeSubsystem::Body() const
{
    const AMarburgumPlayerController* Local = Controller.Get();
    return Local ? Cast<AMarburgumCharacter>(Local->GetPawn()) : nullptr;
}

bool UMarburgumStreetSmokeSubsystem::Bind()
{
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (!UMarburgumSessionSubsystem::IsAdmitted(World))
    {
        return false;
    }
    AMarburgumPlayerController* Local = Cast<AMarburgumPlayerController>(World->GetFirstPlayerController());
    if (!Local || !Cast<AMarburgumCharacter>(Local->GetPawn()))
    {
        return false;
    }
    if (!Car.IsValid())
    {
        TArray<AMarburgumVehicle*> Cars;
        AMarburgumVehicle::GetAll(World, Cars);
        for (AMarburgumVehicle* Candidate : Cars)
        {
            if (Candidate->VehicleId == TEXT("lent_hatchback"))
            {
                Car = Candidate;
            }
        }
        if (!Car.IsValid())
        {
            return false;
        }
    }
    if (Controller.Get() != Local)
    {
        Controller = Local;
        Local->OnVehicleResult.AddWeakLambda(this, [this](bool, FName, FName) { bSeatAnswered = true; });
        StepSeconds = FPlatformTime::Seconds();
        // Look along the street, away from the car, over the body's shoulder.
        Local->SetControlRotation(FRotator(-8.f, Car->GetActorRotation().Yaw, 0.f));
        MB2_STREET_SMOKE_LOG("event=ready at=%s", *Body()->GetActorLocation().ToString());
    }
    return true;
}

void UMarburgumStreetSmokeSubsystem::People(TArray<AMarburgumPedestrian*>& Out, double WithinCm) const
{
    Out.Reset();
    const AMarburgumCharacter* Character = Body();
    if (!Character)
    {
        return;
    }
    for (TActorIterator<AMarburgumPedestrian> It(Character->GetWorld()); It; ++It)
    {
        if (FVector::Dist2D(It->GetActorLocation(), Character->GetActorLocation()) <= WithinCm)
        {
            Out.Add(*It);
        }
    }
}

void UMarburgumStreetSmokeSubsystem::Observe()
{
    TArray<AMarburgumPedestrian*> Near;
    People(Near, CountWithinCm);
    int32 Fleeing = 0;
    int32 Watching = 0;
    int32 Down = 0;
    int32 Gathered = 0;
    int32 Dodging = 0;
    const FVector Here = Body() ? Body()->GetActorLocation() : FVector::ZeroVector;
    for (AMarburgumPedestrian* Person : Near)
    {
        if (!Known.Contains(Person))
        {
            Known.Add(Person);
            // Before the probe, everyone is a spawn of the street: where they are first seen says where they came from.
            if (Step <= 3)
            {
                NearestSpawnCm = FMath::Min(NearestSpawnCm, FVector::Dist2D(Here, Person->GetActorLocation()));
            }
        }
        Fleeing += Person->GetMood() == EMarburgumStreetMood::Flee ? 1 : 0;
        Watching += Person->GetMood() == EMarburgumStreetMood::Watch ? 1 : 0;
        Gathered += Person->GetMood() == EMarburgumStreetMood::Gather ? 1 : 0;
        Dodging += Person->GetMood() == EMarburgumStreetMood::Dodge ? 1 : 0;
        Down += Person->IsDown() ? 1 : 0;
    }
    MostFleeing = FMath::Max(MostFleeing, Fleeing);
    MostWatching = FMath::Max(MostWatching, Watching);
    MostDown = FMath::Max(MostDown, Down);
    MostGathered = FMath::Max(MostGathered, Gathered);
    MostDodging = FMath::Max(MostDodging, Dodging);
}

void UMarburgumStreetSmokeSubsystem::Expect(bool bCondition, const FString& What)
{
    if (!bCondition)
    {
        Problems.Add(What);
        MB2_STREET_SMOKE_LOG("event=expectation_failed what=%s", *What);
    }
}

void UMarburgumStreetSmokeSubsystem::Shot(const TCHAR* Label)
{
    if (ShotPrefix.IsEmpty())
    {
        return;
    }
    PendingShot = FString::Printf(TEXT("%s-%s.png"), *ShotPrefix, Label);
    IFileManager::Get().Delete(*PendingShot, false, true, true);
    FScreenshotRequest::RequestScreenshot(PendingShot, true, false);
}

void UMarburgumStreetSmokeSubsystem::Advance(int32 Next, double Now)
{
    Step = Next;
    StepSeconds = Now;
}

void UMarburgumStreetSmokeSubsystem::Report(const TCHAR* Event, double Now)
{
    TArray<AMarburgumPedestrian*> Near;
    People(Near, CountWithinCm);
    int32 Moods[7] = {};
    for (const AMarburgumPedestrian* Person : Near)
    {
        ++Moods[FMath::Clamp(static_cast<int32>(Person->GetMood()), 0, 6)];
    }
    const AMarburgumCharacter* Character = Body();
    const FVector At = Character ? Character->GetActorLocation() : FVector::ZeroVector;
    MB2_STREET_SMOKE_LOG("event=%s step=%d step_s=%.1f pos=%.0f,%.0f,%.0f people=%d walk=%d flee=%d watch=%d gather=%d dodge=%d fight=%d down=%d "
                         "most_flee=%d most_watch=%d most_down=%d most_gather=%d most_dodge=%d nearest_appear_m=%.1f",
        Event, Step, Now - StepSeconds, At.X, At.Y, At.Z, Near.Num(), Moods[0], Moods[1], Moods[2], Moods[3], Moods[4], Moods[5], Moods[6], MostFleeing,
        MostWatching, MostDown, MostGathered, MostDodging, NearestSpawnCm == TNumericLimits<double>::Max() ? -1.0 : NearestSpawnCm / 100.0);
}

bool UMarburgumStreetSmokeSubsystem::Tick(float DeltaTime)
{
    if (bDone)
    {
        return true;
    }
    const double Now = FPlatformTime::Seconds();
    if (PassAt > 0.0)
    {
        // The last screenshot is written before the process goes.
        if (Now >= PassAt || PendingShot.IsEmpty() || IFileManager::Get().FileExists(*PendingShot))
        {
            Pass();
        }
        return true;
    }
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
    Observe();
    Run(Now);
    return true;
}

void UMarburgumStreetSmokeSubsystem::Run(double Now)
{
    AMarburgumCharacter* Character = Body();
    AMarburgumPlayerController* Local = Controller.Get();
    AMarburgumVehicle* Hatchback = Car.Get();
    if (!Character || !Local || !Hatchback)
    {
        return;
    }
    TArray<AMarburgumPedestrian*> Near;
    switch (Step)
    {
    case 0:
        if (Now - StepSeconds >= SettleSeconds)
        {
            Advance(1, Now);
        }
        return;
    case 1:
        // People appear round the player.
        People(Near, CountWithinCm);
        if (Near.Num() < MinPeople)
        {
            if (Now - StepSeconds > PopulateTimeoutSeconds)
            {
                Report(TEXT("too_few_people"), Now);
                Fail(FString::Printf(TEXT("people=%d"), Near.Num()));
            }
            return;
        }
        Report(TEXT("populated"), Now);
        Marks.Reset();
        for (AMarburgumPedestrian* Person : Near)
        {
            Marks.Add(Person, Person->GetActorLocation());
        }
        Shot(TEXT("people"));
        Advance(2, Now);
        return;
    case 2:
    {
        // ...and walk.
        if (Now - StepSeconds < WalkSeconds)
        {
            return;
        }
        int32 Walked = 0;
        int32 Kept = 0;
        int32 Fell = 0;
        for (const TPair<TWeakObjectPtr<AMarburgumPedestrian>, FVector>& Mark : Marks)
        {
            const AMarburgumPedestrian* Person = Mark.Key.Get();
            if (!Person)
            {
                continue;
            }
            ++Kept;
            const FVector Moved = Person->GetActorLocation() - Mark.Value;
            Walked += Moved.Size2D() >= WalkedCm ? 1 : 0;
            // Nobody sinks through the paving or floats off it while walking.
            Fell += FMath::Abs(Moved.Z) > 400.0 ? 1 : 0;
        }
        MB2_STREET_SMOKE_LOG("event=walked kept=%d walked=%d fell=%d", Kept, Walked, Fell);
        Expect(Walked >= MinWalked, FString::Printf(TEXT("walked=%d_of_%d"), Walked, Kept));
        Expect(Fell == 0, FString::Printf(TEXT("fell=%d"), Fell));
        Expect(NearestSpawnCm >= AppearMinCm, FString::Printf(TEXT("appeared_at_m=%.1f"), NearestSpawnCm / 100.0));
        Report(TEXT("walking"), Now);
        MostFleeing = MostWatching = 0;
        LastSwingSeconds = 0.0;
        Advance(3, Now);
        return;
    }
    case 3:
        // A fight: punches at the air, in the street.
        if (Now - StepSeconds < FightSeconds)
        {
            if (Now - LastSwingSeconds >= SwingSpacingSeconds)
            {
                LastSwingSeconds = Now;
                Character->Jab();
            }
            if (!PendingShot.Contains(TEXT("fight")) && Now - StepSeconds > FightSeconds * 0.6)
            {
                Report(TEXT("fight_mid"), Now);
                Shot(TEXT("fight"));
            }
            return;
        }
        Report(TEXT("fight_over"), Now);
        People(Near, FightWitnessCm);
        Expect(Near.IsEmpty() || MostFleeing + MostWatching >= 1, FString::Printf(TEXT("nobody_reacted_witnesses=%d"), Near.Num()));
        Advance(4, Now);
        return;
    case 4:
        if (Now - StepSeconds < CalmSeconds)
        {
            return;
        }
        Report(TEXT("calm"), Now);
        bSeatAnswered = false;
        Local->RequestEnterVehicle(Hatchback, DriverSeat);
        Advance(5, Now);
        return;
    case 5:
        if (Character->GetSeatedVehicle() != Hatchback)
        {
            if (Now - StepSeconds > SeatTimeoutSeconds)
            {
                Fail(TEXT("never_seated"));
            }
            return;
        }
        Report(TEXT("seated"), Now);
        MostDown = MostGathered = MostDodging = 0;
        Local->SetDrivingOverride(0.7f, 0.f, 0.f, false);
        Advance(6, Now);
        return;
    case 6:
        // Off along the street: the probe puts people in the car's way.
        if (MostDown >= 1 && !PendingShot.Contains(TEXT("run-over")))
        {
            Report(TEXT("struck"), Now);
            Shot(TEXT("run-over"));
        }
        if (Now - StepSeconds < DriveSeconds)
        {
            return;
        }
        Local->SetDrivingOverride(0.f, 1.f, 0.f, false);
        Report(TEXT("braked"), Now);
        Expect(MostDown >= 1, TEXT("nobody_down_after_drive"));
        Expect(MostDodging >= 1, TEXT("nobody_dodged"));
        Advance(7, Now);
        return;
    case 7:
        // Somebody gathers round the person in the road, or runs from it.
        if (MostGathered >= 1 && !PendingShot.Contains(TEXT("gather")))
        {
            Report(TEXT("gathered"), Now);
            Shot(TEXT("gather"));
        }
        if (MostGathered < 1 && Now - StepSeconds < GatherWaitSeconds)
        {
            return;
        }
        Report(TEXT("after_run_over"), Now);
        Expect(MostGathered + MostFleeing + MostWatching >= 1, TEXT("nobody_answered_the_body"));
        PassAt = Now + 3.0;
        return;
    default:
        return;
    }
}

void UMarburgumStreetSmokeSubsystem::Pass()
{
    if (Problems.IsEmpty())
    {
        MB2_STREET_SMOKE_LOG("event=pass");
        bDone = true;
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }
    else
    {
        Fail(TEXT("expectations"));
    }
}

void UMarburgumStreetSmokeSubsystem::Fail(const FString& Reason)
{
    if (bDone)
    {
        return;
    }
    Problems.Add(Reason);
    MB2_STREET_SMOKE_LOG("event=fail reason=%s", *FString::Join(Problems, TEXT(",")));
    bDone = true;
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

#undef MB2_STREET_SMOKE_LOG
