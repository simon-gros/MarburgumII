#include "MarburgumStreetSubsystem.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformMisc.h"
#include "MarburgumCharacter.h"
#include "MarburgumCharacterMovement.h"
#include "MarburgumFighter.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPedestrian.h"
#include "MarburgumVehicle.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

using namespace MarburgumII::Street;

namespace MarburgumStreetPopulation
{
    // The walk graph tools/lent-ground builds for the Lent window.
    const TCHAR* GraphFile = TEXT("Marburgum/Street/lent_walk_graph.json");
    // The population thinks ten times a second, as the people do.
    constexpr double StepSeconds = 0.1;
    // A spawn probe looks for a node this close to the point it picked, and this close in height to the player.
    constexpr double ProbeNodeCm = 1500.0;
    constexpr double ProbeHeightCm = 1500.0;
    constexpr int32 ProbesPerSpawn = 6;
    // Nobody appears on top of somebody else.
    constexpr double SpawnApartCm = 200.0;
    // Someone put out of sight is placed at least this far inside the kept band, so walking away from the player
    // for their unseen grace (a brisk 1.5 m/s for 5 s) does not carry them straight out of it again.
    constexpr double OutOfViewKeepMarginCm = 800.0;
    // Someone put in sight lands this far inside the edge of the view, for the same reason (7.5 m across at 45 m).
    constexpr double InViewInsetDegrees = 15.0;
    // Fights within this distance of one another are one fight.
    constexpr double SameFightCm = 600.0;
    // A body is noticed while it stays down; its incident is refreshed every step and lapses this soon after.
    constexpr double BodyHoldSeconds = 0.5;
    constexpr float HalfHeightCm = 88.f;
    constexpr double SummarySeconds = 10.0;
}

bool UMarburgumStreetSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    if (!Super::ShouldCreateSubsystem(Outer))
    {
        return false;
    }
    const UWorld* World = Cast<UWorld>(Outer);
    return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UMarburgumStreetSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    bEnabled = false;
    if (InWorld.GetNetMode() == NM_Client)
    {
        return;
    }
    // Only a map with a walk graph has a street: Lent, for now.
    if (!InWorld.GetMapName().EndsWith(TEXT("lent")))
    {
        return;
    }
    int32 Switch = 1;
    FParse::Value(FCommandLine::Get(), TEXT("MB2Street="), Switch);
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("MB2_STREET")) == TEXT("0"))
    {
        Switch = 0;
    }
    if (Switch == 0)
    {
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=street_off reason=switch"), StreetLogPrefix);
        return;
    }
    const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), MarburgumStreetPopulation::GraphFile);
    FString Report;
    FString Error;
    const double Started = FPlatformTime::Seconds();
    if (!Graph.Load(InWorld, Path, Report, Error))
    {
        UE_LOG(LogMarburgumNet, Warning, TEXT("%s role=server event=street_off reason=\"%s\""), StreetLogPrefix, *Error);
        return;
    }
    int32 Seed = 1990;
    FParse::Value(FCommandLine::Get(), TEXT("MB2StreetSeed="), Seed);
    bProbe = FParse::Param(FCommandLine::Get(), TEXT("MB2StreetProbe"));
    Random.Initialize(Seed);
    bEnabled = true;
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=street_ready graph=\"%s\" %s seed=%d load_ms=%.0f"), StreetLogPrefix,
        *FPaths::GetCleanFilename(Path), *Report, Seed, (FPlatformTime::Seconds() - Started) * 1000.0);
}

void UMarburgumStreetSubsystem::Deinitialize()
{
    bEnabled = false;
    People.Reset();
    Super::Deinitialize();
}

TStatId UMarburgumStreetSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UMarburgumStreetSubsystem, STATGROUP_Tickables);
}

double UMarburgumStreetSubsystem::Now() const
{
    const UWorld* World = GetWorld();
    return World ? World->GetTimeSeconds() : 0.0;
}

