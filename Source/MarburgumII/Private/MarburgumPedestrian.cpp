#include "MarburgumPedestrian.h"

#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MarburgumCharacter.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumStreetSubsystem.h"
#include "Net/UnrealNetwork.h"

using namespace MarburgumII::Street;
using MarburgumII::Combat::FNpcDefinition;

namespace MarburgumStreetPerson
{
    // Street people are sent to clients this far off (a little beyond where they are culled).
    constexpr float NetCullCm = 17000.f;
    // How far ahead RVO avoidance looks for other street people to pass.
    constexpr float AvoidanceRadiusCm = 250.f;
    // Progress under this, while walking, counts toward being stuck.
    constexpr double ProgressCm = 25.0;
    // After this many turn-backs in a row they are lost (walking into a wall the graph check missed).
    constexpr int32 LostTurnBacks = 4;
    // A walker back off the graph looks this far for a node.
    constexpr double RejoinCm = 2500.0;
    constexpr double RejoinHeightCm = 400.0;
    // Crossing the road is less inviting than staying on the pavement.
    constexpr double CrossingWeight = 0.35;
    // Fleeing off the graph: a step this long straight away.
    constexpr double FleeStepCm = 600.0;
    // A watcher backs away this far at a time.
    constexpr double BackAwayCm = 120.0;
    constexpr double GatherArriveCm = 45.0;
    constexpr double GatherLongestSeconds = 30.0;
    constexpr double BumpLookSeconds = 0.3;
    constexpr double BumpCooldownSeconds = 3.0;
    constexpr double BumpAnswerSeconds = 3.0;
    constexpr uint8 EdgeKindCrossing = 2;
    constexpr uint8 EdgeKindCorner = 3;

    uint32 Shade(uint32 Rgb, float Factor)
    {
        const auto Channel = [Factor](uint32 Value) { return static_cast<uint32>(FMath::Clamp(FMath::RoundToInt(Value * Factor), 0, 255)); };
        return (Channel((Rgb >> 16) & 0xff) << 16) | (Channel((Rgb >> 8) & 0xff) << 8) | Channel(Rgb & 0xff);
    }

    double AngleBetween(double A, double B)
    {
        return FMath::FindDeltaAngleDegrees(A, B);
    }
}

AMarburgumPedestrian::AMarburgumPedestrian()
{
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    // Street people pass each other; players and the cast they walk into (and may answer for it).
    Movement->bUseRVOAvoidance = true;
    Movement->AvoidanceConsiderationRadius = MarburgumStreetPerson::AvoidanceRadiusCm;
    SetNetCullDistanceSquared(FMath::Square(MarburgumStreetPerson::NetCullCm));
}

void AMarburgumPedestrian::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(AMarburgumPedestrian, GroupIndex, COND_InitialOnly);
    DOREPLIFETIME_CONDITION(AMarburgumPedestrian, LookIndex, COND_InitialOnly);
    DOREPLIFETIME_CONDITION(AMarburgumPedestrian, Variation, COND_InitialOnly);
    DOREPLIFETIME(AMarburgumPedestrian, Mood);
}

void AMarburgumPedestrian::InitStreet(UMarburgumStreetSubsystem& InStreet, int32 InSerial, int32 InGroup, int32 InLook, EPersonality InPersonality,
    float InPaceMetresPerSecond, int32 StartEdge, int32 StartNode, uint32 Seed)
{
    StreetRef = &InStreet;
    Serial = InSerial;
    GroupIndex = static_cast<uint8>(InGroup);
    LookIndex = static_cast<uint8>(InLook);
    Variation = static_cast<uint8>(Seed & 0xff);
    Personality = InPersonality;
    Random.Initialize(static_cast<int32>(Seed >> 1));
    WalkPaceCm = InPaceMetresPerSecond * 100.f;
    FleePaceCm = MarburgumII::Gameplay::RunSpeedCm * Random.FRandRange(FleePaceMin, FleePaceMax);
    const FMarburgumWalkGraph& Graph = InStreet.GetGraph();
    Edge = StartEdge;
    ToNode = Graph.Other(StartEdge, StartNode);
    HeadingYaw = (Graph.GetNodes()[ToNode].Location - Graph.GetNodes()[StartNode].Location).Rotation().Yaw;
    SetPace(WalkPaceCm);
    bDefinitionBuilt = false;
}

