#include "MarburgumPlaceRouteWalker.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "MarburgumArea.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlaceRoute.h"
#include "MarburgumPlaces.h"
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

bool UMarburgumPlaceRouteWalker::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return World && World->IsGameWorld() && FParse::Param(FCommandLine::Get(), MarburgumII::PlaceRoute::ServerOption)
        && Super::ShouldCreateSubsystem(Outer);
}

void UMarburgumPlaceRouteWalker::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    const ENetMode NetMode = InWorld.GetNetMode();
    if (NetMode != NM_DedicatedServer && NetMode != NM_ListenServer)
    {
        return;
    }
    UE_LOG(LogMarburgumNet, Log, TEXT("MB2PLACE role=server event=route_armed stops=%d dwell_s=%.1f"),
        UE_ARRAY_COUNT(MarburgumII::PlaceRoute::Stops), MarburgumII::PlaceRoute::DwellSeconds);
    // The named areas the place is checked against first, for reading the route's results.
    for (TActorIterator<AMarburgumArea> It(&InWorld); It; ++It)
    {
        const UBoxComponent* Box = It->FindComponentByClass<UBoxComponent>();
        const FVector Centre = Box ? Box->GetComponentLocation() / 100.0 : FVector::ZeroVector;
        const FVector Extent = Box ? Box->GetScaledBoxExtent() / 100.0 : FVector::ZeroVector;
        UE_LOG(LogMarburgumNet, Log, TEXT("MB2PLACE role=server event=area_box actor=%s id=%s x_m=%.1f y_m=%.1f z_m=%.1f half_x_m=%.1f half_y_m=%.1f half_z_m=%.1f"),
            *It->GetName(), *It->AreaId.ToString(), Centre.X, Centre.Y, Centre.Z, Extent.X, Extent.Y, Extent.Z);
    }
    InWorld.GetTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMarburgumPlaceRouteWalker::Tick), TickSeconds, true);
}

void UMarburgumPlaceRouteWalker::Deinitialize()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(Timer);
    }
    Super::Deinitialize();
}

void UMarburgumPlaceRouteWalker::Tick()
{
    using namespace MarburgumII::PlaceRoute;
    UWorld* World = GetWorld();
    const APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
    ACharacter* Body = Player ? Cast<ACharacter>(Player->GetPawn()) : nullptr;
    if (!Body || Next >= UE_ARRAY_COUNT(Stops))
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

    const FStop& Stop = Stops[Next];
    const FVector2D Ground = MarburgumII::Places::FromPlan(Stop.East, Stop.South);
    // Set down on the walkable top: the first static surface a line straight down meets.
    FHitResult Hit;
    FCollisionQueryParams Params(TEXT("MB2PlaceRoute"), false, Body);
    const bool bHit = World->LineTraceSingleByObjectType(Hit, FVector(Ground, TraceTopCm), FVector(Ground, TraceBottomCm),
        FCollisionObjectQueryParams(ECC_WorldStatic), Params);
    const double HalfHeight = Body->GetCapsuleComponent() ? Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0;
    const FVector At(Ground, (bHit ? Hit.ImpactPoint.Z : 0.0) + HalfHeight + 5.0);
    const bool bMoved = Body->TeleportTo(At, FRotator(0.0, Stop.Yaw, 0.0), false, true);
    UE_LOG(LogMarburgumNet, Log, TEXT("MB2PLACE role=server event=route stop=%s index=%d player=%d ground=%d moved=%d x_m=%.2f y_m=%.2f z_m=%.2f"),
        Stop.Id, Next, Player->PlayerState ? Player->PlayerState->GetPlayerId() : -1, bHit ? 1 : 0, bMoved ? 1 : 0,
        At.X / 100.0, At.Y / 100.0, At.Z / 100.0);
    ++Next;
    NextAt = Now + DwellSeconds;
}
