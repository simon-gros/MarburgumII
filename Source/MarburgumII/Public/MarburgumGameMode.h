#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MarburgumGameMode.generated.h"

/** Server-authoritative entry point, also used by Editor/standalone development. */
UCLASS()
class MARBURGUMII_API AMarburgumGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AMarburgumGameMode();
};