const FNpcDefinition* AMarburgumPedestrian::GetDefinition() const
{
    if (!bDefinitionBuilt)
    {
        using namespace MarburgumII::Combat;
        const FGroup& Group = Groups[FMath::Clamp<int32>(GroupIndex, 0, UE_ARRAY_COUNT(Groups) - 1)];
        const FLook& Look = Looks[FMath::Clamp<int32>(LookIndex, 0, UE_ARRAY_COUNT(Looks) - 1)];
        // Marburgum II's addition: one look is several people, a shade lighter or darker and a little taller or shorter.
        const float Shade = 0.88f + (Variation & 0x0f) / 15.f * 0.24f;
        const float Size = 0.97f + ((Variation >> 4) & 0x0f) / 15.f * 0.06f;
        StreetDefinition = {};
        StreetDefinition.Id = TEXT("street");
        StreetDefinition.Name = Group.Name;
        StreetDefinition.Role = RoleCivilian;
        StreetDefinition.Vitality = StreetVitality;
        StreetDefinition.Stamina = 60.f;
        StreetDefinition.Damage = DefaultNpcDamage;
        StreetDefinition.ReachCm = DefaultNpcReachCm;
        StreetDefinition.AttackIntervalSeconds = DefaultAttackIntervalSeconds;
        StreetDefinition.PursuitRangeCm = DefaultPursuitRangeCm;
        StreetDefinition.PrimaryColor = MarburgumStreetPerson::Shade(Look.PrimaryColor, Shade);
        StreetDefinition.SecondaryColor = MarburgumStreetPerson::Shade(Look.SecondaryColor, Shade);
        StreetDefinition.Scale = Look.Scale * Size;
        bDefinitionBuilt = true;
    }
    return &StreetDefinition;
}

bool AMarburgumPedestrian::IsHostileRole() const
{
    return bAnswersViolence;
}

const TCHAR* AMarburgumPedestrian::MoodIntent(EMarburgumStreetMood Value)
{
    switch (Value)
    {
    case EMarburgumStreetMood::Flee: return IntentFlee;
    case EMarburgumStreetMood::Watch: return IntentWatch;
    case EMarburgumStreetMood::Gather: return IntentGather;
    case EMarburgumStreetMood::Dodge: return IntentDodge;
    case EMarburgumStreetMood::Fight: return IntentFight;
    case EMarburgumStreetMood::Down: return IntentDown;
    default: return IntentWalk;
    }
}

bool AMarburgumPedestrian::IsSpreadingAlarm() const
{
    return (Mood == EMarburgumStreetMood::Flee || Mood == EMarburgumStreetMood::Watch) && AnsweredIncident != INDEX_NONE
        && (AnsweredEvent == EEvent::Fight || AnsweredEvent == EEvent::RunOver);
}

UMarburgumStreetSubsystem* AMarburgumPedestrian::Street() const
{
    return StreetRef.Get();
}

float AMarburgumPedestrian::RandomUnit()
{
    return Random.FRand();
}

void AMarburgumPedestrian::SetPace(float CentimetresPerSecond)
{
    GetCharacterMovement()->MaxWalkSpeed = CentimetresPerSecond;
}

// ---- Thinking ----

