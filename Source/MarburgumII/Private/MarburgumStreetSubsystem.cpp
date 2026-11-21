#include "MarburgumStreetSubsystem.h"

#include "Algo/Reverse.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
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
#include "MarburgumWorldLoading.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Templates/Function.h"

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
    // The graph's nodes are grounded by tracing, so on the World Partition map every cell must be in first.
    MarburgumII::WorldLoading::LoadWholeWorldOnServer(InWorld, TEXT("street"));
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
#if !UE_BUILD_SHIPPING
    bCrowdMatrix = FParse::Param(FCommandLine::Get(), TEXT("MB2CrowdMatrix"));
    bCrowdNight = bCrowdMatrix && FParse::Param(FCommandLine::Get(), TEXT("MB2CrowdNight"));
    float Settle = 90.f;
    if (FParse::Value(FCommandLine::Get(), TEXT("MB2CrowdSettle="), Settle) && Settle >= 20.f && Settle <= 180.f)
    {
        CrowdSettleSeconds = Settle;
    }
#endif
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

void UMarburgumStreetSubsystem::StepCrowdMatrix(double Time)
{
#if UE_BUILD_SHIPPING
    static_cast<void>(Time);
    return;
#else
    if (!bCrowdMatrix || CrowdPhase == 11)
    {
        return;
    }
    CrowdMaxPeople = FMath::Max(CrowdMaxPeople, CountPeople());
    if (CrowdLastTarget >= 0 && TargetPeople != CrowdLastTarget && CrowdPhase > 0 && CrowdPhase < 6)
    {
        ++CrowdBudgetChanges;
    }
    CrowdLastTarget = TargetPeople;
    TArray<FMarburgumStreetViewer> Ordered = Viewers;
    Ordered.Sort([](const FMarburgumStreetViewer& A, const FMarburgumStreetViewer& B)
    {
        const int32 IdA = A.Character.IsValid() && A.Character->GetPlayerState() ? A.Character->GetPlayerState()->GetPlayerId() : MAX_int32;
        const int32 IdB = B.Character.IsValid() && B.Character->GetPlayerState() ? B.Character->GetPlayerState()->GetPlayerId() : MAX_int32;
        return IdA < IdB;
    });
    auto NodeOf = [&](uint8 Zone, const TArray<FVector>& Avoid, double MinCm) -> int32
    {
        int32 Best = INDEX_NONE;
        float Activity = -1.f;
        for (int32 Index = 0; Index < Graph.GetNodes().Num(); ++Index)
        {
            const FMarburgumWalkGraph::FNode& Node = Graph.GetNodes()[Index];
            if (Node.bRoad || Node.bBridge || Node.Component != Graph.LargestComponent() || Node.Zone != Zone)
            {
                continue;
            }
            bool bClose = false;
            for (const FVector& At : Avoid)
            {
                bClose |= FVector::Dist2D(Node.Location, At) < MinCm;
            }
            if (!bClose && Node.Activity > Activity)
            {
                Activity = Node.Activity;
                Best = Index;
            }
        }
        return Best;
    };
    auto Bank = [&](bool bNorth) -> int32
    {
        int32 Best = INDEX_NONE;
        double BestX = bNorth ? -TNumericLimits<double>::Max() : TNumericLimits<double>::Max();
        for (int32 Index = 0; Index < Graph.GetNodes().Num(); ++Index)
        {
            const FMarburgumWalkGraph::FNode& Node = Graph.GetNodes()[Index];
            // The banks' far ends within the nine windows the scenario was qualified on (±768 m): the five by five's
            // (ADR-090) lie 1.28 km out, both in residential streets, and D wants two kinds of place.
            if (Node.bRoad || Node.bBridge || Node.Component != Graph.LargestComponent() || FMath::Abs(Node.Location.Y) > 4000.0
                || FMath::Abs(Node.Location.X) > 76800.0)
            {
                continue;
            }
            const bool bSide = bNorth ? Node.Location.X > 12000.0 : Node.Location.X < -12000.0;
            if (!bSide)
            {
                continue;
            }
            if ((bNorth && Node.Location.X > BestX) || (!bNorth && Node.Location.X < BestX))
            {
                BestX = Node.Location.X;
                Best = Index;
            }
        }
        return Best;
    };
    auto Place = [&](const FMarburgumStreetViewer& Viewer, int32 Node) -> bool
    {
        AMarburgumCharacter* Who = Viewer.Character.Get();
        if (!Who || Node == INDEX_NONE)
        {
            return false;
        }
        return Who->TeleportTo(Graph.GetNodes()[Node].Location + FVector(0.0, 0.0, 120.0), FRotator::ZeroRotator, false, true);
    };
    // At the end of the loaded window a player looks back into the town. Their view is where most newcomers are put,
    // and a client's own spawn heading can point past the edge of the world, where there is no pavement at all.
    auto FaceTown = [&](const FMarburgumStreetViewer& Viewer) -> bool
    {
        AMarburgumCharacter* Who = Viewer.Character.Get();
        AController* Controller = Who ? Who->GetController() : nullptr;
        if (!Controller)
        {
            return false;
        }
        const FRotator Toward(0.0, (-Who->GetActorLocation()).Rotation().Yaw, 0.0);
        Controller->SetControlRotation(Toward);
        if (APlayerController* Player = Cast<APlayerController>(Controller))
        {
            Player->ClientSetRotation(Toward, true);
        }
        return true;
    };
    auto Begin = [&](uint8 Phase)
    {
        CrowdPhase = Phase;
        CrowdPhaseSince = Time;
        CrowdSampleAt = Time;
        bCrowdPlaced = false;
        CrowdBudgetChanges = 0;
        CrowdStable = 0;
        CrowdHitStable = 0;
        CrowdLastPeople = -1;
        CrowdLastHits = 0;
        CrowdSpawnTries = 0;
        CrowdSpawnHits = 0;
        CrowdSpawnMisses = 0;
        CrowdRejectNode = 0;
        CrowdRejectComponent = 0;
        CrowdRejectBand = 0;
        CrowdRejectActivity = 0;
        CrowdRejectCollision = 0;
        CrowdDespawnHome = 0;
        CrowdDespawnCull = 0;
        CrowdStepMs = 0.0;
        CrowdStepN = 0;
    };
    const auto Snapshot = [&](const TCHAR* Event, const TCHAR* Name, const TCHAR* State, bool bPass)
    {
        FString Zones, Wants, Haves, Near, At;
        for (int32 Index = 0; Index < Ordered.Num(); ++Index)
        {
            const int32 Slot = Viewers.IndexOfByPredicate([&](const FMarburgumStreetViewer& Viewer)
            {
                return Viewer.Character.Get() == Ordered[Index].Character.Get();
            });
            const FVector Here = Ordered[Index].Location;
            int32 Count = 0;
            for (const TWeakObjectPtr<AMarburgumPedestrian>& Person : People)
            {
                Count += Person.IsValid() && FVector::Dist2D(Person->GetActorLocation(), Here) < 8000.0 ? 1 : 0;
            }
            const TCHAR* Zone = ZoneNames[FMath::Clamp<int32>(static_cast<int32>(ZoneAt(Here)), 0, ZoneCount - 1)];
            Zones += FString::Printf(TEXT("%s%s"), Index ? TEXT(",") : TEXT(""), Zone);
            Wants += FString::Printf(TEXT("%s%d"), Index ? TEXT(",") : TEXT(""), ViewerWants.IsValidIndex(Slot) ? ViewerWants[Slot] : -1);
            Haves += FString::Printf(TEXT("%s%d"), Index ? TEXT(",") : TEXT(""), ViewerHaves.IsValidIndex(Slot) ? ViewerHaves[Slot] : -1);
            Near += FString::Printf(TEXT("%s%d"), Index ? TEXT(",") : TEXT(""), Count);
            At += FString::Printf(TEXT("%s%.0f,%.0f"), Index ? TEXT(";") : TEXT(""), Here.X, Here.Y);
        }
        const double StepMs = CrowdStepN > 0 ? CrowdStepMs / static_cast<double>(CrowdStepN) : 0.0;
        UE_LOG(LogMarburgumNet, Log, TEXT("MB2_CROWD role=server event=%s name=%s state=%s pass=%d players=%d zones=%s wants=%s haves=%s near=%s at=%s people=%d target=%d changes=%d max_people=%d seconds=%.0f spawn_tries=%d spawn_hits=%d spawn_misses=%d reject_node=%d reject_component=%d reject_band=%d reject_activity=%d reject_collision=%d despawn_home=%d despawn_cull=%d step_ms=%.2f step_n=%d zone_lookups=%d zone_hits=%d zone_lookup_ms=%.2f"),
            Event, Name, State, bPass ? 1 : 0, Ordered.Num(), *Zones, *Wants, *Haves, *Near, *At, CountPeople(), TargetPeople, CrowdBudgetChanges, CrowdMaxPeople,
            Time - CrowdPhaseSince, CrowdSpawnTries, CrowdSpawnHits, CrowdSpawnMisses, CrowdRejectNode, CrowdRejectComponent, CrowdRejectBand, CrowdRejectActivity,
            CrowdRejectCollision, CrowdDespawnHome, CrowdDespawnCull, StepMs, CrowdStepN, ZoneLookups, ZoneCacheHits, ZoneLookupSeconds * 1000.0);
    };
    auto FinishMatrix = [&]()
    {
        CrowdPhase = 11;
        UE_LOG(LogMarburgumNet, Log, TEXT("MB2_CROWD role=server event=matrix_done max_people=%d"), CrowdMaxPeople);
    };
    if (CrowdPhase != 0 && CrowdPhase != 11 && (CountPeople() > MaxPeople || CrowdMaxPeople > MaxPeople))
    {
        Snapshot(TEXT("scenario"), TEXT("over_cap"), TEXT("failed"), false);
        FinishMatrix();
        return;
    }
    auto Observe = [&](const TCHAR* Name, bool bRules, double Limit, TFunctionRef<void()> Next)
    {
        if (Time - CrowdSampleAt < 5.0)
        {
            return;
        }
        CrowdSampleAt = Time;
        const int32 PeopleNow = CountPeople();
        const bool bCountFlat = PeopleNow == CrowdLastPeople;
        const bool bHitsFlat = CrowdSpawnHits == CrowdLastHits;
        CrowdStable = bCountFlat ? CrowdStable + 1 : 0;
        CrowdHitStable = bHitsFlat ? CrowdHitStable + 1 : 0;
        CrowdLastPeople = PeopleNow;
        CrowdLastHits = CrowdSpawnHits;
        const double Elapsed = Time - CrowdPhaseSince;
        // Replacing someone who walked out of the keep band is still a settled count. A stall is no successful spawn.
        const bool bAtTarget = bRules && TargetPeople > 0 && PeopleNow == TargetPeople && CrowdStable >= 2 && PeopleNow <= MaxPeople;
        const bool bConstrained = bRules && !bAtTarget && PeopleNow < TargetPeople && CrowdHitStable >= 3 && Elapsed >= 20.0;
        if (!bAtTarget && Elapsed < Limit)
        {
            if (!bConstrained)
            {
                Snapshot(TEXT("settle"), Name, TEXT("settling"), false);
                return;
            }
        }
        const TCHAR* State = !bRules ? TEXT("failed") : (bAtTarget ? TEXT("settled") : (bConstrained ? TEXT("constrained") : TEXT("timeout")));
        Snapshot(TEXT("scenario"), Name, State, bAtTarget);
        Next();
    };
    if (CrowdPhase == 0)
    {
        if (bCrowdNight)
        {
            if (Ordered.Num() >= 2)
            {
                Begin(20);
            }
            return;
        }
        if (Ordered.IsEmpty())
        {
            return;
        }
        if (!bCrowdPlaced)
        {
            const int32 Commercial = NodeOf(static_cast<uint8>(EZone::Commercial), {}, 0.0);
            if (!Place(Ordered[0], Commercial))
            {
                Snapshot(TEXT("scenario"), TEXT("place_failed"), TEXT("failed"), false);
                FinishMatrix();
                return;
            }
            bCrowdPlaced = true;
            CrowdPhaseSince = Time;
            CrowdSampleAt = Time;
            return;
        }
        const bool bCommercial = ZoneAt(Ordered[0].Location) == EZone::Commercial && TargetPeople > 0;
        Observe(TEXT("solo"), bCommercial, 40.0, [&]()
        {
            UE_LOG(LogMarburgumNet, Log, TEXT("MB2_CROWD role=server event=scenario name=solo phase=please_second players=%d"), Ordered.Num());
            Begin(12);
        });
        return;
    }
    if (CrowdPhase == 12)
    {
        if (Ordered.Num() >= 2)
        {
            Begin(1);
        }
        return;
    }
    if (!bCrowdPlaced)
    {
        bool bOk = false;
        if (CrowdPhase == 1 && Ordered.Num() >= 2)
        {
            const int32 Commercial = NodeOf(static_cast<uint8>(EZone::Commercial), {}, 0.0);
            TArray<FVector> Avoid;
            if (Commercial != INDEX_NONE)
            {
                Avoid.Add(Graph.GetNodes()[Commercial].Location);
            }
            const int32 Houses = NodeOf(static_cast<uint8>(EZone::Residential), Avoid, 20000.0);
            bOk = Place(Ordered[0], Commercial) && Place(Ordered[1], Houses);
        }
        else if (CrowdPhase == 2 && Ordered.Num() >= 2)
        {
            const int32 First = NodeOf(static_cast<uint8>(EZone::Commercial), {}, 0.0);
            int32 Second = INDEX_NONE;
            double Best = CrowdShareCm;
            if (First != INDEX_NONE)
            {
                const FVector From = Graph.GetNodes()[First].Location;
                for (int32 Index = 0; Index < Graph.GetNodes().Num(); ++Index)
                {
                    const FMarburgumWalkGraph::FNode& Node = Graph.GetNodes()[Index];
                    const double Distance = FVector::Dist2D(Node.Location, From);
                    if (!Node.bRoad && !Node.bBridge && Node.Component == Graph.LargestComponent() && Node.Zone == static_cast<uint8>(EZone::Commercial)
                        && Distance >= 800.0 && Distance < Best)
                    {
                        Best = Distance;
                        Second = Index;
                    }
                }
            }
            bOk = Second != INDEX_NONE && Place(Ordered[0], First) && Place(Ordered[1], Second);
        }
        else if (CrowdPhase == 3 && Ordered.Num() >= 2)
        {
            const int32 Away = NodeOf(static_cast<uint8>(EZone::Residential), { Ordered[0].Location }, 20000.0);
            bOk = Place(Ordered[1], Away);
        }
        else if (CrowdPhase == 4 && Ordered.Num() >= 2)
        {
            bOk = Place(Ordered[1], Graph.NearestNode(Ordered[0].Location, 5000.0, 2000.0));
        }
        else if (CrowdPhase == 5 && Ordered.Num() >= 2)
        {
            bOk = Place(Ordered[0], Bank(false)) && Place(Ordered[1], Bank(true)) && FaceTown(Ordered[0]) && FaceTown(Ordered[1]);
        }
        else if (CrowdPhase == 9 && Ordered.Num() >= 3)
        {
            const int32 First = NodeOf(static_cast<uint8>(EZone::OldTown), {}, 0.0);
            TArray<FVector> Avoid;
            if (First != INDEX_NONE)
            {
                Avoid.Add(Graph.GetNodes()[First].Location);
            }
            const int32 Second = NodeOf(static_cast<uint8>(EZone::OldTown), Avoid, 15000.0);
            if (Second != INDEX_NONE)
            {
                Avoid.Add(Graph.GetNodes()[Second].Location);
            }
            const int32 Third = NodeOf(static_cast<uint8>(EZone::OldTown), Avoid, 15000.0);
            bOk = Place(Ordered[0], First) && Place(Ordered[1], Second) && Place(Ordered[2], Third);
        }
        else if (CrowdPhase == 20 && Ordered.Num() >= 2)
        {
            const int32 First = NodeOf(static_cast<uint8>(EZone::ResidentialBlocks), {}, 0.0);
            int32 Second = INDEX_NONE;
            double Best = CrowdShareCm;
            if (First != INDEX_NONE)
            {
                const FVector From = Graph.GetNodes()[First].Location;
                for (int32 Index = 0; Index < Graph.GetNodes().Num(); ++Index)
                {
                    const FMarburgumWalkGraph::FNode& Node = Graph.GetNodes()[Index];
                    const double Distance = FVector::Dist2D(Node.Location, From);
                    if (!Node.bRoad && !Node.bBridge && Node.Component == Graph.LargestComponent() && Node.Zone == static_cast<uint8>(EZone::ResidentialBlocks)
                        && Distance >= 800.0 && Distance < Best)
                    {
                        Best = Distance;
                        Second = Index;
                    }
                }
            }
            bOk = Second != INDEX_NONE && Place(Ordered[0], First) && Place(Ordered[1], Second);
        }
        if (CrowdPhase == 6 || CrowdPhase == 8 || CrowdPhase == 10)
        {
            if (CrowdPhase == 10)
            {
                UE_LOG(LogMarburgumNet, Log, TEXT("MB2_CROWD role=server event=scenario name=cap phase=please_drop_cap players=%d"), Ordered.Num());
            }
            bCrowdPlaced = true;
            return;
        }
        if (CrowdPhase == 7)
        {
            if (Ordered.Num() >= 2)
            {
                bCrowdPlaced = true;
                CrowdPhaseSince = Time;
            }
            return;
        }
        if (!bOk)
        {
            Snapshot(TEXT("scenario"), TEXT("place_failed"), TEXT("failed"), false);
            FinishMatrix();
            return;
        }
        bCrowdPlaced = true;
        CrowdPhaseSince = Time;
        return;
    }
    if (CrowdPhase == 6)
    {
        UE_LOG(LogMarburgumNet, Log, TEXT("MB2_CROWD role=server event=scenario name=E phase=please_drop players=%d"), Ordered.Num());
        if (Ordered.Num() <= 1 && Time - CrowdPhaseSince > 2.0)
        {
            Snapshot(TEXT("scenario"), TEXT("E_dropped"), Ordered.Num() == 1 ? TEXT("lifecycle") : TEXT("failed"), Ordered.Num() == 1 && CountPeople() <= MaxPeople);
            Begin(7);
        }
        else if (Time - CrowdPhaseSince > 45.0)
        {
            Snapshot(TEXT("scenario"), TEXT("E_dropped"), TEXT("timeout"), false);
            FinishMatrix();
        }
        return;
    }
    if (CrowdPhase == 8)
    {
        if (Ordered.Num() >= 3)
        {
            Begin(9);
        }
        return;
    }
    if (CrowdPhase == 10)
    {
        if (Ordered.Num() <= 2 && Time - CrowdPhaseSince > 12.0)
        {
            const bool bHeld = CrowdMaxPeople <= MaxPeople && CountPeople() <= MaxPeople && TargetPeople < 48;
            Snapshot(TEXT("scenario"), TEXT("cap_drop"), bHeld ? TEXT("lifecycle") : TEXT("failed"), bHeld);
            FinishMatrix();
        }
        else if (Time - CrowdPhaseSince > 70.0)
        {
            Snapshot(TEXT("scenario"), TEXT("cap_drop"), TEXT("timeout"), false);
            FinishMatrix();
        }
        return;
    }
    auto WantOf = [&](const FMarburgumStreetViewer& Viewer) -> int32
    {
        const int32 Slot = Viewers.IndexOfByPredicate([&](const FMarburgumStreetViewer& Other)
        {
            return Other.Character.Get() == Viewer.Character.Get();
        });
        return ViewerWants.IsValidIndex(Slot) ? ViewerWants[Slot] : 0;
    };
    const double Apart = Ordered.Num() >= 2 ? FVector::Dist2D(Ordered[0].Location, Ordered[1].Location) : 1.0e9;
    if (CrowdPhase == 1)
    {
        const bool bZones = Ordered.Num() >= 2 && ZoneAt(Ordered[0].Location) == EZone::Commercial && ZoneAt(Ordered[1].Location) == EZone::Residential;
        const int32 WantA = WantOf(Ordered[0]);
        const int32 WantB = Ordered.Num() > 1 ? WantOf(Ordered[1]) : 0;
        Observe(TEXT("A"), bZones && WantA > WantB && WantA > 0 && WantB > 0, 40.0, [&]() { Begin(2); });
    }
    else if (CrowdPhase == 2)
    {
        const int32 Solo = PeopleFor(1, Hour.Density * Weather);
        Observe(TEXT("B"), Apart < CrowdShareCm && TargetPeople <= Solo && TargetPeople > 0 && TargetPeople < Solo * 2, 40.0, [&]() { Begin(3); });
    }
    else if (CrowdPhase == 3)
    {
        Observe(TEXT("C_apart"), Apart > CrowdShareCm && CrowdBudgetChanges <= 4, 40.0, [&]() { Begin(4); });
    }
    else if (CrowdPhase == 4)
    {
        Observe(TEXT("C_back"), Apart < CrowdShareCm && CrowdBudgetChanges <= 4, 40.0, [&]() { Begin(5); });
    }
    else if (CrowdPhase == 5)
    {
        const bool bBanks = Ordered.Num() >= 2 && Ordered[0].Location.X < -10000.0 && Ordered[1].Location.X > 10000.0
            && ZoneAt(Ordered[0].Location) != ZoneAt(Ordered[1].Location);
        Observe(TEXT("D"), bBanks, CrowdSettleSeconds, [&]() { Begin(6); });
    }
    else if (CrowdPhase == 7)
    {
        const int32 WantSum = WantOf(Ordered[0]) + (Ordered.Num() > 1 ? WantOf(Ordered[1]) : 0);
        Observe(TEXT("E"), Ordered.Num() >= 2 && WantSum == TargetPeople && TargetPeople > 0, 40.0, [&]()
        {
            UE_LOG(LogMarburgumNet, Log, TEXT("MB2_CROWD role=server event=scenario name=cap phase=please_third players=%d"), Ordered.Num());
            Begin(8);
        });
    }
    else if (CrowdPhase == 9)
    {
        const int32 Unconstrained = 0;
        static_cast<void>(Unconstrained);
        Observe(TEXT("cap"), Ordered.Num() >= 3 && TargetPeople == MaxPeople && CrowdMaxPeople <= MaxPeople, CrowdSettleSeconds, [&]() { Begin(10); });
    }
    else if (CrowdPhase == 20)
    {
        const bool bShared = Ordered.Num() >= 2 && ZoneAt(Ordered[0].Location) == EZone::ResidentialBlocks && ZoneAt(Ordered[1].Location) == EZone::ResidentialBlocks
            && Apart < CrowdShareCm && TargetPeople == 2;
        Observe(TEXT("night"), bShared, CrowdSettleSeconds, [&]() { FinishMatrix(); });
    }
#endif
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
        if (Viewers.IsEmpty())
        {
            if (BridgeWaitUntil < 0.0)
            {
                BridgeWaitUntil = Time + 90.0;
            }
            if (Time < BridgeWaitUntil)
            {
                return;
            }
        }
        const int32 Largest = Graph.LargestComponent();
        int32 Seed = INDEX_NONE;
        double SeedScore = TNumericLimits<double>::Max();
        for (int32 Node = 0; Node < Graph.GetNodes().Num(); ++Node)
        {
            const FMarburgumWalkGraph::FNode& Here = Graph.GetNodes()[Node];
            if (!Here.bBridge || Here.Component != Largest || FMath::Abs(Here.Location.Y) > 2500.0 || FMath::Abs(Here.Location.X) > 20000.0)
            {
                continue;
            }
            const double Score = FMath::Abs(Here.Location.Y);
            if (Score < SeedScore)
            {
                SeedScore = Score;
                Seed = Node;
            }
        }
        TArray<uint8> OnDeck;
        OnDeck.Init(0, Graph.GetNodes().Num());
        double MinX = 0.0;
        double MaxX = 0.0;
        if (Seed != INDEX_NONE)
        {
            TArray<int32> Queue;
            Queue.Add(Seed);
            OnDeck[Seed] = 1;
            MinX = MaxX = Graph.GetNodes()[Seed].Location.X;
            for (int32 Head = 0; Head < Queue.Num(); ++Head)
            {
                const int32 Node = Queue[Head];
                MinX = FMath::Min(MinX, Graph.GetNodes()[Node].Location.X);
                MaxX = FMath::Max(MaxX, Graph.GetNodes()[Node].Location.X);
                for (const int32 Edge : Graph.Touching(Node))
                {
                    const int32 Next = Graph.Other(Edge, Node);
                    const FMarburgumWalkGraph::FNode& There = Graph.GetNodes()[Next];
                    if (OnDeck[Next] || !There.bBridge || FMath::Abs(There.Location.Y) > 4000.0)
                    {
                        continue;
                    }
                    OnDeck[Next] = 1;
                    Queue.Add(Next);
                }
            }
        }
        auto Bank = [&](bool bLow) -> int32
        {
            const double Limit = bLow ? MinX : MaxX;
            TArray<int32> Previous;
            Previous.Init(INDEX_NONE, Graph.GetNodes().Num());
            TArray<int32> Queue;
            for (int32 Node = 0; Node < OnDeck.Num(); ++Node)
            {
                if (!OnDeck[Node])
                {
                    continue;
                }
                const double X = Graph.GetNodes()[Node].Location.X;
                if (FMath::Abs(X - Limit) > 1500.0)
                {
                    continue;
                }
                Previous[Node] = Node;
                Queue.Add(Node);
            }
            for (int32 Head = 0; Head < Queue.Num() && Head < 4000; ++Head)
            {
                const int32 Node = Queue[Head];
                const FMarburgumWalkGraph::FNode& Here = Graph.GetNodes()[Node];
                const bool bBeyond = bLow ? Here.Location.X < MinX - 800.0 : Here.Location.X > MaxX + 800.0;
                if (!Here.bBridge && bBeyond)
                {
                    return Node;
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
            return INDEX_NONE;
        };
        const int32 LowBank = Seed != INDEX_NONE && MaxX - MinX >= 15000.0 ? Bank(true) : INDEX_NONE;
        const int32 HighBank = LowBank != INDEX_NONE ? Bank(false) : INDEX_NONE;
        TArray<int32> Previous;
        Previous.Init(INDEX_NONE, Graph.GetNodes().Num());
        if (LowBank != INDEX_NONE && HighBank != INDEX_NONE)
        {
            TArray<int32> Queue;
            Queue.Add(LowBank);
            Previous[LowBank] = LowBank;
            for (int32 Head = 0; Head < Queue.Num() && Head < 8000 && Previous[HighBank] == INDEX_NONE; ++Head)
            {
                const int32 Node = Queue[Head];
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
        }
        if (HighBank == INDEX_NONE || Previous.IsValidIndex(HighBank) == false || Previous[HighBank] == INDEX_NONE)
        {
            BridgeCrossStart = Time;
            Finish(TEXT("no_stari_route"), nullptr);
            return;
        }
        BridgeRoute.Reset();
        for (int32 Node = HighBank; Node != LowBank; Node = Previous[Node])
        {
            BridgeRoute.Add(Node);
        }
        BridgeRoute.Add(LowBank);
        Algo::Reverse(BridgeRoute);
        double Metres = 0.0;
        for (int32 Index = 1; Index < BridgeRoute.Num(); ++Index)
        {
            Metres += FVector::Dist(Graph.GetNodes()[BridgeRoute[Index - 1]].Location, Graph.GetNodes()[BridgeRoute[Index]].Location);
        }
        if (Metres < 18000.0)
        {
            BridgeCrossStart = Time;
            Finish(TEXT("route_too_short"), nullptr);
            return;
        }
        AMarburgumPedestrian* Person = SpawnAt(LowBank, nullptr, TEXT("bridge_cross"), 0.0);
        if (!Person)
        {
            BridgeCrossStart = Time;
            Finish(TEXT("no_spawn"), nullptr);
            return;
        }
        Person->NpcId = TEXT("bridge_cross");
        Person->SetScriptedRoute(BridgeRoute);
        BridgeCross = Person;
        BridgeLeg = 0;
        BridgeCrossStart = Time;
        BridgeCrossLog = Time;
        const FVector From = Graph.GetNodes()[BridgeRoute[0]].Location;
        const FVector To = Graph.GetNodes()[BridgeRoute.Last()].Location;
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=bridge_cross result=started leg=south_to_north nodes=%d metres=%.0f from=%.0f,%.0f,%.0f to=%.0f,%.0f,%.0f"),
            StreetLogPrefix, BridgeRoute.Num(), Metres / 100.0, From.X, From.Y, From.Z, To.X, To.Y, To.Z);
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
        const TCHAR* Direction = BridgeLeg == 0 ? TEXT("south_to_north") : TEXT("north_to_south");
        const FVector At = Person->GetActorLocation();
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=bridge_cross result=arrived leg=%s seconds=%.1f at=%.0f,%.0f,%.0f"),
            StreetLogPrefix, Direction, Time - BridgeCrossStart, At.X, At.Y, At.Z);
        if (BridgeLeg == 0)
        {
            Algo::Reverse(BridgeRoute);
            Person->SetScriptedRoute(BridgeRoute);
            BridgeLeg = 1;
            BridgeCrossStart = Time;
            BridgeCrossLog = Time;
            const FVector To = Graph.GetNodes()[BridgeRoute.Last()].Location;
            UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=bridge_cross result=started leg=north_to_south nodes=%d from=%.0f,%.0f,%.0f to=%.0f,%.0f,%.0f"),
                StreetLogPrefix, BridgeRoute.Num(), At.X, At.Y, At.Z, To.X, To.Y, To.Z);
            return;
        }
        Finish(TEXT("arrived_both"), Person);
        return;
    }
    if (Time - BridgeCrossStart > 360.0)
    {
        Finish(TEXT("timeout"), Person);
        return;
    }
    if (Time - BridgeCrossLog >= 5.0)
    {
        BridgeCrossLog = Time;
        const FVector At = Person->GetActorLocation();
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=bridge_cross result=walking leg=%d seconds=%.1f at=%.0f,%.0f,%.0f edge=%d node=%d"),
            StreetLogPrefix, BridgeLeg, Time - BridgeCrossStart, At.X, At.Y, At.Z, Person->GetWalkEdge(), Person->GetWalkNode());
    }
}

