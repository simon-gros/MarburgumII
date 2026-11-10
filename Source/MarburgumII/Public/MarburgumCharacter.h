#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MarburgumCharacter.generated.h"

class UAnimMontage;
class UAnimSequenceBase;
class UCameraComponent;
class USpringArmComponent;

/**
 * Third-person player character for the Stage 1 interaction loop (ADR-023). Movement uses the engine's
 * networked CharacterMovementComponent, so the server knows the real pawn position it validates
 * interaction range against. UMarburgumCharacterMovement adds running (hold Shift) with the original
 * breath rules (ADR-020) and reports clients whose move timestamps run ahead of server time (ADR-019).
 * The body is the protagonist prototype on the mannequin skeleton, animated by the template's
 * ABP_Unarmed (ADR-021); the camera orbits on a spring arm and the body turns to face its movement.
 * In the water the body plays the original game's swim cycle, A_Swim, through the template's DefaultSlot
 * (ADR-032); that is presentation only and never runs on a dedicated server.
 */
UCLASS()
class MARBURGUMII_API AMarburgumCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    explicit AMarburgumCharacter(const FObjectInitializer& ObjectInitializer);

    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual void Tick(float DeltaSeconds) override;

    /** Presentation evidence: the swim clip is playing on this body. */
    bool IsSwimClipPlaying() const;

private:
    void MoveForward(float Value);
    void MoveRight(float Value);
    void MoveBackward(float Value);
    void MoveLeft(float Value);
    void Turn(float Value);
    void LookUp(float Value);
    void StartRunning();
    void StopRunning();

    UPROPERTY(VisibleAnywhere, Category = "Marburgum|Character")
    TObjectPtr<USpringArmComponent> CameraBoom;

    UPROPERTY(VisibleAnywhere, Category = "Marburgum|Character")
    TObjectPtr<UCameraComponent> Camera;

    void UpdateSwimPresentation();

    UPROPERTY()
    TObjectPtr<UAnimSequenceBase> SwimClip;

    UPROPERTY(Transient)
    TObjectPtr<UAnimMontage> SwimMontage;

    FVector MeshRestOffset = FVector::ZeroVector;
    float SwimLift = 0.f;
};