void AMarburgumPedestrian::Think()
{
    using namespace MarburgumII::Combat;
    ++TickCount;
    const FVector Here = GetActorLocation();
    const double MovedCm = FVector::Dist2D(Here, LastThinkLocation);
    const bool bWasWalking = bWalking;
    LastThinkLocation = Here;
    bLastStepFailed = bWasWalking && MovedCm < GetCharacterMovement()->MaxWalkSpeed * TickSeconds * 0.2;

    UMarburgumStreetSubsystem* S = Street();
    if (!S)
    {
        Stop();
        return;
    }
    const double Now = S->Now();

    if (State == EMarburgumFighterState::Incapacitated)
    {
        SetMood(EMarburgumStreetMood::Down, 0.0);
        RecoverRemaining -= TickSeconds;
        if (RecoverRemaining <= 0.f)
        {
            Recover();
        }
        return;
    }
    if (bAnswersViolence)
    {
        if (Target.IsValid())
        {
            SetMood(EMarburgumStreetMood::Fight, 0.0);
            ThinkHostile(*Target);
            return;
        }
        GiveUp();
    }

    if (Mood != EMarburgumStreetMood::Walk && Now >= MoodUntil)
    {
        EndMood();
    }
    if (bUnbothered && !S->FindIncident(AnsweredIncident))
    {
        bUnbothered = false;
        AnsweredIncident = INDEX_NONE;
        AnsweredLoudness = 0;
    }

    if (!SenseCars(Now) && !SenseIncidents(Now))
    {
        SenseBumps(Now);
    }
    if (bAnswersViolence)
    {
        return;
    }

    switch (Mood)
    {
    case EMarburgumStreetMood::Flee:
        StepFlee(Now);
        break;
    case EMarburgumStreetMood::Watch:
        StepWatch(Now);
        break;
    case EMarburgumStreetMood::Gather:
        StepGather(Now);
        break;
    case EMarburgumStreetMood::Dodge:
        break;
    default:
        StepWalk(Now, TickSeconds);
        break;
    }
}

void AMarburgumPedestrian::MakeProbe(const FVector& At, double Seconds, bool bDodges, float InVitality)
{
    bNeverDodges = !bDodges;
    Vitality = InVitality;
    VitalityPoints = static_cast<uint8>(FMath::Clamp(FMath::CeilToInt(Vitality), 0, 255));
    Edge = INDEX_NONE;
    bRejoining = true;
    BeginWatch(At, Street() ? Street()->Now() : 0.0);
    MoodUntil += Seconds - WatchSeconds;
    // A probe has seen nothing yet: whatever happens near it is rolled for as usual.
    AnsweredIncident = INDEX_NONE;
    AnsweredLoudness = 0;
    LogEvent(TEXT("probe"), FString::Printf(TEXT("dodges=%d vitality=%.0f"), bDodges ? 1 : 0, Vitality));
}

bool AMarburgumPedestrian::SenseCars(double Now)
{
    if (Mood == EMarburgumStreetMood::Dodge)
    {
        return true;
    }
    if (bNeverDodges)
    {
        return false;
    }
    const FVector Here = GetActorLocation();
    for (const FMarburgumStreetMover& Car : Street()->GetMovers())
    {
        if (FMath::Abs(Car.Location.Z - Here.Z) > RunOverHeightCm)
        {
            continue;
        }
        double Seconds = 0.0;
        if (!WillPassClose(FVector2D(Here), FVector2D(Car.Location), FVector2D(Car.Velocity), DodgeLookaheadSeconds, DodgeClearanceCm, &Seconds))
        {
            continue;
        }
        // Off the car's line, to whichever side of it they already are.
        const FVector2D Along = FVector2D(Car.Velocity).GetSafeNormal();
        const FVector2D Offset = FVector2D(Here) - FVector2D(Car.Location);
        const double Side = Along.X * Offset.Y - Along.Y * Offset.X >= 0.0 ? 1.0 : -1.0;
        const FVector Away(-Along.Y * Side, Along.X * Side, 0.0);
        SetPace(FleePaceCm);
        WalkTo(Here + Away * DodgeStepCm);
        SetMood(EMarburgumStreetMood::Dodge, Now + DodgeSeconds);
        bRejoining = false;
        Edge = INDEX_NONE;
        LogEvent(TEXT("dodge"), FString::Printf(TEXT("car_speed_ms=%.1f due_s=%.2f"), Car.Velocity.Size2D() / 100.0, Seconds));
        return true;
    }
    return false;
}

