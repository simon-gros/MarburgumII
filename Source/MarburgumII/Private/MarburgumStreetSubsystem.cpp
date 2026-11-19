#include "MarburgumStreetSubsystem.h"

#include "Algo/Reverse.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformMisc.h"
#include "MarburgumCharacter.h"
#include "MarburgumCharacterMovement.h"
#include "MarburgumFighter.h"
#include "MarburgumGameState.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPedestrian.h"
#include "MarburgumVehicle.h"
#include "MarburgumWater.h"
#include "MarburgumWorldClock.h"
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
    // The probe's finder (smoke only): sent once the run-over has faded this long, to pavement this far from the body.
    constexpr double ProbeFinderAfterSeconds = 1.0;
    constexpr double ProbeFinderNearCm = 800.0;
    constexpr double ProbeFinderFarCm = 1500.0;
    constexpr double ProbeFinderHeightCm = 200.0;
    constexpr float HalfHeightCm = 88.f;
    constexpr double SummarySeconds = 10.0;
    // The water probe (ADR-046): the node nearest the first player (within this) with the river this far off it
    // is scared from this far the other side, and watched for this long, out of the cull's reach.
    constexpr double WaterProbeSearchCm = 30000.0;
    constexpr double WaterProbeReachCm = 400.0;
    constexpr double WaterProbeThreatCm = 300.0;
    constexpr double WaterProbeSeconds = 6.0;
    // A player's street is the zone of the walk-graph node nearest them within this (ADR-068); with none, the countryside.
    constexpr double ViewerZoneSearchCm = 15000.0;
    // A new person is put on a street with a chance of its activity, never under this: busy streets fill first, as the
    // original's TrySpawnPedestrian did.
    constexpr float MinSpawnActivity = 0.15f;
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
    bBridgeCross = FParse::Param(FCommandLine::Get(), TEXT("MB2BridgeCross"));
    int32 Places = 1;
    FParse::Value(FCommandLine::Get(), TEXT("MB2StreetPlaces="), Places);
    bPlaces = Places != 0;
    Random.Initialize(Seed);
    bEnabled = true;
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=street_ready graph=\"%s\" %s seed=%d places=%d load_ms=%.0f"), StreetLogPrefix,
        *FPaths::GetCleanFilename(Path), *Report, Seed, bPlaces ? 1 : 0, (FPlatformTime::Seconds() - Started) * 1000.0);
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