int32 UMarburgumStreetSubsystem::CountPeople() const
{
    int32 Count = 0;
    for (const TWeakObjectPtr<AMarburgumPedestrian>& Person : People)
    {
        Count += Person.IsValid() ? 1 : 0;
    }
    return Count;
}

const FMarburgumStreetIncident* UMarburgumStreetSubsystem::FindIncident(int32 Id) const
{
    return Incidents.FindByPredicate([Id](const FMarburgumStreetIncident& Incident) { return Incident.Id == Id; });
}

void UMarburgumStreetSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (!bEnabled)
    {
        return;
    }
    // Every frame: a fast car covers a body length in a few frames.
    RunPeopleOver(DeltaTime);
    StepAccumulator = FMath::Min(StepAccumulator + DeltaTime, 1.0);
    while (StepAccumulator >= MarburgumStreetPopulation::StepSeconds)
    {
        StepAccumulator -= MarburgumStreetPopulation::StepSeconds;
        Step(Now());
    }
}

void UMarburgumStreetSubsystem::Step(double Time)
{
    People.RemoveAll([](const TWeakObjectPtr<AMarburgumPedestrian>& Person) { return !Person.IsValid(); });
    GatherViewers();
    GatherIncidents(Time);
    // The alarms people raise this step are heard on the next one: a scare spreads one hop a step.
    Alarms.Reset();
    for (const TWeakObjectPtr<AMarburgumPedestrian>& Person : People)
    {
        if (Person->IsSpreadingAlarm())
        {
            Alarms.Add({ Person->GetActorLocation(), Person->GetAlarmIncident() });
        }
    }
    MarkSeenAndCull(Time);
    SpawnPeople(Time, MarburgumStreetPopulation::StepSeconds);
    StepProbe();

    if (Time - LastSummarySeconds >= MarburgumStreetPopulation::SummarySeconds)
    {
        LastSummarySeconds = Time;
        int32 ByMood[7] = {};
        for (const TWeakObjectPtr<AMarburgumPedestrian>& Person : People)
        {
            ++ByMood[FMath::Clamp(static_cast<int32>(Person->GetMood()), 0, 6)];
        }
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=street_summary people=%d players=%d incidents=%d walk=%d flee=%d watch=%d gather=%d dodge=%d fight=%d down=%d"),
            StreetLogPrefix, People.Num(), Viewers.Num(), Incidents.Num(), ByMood[0], ByMood[1], ByMood[2], ByMood[3], ByMood[4], ByMood[5],
            ByMood[6]);
    }
}

void UMarburgumStreetSubsystem::GatherViewers()
{
    Viewers.Reset();
    for (TActorIterator<AMarburgumCharacter> It(GetWorld()); It; ++It)
    {
        if (!It->GetPlayerState())
        {
            continue;
        }
        FMarburgumStreetViewer Viewer;
        Viewer.Character = *It;
        const AMarburgumVehicle* Car = It->GetSeatedVehicle();
        Viewer.Location = Car ? Car->GetActorLocation() : It->GetActorLocation();
        Viewer.Velocity = Car ? Car->GetVelocity() : It->GetVelocity();
        const UMarburgumCharacterMovement* Movement = Cast<UMarburgumCharacterMovement>(It->GetCharacterMovement());
        Viewer.bOnFoot = !Car && Movement && !Movement->IsDown();
        const double Yaw = It->GetController() ? It->GetController()->GetControlRotation().Yaw : It->GetActorRotation().Yaw;
        Viewer.Facing = FVector2D(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)));
        Viewers.Add(Viewer);
    }
}

int32 UMarburgumStreetSubsystem::AddIncident(EEvent Event, const FVector& At, bool bPlayer, double Until, AActor* Who)
{
    FMarburgumStreetIncident Incident;
    Incident.Id = NextIncident++;
    Incident.Event = Event;
    Incident.Location = At;
    Incident.bPlayer = bPlayer;
    Incident.UntilSeconds = Until;
    Incident.Who = Who;
    Incidents.Add(Incident);
    return Incident.Id;
}

