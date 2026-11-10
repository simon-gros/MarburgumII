#include "MarburgumGameMode.h"

#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
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
    FString Error = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
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
