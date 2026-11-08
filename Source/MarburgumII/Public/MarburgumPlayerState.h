#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "MarburgumPlayerState.generated.h"

/** Network-visible per-player state foundation. Account identity and durable saves are not implemented. */
UCLASS()
class MARBURGUMII_API AMarburgumPlayerState : public APlayerState
{
    GENERATED_BODY()
};
