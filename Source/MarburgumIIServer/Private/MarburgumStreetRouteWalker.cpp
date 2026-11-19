#include "MarburgumStreetRouteWalker.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlaces.h"
#include "MarburgumStreetRoute.h"
#include "MarburgumStreetSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"

namespace
{
    constexpr float TickSeconds = 0.25f;
    /** The trace down for a stop's walkable top starts this high above the river and ends this far below it. */
    constexpr double TraceTopCm = 10000.0;
    constexpr double TraceBottomCm = -3000.0;
}

bool UMarburgumStreetRouteWalker::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return World && World->IsGameWorld() && FParse::Param(FCommandLine::Get(), MarburgumII::StreetRoute::ServerOption)
        && Super::ShouldCreateSubsystem(Outer);
}

void UMarburgumStreetRouteWalker::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    const ENetMode NetMode = InWorld.GetNetMode();
    if (NetMode != NM_DedicatedServer && NetMode != NM_ListenServer)
    {
        return;
    }
    using namespace MarburgumII::StreetRoute;
    FString Name = TEXT("districts");
    FParse::Value(FCommandLine::Get(), RouteNameOption, Name);
    const bool bPlaces = Name == TEXT("places");
    Route = bPlaces ? TArrayView<const FStop>(PlaceStops) : TArrayView<const FStop>(Stops);
    Dwell = bPlaces ? PlaceDwellSeconds : DwellSeconds;
    UE_LOG(LogMarburgumNet, Log, TEXT("MB2_STREET role=server event=route_armed route=%s stops=%d dwell_s=%.1f"), bPlaces ? TEXT("places") : TEXT("districts"),
        Route.Num(), Dwell);
    InWorld.GetTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMarburgumStreetRouteWalker::Tick), TickSeconds, true);
}

void UMarburgumStreetRouteWalker::Deinitialize()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(Timer);
    }
    Super::Deinitialize();
}

void UMarburgumStreetRouteWalker::Tick()
{
    using namespace MarburgumII::StreetRoute;
    UWorld* World = GetWorld();
    APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
    ACharacter* Body = Player ? Cast<ACharacter>(Player->GetPawn()) : nullptr;
    if (!Body)
    {
        return;
    }
    const double Now = World->GetTimeSeconds();
    if (BodySince < 0.0)
    {
        BodySince = Now;
        NextAt = Now + LeadSeconds;
    }
    if (Now < NextAt)
    {
        return;
    }
    if (Next >= Route.Num())
    {
        if (Next == Route.Num())
        {
            UE_LOG(LogMarburgumNet, Log, TEXT("MB2_STREET role=server event=route_done stops=%d"), Next);
            ++Next;
        }
        return;
    }

    const FStop& Stop = Route[Next];
    const FVector2D Ground = MarburgumII::Places::FromPlan(Stop.East, Stop.South);
    // Set down on the walkable top: the first static surface a line straight down meets.
    FHitResult Hit;
    FCollisionQueryParams Params(TEXT("MB2StreetRoute"), false, Body);
    const bool bHit = World->LineTraceSingleByObjectType(Hit, FVector(Ground, TraceTopCm), FVector(Ground, TraceBottomCm),
        FCollisionObjectQueryParams(ECC_WorldStatic), Params);
    const double HalfHeight = Body->GetCapsuleComponent() ? Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0;
    const FVector At(Ground, (bHit ? Hit.ImpactPoint.Z : 0.0) + HalfHeight + 5.0);
    const bool bMoved = Body->TeleportTo(At, FRotator(0.0, Stop.Yaw, 0.0), false, true);
    // And look along the street, a little down, so the client's view and captures show who is there.
    Player->ClientSetRotation(FRotator(-8.0, Stop.Yaw, 0.0));
    // The zone the street itself reads there, against the one the route was chosen for.
    const UMarburgumStreetSubsystem* Street = World->GetSubsystem<UMarburgumStreetSubsystem>();
    const MarburgumII::Street::EZone Found = Street && Street->IsEnabled() ? Street->ZoneAt(At) : MarburgumII::Street::EZone::Count;
    UE_LOG(LogMarburgumNet, Log, TEXT("MB2_STREET role=server event=route stop=%s index=%d player=%d ground=%d moved=%d want_zone=%s zone=%s x_m=%.2f y_m=%.2f z_m=%.2f"),
        Stop.Id, Next, Player->PlayerState ? Player->PlayerState->GetPlayerId() : -1, bHit ? 1 : 0, bMoved ? 1 : 0,
        MarburgumII::Street::ZoneNames[static_cast<int32>(Stop.Zone)],
        Found == MarburgumII::Street::EZone::Count ? TEXT("-") : MarburgumII::Street::ZoneNames[static_cast<int32>(Found)],
        At.X / 100.0, At.Y / 100.0, At.Z / 100.0);
    ++Next;
    NextAt = Now + Dwell;
}