void UMarburgumStreetSubsystem::StepBridgeCross(double Time)
{
    if (!bBridgeCross || bBridgeCrossDone)
    {
        return;
    }
    auto Finish = [&](const TCHAR* Result, const AMarburgumPedestrian* Person)
    {
        bBridgeCrossDone = true;
        const FVector At = Person ? Person->GetActorLocation() : FVector::ZeroVector;
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=bridge_cross result=%s seconds=%.1f at=%.0f,%.0f,%.0f zone_lookups=%d zone_hits=%d zone_lookup_ms=%.2f"),
            StreetLogPrefix, Result, Time - BridgeCrossStart, At.X, At.Y, At.Z, ZoneLookups, ZoneCacheHits, ZoneLookupSeconds * 1000.0);
    };
    if (!BridgeCross.IsValid())
    {
        const int32 Largest = Graph.LargestComponent();
        int32 BestEdge = INDEX_NONE;
        double BestSpan = 0.0;
        for (int32 Edge = 0; Edge < Graph.GetEdges().Num(); ++Edge)
        {
            const FMarburgumWalkGraph::FEdge& Span = Graph.GetEdges()[Edge];
            const FMarburgumWalkGraph::FNode& A = Graph.GetNodes()[Span.A];
            const FMarburgumWalkGraph::FNode& B = Graph.GetNodes()[Span.B];
            if (A.Component != Largest || B.Component != Largest)
            {
                continue;
            }
            if (Span.Kind != 4 && !(A.bBridge && B.bBridge))
            {
                continue;
            }
            const double Delta = FMath::Abs(A.Location.X - B.Location.X);
            if (Delta >= 800.0 && Delta > BestSpan)
            {
                BestSpan = Delta;
                BestEdge = Edge;
            }
        }
        if (BestEdge == INDEX_NONE)
        {
            BridgeCrossStart = Time;
            Finish(TEXT("no_bridge"), nullptr);
            return;
        }
        const FMarburgumWalkGraph::FEdge& Span = Graph.GetEdges()[BestEdge];
        const int32 Near = Graph.GetNodes()[Span.A].Location.X <= Graph.GetNodes()[Span.B].Location.X ? Span.A : Span.B;
        const int32 Far = Graph.Other(BestEdge, Near);
        const double Direction = FMath::Sign(Graph.GetNodes()[Far].Location.X - Graph.GetNodes()[Near].Location.X);
        TArray<int32> Previous;
        Previous.Init(INDEX_NONE, Graph.GetNodes().Num());
        TArray<int32> Queue;
        Queue.Add(Near);
        Previous[Near] = Near;
        int32 Goal = INDEX_NONE;
        for (int32 Head = 0; Head < Queue.Num() && Head < 4000 && Goal == INDEX_NONE; ++Head)
        {
            const int32 Node = Queue[Head];
            const FMarburgumWalkGraph::FNode& Here = Graph.GetNodes()[Node];
            if (Node != Near && !Here.bBridge && (Here.Location.X - Graph.GetNodes()[Far].Location.X) * Direction >= 1500.0)
            {
                Goal = Node;
                break;
            }
            for (const int32 Edge : Graph.Touching(Node))
            {
                const int32 Next = Graph.Other(Edge, Node);
                if (Previous[Next] == INDEX_NONE && Graph.GetNodes()[Next].Component == Largest)
                {
                    Previous[Next] = Node;
                    Queue.Add(Next);
                }
            }
        }
        if (Goal == INDEX_NONE)
        {
            BridgeCrossStart = Time;
            Finish(TEXT("no_route"), nullptr);
            return;
        }
        TArray<int32> Route;
        for (int32 Node = Goal; Node != Near; Node = Previous[Node])
        {
            Route.Add(Node);
        }
        Route.Add(Near);
        Algo::Reverse(Route);
        AMarburgumPedestrian* Person = SpawnAt(Near, nullptr, TEXT("bridge_cross"), 0.0);
        if (!Person)
        {
            BridgeCrossStart = Time;
            Finish(TEXT("no_spawn"), nullptr);
            return;
        }
        Person->NpcId = TEXT("bridge_cross");
        Person->SetScriptedRoute(Route);
        BridgeCross = Person;
        BridgeCrossStart = Time;
        BridgeCrossLog = Time;
        const FVector From = Graph.GetNodes()[Route[0]].Location;
        const FVector To = Graph.GetNodes()[Route.Last()].Location;
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=bridge_cross result=started nodes=%d from=%.0f,%.0f,%.0f to=%.0f,%.0f,%.0f"),
            StreetLogPrefix, Route.Num(), From.X, From.Y, From.Z, To.X, To.Y, To.Z);
        return;
    }
    AMarburgumPedestrian* Person = BridgeCross.Get();
    const UMarburgumWaterSubsystem* Water = GetWorld()->GetSubsystem<UMarburgumWaterSubsystem>();
    if (Water && Water->IsWet(Person->GetActorLocation()))
    {
        Finish(TEXT("wet"), Person);
        return;
    }
    if (Person->IsLost())
    {
        Finish(TEXT("lost"), Person);
        return;
    }
    if (Person->IsScriptedDone())
    {
        Finish(TEXT("arrived"), Person);
        return;
    }
    if (Time - BridgeCrossStart > 240.0)
    {
        Finish(TEXT("timeout"), Person);
        return;
    }
    if (Time - BridgeCrossLog >= 2.0)
    {
        BridgeCrossLog = Time;
        const FVector At = Person->GetActorLocation();
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=bridge_cross result=walking seconds=%.1f at=%.0f,%.0f,%.0f edge=%d node=%d"),
            StreetLogPrefix, Time - BridgeCrossStart, At.X, At.Y, At.Z, Person->GetWalkEdge(), Person->GetWalkNode());
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
    UpdateHour(Time);
    MarkSeenAndCull(Time);
    SendSomeoneHome(Time);
    SpawnPeople(Time, MarburgumStreetPopulation::StepSeconds);
    StepProbe();
    StepWaterProbe(Time);
    StepBridgeCross(Time);

    if (Time - LastSummarySeconds >= MarburgumStreetPopulation::SummarySeconds)
    {
        LastSummarySeconds = Time;
        int32 ByMood[8] = {};
        // Nobody walks into the river (ADR-046); anyone in it got there some other way, and is counted.
        int32 Swimming = 0;
        int32 AtPlaces[AttractorCount] = {};
        for (const TWeakObjectPtr<AMarburgumPedestrian>& Person : People)
        {
            ++ByMood[FMath::Clamp(static_cast<int32>(Person->GetMood()), 0, 7)];
            Swimming += Person->GetCharacterMovement()->IsSwimming() ? 1 : 0;
            if (Graph.GetAttractors().IsValidIndex(Person->GetAttractor()))
            {
                ++AtPlaces[Graph.GetAttractors()[Person->GetAttractor()].Kind];
            }
        }
        FString PlaceCounts;
        for (int32 Kind = 0; Kind < AttractorCount; ++Kind)
        {
            PlaceCounts += FString::Printf(TEXT("%s%s=%d"), Kind ? TEXT(",") : TEXT(""), AttractorNames[Kind], AtPlaces[Kind]);
        }
        int32 ByGroup[GroupCount] = {};
        for (const TWeakObjectPtr<AMarburgumPedestrian>& Person : People)
        {
            ++ByGroup[FMath::Clamp(Person->GetGroupIndex(), 0, GroupCount - 1)];
        }
        FString GroupCounts;
        for (int32 Group = 0; Group < GroupCount; ++Group)
        {
            GroupCounts += FString::Printf(TEXT("%s%s=%d"), Group ? TEXT(",") : TEXT(""), Groups[Group].Id, ByGroup[Group]);
        }
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=street_summary people=%d target=%d period=%s players=%d incidents=%d walk=%d flee=%d watch=%d gather=%d dodge=%d fight=%d down=%d visit=%d swimming=%d groups=%s at_places=%s went_in=%d came_out=%d found_waiting=%d"),
            StreetLogPrefix, People.Num(), TargetPeople, PeriodNames[static_cast<int32>(Hour.Period)], Viewers.Num(), Incidents.Num(), ByMood[0], ByMood[1],
            ByMood[2], ByMood[3], ByMood[4], ByMood[5], ByMood[6], ByMood[7], Swimming, *GroupCounts, *PlaceCounts, WentIn, CameOut, FoundWaiting);
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
        if (Person == WaterProbe.Get() && Time < WaterProbeUntil)
        {
            continue;
        }
        if (Person == BridgeCross.Get())
        {
            continue;
        }
        const TCHAR* Reason = nullptr;
        if (Viewers.IsEmpty())
        {
            Reason = TEXT("nobody_about");
        }
        else if (Person->HasGoneIn())
        {
            // Through a door (ADR-069): off the street.
            Reason = TEXT("went_in");
            ++WentIn;
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

void UMarburgumStreetSubsystem::UpdateHour(double Time)
{
    // Without a clock (a map run with none) the street keeps ADR-044's: day, under a clear sky.
    FDateTime Utc;
    bClockKnown = AMarburgumGameState::WorldUtc(GetWorld(), Utc);
    MarburgumII::Clock::FCivil Local;
    if (bClockKnown)
    {
        Local = MarburgumII::Clock::MarburgumAt(Utc);
        ClockWeekday = Local.Weekday;
        ClockHour = Local.HourOfDay();
    }
    else
    {
        ClockWeekday = 1;
        ClockHour = 12.0;
    }
    const FMarburgumWeather* Sky = AMarburgumGameState::WeatherOf(GetWorld());
    const float Falling = Sky && Sky->bKnown ? Sky->Precipitation : 0.f;
    Weather = WeatherShare(Falling);

    // Each player brings the street they stand on (ADR-068): a shopping street at noon fills to twenty, a lane of
    // houses at three in the morning keeps one. The numbers stay with that player (CrowdBudgets). The first player's
    // hour is the one the logs and GetHour() report. The district is not read again inside ZoneHoldCm.
    ZoneHolds.RemoveAll([this](const FZoneHold& Hold)
    {
        if (!Hold.Character.IsValid())
        {
            return true;
        }
        for (const FMarburgumStreetViewer& Viewer : Viewers)
        {
            if (Viewer.Character.Get() == Hold.Character.Get())
            {
                return false;
            }
        }
        return true;
    });
    FString ZoneList;
    TArray<float> Shares;
    Shares.SetNum(Viewers.Num());
    for (int32 Index = 0; Index < Viewers.Num(); ++Index)
    {
        const int32 Node = ZoneNodeFor(Viewers[Index]);
        const EZone Zone = Node == INDEX_NONE ? EZone::Rural : static_cast<EZone>(Graph.GetNodes()[Node].Zone);
        const FStreetHour ViewerHour = StreetHourAt(ClockWeekday, ClockHour, Zone);
        Shares[Index] = ViewerHour.Density * Weather;
        ZoneList += FString::Printf(TEXT("%s%s"), Index ? TEXT(",") : TEXT(""), ZoneNames[static_cast<int32>(ViewerHour.Zone)]);
        if (Index == 0)
        {
            Hour = ViewerHour;
        }
    }
    if (Viewers.IsEmpty())
    {
        Hour = StreetHourAt(ClockWeekday, ClockHour);
        ViewerWants.Reset();
        ViewerHaves.Reset();
        TargetPeople = 0;
    }
    else
    {
        ViewerWants.SetNum(Viewers.Num());
        TArray<FVector2D> BudgetAt;
        BudgetAt.Reserve(Viewers.Num());
        for (const FMarburgumStreetViewer& Viewer : Viewers)
        {
            BudgetAt.Add(FVector2D(Viewer.Location));
        }
        CrowdBudgetsShared(BudgetAt.GetData(), Shares.GetData(), Viewers.Num(), ViewerWants.GetData());
        TargetPeople = 0;
        for (const int32 Want : ViewerWants)
        {
            TargetPeople += Want;
        }
        ViewerHaves.Init(0, Viewers.Num());
        TArray<FVector2D> Spots;
        Spots.Reserve(Viewers.Num());
        for (const FMarburgumStreetViewer& Viewer : Viewers)
        {
            Spots.Add(FVector2D(Viewer.Location));
        }
        for (const TWeakObjectPtr<AMarburgumPedestrian>& Person : People)
        {
            if (!Person.IsValid())
            {
                continue;
            }
            const int32 Owner = NearestViewerIndex(FVector2D(Person->GetActorLocation()), Spots.GetData(), Spots.Num());
            if (ViewerHaves.IsValidIndex(Owner))
            {
                ++ViewerHaves[Owner];
            }
        }
    }

    // Logged when the period, the day, a player's street or the number changes, not every step.
    const FString Key = FString::Printf(TEXT("%d/%d/%d/%d/%s"), static_cast<int32>(Hour.Period), Hour.Weekday, TargetPeople, bClockKnown ? 1 : 0, *ZoneList);
    if (Key == LoggedHour)
    {
        return;
    }
    LoggedHour = Key;
    FString Mix;
    for (int32 Group = 0; Group < GroupCount; ++Group)
    {
        Mix += FString::Printf(TEXT("%s%s=%d"), Group ? TEXT(",") : TEXT(""), Groups[Group].Id, Hour.Mix[Group]);
    }
    FString Open;
    for (int32 Kind = 0; Kind < AttractorCount; ++Kind)
    {
        Open += FString::Printf(TEXT("%s%s=%.2f"), Kind ? TEXT(",") : TEXT(""), AttractorNames[Kind], AttractorOpenNow(static_cast<EAttractor>(Kind)));
    }
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=street_hour clock=%d marburgum=\"%s %s\" weekday=%d period=%s period_day=%d zones=%s zone=%s density=%.2f precipitation=%.2f weather=%.2f players=%d target=%d mix=%s open=%s"),
        StreetLogPrefix, bClockKnown ? 1 : 0, bClockKnown ? *MarburgumII::Clock::DateLabel(Local) : TEXT("-"), bClockKnown ? *MarburgumII::Clock::TimeLabel(Local) : TEXT("-"),
        bClockKnown ? Local.Weekday : -1, PeriodNames[static_cast<int32>(Hour.Period)], Hour.Weekday, ZoneList.IsEmpty() ? TEXT("-") : *ZoneList,
        ZoneNames[static_cast<int32>(Hour.Zone)], Hour.Density, Falling, Weather, Viewers.Num(), TargetPeople, *Mix, *Open);
    FString WantList;
    FString HaveList;
    for (int32 Index = 0; Index < ViewerWants.Num(); ++Index)
    {
        WantList += FString::Printf(TEXT("%s%d"), Index ? TEXT(",") : TEXT(""), ViewerWants[Index]);
        HaveList += FString::Printf(TEXT("%s%d"), Index ? TEXT(",") : TEXT(""), ViewerHaves.IsValidIndex(Index) ? ViewerHaves[Index] : 0);
    }
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=street_budget players=%d target=%d wants=%s haves=%s"),
        StreetLogPrefix, Viewers.Num(), TargetPeople, WantList.IsEmpty() ? TEXT("-") : *WantList, HaveList.IsEmpty() ? TEXT("-") : *HaveList);
}