void UMarburgumStreetSubsystem::ReportFight(AActor* Who, const FVector& At, bool bPlayer)
{
    if (!bEnabled)
    {
        return;
    }
    const double Until = Now() + FightIncidentSeconds;
    for (FMarburgumStreetIncident& Incident : Incidents)
    {
        if (Incident.Event == EEvent::Fight && FVector::Dist2D(Incident.Location, At) < MarburgumStreetPopulation::SameFightCm)
        {
            // One fight, wherever in it the punch was thrown; a player in it makes it the player's.
            Incident.Location = FMath::Lerp(Incident.Location, At, 0.5);
            Incident.UntilSeconds = FMath::Max(Incident.UntilSeconds, Until);
            Incident.bPlayer = Incident.bPlayer || bPlayer;
            return;
        }
    }
    const int32 Id = AddIncident(EEvent::Fight, At, bPlayer, Until, Who);
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=incident id=%d kind=%s player=%d who=%s at=%.0f,%.0f,%.0f"), StreetLogPrefix, Id,
        EventNames[static_cast<int32>(EEvent::Fight)], bPlayer ? 1 : 0, Who ? *Who->GetName() : TEXT("-"), At.X, At.Y, At.Z);
}

void UMarburgumStreetSubsystem::GatherIncidents(double Time)
{
    // People on the ground: the cast, the street, and players.
    TSet<TWeakObjectPtr<AActor>> Down;
    for (TActorIterator<AMarburgumFighter> It(GetWorld()); It; ++It)
    {
        if (It->IsDown())
        {
            Down.Add(*It);
        }
    }
    for (TActorIterator<AMarburgumCharacter> It(GetWorld()); It; ++It)
    {
        const UMarburgumCharacterMovement* Movement = Cast<UMarburgumCharacterMovement>(It->GetCharacterMovement());
        if (Movement && Movement->IsDown() && !It->GetSeatedVehicle() && !It->GetCharacterMovement()->IsSwimming())
        {
            Down.Add(*It);
        }
    }
    for (auto It = BodyIncidents.CreateIterator(); It; ++It)
    {
        if (!Down.Contains(It.Key()))
        {
            It.RemoveCurrent();
        }
    }
    for (const TWeakObjectPtr<AActor>& Body : Down)
    {
        const FVector At = Body->GetActorLocation();
        int32* Id = BodyIncidents.Find(Body);
        FMarburgumStreetIncident* Existing = Id ? Incidents.FindByPredicate([Id](const FMarburgumStreetIncident& I) { return I.Id == *Id; }) : nullptr;
        if (Existing)
        {
            Existing->Location = At;
            Existing->UntilSeconds = Time + MarburgumStreetPopulation::BodyHoldSeconds;
            continue;
        }
        const bool bPlayer = Cast<AMarburgumCharacter>(Body.Get()) != nullptr;
        const int32 NewId = AddIncident(EEvent::Body, At, bPlayer, Time + MarburgumStreetPopulation::BodyHoldSeconds, Body.Get());
        BodyIncidents.Add(Body, NewId);
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=incident id=%d kind=%s player=%d who=%s at=%.0f,%.0f,%.0f"), StreetLogPrefix, NewId,
            EventNames[static_cast<int32>(EEvent::Body)], bPlayer ? 1 : 0, *Body->GetName(), At.X, At.Y, At.Z);
    }
    Incidents.RemoveAll([Time](const FMarburgumStreetIncident& Incident) { return Incident.UntilSeconds < Time; });

    Movers.Reset();
    TArray<AMarburgumVehicle*> Cars;
    AMarburgumVehicle::GetAll(GetWorld(), Cars);
    for (const AMarburgumVehicle* Car : Cars)
    {
        const UBoxComponent* Body = Car->GetBody();
        if (!Body || !Body->IsSimulatingPhysics())
        {
            continue;
        }
        const FVector Velocity = Body->GetPhysicsLinearVelocity();
        if (Velocity.Size2D() >= DodgeMinSpeedCm)
        {
            Movers.Add({ Body->GetComponentLocation(), Velocity });
        }
    }
}

