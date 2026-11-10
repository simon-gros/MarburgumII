#include "MarburgumGameMode.h"

#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "MarburgumCharacter.h"
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
