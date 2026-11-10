#include "MarburgumCharacter.h"

#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"
#include "MarburgumCharacterMovement.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    constexpr float CapsuleHalfHeight = 88.f;
}

AMarburgumCharacter::AMarburgumCharacter(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<UMarburgumCharacterMovement>(ACharacter::CharacterMovementComponentName))
{
    GetCapsuleComponent()->InitCapsuleSize(34.f, CapsuleHalfHeight);

    // Third person (ADR-023): the mouse turns the camera, and the body turns toward the way it moves.
    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw = false;
    bUseControllerRotationRoll = false;
    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->RotationRate = FRotator(0.f, 540.f, 0.f);

    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(GetCapsuleComponent());
    CameraBoom->SetRelativeLocation(FVector(0.f, 0.f, 50.f));
    CameraBoom->TargetArmLength = 350.f;
    CameraBoom->bUsePawnControlRotation = true;

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
    Camera->bUsePawnControlRotation = false;

    // Protagonist prototype (ADR-021). The mannequin's pivot is at its feet and it faces +Y.
    USkeletalMeshComponent* Body = GetMesh();
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> Protagonist(
        TEXT("/Game/Characters/Protagonist/SKM_Protagonist.SKM_Protagonist"));
    if (Protagonist.Succeeded())
    {
        Body->SetSkeletalMesh(Protagonist.Object);
    }
    static ConstructorHelpers::FClassFinder<UAnimInstance> Unarmed(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"));
    if (Unarmed.Succeeded())
    {
        Body->SetAnimationMode(EAnimationMode::AnimationBlueprint);
        Body->SetAnimInstanceClass(Unarmed.Class);
    }
    Body->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -CapsuleHalfHeight), FRotator(0.f, -90.f, 0.f));
    // Nothing reads bone poses on the server and no root motion is used, so a body nobody sees
    // (every body on a dedicated server) skips its animation.
    Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
}

void AMarburgumCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    // Direct key bindings until an input-mapping/rebinding design exists (UI-02 input work).
    PlayerInputComponent->BindAxisKey(EKeys::W, this, &AMarburgumCharacter::MoveForward);
    PlayerInputComponent->BindAxisKey(EKeys::S, this, &AMarburgumCharacter::MoveBackward);
    PlayerInputComponent->BindAxisKey(EKeys::D, this, &AMarburgumCharacter::MoveRight);
    PlayerInputComponent->BindAxisKey(EKeys::A, this, &AMarburgumCharacter::MoveLeft);
    PlayerInputComponent->BindAxisKey(EKeys::MouseX, this, &AMarburgumCharacter::Turn);
    PlayerInputComponent->BindAxisKey(EKeys::MouseY, this, &AMarburgumCharacter::LookUp);
    PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &ACharacter::Jump);
    PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Released, this, &ACharacter::StopJumping);
    PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Pressed, this, &AMarburgumCharacter::StartRunning);
    PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Released, this, &AMarburgumCharacter::StopRunning);
}

void AMarburgumCharacter::StartRunning()
{
    if (UMarburgumCharacterMovement* Movement = Cast<UMarburgumCharacterMovement>(GetCharacterMovement()))
    {
        Movement->SetWantsToRun(true);
    }
}

void AMarburgumCharacter::StopRunning()
{
    if (UMarburgumCharacterMovement* Movement = Cast<UMarburgumCharacterMovement>(GetCharacterMovement()))
    {
        Movement->SetWantsToRun(false);
    }
}

void AMarburgumCharacter::MoveForward(float Value)
{
    if (Value != 0.f && Controller)
    {
        const FRotator Yaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
        AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Value);
    }
}

void AMarburgumCharacter::MoveRight(float Value)
{
    if (Value != 0.f && Controller)
    {
        const FRotator Yaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
        AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Value);
    }
}

void AMarburgumCharacter::MoveBackward(float Value)
{
    MoveForward(-Value);
}

void AMarburgumCharacter::MoveLeft(float Value)
{
    MoveRight(-Value);
}

void AMarburgumCharacter::Turn(float Value)
{
    AddControllerYawInput(Value);
}

void AMarburgumCharacter::LookUp(float Value)
{
    AddControllerPitchInput(-Value);
}