bool AMarburgumPedestrian::SenseIncidents(double Now)
{
    UMarburgumStreetSubsystem* S = Street();
    const FVector Here = GetActorLocation();
    const FMarburgumStreetIncident* Best = nullptr;
    int32 BestLoudness = 0;
    double BestDistance = TNumericLimits<double>::Max();
    bool bBestSpread = false;
    const int32 Held = Mood == EMarburgumStreetMood::Walk && !bUnbothered ? 0 : AnsweredLoudness;
    const auto Consider = [&](const FMarburgumStreetIncident& Incident, bool bSpread)
    {
        if (Rolled.Contains(Incident.Id) || Incident.Who.Get() == this)
        {
            return;
        }
        const int32 Loud = Loudness(Incident.Event);
        if (Loud <= Held)
        {
            return;
        }
        const double Distance = FVector::Dist2D(Here, Incident.Location);
        if (Loud > BestLoudness || (Loud == BestLoudness && Distance < BestDistance))
        {
            Best = &Incident;
            BestLoudness = Loud;
            BestDistance = Distance;
            bBestSpread = bSpread;
        }
    };
    for (const FMarburgumStreetIncident& Incident : S->GetIncidents())
    {
        const double Distance = FVector::Dist2D(Here, Incident.Location);
        const bool bNear = Incident.Event == EEvent::Body
            ? Distance <= BodyNoticeCm && FMath::Abs(Incident.Location.Z - Here.Z) <= BodyNoticeHeightCm
            : Distance <= FightNoticeCm;
        if (bNear)
        {
            Consider(Incident, false);
        }
    }
    // The scare spreads: someone near is running from or watching a fight this person has not answered yet.
    for (const UMarburgumStreetSubsystem::FAlarm& Alarm : S->GetAlarms())
    {
        if (FVector::Dist2D(Here, Alarm.Location) <= CrowdSpreadCm && Alarm.Location != Here)
        {
            if (const FMarburgumStreetIncident* Incident = S->FindIncident(Alarm.Incident))
            {
                Consider(*Incident, true);
            }
        }
    }
    return Best && Answer(*Best, Best->Event, Now, bBestSpread);
}

bool AMarburgumPedestrian::Answer(const FMarburgumStreetIncident& Incident, EEvent Event, double Now, bool bSpread)
{
    Rolled.Add(Incident.Id);
    const ESource Source = Incident.bPlayer ? ESource::Player : ESource::Stranger;
    EResponse Response = Decide(Personality, Event, Source, RandomUnit());
    // Somebody who would fight over a fight they only saw joins nobody's: they watch it.
    if (Response == EResponse::Fight)
    {
        Response = EResponse::Watch;
    }
    LogEvent(TEXT("react"), FString::Printf(TEXT("event=%s response=%s incident=%d source=%s spread=%d distance_m=%.1f"), EventNames[static_cast<int32>(Event)],
        ResponseNames[static_cast<int32>(Response)], Incident.Id, Incident.bPlayer ? TEXT("player") : TEXT("stranger"), bSpread ? 1 : 0,
        FVector::Dist2D(GetActorLocation(), Incident.Location) / 100.0));
    AnsweredIncident = Incident.Id;
    AnsweredLoudness = Loudness(Event);
    AnsweredEvent = Event;
    switch (Response)
    {
    case EResponse::Flee:
        bUnbothered = false;
        BeginFlee(Incident.Location, Now, FleeSeconds);
        return true;
    case EResponse::Watch:
        bUnbothered = false;
        if (Event == EEvent::Body)
        {
            BeginGather(Incident.Location, Now);
        }
        else
        {
            BeginWatch(Incident.Location, Now);
        }
        return true;
    default:
        // Carry on. The same incident is not rolled again, and nobody walks into a fight anyway (StepWalk).
        bUnbothered = true;
        if (Mood != EMarburgumStreetMood::Walk)
        {
            EndMood();
            bUnbothered = true;
            AnsweredIncident = Incident.Id;
            AnsweredLoudness = Loudness(Event);
            AnsweredEvent = Event;
        }
        return false;
    }
}