int32 UMarburgumStreetSubsystem::ZoneNodeFor(const FMarburgumStreetViewer& Viewer)
{
    FZoneHold* Hold = nullptr;
    for (FZoneHold& Existing : ZoneHolds)
    {
        if (Existing.Character.Get() == Viewer.Character.Get())
        {
            Hold = &Existing;
            break;
        }
    }
    if (!Hold)
    {
        FZoneHold Added;
        Added.Character = Viewer.Character;
        ZoneHolds.Add(Added);
        Hold = &ZoneHolds.Last();
    }
    const double Moved = Hold->Node == INDEX_NONE ? TNumericLimits<double>::Max() : FVector::Dist2D(Hold->Location, Viewer.Location);
    const double MovedUp = Hold->Node == INDEX_NONE ? TNumericLimits<double>::Max() : FMath::Abs(Hold->Location.Z - Viewer.Location.Z);
    ++ZoneLookups;
    if (HeldZoneNode(Hold->Node, Moved, INDEX_NONE, ZoneHoldCm, MovedUp) == Hold->Node && Hold->Node != INDEX_NONE
        && Graph.GetNodes().IsValidIndex(Hold->Node))
    {
        ++ZoneCacheHits;
        return Hold->Node;
    }
    const double Started = FPlatformTime::Seconds();
    Hold->Node = Graph.NearestNode(Viewer.Location, MarburgumStreetPopulation::ViewerZoneSearchCm, MarburgumStreetPopulation::ViewerZoneSearchCm);
    ZoneLookupSeconds += FPlatformTime::Seconds() - Started;
    Hold->Location = Viewer.Location;
    return Hold->Node;
}