bool UMarburgumStreetSubsystem::InViewOf(const FMarburgumStreetViewer& Viewer, const FVector& At, double InsetDegrees) const
{
    const FVector2D Offset(At.X - Viewer.Location.X, At.Y - Viewer.Location.Y);
    const double Distance = Offset.Size();
    if (Distance < 1.0)
    {
        return true;
    }
    return FVector2D::DotProduct(Offset / Distance, Viewer.Facing) >= FMath::Cos(FMath::DegreesToRadians(ViewHalfAngleDegrees - InsetDegrees));
}

void UMarburgumStreetSubsystem::MarkSeenAndCull(double Time)
{
    for (TWeakObjectPtr<AMarburgumPedestrian>& Weak : People)
    {
        AMarburgumPedestrian* Person = Weak.Get();
        const FVector At = Person->GetActorLocation();
        double Nearest = TNumericLimits<double>::Max();
        for (const FMarburgumStreetViewer& Viewer : Viewers)
        {
            const double Distance = FVector::Dist2D(Viewer.Location, At);
            Nearest = FMath::Min(Nearest, Distance);
            if (Distance < CullCm && InViewOf(Viewer, At))
            {
                Person->LastSeenSeconds = Time;
            }
        }
        const bool bUnseen = Time - Person->LastSeenSeconds > UnseenSeconds;
        const TCHAR* Reason = nullptr;
        if (Viewers.IsEmpty())
        {
            Reason = TEXT("nobody_about");
        }
        else if (Person->IsOut())
        {
            // Someone lying in the street stays a while: taken away once nobody is close and nobody sees it.
            if (bUnseen && (Nearest > CullCm || (Nearest > ClearBodyCm && Time - Person->GetDownSinceSeconds() > ClearBodySeconds)))
            {
                Reason = TEXT("body_cleared");
            }
        }
        else if (Nearest > CullCm)
        {
            Reason = TEXT("far");
        }
        else if (Nearest > KeepCm && bUnseen)
        {
            Reason = TEXT("unseen");
        }
        else if (Person->IsLost() && bUnseen)
        {
            Reason = TEXT("lost");
        }
        if (Reason)
        {
            UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=despawn npc=%s reason=%s nearest_player_m=%.1f"), StreetLogPrefix,
                *Person->GetNpcId().ToString(), Reason, Nearest == TNumericLimits<double>::Max() ? -1.0 : Nearest / 100.0);
            Person->Destroy();
            Weak.Reset();
        }
    }
    People.RemoveAll([](const TWeakObjectPtr<AMarburgumPedestrian>& Person) { return !Person.IsValid(); });
}

bool UMarburgumStreetSubsystem::SpawnPointOk(const FVector& At, bool bInView, bool bNearPlayers) const
{
    for (const FMarburgumStreetViewer& Viewer : Viewers)
    {
        const double Distance = FVector::Dist2D(Viewer.Location, At);
        if (!bNearPlayers && (Distance < SpawnOutOfViewMinCm || (InViewOf(Viewer, At) && Distance < SpawnInViewMinCm)))
        {
            return false;
        }
    }
    for (const TWeakObjectPtr<AMarburgumPedestrian>& Person : People)
    {
        if (Person.IsValid() && FVector::Dist2D(Person->GetActorLocation(), At) < MarburgumStreetPopulation::SpawnApartCm)
        {
            return false;
        }
    }
    return true;
}