bool AMarburgumPedestrian::SenseBumps(double Now)
{
    if (Now < NextBumpSeconds || Mood != EMarburgumStreetMood::Walk)
    {
        return false;
    }
    NextBumpSeconds = Now + MarburgumStreetPerson::BumpLookSeconds;
    const FVector Here = GetActorLocation();
    for (const FMarburgumStreetViewer& Viewer : Street()->GetViewers())
    {
        AMarburgumCharacter* Player = Viewer.Character.Get();
        if (!Player || !Viewer.bOnFoot)
        {
            continue;
        }
        const FVector2D Offset = FVector2D(Here) - FVector2D(Viewer.Location);
        const double Speed = FVector2D(Viewer.Velocity).Size();
        if (Offset.Size() > BumpCm || Speed < BumpSpeedMinCm || Speed > BumpSpeedMaxCm
            || FVector2D::DotProduct(FVector2D(Viewer.Velocity), Offset) <= 0.0)
        {
            continue;
        }
        NextBumpSeconds = Now + MarburgumStreetPerson::BumpCooldownSeconds;
        const EResponse Response = Decide(Personality, EEvent::Bumped, ESource::Player, RandomUnit());
        LogEvent(TEXT("react"), FString::Printf(TEXT("event=%s response=%s incident=-1 source=player spread=0 distance_m=%.1f"),
            EventNames[static_cast<int32>(EEvent::Bumped)], ResponseNames[static_cast<int32>(Response)], Offset.Size() / 100.0));
        AnsweredIncident = INDEX_NONE;
        AnsweredEvent = EEvent::Bumped;
        AnsweredLoudness = Loudness(EEvent::Bumped);
        switch (Response)
        {
        case EResponse::Flee:
            BeginFlee(Viewer.Location, Now, MarburgumStreetPerson::BumpAnswerSeconds);
            return true;
        case EResponse::Watch:
            BeginWatch(Viewer.Location, Now);
            MoodUntil = Now + MarburgumStreetPerson::BumpAnswerSeconds;
            return true;
        case EResponse::Fight:
            bAnswersViolence = true;
            Target = Player;
            Home = Here;
            SetMood(EMarburgumStreetMood::Fight, 0.0);
            SetStateAndIntent(EMarburgumFighterState::Hostile, EMarburgumFighterIntent::Close);
            return true;
        default:
            return false;
        }
    }
    return false;
}

// ---- Moods ----

void AMarburgumPedestrian::SetMood(EMarburgumStreetMood NewMood, double Until)
{
    MoodUntil = Until;
    if (NewMood == Mood)
    {
        return;
    }
    const EMarburgumStreetMood Old = Mood;
    Mood = NewMood;
    EMarburgumFighterIntent FighterIntent = EMarburgumFighterIntent::Wander;
    switch (NewMood)
    {
    case EMarburgumStreetMood::Flee:
    case EMarburgumStreetMood::Dodge:
        FighterIntent = EMarburgumFighterIntent::Avoid;
        break;
    case EMarburgumStreetMood::Watch:
    case EMarburgumStreetMood::Gather:
        FighterIntent = EMarburgumFighterIntent::Watch;
        break;
    case EMarburgumStreetMood::Fight:
        FighterIntent = Intent;
        break;
    case EMarburgumStreetMood::Down:
        FighterIntent = EMarburgumFighterIntent::Wait;
        break;
    default:
        break;
    }
    if (NewMood != EMarburgumStreetMood::Fight && NewMood != EMarburgumStreetMood::Down)
    {
        SetStateAndIntent(EMarburgumFighterState::Idle, FighterIntent);
    }
    LogEvent(TEXT("mood"), FString::Printf(TEXT("from=%s to=%s"), MoodIntent(Old), MoodIntent(NewMood)));
}

void AMarburgumPedestrian::BeginFlee(const FVector& From, double Now, double Seconds)
{
    Threat = From;
    SetPace(FleePaceCm);
    SetMood(EMarburgumStreetMood::Flee, Now + Seconds);
    const FMarburgumWalkGraph& Graph = Street()->GetGraph();
    bFleeOffGraph = Edge == INDEX_NONE || bRejoining;
    if (!bFleeOffGraph)
    {
        // Along the edge they are on, whichever way leads away.
        const int32 FromNode = Graph.Other(Edge, ToNode);
        if (FVector::DistSquared2D(Graph.GetNodes()[ToNode].Location, Threat) < FVector::DistSquared2D(Graph.GetNodes()[FromNode].Location, Threat))
        {
            ToNode = FromNode;
        }
    }
}

void AMarburgumPedestrian::BeginWatch(const FVector& At, double Now)
{
    Threat = At;
    SetPace(WalkPaceCm);
    Stop();
    SetMood(EMarburgumStreetMood::Watch, Now + WatchSeconds);
}