float UMarburgumStreetSubsystem::AttractorOpenNow(EAttractor Kind) const
{
    return AttractorOpen(Kind, StreetHourAt(ClockWeekday, ClockHour), ClockWeekday, ClockHour);
}

int32 UMarburgumStreetSubsystem::FreeSlot(int32 Attractor) const
{
    if (!Graph.GetAttractors().IsValidIndex(Attractor))
    {
        return INDEX_NONE;
    }
    uint32 Taken = 0;
    for (const TWeakObjectPtr<AMarburgumPedestrian>& Person : People)
    {
        if (Person.IsValid() && Person->GetAttractor() == Attractor)
        {
            Taken |= 1u << FMath::Clamp(Person->GetAttractorSlot(), 0, 31);
        }
    }
    for (int32 Slot = 0; Slot < Graph.GetAttractors()[Attractor].Spots.Num(); ++Slot)
    {
        if ((Taken & (1u << Slot)) == 0)
        {
            return Slot;
        }
    }
    return INDEX_NONE;
}

EZone UMarburgumStreetSubsystem::ZoneAt(const FVector& At) const
{
    const int32 Node = Graph.NearestNode(At, MarburgumStreetPopulation::ViewerZoneSearchCm, MarburgumStreetPopulation::ViewerZoneSearchCm);
    return Node == INDEX_NONE ? EZone::Rural : static_cast<EZone>(Graph.GetNodes()[Node].Zone);
}