bool UMarburgumStreetSubsystem::TrySpawnFor(const FMarburgumStreetViewer& Viewer, bool bInView)
{
    using namespace MarburgumStreetPopulation;
    const double FacingYaw = FMath::RadiansToDegrees(FMath::Atan2(Viewer.Facing.Y, Viewer.Facing.X));
    for (int32 Probe = 0; Probe < ProbesPerSpawn; ++Probe)
    {
        double Yaw;
        double Distance;
        if (bInView)
        {
            Yaw = FacingYaw + Random.FRandRange(-(ViewHalfAngleDegrees - InViewInsetDegrees), ViewHalfAngleDegrees - InViewInsetDegrees);
            Distance = Random.FRandRange(SpawnInViewMinCm, SpawnInViewMaxCm);
        }
        else
        {
            Yaw = FacingYaw + 180.0 + Random.FRandRange(-(180.0 - ViewHalfAngleDegrees), 180.0 - ViewHalfAngleDegrees);
            Distance = Random.FRandRange(SpawnOutOfViewMinCm, SpawnOutOfViewMaxCm);
        }
        const FVector Point = Viewer.Location + FRotator(0.0, Yaw, 0.0).Vector() * Distance;
        const int32 Node = Graph.NearestNode(Point, ProbeNodeCm, ProbeHeightCm);
        if (Node == INDEX_NONE || Graph.GetNodes()[Node].bRoad)
        {
            continue;
        }
        const FVector At = Graph.GetNodes()[Node].Location;
        // Snapped to a node, the place must still lie inside the band where people are kept, or they go at once, and
        // on the side of the view it was picked for: an "in view" node the player cannot see is never marked seen.
        const double FromViewer = FVector::Dist2D(Viewer.Location, At);
        if (FromViewer > (bInView ? CullCm - 200.0 : KeepCm - OutOfViewKeepMarginCm) || (bInView ? !InViewOf(Viewer, At, InViewInsetDegrees) : InViewOf(Viewer, At))
            || !SpawnPointOk(At, bInView) || Graph.Touching(Node).IsEmpty())
        {
            continue;
        }

        if (SpawnAt(Node, nullptr, bInView ? TEXT("in_view") : TEXT("out_of_view"), FVector::Dist2D(Viewer.Location, At)))
        {
            return true;
        }
    }
    return false;
}

AMarburgumPedestrian* UMarburgumStreetSubsystem::SpawnAt(int32 Node, const FVector* Override, const TCHAR* Why, double PlayerDistanceCm)
{
    using namespace MarburgumStreetPopulation;
    const FVector NodeAt = Graph.GetNodes()[Node].Location;
    const FVector At = Override ? *Override : NodeAt;
    const int32 GroupIndex = PickWeighted(LentMix, UE_ARRAY_COUNT(LentMix), Random.FRand());
    const FGroup& Group = Groups[GroupIndex];
    const int32 LookIndex = FMath::Max(0, PickLook(Group, Random.FRand()));
    const EPersonality Personality = PickPersonality(Group, Random.FRand());
    const float Pace = FMath::Lerp(Group.WalkSpeedMin, Group.WalkSpeedMax, Random.FRand());
    const TArray<int32>& Edges = Graph.Touching(Node);
    const int32 Edge = Edges[Random.RandHelper(Edges.Num())];
    const FVector Toward = Graph.GetNodes()[Graph.Other(Edge, Node)].Location;
    const FTransform Spot(FRotator(0.0, (Toward - NodeAt).Rotation().Yaw, 0.0), At + FVector(0.0, 0.0, HalfHeightCm + 2.0));
    AMarburgumPedestrian* Person = GetWorld()->SpawnActorDeferred<AMarburgumPedestrian>(AMarburgumPedestrian::StaticClass(), Spot, nullptr,
        nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding);
    if (!Person)
    {
        return nullptr;
    }
    const int32 Serial = NextSerial++;
    Person->NpcId = FName(*FString::Printf(TEXT("street_%d"), Serial));
    Person->InitStreet(*this, Serial, GroupIndex, LookIndex, Personality, Pace, Edge, Node, Random.GetUnsignedInt());
    Person->FinishSpawning(Spot);
    if (!IsValid(Person))
    {
        return nullptr;
    }
    People.Add(Person);
    // Just put where somebody may be looking: counted as seen now, so the cull gives them their grace.
    Person->LastSeenSeconds = Now();
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=spawn npc=%s group=%s profile=%s personality=%s pace=%.2f why=%s player_m=%.1f at=%.0f,%.0f,%.0f"),
        StreetLogPrefix, *Person->GetNpcId().ToString(), Group.Id, Looks[LookIndex].Profile, PersonalityNames[static_cast<int32>(Personality)], Pace,
        Why, PlayerDistanceCm / 100.0, At.X, At.Y, At.Z);
    return Person;
}

