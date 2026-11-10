#include "MarburgumCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputCoreTypes.h"
#include "UObject/ConstructorHelpers.h"

AMarburgumCharacter::AMarburgumCharacter()
{
    GetCapsuleComponent()->InitCapsuleSize(34.f, 88.f);
    bUseControllerRotationYaw = true;
    GetCharacterMovement()->MaxWalkSpeed = 450.f;

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(GetCapsuleComponent());
    Camera->SetRelativeLocation(FVector(0.f, 0.f, 64.f));
    Camera->bUsePawnControlRotation = true;

    // Placeholder body so other players can see where someone stands; hidden from its own camera.
    Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
    Body->SetupAttachment(GetCapsuleComponent());
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    if (Cylinder.Succeeded())
    {
        Body->SetStaticMesh(Cylinder.Object);
    }
    Body->SetRelativeScale3D(FVector(0.6f, 0.6f, 1.76f));
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetOwnerNoSee(true);
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
