#include "MarburgumDoorTour.h"

#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"

namespace
{
    constexpr float TickSeconds = 0.25f;
    /** Long enough for the tourist to bind and report its doors at home. */
    constexpr double LeadSeconds = 12.0;
    /** Long enough away for the client to drop the building and the doors to leave its relevancy. */
    constexpr double DwellSeconds = 20.0;
    const TCHAR* TouristSuffix = TEXT("-tour");
}

bool UMarburgumDoorTour::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    FString Ignored;
    return World && World->IsGameWorld() && FParse::Value(FCommandLine::Get(), TEXT("MB2DoorTour="), Ignored) && Super::ShouldCreateSubsystem(Outer);
}

void UMarburgumDoorTour::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    if (InWorld.GetNetMode() != NM_DedicatedServer && InWorld.GetNetMode() != NM_ListenServer)
    {
        return;
    }
    FParse::Value(FCommandLine::Get(), TEXT("MB2DoorTour="), Away);
    UE_LOG(LogMarburgumNet, Log, TEXT("MB2DOORTOUR role=server event=armed away=%s"), *Away);
    InWorld.GetTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMarburgumDoorTour::Tick), TickSeconds, true);
}

void UMarburgumDoorTour::Deinitialize()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(Timer);
    }
    Super::Deinitialize();
}

bool UMarburgumDoorTour::Move(APawn* Body, const FVector& At, const FRotator& Rotation) const
{
    const bool bMoved = Body->TeleportTo(At, Rotation, false, true);
    if (APlayerController* Player = Cast<APlayerController>(Body->GetController()))
    {
        Player->ClientSetRotation(Rotation);
    }
    return bMoved;
}

void UMarburgumDoorTour::Tick()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }
    const double Now = World->GetTimeSeconds();
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* Player = It->Get();
        APawn* Body = Player ? Player->GetPawn() : nullptr;
        const AMarburgumPlayerState* State = Player ? Player->GetPlayerState<AMarburgumPlayerState>() : nullptr;
        if (!Body || !State || !State->GetCharacterKey().EndsWith(TouristSuffix))
        {
            continue;
        }
        FTrip* Trip = Trips.Find(Body);
        if (!Trip)
        {
            Trip = &Trips.Add(Body, FTrip{ Body->GetActorLocation(), Body->GetActorRotation(), Now, 0 });
            continue;
        }
        if (Trip->Leg == 0 && Now - Trip->SeenSeconds > LeadSeconds)
        {
            const FName Tag(*FString::Printf(TEXT("door_%s"), *Away));
            const APlayerStart* Start = nullptr;
            for (TActorIterator<APlayerStart> Starts(World); Starts && !Start; ++Starts)
            {
                Start = Starts->PlayerStartTag == Tag ? *Starts : nullptr;
            }
            if (!Start)
            {
                UE_LOG(LogMarburgumNet, Warning, TEXT("MB2DOORTOUR role=server event=no_start away=%s"), *Away);
                Trip->Leg = 2;
                continue;
            }
            const bool bMoved = Move(Body, Start->GetActorLocation(), Start->GetActorRotation());
            UE_LOG(LogMarburgumNet, Log, TEXT("MB2DOORTOUR role=server event=away player=%d to=%s moved=%d distance_m=%.1f"), State->GetPlayerId(), *Away,
                bMoved ? 1 : 0, FVector::Dist2D(Trip->Home, Start->GetActorLocation()) / 100.0);
            Trip->Leg = 1;
            Trip->SeenSeconds = Now;
        }
        else if (Trip->Leg == 1 && Now - Trip->SeenSeconds > DwellSeconds)
        {
            const bool bMoved = Move(Body, Trip->Home, Trip->HomeRotation);
            UE_LOG(LogMarburgumNet, Log, TEXT("MB2DOORTOUR role=server event=back player=%d moved=%d"), State->GetPlayerId(), bMoved ? 1 : 0);
            Trip->Leg = 2;
        }
    }
}
