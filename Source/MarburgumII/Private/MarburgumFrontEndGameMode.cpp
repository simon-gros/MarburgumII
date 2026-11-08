#include "MarburgumFrontEndGameMode.h"

#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"

AMarburgumFrontEndGameMode::AMarburgumFrontEndGameMode()
{
    // Deliberately the engine base classes: no Marburgum GameState, request channel or pawn exists
    // offline, so no client-side code path can mistake the front-end for an authoritative world.
    DefaultPawnClass = nullptr;
    GameStateClass = AGameStateBase::StaticClass();
    PlayerControllerClass = APlayerController::StaticClass();
    bStartPlayersAsSpectators = true;
}
