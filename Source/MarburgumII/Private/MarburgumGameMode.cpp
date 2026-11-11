#include "MarburgumGameMode.h"

#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "MarburgumCharacter.h"
#include "MarburgumCombatContract.h"
#include "MarburgumFighter.h"
#include "MarburgumGameState.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlayerController.h"
#include "MarburgumPlayerState.h"

AMarburgumGameMode::AMarburgumGameMode()
{
    // AGameModeBase exists on the authority only. Use replicated GameState/PlayerState for clients.
    GameStateClass = AMarburgumGameState::StaticClass();
    PlayerStateClass = AMarburgumPlayerState::StaticClass();
    PlayerControllerClass = AMarburgumPlayerController::StaticClass();
    DefaultPawnClass = AMarburgumCharacter::StaticClass();
}

AMarburgumPlayerState* AMarburgumGameMode::FindPlayerByCharacterKey(const FString& Key) const
{
    if (!GameState)
    {
        return nullptr;
    }
    for (APlayerState* State : GameState->PlayerArray)
    {
        AMarburgumPlayerState* Marburgum = Cast<AMarburgumPlayerState>(State);
        if (Marburgum && !Marburgum->GetCharacterKey().IsEmpty() && Marburgum->GetCharacterKey() == Key)
        {
            return Marburgum;
        }
    }
    return nullptr;
}

FString AMarburgumGameMode::CheckCharacterKey(const FString& Key) const
{
    if (Key.IsEmpty())
    {
        return FString();
    }
    if (!MarburgumII::Gameplay::IsValidCharacterKey(Key))
    {
        return TEXT("Invalid character key: use 3-40 characters a-z, 0-9, '-' or '_'.");
    }
    if (FindPlayerByCharacterKey(Key))
    {
        return TEXT("This character is already connected to the server.");
    }
    return FString();
}

void AMarburgumGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
    Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
    if (!ErrorMessage.IsEmpty())
    {
        return;
    }
    const FString Key = UGameplayStatics::ParseOption(Options, MarburgumII::Gameplay::CharacterOption);
    ErrorMessage = CheckCharacterKey(Key);
    if (!ErrorMessage.IsEmpty())
    {
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=admission_refused stage=prelogin character=%s reason=\"%s\""),
            MarburgumII::Gameplay::InteractLogPrefix, *Key.Left(40), *ErrorMessage);
    }
}

FString AMarburgumGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
    // The base class picks the start spot, so the requested tag must be known before it runs.
    const FString StartTag = UGameplayStatics::ParseOption(Options, MarburgumII::Gameplay::StartOption);
    if (NewPlayerController && !StartTag.IsEmpty() && StartTag.Len() <= 40)
    {
        RequestedStarts.Add(NewPlayerController, FName(*StartTag));
    }
    FString Error = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
    RequestedStarts.Remove(NewPlayerController);
    if (!Error.IsEmpty())
    {
        return Error;
    }
    // Checked again here: two pending logins with the same key can both pass PreLogin.
    const FString Key = UGameplayStatics::ParseOption(Options, MarburgumII::Gameplay::CharacterOption);
    Error = CheckCharacterKey(Key);
    if (!Error.IsEmpty())
    {
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=admission_refused stage=init character=%s reason=\"%s\""),
            MarburgumII::Gameplay::InteractLogPrefix, *Key.Left(40), *Error);
        return Error;
    }
    if (AMarburgumPlayerState* State = NewPlayerController ? NewPlayerController->GetPlayerState<AMarburgumPlayerState>() : nullptr)
    {
        State->SetCharacterKeyAuthority(Key);
    }
    return FString();
}

