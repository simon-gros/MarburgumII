#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MarburgumCharacter.generated.h"

class UCameraComponent;
class UStaticMeshComponent;

/**
 * Minimal first-person test character for the Stage 1 interaction loop. Movement uses the engine's
 * networked CharacterMovementComponent, so the server knows the real pawn position it validates
 * interaction range against; UMarburgumCharacterMovement reports clients whose move timestamps run
 * ahead of server time (ADR-019). The body is a placeholder primitive; no character art exists yet.
 */
UCLASS()
class MARBURGUMII_API AMarburgumCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    explicit AMarburgumCharacter(const FObjectInitializer& ObjectInitializer);

    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
    void MoveForward(float Value);
    void MoveRight(float Value);
    void MoveBackward(float Value);
    void MoveLeft(float Value);
    void Turn(float Value);
    void LookUp(float Value);

    UPROPERTY(VisibleAnywhere, Category = "Marburgum|Character")
    TObjectPtr<UCameraComponent> Camera;

    UPROPERTY(VisibleAnywhere, Category = "Marburgum|Character")
    TObjectPtr<UStaticMeshComponent> Body;
};