void UMarburgumStreetSubsystem::StepProbe()
{
    if (!bProbe || bProbeDone)
    {
        return;
    }
    TArray<AMarburgumVehicle*> Cars;
    AMarburgumVehicle::GetAll(GetWorld(), Cars);
    for (AMarburgumVehicle* Car : Cars)
    {
        const bool bDriven = !Car->GetDriverSeatId().IsNone() && Car->GetOccupant(Car->GetDriverSeatId()) != nullptr;
        if (!bDriven || Car->GetForwardSpeed() < 3.f)
        {
            continue;
        }
        bProbeDone = true;
        const FVector Forward = Car->GetActorForwardVector().GetSafeNormal2D();
        const FVector Right(-Forward.Y, Forward.X, 0.0);
        FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumStreetProbe), true, Car);
        const auto Ground = [this, &Params](const FVector& Point, FVector& Out)
        {
            FHitResult Hit;
            if (!GetWorld()->LineTraceSingleByChannel(Hit, Point + FVector(0.0, 0.0, 300.0), Point - FVector(0.0, 0.0, 600.0), ECC_Visibility, Params))
            {
                return false;
            }
            Out = Hit.ImpactPoint;
            return true;
        };
        // In the car's way: one who will not jump clear (and is easily out), and one further on who will.
        const struct { double AheadCm; bool bDodges; float Vitality; } Probes[] = { { 1100.0, false, 30.f }, { 3200.0, true, StreetVitality } };
        for (const auto& Probe : Probes)
        {
            FVector At;
            const FVector Want = Car->GetActorLocation() + Forward * Probe.AheadCm;
            const int32 Node = Graph.NearestNode(Want, 3000.0, 1000.0);
            if (Node == INDEX_NONE || !Ground(Want, At))
            {
                UE_LOG(LogMarburgumNet, Warning, TEXT("%s role=server event=probe_skipped ahead_m=%.0f"), StreetLogPrefix, Probe.AheadCm / 100.0);
                continue;
            }
            if (AMarburgumPedestrian* Person = SpawnAt(Node, &At, Probe.bDodges ? TEXT("probe_dodges") : TEXT("probe_stands"), Probe.AheadCm))
            {
                Person->MakeProbe(At + Right * 500.0, 20.0, Probe.bDodges, Probe.Vitality);
            }
        }
        // Witnesses on the pavement either side of where it will happen.
        TArray<int32> Near;
        Graph.NodesWithin(Car->GetActorLocation() + Forward * 1100.0, 1600.0, Near);
        int32 Witnesses = 0;
        for (const int32 Node : Near)
        {
            const FVector At = Graph.GetNodes()[Node].Location;
            const double Across = FMath::Abs(FVector::DotProduct(At - Car->GetActorLocation(), Right));
            if (Witnesses < 4 && !Graph.GetNodes()[Node].bRoad && Across > 400.0 && SpawnPointOk(At, false, true))
            {
                Witnesses += SpawnAt(Node, nullptr, TEXT("probe_witness"), FVector::Dist2D(At, Car->GetActorLocation())) ? 1 : 0;
            }
        }
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=probe vehicle=%s speed_ms=%.1f witnesses=%d"), StreetLogPrefix,
            *Car->VehicleId.ToString(), Car->GetForwardSpeed(), Witnesses);
        return;
    }
}