AActor* AMarburgumGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
    const FName* Requested = Player ? RequestedStarts.Find(Player) : nullptr;
    TArray<APlayerStart*> Tagged;
    TArray<APlayerStart*> Arrival;
    TArray<APlayerStart*> All;
    for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
    {
        All.Add(*It);
        if (Requested && It->PlayerStartTag == *Requested)
        {
            Tagged.Add(*It);
        }
        if (It->PlayerStartTag == MarburgumII::Gameplay::DefaultStartTag)
        {
            Arrival.Add(*It);
        }
    }
    const TArray<APlayerStart*>& Candidates = !Tagged.IsEmpty() ? Tagged : !Arrival.IsEmpty() ? Arrival : All;
    if (Candidates.IsEmpty())
    {
        return Super::ChoosePlayerStart_Implementation(Player);
    }
    // The first start without a pawn standing on it; level order keeps this deterministic.
    for (APlayerStart* Start : Candidates)
    {
        bool bOccupied = false;
        for (TActorIterator<APawn> Pawn(GetWorld()); Pawn && !bOccupied; ++Pawn)
        {
            bOccupied = FVector::DistSquared2D(Pawn->GetActorLocation(), Start->GetActorLocation()) < FMath::Square(100.f);
        }
        if (!bOccupied)
        {
            return Start;
        }
    }
    return Candidates[0];
}

void AMarburgumGameMode::Logout(AController* Exiting)
{
    RequestedStarts.Remove(Exiting);
    Super::Logout(Exiting);
}

void AMarburgumGameMode::StartPlay()
{
    Super::StartPlay();
    SpawnFighters();
}

void AMarburgumGameMode::SpawnFighters()
{
    using namespace MarburgumII::Combat;
    UWorld* World = GetWorld();
    // The cast belongs to the original's Lent; any other map (the front end, test maps) has nobody to fight.
    if (!World || !World->GetMapName().EndsWith(TEXT("lent")))
    {
        return;
    }
    constexpr float HalfHeight = 88.f;
    // Where to look for the ground the map has there: from a little above the original's height (under any
    // bridge or eave over the spot), then from high above, down to well below it.
    constexpr double GroundAboveCm[] = { 300.0, 1500.0 };
    constexpr double GroundBelowCm = 1500.0;
    // Where a test client starts: this far in front of the person, facing them.
    constexpr double StartAheadCm = 300.0;
    for (const FNpcDefinition& Npc : Npcs)
    {
        const FVector Original = GodotPositionToUnreal(Npc.GodotPosition);
        const double Yaw = GodotFacingToUnrealYaw(Npc.GodotFacing);
        FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumFighterGround), true);
        FHitResult Ground;
        bool bGround = false;
        for (const double Above : GroundAboveCm)
        {
            bGround = bGround
                || World->LineTraceSingleByChannel(Ground, Original + FVector(0.0, 0.0, Above), Original - FVector(0.0, 0.0, GroundBelowCm),
                    ECC_Visibility, Params);
        }
        if (!bGround)
        {
            UE_LOG(LogMarburgumNet, Warning, TEXT("%s role=server event=npc_skipped npc=%s reason=no_ground original=%s"), CombatLogPrefix, Npc.Id,
                *Original.ToString());
            continue;
        }
        const FVector At = Ground.ImpactPoint + FVector(0.0, 0.0, HalfHeight + 2.0);
        const FTransform Spot(FRotator(0.0, Yaw, 0.0), At);
        AMarburgumFighter* Fighter = World->SpawnActorDeferred<AMarburgumFighter>(AMarburgumFighter::StaticClass(), Spot, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
        if (!Fighter)
        {
            continue;
        }
        Fighter->NpcId = FName(Npc.Id);
        Fighter->FinishSpawning(Spot);

        const FVector Ahead = At + FRotator(0.0, Yaw, 0.0).Vector() * StartAheadCm;
        FHitResult StartGround;
        if (World->LineTraceSingleByChannel(StartGround, Ahead + FVector(0.0, 0.0, 300.0), Ahead - FVector(0.0, 0.0, 600.0), ECC_Visibility, Params))
        {
            FActorSpawnParameters StartParams;
            StartParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            const FVector StartAt = StartGround.ImpactPoint + FVector(0.0, 0.0, 96.0);
            if (APlayerStart* Start = World->SpawnActor<APlayerStart>(StartAt, (At - StartAt).GetSafeNormal2D().Rotation(), StartParams))
            {
                Start->PlayerStartTag = FName(*FString::Printf(TEXT("fight_%s"), Npc.Id));
            }
        }
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=npc_spawned npc=%s role_kind=%s at=%.0f,%.0f,%.0f yaw=%.1f original_z=%.0f"),
            CombatLogPrefix, Npc.Id, Npc.Role, At.X, At.Y, At.Z, Yaw, Original.Z);
    }
}