void AMarburgumPedestrian::BeginGather(const FVector& Body, double Now)
{
    Threat = Body;
    SetPace(WalkPaceCm);
    // Each onlooker has their own place round the body, on the side they came from.
    const FVector From = (GetActorLocation() - Body).GetSafeNormal2D();
    const double Spread = (static_cast<double>(Serial % 5) - 2.0) * 25.0;
    GatherSpot = Body + FRotator(0.0, From.Rotation().Yaw + Spread, 0.0).Vector() * GatherOffsetCm;
    bGatherArrived = false;
    SetMood(EMarburgumStreetMood::Gather, Now + MarburgumStreetPerson::GatherLongestSeconds);
}

void AMarburgumPedestrian::EndMood()
{
    SetPace(WalkPaceCm);
    AnsweredIncident = INDEX_NONE;
    AnsweredLoudness = 0;
    AnsweredEvent = EEvent::Count;
    bUnbothered = false;
    bFleeOffGraph = false;
    SetMood(EMarburgumStreetMood::Walk, 0.0);
    Rejoin();
}

void AMarburgumPedestrian::StepFlee(double Now)
{
    const FVector Here = GetActorLocation();
    if (bFleeOffGraph || Edge == INDEX_NONE)
    {
        WalkTo(Here + (Here - Threat).GetSafeNormal2D() * MarburgumStreetPerson::FleeStepCm);
        return;
    }
    const FMarburgumWalkGraph& Graph = Street()->GetGraph();
    if (FVector::Dist2D(Here, Graph.GetNodes()[ToNode].Location) < NodeArriveCm)
    {
        TakeNextEdge(ToNode, true);
    }
    WalkTo(EdgeGoal());
}

void AMarburgumPedestrian::StepWatch(double Now)
{
    if (const FMarburgumStreetIncident* Incident = Street()->FindIncident(AnsweredIncident))
    {
        Threat = Incident->Location;
    }
    const FVector Here = GetActorLocation();
    if (FVector::Dist2D(Here, Threat) < WatchStandoffCm)
    {
        WalkTo(Here + (Here - Threat).GetSafeNormal2D() * MarburgumStreetPerson::BackAwayCm);
        return;
    }
    Stop();
    FaceToward(Threat);
}

void AMarburgumPedestrian::StepGather(double Now)
{
    const FMarburgumStreetIncident* Body = Street()->FindIncident(AnsweredIncident);
    if (!Body)
    {
        // Up again, or taken away: nothing more to see.
        EndMood();
        return;
    }
    Threat = Body->Location;
    if (!bGatherArrived)
    {
        if (FVector::Dist2D(GetActorLocation(), GatherSpot) > MarburgumStreetPerson::GatherArriveCm)
        {
            WalkTo(GatherSpot);
            return;
        }
        bGatherArrived = true;
        MoodUntil = FMath::Min(MoodUntil, Now + Random.FRandRange(GatherSecondsMin, GatherSecondsMax));
        LogEvent(TEXT("gathered"), FString::Printf(TEXT("incident=%d body_m=%.1f"), AnsweredIncident, FVector::Dist2D(GetActorLocation(), Threat) / 100.0));
    }
    Stop();
    FaceToward(Threat);
}

// ---- Walking the graph ----

FVector AMarburgumPedestrian::EdgeGoal() const
{
    const FMarburgumWalkGraph& Graph = StreetRef->GetGraph();
    const FVector To = Graph.GetNodes()[ToNode].Location;
    if (bRejoining || Edge == INDEX_NONE)
    {
        return To;
    }
    const FVector From = Graph.GetNodes()[Graph.Other(Edge, ToNode)].Location;
    const FVector Along = (To - From).GetSafeNormal2D();
    // Keep to the right, so people coming the other way pass.
    return To + FVector(-Along.Y, Along.X, 0.0) * KeepRightCm;
}

bool AMarburgumPedestrian::InFight(const FVector& Point) const
{
    for (const FMarburgumStreetIncident& Incident : StreetRef->GetIncidents())
    {
        if (Incident.Event == EEvent::Fight && Incident.Who.Get() != this && FVector::Dist2D(Point, Incident.Location) < FightClearanceCm)
        {
            return true;
        }
    }
    return false;
}

