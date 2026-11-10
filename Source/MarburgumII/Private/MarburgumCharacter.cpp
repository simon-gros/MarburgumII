#include "MarburgumCharacter.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"
#include "MarburgumCharacterMovement.h"
#include "MarburgumGameplayContract.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    constexpr float CapsuleHalfHeight = 88.f;

    // The swim cycle is played through the template animation blueprint's montage slot.
    const FName SwimSlot(TEXT("DefaultSlot"));
    constexpr float SwimBlendSeconds = 0.25f;
    // Enough loops of the 1.2 s cycle for any swim; the clip is restarted if it ever runs out.
    constexpr int32 SwimLoops = 100000;
    // The swim pose leans the chest onto the water, which puts the head about 17 cm under where the
    // original drew the surface. The body is drawn this much higher while it swims, so the face is
    // out; the mouth rule (MouthHeightCm) is the movement's and does not move.
    constexpr float SwimLiftCm = 20.f;
    constexpr float SwimLiftCmPerSecond = 60.f;
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
    MeshRestOffset = Body->GetRelativeLocation();

    // The original game's swim cycle, generated onto the mannequin skeleton (ADR-032).
    static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> Swim(TEXT("/Game/Characters/Animations/A_Swim.A_Swim"));
    if (Swim.Succeeded())
    {
        SwimClip = Swim.Object;
    }
}

void AMarburgumCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (GetNetMode() != NM_DedicatedServer)
    {
        UpdateSwimPresentation();
        const float Target = SwimMontage ? SwimLiftCm : 0.f;
        SwimLift = FMath::FInterpConstantTo(SwimLift, Target, DeltaSeconds, SwimLiftCmPerSecond);
        GetMesh()->SetRelativeLocation(MeshRestOffset + FVector(0.f, 0.f, SwimLift));
    }
}

void AMarburgumCharacter::UpdateSwimPresentation()
{
    UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
    if (!Anim || !SwimClip)
    {
        return;
    }
    const bool bSwimming = GetCharacterMovement()->IsSwimming();
    if (!bSwimming)
    {
        if (SwimMontage)
        {
            Anim->Montage_Stop(SwimBlendSeconds, SwimMontage);
            SwimMontage = nullptr;
        }
        return;
    }
    if (!SwimMontage || !Anim->Montage_IsPlaying(SwimMontage))
    {
        SwimMontage = Anim->PlaySlotAnimationAsDynamicMontage(SwimClip, SwimSlot, SwimBlendSeconds, SwimBlendSeconds, 1.f, SwimLoops);
    }
    if (SwimMontage)
    {
        // As the original paced the clip: by the visible speed, or at the stroke's own pace when
        // treading water.
        const float Speed = GetVelocity().Size2D();
        const float Pace = Speed > 15.f ? Speed : MarburgumII::Gameplay::SwimSpeedCm;
        Anim->Montage_SetPlayRate(SwimMontage, FMath::Clamp(Pace / MarburgumII::Gameplay::SwimSpeedCm, 0.75f, 1.4f));
    }
}

bool AMarburgumCharacter::IsSwimClipPlaying() const
{
    const UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
    return Anim && SwimMontage && Anim->Montage_IsPlaying(SwimMontage);
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
