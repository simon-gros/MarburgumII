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
    // Turning from the stroke to treading water and back: the body swings between lying and upright.
    constexpr float SwimPoseBlendSeconds = 0.45f;
    // Enough loops of either cycle for any swim; the clip is restarted if it ever runs out.
    constexpr int32 SwimLoops = 100000;
    // The stroke lies along the water (ADR-033): the pelvis 96 cm and the head joint 115 cm above the
    // feet, against a surface 140 cm up while the swimmer steers. The body is drawn this much higher
    // while it strokes, so the back is just under the surface and the face is out. Treading water is
    // upright and the mouth is where the movement's mouth rule (MouthHeightCm) has it, so it is not
    // lifted (ADR-036). The rule itself does not move.
    constexpr float SwimLiftCm = 32.f;
    // Horizontal speed at which a swimmer starts to stroke, and under which it goes back to treading
    // water. The gap keeps a swimmer that is slowing down from flicking between the two.
    constexpr float StrokeFromCmPerSecond = 40.f;
    constexpr float TreadUnderCmPerSecond = 20.f;
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
    // Treading water, upright, when the swimmer is not going anywhere (ADR-036).
    static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> Tread(TEXT("/Game/Characters/Animations/A_SwimTread.A_SwimTread"));
    if (Tread.Succeeded())
    {
        TreadClip = Tread.Object;
    }
}

void AMarburgumCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (GetNetMode() != NM_DedicatedServer)
    {
        UpdateSwimPresentation();
        const float Target = SwimMontage && !bTreading ? SwimLiftCm : 0.f;
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
            bTreading = false;
        }
        return;
    }
    // The visible speed decides, so a remote body chooses as its owner's does.
    const float Speed = GetVelocity().Size2D();
    const bool bWasPlaying = SwimMontage && Anim->Montage_IsPlaying(SwimMontage);
    const float Threshold = bWasPlaying && !bTreading ? TreadUnderCmPerSecond : StrokeFromCmPerSecond;
    const bool bTread = TreadClip && Speed < Threshold;
    UAnimSequenceBase* Clip = bTread ? TreadClip.Get() : SwimClip.Get();
    if (!bWasPlaying || bTread != bTreading)
    {
        // A new montage in the slot blends the playing one out as it blends in.
        const float BlendIn = bWasPlaying ? SwimPoseBlendSeconds : SwimBlendSeconds;
        SwimMontage = Anim->PlaySlotAnimationAsDynamicMontage(Clip, SwimSlot, BlendIn, SwimBlendSeconds, 1.f, SwimLoops);
        bTreading = SwimMontage && Clip == TreadClip;
    }
    if (SwimMontage && !bTreading)
    {
        // As the original paced the stroke: by the visible speed.
        const float Pace = FMath::Max(Speed, 15.f);
        Anim->Montage_SetPlayRate(SwimMontage, FMath::Clamp(Pace / MarburgumII::Gameplay::SwimSpeedCm, 0.75f, 1.4f));
    }
}

bool AMarburgumCharacter::IsSwimClipPlaying() const
{
    const UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
    return Anim && SwimMontage && Anim->Montage_IsPlaying(SwimMontage);
}

bool AMarburgumCharacter::IsTreadingWater() const
{
    return bTreading && IsSwimClipPlaying();
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
