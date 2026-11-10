#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MarburgumCharacterMovement.generated.h"

/**
 * The engine's networked character movement, plus server evidence when a client's move timestamps run
 * ahead of server time (ADR-019). Detection and resolution themselves are the engine's, configured in
 * [/Script/Engine.GameNetworkManager]; this class only reports them, at most once a second per
 * character, as `MB2INTERACT role=server event=time_discrepancy`.
 */
UCLASS()
class MARBURGUMII_API UMarburgumCharacterMovement : public UCharacterMovementComponent
{
    GENERATED_BODY()

public:
    virtual void OnTimeDiscrepancyDetected(float CurrentTimeDiscrepancy, float LifetimeRawTimeDiscrepancy, float Lifetime,
        float CurrentMoveError) override;

private:
    int32 Detections = 0;
    int32 UnreportedDetections = 0;
    double LastReportSeconds = -1.0;
};