bool AMarburgumPedestrian::Rejoin()
{
    const FMarburgumWalkGraph& Graph = Street()->GetGraph();
    const int32 Node = Graph.NearestNode(GetActorLocation(), MarburgumStreetPerson::RejoinCm, MarburgumStreetPerson::RejoinHeightCm);
    if (Node == INDEX_NONE)
    {
        return false;
    }
    Edge = INDEX_NONE;
    ToNode = Node;
    bRejoining = true;
    LastProgressSeconds = Street()->Now();
    LastProgressLocation = GetActorLocation();
    return true;
}

void AMarburgumPedestrian::TakeNextEdge(int32 Node, bool bFleeing)
{
    const FMarburgumWalkGraph& Graph = Street()->GetGraph();
    const TArray<int32>& Touching = Graph.Touching(Node);
    if (Touching.IsEmpty())
    {
        bLost = true;
        return;
    }
    const FVector At = Graph.GetNodes()[Node].Location;
    TArray<double> Weights;
    Weights.SetNumZeroed(Touching.Num());
    double Total = 0.0;
    for (int32 Index = 0; Index < Touching.Num(); ++Index)
    {
        const int32 Candidate = Touching[Index];
        // Never straight back the way they came, unless the path ends here.
        if (Candidate == Edge && Touching.Num() > 1)
        {
            continue;
        }
        const FVector Next = Graph.GetNodes()[Graph.Other(Candidate, Node)].Location;
        double Weight;
        if (bFleeing)
        {
            // Away from it: the more an edge gains on the threat, the likelier.
            const double Gain = FVector::Dist2D(Next, Threat) - FVector::Dist2D(At, Threat);
            Weight = FMath::Exp(FMath::Clamp(Gain / 300.0, -6.0, 6.0));
        }
        else
        {
            const double Turn = FMath::DegreesToRadians(MarburgumStreetPerson::AngleBetween(HeadingYaw, (Next - At).Rotation().Yaw));
            Weight = FMath::Exp(Straightness * FMath::Cos(Turn));
            const uint8 Kind = Graph.GetEdges()[Candidate].Kind;
            if (Kind == MarburgumStreetPerson::EdgeKindCrossing || Kind == MarburgumStreetPerson::EdgeKindCorner)
            {
                Weight *= MarburgumStreetPerson::CrossingWeight;
            }
        }
        Weights[Index] = Weight;
        Total += Weight;
    }
    int32 Pick = 0;
    if (Total > 0.0)
    {
        double Left = RandomUnit() * Total;
        for (int32 Index = 0; Index < Weights.Num(); ++Index)
        {
            Left -= Weights[Index];
            if (Left <= 0.0 && Weights[Index] > 0.0)
            {
                Pick = Index;
                break;
            }
        }
    }
    Edge = Touching[Pick];
    ToNode = Graph.Other(Edge, Node);
    bRejoining = false;
    // Their way bends toward the street they took.
    const double EdgeYaw = (Graph.GetNodes()[ToNode].Location - At).Rotation().Yaw;
    HeadingYaw = FRotator::NormalizeAxis(HeadingYaw + MarburgumStreetPerson::AngleBetween(HeadingYaw, EdgeYaw) * 0.5);
}

void AMarburgumPedestrian::StepWalk(double Now, double DeltaSeconds)
{
    const FMarburgumWalkGraph& Graph = Street()->GetGraph();
    if (ToNode == INDEX_NONE && !Rejoin())
    {
        Stop();
        bLost = true;
        return;
    }
    const FVector Here = GetActorLocation();
    HeadingYaw = FRotator::NormalizeAxis(HeadingYaw + Random.FRandRange(-1.f, 1.f) * HeadingDriftDegreesPerSecond * DeltaSeconds * 10.0);
    if (FVector::Dist2D(Here, Graph.GetNodes()[ToNode].Location) < NodeArriveCm)
    {
        TakeNextEdge(ToNode, false);
    }
    // Nobody walks into a fight, whatever they made of it: turn back short of it.
    const FVector Goal = EdgeGoal();
    const FVector Ahead = Here + (Goal - Here).GetSafeNormal2D() * FightClearanceCm;
    if (Edge != INDEX_NONE && (InFight(Goal) || InFight(Ahead)))
    {
        ToNode = Graph.Other(Edge, ToNode);
    }

    // Stuck against something the graph check missed (a parked car, a crowd): turn back, and after a few
    // turn-backs in a row give up as lost.
    if (FVector::Dist2D(Here, LastProgressLocation) >= MarburgumStreetPerson::ProgressCm)
    {
        LastProgressLocation = Here;
        LastProgressSeconds = Now;
        TurnBacks = 0;
    }
    else if (Now - LastProgressSeconds > StuckSeconds)
    {
        LastProgressSeconds = Now;
        if (++TurnBacks > MarburgumStreetPerson::LostTurnBacks)
        {
            bLost = true;
        }
        if (Edge != INDEX_NONE)
        {
            ToNode = Graph.Other(Edge, ToNode);
        }
        else
        {
            Rejoin();
        }
        LogEvent(TEXT("stuck"), FString::Printf(TEXT("turn_backs=%d"), TurnBacks));
    }
    WalkTo(EdgeGoal());
}