void UMarburgumStreetSubsystem::Step(double Time)
{
    const double StepStarted = bCrowdMatrix ? FPlatformTime::Seconds() : 0.0;
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
    StepCrowdMatrix(Time);
    if (bCrowdMatrix)
    {
        CrowdStepMs += (FPlatformTime::Seconds() - StepStarted) * 1000.0;
        ++CrowdStepN;
    }

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
            if (bCrowdMatrix)
            {
                ++CrowdDespawnCull;
            }
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
    if (bCrowdMatrix)
    {
        ++CrowdDespawnHome;
    }
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
            if (bCrowdMatrix)
            {
                ++CrowdSpawnTries;
                ++CrowdRejectNode;
            }
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
            if (bCrowdMatrix)
            {
                ++CrowdSpawnTries;
                ++CrowdRejectComponent;
            }
            continue;
        }
        const FVector At = Graph.GetNodes()[Node].Location;
        if (!InSpawnBand(Viewer, At, bInView) || Graph.Touching(Node).IsEmpty())
        {
            if (bCrowdMatrix)
            {
                ++CrowdSpawnTries;
                ++CrowdRejectBand;
            }
            continue;
        }
        // Busy streets fill first: a quiet one takes its share of the newcomers in proportion to its activity.
        if (Random.FRand() > FMath::Max(MinSpawnActivity, Graph.GetNodes()[Node].Activity))
        {
            if (bCrowdMatrix)
            {
                ++CrowdSpawnTries;
                ++CrowdRejectActivity;
            }
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
        if (bCrowdMatrix)
        {
            ++CrowdSpawnTries;
            ++CrowdRejectCollision;
        }
        return nullptr;
    }
    const int32 Serial = NextSerial++;
    Person->NpcId = FName(*FString::Printf(TEXT("street_%d"), Serial));
    Person->InitStreet(*this, Serial, GroupIndex, LookIndex, Personality, Pace, Edge, Node, Random.GetUnsignedInt());
    Person->FinishSpawning(Spot);
    if (!IsValid(Person))
    {
        if (bCrowdMatrix)
        {
            ++CrowdSpawnTries;
            ++CrowdRejectCollision;
        }
        return nullptr;
    }
    if (bCrowdMatrix)
    {
        ++CrowdSpawnTries;
        ++CrowdSpawnHits;
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
        else if (bCrowdMatrix)
        {
            ++CrowdSpawnMisses;
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