void UMarburgumStreetSubsystem::SendSomeoneHome(double Time)
{
    if (Viewers.IsEmpty() || Time - LastHomeSeconds < HomeEverySeconds)
    {
        return;
    }
    bool bDistrictOver = false;
    for (int32 Index = 0; Index < ViewerWants.Num() && Index < ViewerHaves.Num(); ++Index)
    {
        bDistrictOver |= ViewerHaves[Index] > ViewerWants[Index];
    }
    const bool bGlobalOver = CountPeople() > TargetPeople;
    if (!bDistrictOver && !bGlobalOver)
    {
        return;
    }
    TArray<FVector2D> Spots;
    Spots.Reserve(Viewers.Num());
    for (const FMarburgumStreetViewer& Viewer : Viewers)
    {
        Spots.Add(FVector2D(Viewer.Location));
    }
    AMarburgumPedestrian* Furthest = nullptr;
    double FurthestCm = -1.0;
    int32 Owner = INDEX_NONE;
    for (int32 Pass = 0; Pass < 2 && !Furthest; ++Pass)
    {
        // The street that is over its own number gives someone back first. Only a crowd over the cap, with nobody
        // spare on an over-full street, falls back to the furthest unseen person anywhere.
        const bool bOnlyOverBudget = Pass == 0;
        if (bOnlyOverBudget && !bDistrictOver)
        {
            continue;
        }
        if (!bOnlyOverBudget && !bGlobalOver)
        {
            break;
        }
        for (const TWeakObjectPtr<AMarburgumPedestrian>& Weak : People)
        {
            AMarburgumPedestrian* Person = Weak.Get();
            // Only someone walking about whom nobody has seen lately: not a body, not anyone fleeing, watching or fighting.
            if (!Person || Person == WaterProbe.Get() || Person == BridgeCross.Get() || Person->IsDown() || Person->GetMood() != EMarburgumStreetMood::Walk
                || Time - Person->LastSeenSeconds <= UnseenSeconds)
            {
                continue;
            }
            const int32 Theirs = NearestViewerIndex(FVector2D(Person->GetActorLocation()), Spots.GetData(), Spots.Num());
            if (bOnlyOverBudget && !(ViewerHaves.IsValidIndex(Theirs) && ViewerWants.IsValidIndex(Theirs) && ViewerHaves[Theirs] > ViewerWants[Theirs]))
            {
                continue;
            }
            double Nearest = TNumericLimits<double>::Max();
            for (const FMarburgumStreetViewer& Viewer : Viewers)
            {
                Nearest = FMath::Min(Nearest, FVector::Dist2D(Viewer.Location, Person->GetActorLocation()));
            }
            // Nobody goes home closer to a player than anybody appears: someone just behind them does not vanish.
            if (Nearest >= SpawnOutOfViewMinCm && Nearest > FurthestCm)
            {
                FurthestCm = Nearest;
                Furthest = Person;
                Owner = Theirs;
            }
        }
    }
    if (!Furthest)
    {
        return;
    }
    LastHomeSeconds = Time;
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=despawn npc=%s reason=home group=%s nearest_player_m=%.1f people=%d target=%d period=%s"),
        StreetLogPrefix, *Furthest->GetNpcId().ToString(), Groups[FMath::Clamp(Furthest->GetGroupIndex(), 0, GroupCount - 1)].Id, FurthestCm / 100.0,
        CountPeople(), TargetPeople, PeriodNames[static_cast<int32>(Hour.Period)]);
    Furthest->Destroy();
    if (ViewerHaves.IsValidIndex(Owner) && ViewerHaves[Owner] > 0)
    {
        --ViewerHaves[Owner];
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

FVector UMarburgumStreetSubsystem::SpawnProbe(const FMarburgumStreetViewer& Viewer, bool bInView)
{
    using namespace MarburgumStreetPopulation;
    const double FacingYaw = FMath::RadiansToDegrees(FMath::Atan2(Viewer.Facing.Y, Viewer.Facing.X));
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
    return Viewer.Location + FRotator(0.0, Yaw, 0.0).Vector() * Distance;
}

bool UMarburgumStreetSubsystem::InSpawnBand(const FMarburgumStreetViewer& Viewer, const FVector& At, bool bInView) const
{
    using namespace MarburgumStreetPopulation;
    // Snapped to a node, the place must still lie inside the band where people are kept, or they go at once, and on the
    // side of the view it was picked for: an "in view" node the player cannot see is never marked seen.
    const double FromViewer = FVector::Dist2D(Viewer.Location, At);
    return FromViewer <= (bInView ? CullCm - 200.0 : KeepCm - OutOfViewKeepMarginCm)
        && (bInView ? InViewOf(Viewer, At, InViewInsetDegrees) : !InViewOf(Viewer, At)) && SpawnPointOk(At, bInView);
}

bool UMarburgumStreetSubsystem::TrySpawnFor(const FMarburgumStreetViewer& Viewer, bool bInView)
{
    using namespace MarburgumStreetPopulation;
    // Some come out of an open café or shop, or are already waiting at a stop, rather than walking in (ADR-069).
    if (bPlaces && Random.FRand() < DoorSpawnShare && TrySpawnAtPlace(Viewer, bInView))
    {
        return true;
    }
    for (int32 Probe = 0; Probe < ProbesPerSpawn; ++Probe)
    {
        const FVector Point = SpawnProbe(Viewer, bInView);
        const int32 Node = Graph.NearestNode(Point, ProbeNodeCm, ProbeHeightCm);
        if (Node == INDEX_NONE || Graph.GetNodes()[Node].bRoad)
        {
            continue;
        }
        bool bLargerComponent = false;
        if (Graph.ComponentSize(Node) < MinSpawnComponent)
        {
            TArray<int32> Near;
            Graph.NodesWithin(Point, ProbeNodeCm, Near);
            for (const int32 Other : Near)
            {
                if (Graph.ComponentSize(Other) >= MinSpawnComponent)
                {
                    bLargerComponent = true;
                    break;
                }
            }
        }
        if (!SpawnComponentOk(Graph.ComponentSize(Node), bLargerComponent))
        {
            continue;
        }
        const FVector At = Graph.GetNodes()[Node].Location;
        if (!InSpawnBand(Viewer, At, bInView) || Graph.Touching(Node).IsEmpty())
        {
            continue;
        }
        // Busy streets fill first: a quiet one takes its share of the newcomers in proportion to its activity.
        if (Random.FRand() > FMath::Max(MinSpawnActivity, Graph.GetNodes()[Node].Activity))
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

bool UMarburgumStreetSubsystem::TrySpawnAtPlace(const FMarburgumStreetViewer& Viewer, bool bInView)
{
    using namespace MarburgumStreetPopulation;
    TArray<int32> Near;
    Graph.AttractorsWithin(SpawnProbe(Viewer, bInView), ProbeNodeCm, Near);
    if (Near.IsEmpty())
    {
        return false;
    }
    const int32 Attractor = Near[Random.RandHelper(Near.Num())];
    const FMarburgumWalkGraph::FAttractor& Place = Graph.GetAttractors()[Attractor];
    const EAttractor Kind = static_cast<EAttractor>(Place.Kind);
    const EVisit Action = AttractorKinds[Place.Kind].Visit;
    if (Action == EVisit::Look || Graph.Touching(Place.Node).IsEmpty() || Random.FRand() >= AttractorOpenNow(Kind))
    {
        return false;
    }
    const int32 Slot = Action == EVisit::Wait ? FreeSlot(Attractor) : 0;
    if (Slot == INDEX_NONE)
    {
        return false;
    }
    const FVector At = Place.Spots[Slot];
    if (!InSpawnBand(Viewer, At, bInView))
    {
        return false;
    }
    const double Distance = FVector::Dist2D(Viewer.Location, At);
    AMarburgumPedestrian* Person = SpawnAt(Place.Node, &At, Action == EVisit::Wait ? TEXT("waiting") : TEXT("door"), Distance);
    if (!Person)
    {
        return false;
    }
    if (Action == EVisit::Wait)
    {
        Person->StartAt(Attractor, Slot, Now());
        ++FoundWaiting;
    }
    else
    {
        Person->ComeOutAt(Attractor);
        ++CameOut;
    }
    return true;
}

AMarburgumPedestrian* UMarburgumStreetSubsystem::SpawnAt(int32 Node, const FVector* Override, const TCHAR* Why, double PlayerDistanceCm)
{
    using namespace MarburgumStreetPopulation;
    const FVector NodeAt = Graph.GetNodes()[Node].Location;
    const FVector At = Override ? *Override : NodeAt;
    // Who comes is who walks this street at this hour (ADR-068).
    const FStreetHour Here = StreetHourAt(ClockWeekday, ClockHour, static_cast<EZone>(Graph.GetNodes()[Node].Zone));
    const int32 GroupIndex = PickWeighted(Here.Mix, GroupCount, Random.FRand());
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
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=spawn npc=%s group=%s profile=%s personality=%s pace=%.2f why=%s zone=%s activity=%.2f player_m=%.1f at=%.0f,%.0f,%.0f"),
        StreetLogPrefix, *Person->GetNpcId().ToString(), Group.Id, Looks[LookIndex].Profile, PersonalityNames[static_cast<int32>(Personality)], Pace,
        Why, ZoneNames[static_cast<int32>(Here.Zone)], Graph.GetNodes()[Node].Activity, PlayerDistanceCm / 100.0, At.X, At.Y, At.Z);
    return Person;
}

void UMarburgumStreetSubsystem::StepProbe()
{
    if (!bProbe)
    {
        return;
    }
    if (bProbeDone)
    {
        StepProbeFinder();
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
                if (!Probe.bDodges)
                {
                    ProbeVictim = Person;
                }
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

void UMarburgumStreetSubsystem::StepProbeFinder()
{
    using namespace MarburgumStreetPopulation;
    if (bProbeFinderDone || !ProbeVictim.IsValid())
    {
        return;
    }
    const int32* Id = BodyIncidents.Find(TWeakObjectPtr<AActor>(ProbeVictim.Get()));
    const FMarburgumStreetIncident* Body = Id ? FindIncident(*Id) : nullptr;
    if (!Body)
    {
        ProbeBodySince = -1.0;
        return;
    }
    const double Time = Now();
    if (ProbeBodySince < 0.0)
    {
        ProbeBodySince = Time;
    }
    // The run-over is louder than the body and answered first by everyone near it; wait for it to fade, so the one
    // who comes along now has only the body to answer (the witnesses are all taken up with the run-over).
    if (Time - ProbeBodySince < RunOverIncidentSeconds + ProbeFinderAfterSeconds)
    {
        return;
    }
    bProbeFinderDone = true;
    TArray<int32> Near;
    Graph.NodesWithin(Body->Location, ProbeFinderFarCm, Near);
    int32 Best = INDEX_NONE;
    double BestDistance = TNumericLimits<double>::Max();
    for (const int32 Node : Near)
    {
        const FVector At = Graph.GetNodes()[Node].Location;
        const double Distance = FVector::Dist2D(At, Body->Location);
        if (!Graph.GetNodes()[Node].bRoad && Distance >= ProbeFinderNearCm && FMath::Abs(At.Z - Body->Location.Z) <= ProbeFinderHeightCm
            && Distance < BestDistance && SpawnPointOk(At, false, true))
        {
            Best = Node;
            BestDistance = Distance;
        }
    }
    AMarburgumPedestrian* Finder = Best != INDEX_NONE ? SpawnAt(Best, nullptr, TEXT("probe_finder"), BestDistance) : nullptr;
    if (!Finder)
    {
        UE_LOG(LogMarburgumNet, Warning, TEXT("%s role=server event=probe_finder_skipped incident=%d nodes=%d"), StreetLogPrefix, Body->Id, Near.Num());
        return;
    }
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=probe_finder npc=%s incident=%d body_m=%.1f"), StreetLogPrefix,
        *Finder->GetNpcId().ToString(), Body->Id, BestDistance / 100.0);
}

void UMarburgumStreetSubsystem::StepWaterProbe(double Time)
{
    using namespace MarburgumStreetPopulation;
    if (!bProbe)
    {
        return;
    }
    if (AMarburgumPedestrian* Person = WaterProbe.Get())
    {
        bWaterProbeSwam |= Person->GetCharacterMovement()->IsSwimming();
        if (Time < WaterProbeUntil)
        {
            return;
        }
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=water_probe_done npc=%s swam=%d moved_m=%.1f mood=%s"), StreetLogPrefix,
            *Person->GetNpcId().ToString(), bWaterProbeSwam ? 1 : 0, FVector::Dist2D(Person->GetActorLocation(), WaterProbeStart) / 100.0,
            AMarburgumPedestrian::MoodIntent(Person->GetMood()));
        WaterProbe.Reset();
        return;
    }
    if (bWaterProbeDone || Viewers.IsEmpty())
    {
        return;
    }
    bWaterProbeDone = true;
    const UMarburgumWaterSubsystem* Water = GetWorld()->GetSubsystem<UMarburgumWaterSubsystem>();
    if (!Water)
    {
        UE_LOG(LogMarburgumNet, Warning, TEXT("%s role=server event=water_probe_skipped reason=no_water"), StreetLogPrefix);
        return;
    }
    // The pavement node nearest the player with the river a few metres off it.
    const FVector Player = Viewers[0].Location;
    TArray<int32> Near;
    Graph.NodesWithin(Player, WaterProbeSearchCm, Near);
    Near.Sort([&](int32 A, int32 B)
    {
        return FVector::DistSquared2D(Graph.GetNodes()[A].Location, Player) < FVector::DistSquared2D(Graph.GetNodes()[B].Location, Player);
    });
    for (const int32 Node : Near)
    {
        const FVector At = Graph.GetNodes()[Node].Location;
        for (int32 Way = 0; Way < 16; ++Way)
        {
            const FVector Toward = FRotator(0.0, Way * 22.5, 0.0).Vector();
            if (!Water->IsWet(At + Toward * WaterProbeReachCm) || !SpawnPointOk(At, false, true))
            {
                continue;
            }
            AMarburgumPedestrian* Person = SpawnAt(Node, nullptr, TEXT("probe_water"), FVector::Dist2D(At, Player));
            if (!Person)
            {
                continue;
            }
            const FVector Threat = At - Toward * WaterProbeThreatCm;
            Person->MakeWaterProbe(Threat, WaterProbeSeconds);
            WaterProbe = Person;
            WaterProbeUntil = Time + WaterProbeSeconds;
            WaterProbeStart = Person->GetActorLocation();
            bWaterProbeSwam = false;
            UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=water_probe npc=%s at=%.0f,%.0f,%.0f straight_away_wet=1 player_m=%.1f"), StreetLogPrefix,
                *Person->GetNpcId().ToString(), At.X, At.Y, At.Z, FVector::Dist2D(At, Player) / 100.0);
            return;
        }
    }
    UE_LOG(LogMarburgumNet, Warning, TEXT("%s role=server event=water_probe_skipped reason=no_node_by_the_river"), StreetLogPrefix);
}

void UMarburgumStreetSubsystem::SpawnPeople(double Time, float DeltaSeconds)
{
    if (Viewers.IsEmpty())
    {
        SpawnBudget = 0.0;
        return;
    }
    SpawnBudget = FMath::Min(SpawnBudget + SpawnsPerSecond * DeltaSeconds, 6.0);
    while (CountPeople() < TargetPeople && SpawnBudget >= 1.0 && ViewerWants.Num() == Viewers.Num() && ViewerHaves.Num() == Viewers.Num())
    {
        const int32 ViewerIndex = ViewerShortfall(ViewerWants.GetData(), ViewerHaves.GetData(), Viewers.Num(), ViewerCursor);
        if (ViewerIndex == INDEX_NONE)
        {
            break;
        }
        SpawnBudget -= 1.0;
        ViewerCursor = ViewerIndex + 1;
        const FMarburgumStreetViewer& Viewer = Viewers[ViewerIndex];
        const bool bInView = Random.FRand() < InViewSpawnShare;
        const bool bSpawned = TrySpawnFor(Viewer, bInView) || TrySpawnFor(Viewer, !bInView);
        if (bSpawned)
        {
            ++ViewerHaves[ViewerIndex];
        }
    }
}

void UMarburgumStreetSubsystem::RunPeopleOver(float DeltaSeconds)
{
    for (auto It = LastCarCentres.CreateIterator(); It; ++It)
    {
        if (!It.Key().IsValid())
        {
            It.RemoveCurrent();
        }
    }
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