// ---- Being hit ----

void AMarburgumPedestrian::OnBlowLanded(AMarburgumCharacter* Attacker, bool bFloored)
{
    if (bFloored)
    {
        bOut = true;
        bAnswersViolence = false;
        SetMood(EMarburgumStreetMood::Down, 0.0);
        return;
    }
    if (bAnswersViolence)
    {
        Super::OnBlowLanded(Attacker, bFloored);
        return;
    }
    UMarburgumStreetSubsystem* S = Street();
    if (!Attacker || !S)
    {
        return;
    }
    const EResponse Response = Decide(Personality, EEvent::Attacked, ESource::Player, RandomUnit());
    LogEvent(TEXT("react"), FString::Printf(TEXT("event=%s response=%s incident=-1 source=player spread=0 distance_m=%.1f"),
        EventNames[static_cast<int32>(EEvent::Attacked)], ResponseNames[static_cast<int32>(Response)],
        FVector::Dist2D(GetActorLocation(), Attacker->GetActorLocation()) / 100.0));
    AnsweredIncident = INDEX_NONE;
    AnsweredEvent = EEvent::Attacked;
    AnsweredLoudness = Loudness(EEvent::Attacked);
    if (Response == EResponse::Fight)
    {
        bAnswersViolence = true;
        Home = GetActorLocation();
        SetMood(EMarburgumStreetMood::Fight, 0.0);
        Super::OnBlowLanded(Attacker, bFloored);
        return;
    }
    BeginFlee(Attacker->GetActorLocation(), S->Now(), FleeSeconds);
}

void AMarburgumPedestrian::GiveUp()
{
    Super::GiveUp();
    bAnswersViolence = false;
    EndMood();
}

void AMarburgumPedestrian::Recover()
{
    if (!bKnockedDown)
    {
        // Out: they stay where they fell until nobody is about to see them taken away (the street's cull).
        bOut = true;
        RecoverRemaining = TNumericLimits<float>::Max();
        return;
    }
    const FVector From = KnockedFrom;
    Super::Recover();
    // Up, and away from where it happened.
    if (UMarburgumStreetSubsystem* S = Street())
    {
        AnsweredIncident = INDEX_NONE;
        AnsweredEvent = EEvent::RunOver;
        AnsweredLoudness = Loudness(EEvent::RunOver);
        bAnswersViolence = bAnswersViolence && Target.IsValid();
        if (!bAnswersViolence)
        {
            Edge = INDEX_NONE;
            bRejoining = true;
            BeginFlee(From, S->Now(), FleeSeconds);
        }
    }
}

void AMarburgumPedestrian::LogEvent(const TCHAR* Event, const FString& Detail) const
{
    // The fighter's per-intent chatter would be every step of every walker; keep what changes something.
    if (FCString::Strcmp(Event, TEXT("npc_intent")) == 0 || FCString::Strcmp(Event, TEXT("npc_ready")) == 0)
    {
        return;
    }
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=%s npc=%s group=%s personality=%s mood=%s pos=%.0f,%.0f,%.0f %s"), StreetLogPrefix, Event,
        *NpcId.ToString(), Groups[FMath::Clamp<int32>(GroupIndex, 0, UE_ARRAY_COUNT(Groups) - 1)].Id, PersonalityNames[static_cast<int32>(Personality)],
        MoodIntent(Mood), GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z, *Detail);
}
