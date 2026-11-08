#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MarburgumFrontEndGameMode.generated.h"

/**
 * Game mode of the client front-end map (/Game/Maps/FrontEnd).
 *
 * Owns no gameplay rules, spawns no pawn and commits nothing: a client that is not connected to an
 * authoritative server sits here instead of simulating the game world locally.
 */
UCLASS()
class MARBURGUMII_API AMarburgumFrontEndGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AMarburgumFrontEndGameMode();
};
