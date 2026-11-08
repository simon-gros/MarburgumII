#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "MarburgumGameState.generated.h"

/** Server-authored, replicated connection contract marker for the first networking smoke test. */
UCLASS()
class MARBURGUMII_API AMarburgumGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    AMarburgumGameState();

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Marburgum|Network")
    int32 NetworkContractVersion = 0;

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