void UMarburgumStreetSubsystem::SpawnPeople(double Time, float DeltaSeconds)
{
    if (Viewers.IsEmpty())
    {
        SpawnBudget = 0.0;
        return;
    }
    SpawnBudget = FMath::Min(SpawnBudget + SpawnsPerSecond * DeltaSeconds, 6.0);
    const int32 Target = FMath::Min(PeoplePerPlayer * Viewers.Num(), MaxPeople);
    while (CountPeople() < Target && SpawnBudget >= 1.0)
    {
        SpawnBudget -= 1.0;
        const FMarburgumStreetViewer& Viewer = Viewers[ViewerCursor++ % Viewers.Num()];
        const bool bInView = Random.FRand() < InViewSpawnShare;
        if (!TrySpawnFor(Viewer, bInView))
        {
            TrySpawnFor(Viewer, !bInView);
        }
    }
}

void UMarburgumStreetSubsystem::RunPeopleOver(float DeltaSeconds)
{
    TArray<AMarburgumVehicle*> Cars;
    AMarburgumVehicle::GetAll(GetWorld(), Cars);
    for (AMarburgumVehicle* Car : Cars)
    {
        UBoxComponent* Body = Car->GetBody();
        if (!Body || !Body->IsSimulatingPhysics())
        {
            continue;
        }
        const FVector Centre = Body->GetComponentLocation();
        const FVector* Last = LastCarCentres.Find(Car);
        const FVector Previous = Last ? *Last : Centre;
        LastCarCentres.Add(Car, Centre);
        const FVector Velocity = Body->GetPhysicsLinearVelocity();
        const float Speed = Velocity.Size2D();
        if (Speed < RunOverMinSpeedCm)
        {
            continue;
        }
        const FVector2D Forward = FVector2D(Body->GetForwardVector()).GetSafeNormal();
        const FVector2D C(Centre);
        const double Travelled = FVector2D::DotProduct(C - FVector2D(Previous), Forward);
        // A little ahead of the body too: the check runs before this frame's physics moves it into them.
        const double Ahead = FVector2D::DotProduct(FVector2D(Velocity), Forward) * DeltaSeconds * 1.5;
        const FVector Extent = Body->GetScaledBoxExtent();
        const bool bPlayerDriving = !Car->GetDriverSeatId().IsNone() && Car->GetOccupant(Car->GetDriverSeatId()) != nullptr;
        for (TActorIterator<AMarburgumFighter> It(GetWorld()); It; ++It)
        {
            AMarburgumFighter* Person = *It;
            if (Person->IsIncapacitatedAuthority())
            {
                continue;
            }
            const FVector At = Person->GetActorLocation();
            if (FMath::Abs(At.Z - Centre.Z) > RunOverHeightCm)
            {
                continue;
            }
            const FVector2D P(At);
            if (!InSweptBody(P, C, Forward, Extent.X, Extent.Y, Travelled) && !InSweptBody(P, C, Forward, Extent.X, Extent.Y, -Ahead))
            {
                continue;
            }
            const float Damage = RunOverDamage(Speed);
            Person->KnockDownAuthority(Car, Damage, RunOverDownSeconds);
            const int32 Id = AddIncident(EEvent::RunOver, At, bPlayerDriving, Now() + RunOverIncidentSeconds, Person);
            UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=run_over npc=%s vehicle=%s speed_ms=%.1f damage=%.1f out=%d player_driving=%d incident=%d at=%.0f,%.0f,%.0f"),
                StreetLogPrefix, *Person->GetNpcId().ToString(), *Car->VehicleId.ToString(), Speed / 100.f, Damage,
                Person->GetVitalityPoints() == 0 ? 1 : 0, bPlayerDriving ? 1 : 0, Id, At.X, At.Y, At.Z);
        }
    }
}
