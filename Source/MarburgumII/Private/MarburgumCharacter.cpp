#include "MarburgumCharacter.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"
#include "MarburgumCharacterMovement.h"
#include "MarburgumCombatContract.h"
#include "MarburgumFighter.h"
#include "MarburgumFightPresentation.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlayerController.h"
#include "MarburgumPlayerState.h"
#include "MarburgumStreetSubsystem.h"
#include "MarburgumVehicle.h"
#include "MarburgumVehicleContract.h"
#include "Net/UnrealNetwork.h"
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

    // The original's enter and exit clips are one-shots played at the door (tools/build_character.gd).
    const FName VehicleSlot(TEXT("DefaultSlot"));
    constexpr float VehicleClipBlendSeconds = 0.15f;
    // How quickly the camera arm and its pivot move between walking and driving framing.
    constexpr float CameraFramingSpeed = 4.f;
    constexpr float OnFootBoomHeightCm = 50.f;
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
    MeshRestRotation = Body->GetRelativeRotation();
    OnFootArmLength = CameraBoom->TargetArmLength;
    OnFootBoomOffset = CameraBoom->GetRelativeLocation();

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

    // The original game's enter and exit clips, generated onto the mannequin skeleton (ADR-039).
    static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> Enter(MarburgumII::Vehicles::EnterClipPath);
    if (Enter.Succeeded())
    {
        EnterVehicleClip = Enter.Object;
    }
    static ConstructorHelpers::FObjectFinder<UAnimSequenceBase> Exit(MarburgumII::Vehicles::ExitClipPath);
    if (Exit.Succeeded())
    {
        ExitVehicleClip = Exit.Object;
    }
}

void AMarburgumCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMarburgumCharacter, Seating);
    // The owner draws its own prediction from its movement component.
    DOREPLIFETIME_CONDITION(AMarburgumCharacter, FightPose, COND_SkipOwner);
}

void AMarburgumCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (HasAuthority())
    {
        FightPose = GetFightPose();
    }
    if (GetNetMode() != NM_DedicatedServer)
    {
        UpdateSwimPresentation();
        UpdateFightPresentation();
        const float Target = SwimMontage && !bTreading ? SwimLiftCm : 0.f;
        SwimLift = FMath::FInterpConstantTo(SwimLift, Target, DeltaSeconds, SwimLiftCmPerSecond);
        if (!Seating.Vehicle)
        {
            GetMesh()->SetRelativeLocation(MeshRestOffset + FVector(0.f, 0.f, SwimLift));
        }
        UpdateSeatPresentation(DeltaSeconds);
        if (IsLocallyControlled())
        {
            UpdateDrivingCamera(DeltaSeconds);
        }
    }
}

void AMarburgumCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // A body that leaves the world gives its seat back (the original's release_user).
    if (HasAuthority() && Seating.Vehicle)
    {
        Seating.Vehicle->ReleaseAuthority(GetPlayerState());
    }
    Super::EndPlay(EndPlayReason);
}

// ---- Vehicles ----

bool AMarburgumCharacter::IsDriving() const
{
    return Seating.Vehicle && Seating.SeatId == Seating.Vehicle->GetDriverSeatId();
}

bool AMarburgumCharacter::CanEnterVehicle() const
{
    const UMarburgumCharacterMovement* Movement = Cast<UMarburgumCharacterMovement>(GetCharacterMovement());
    return Movement && !Movement->IsDown();
}

bool AMarburgumCharacter::IsBodyShown() const
{
    return GetMesh() && GetMesh()->IsVisible();
}

void AMarburgumCharacter::SeatAuthority(AMarburgumVehicle* Vehicle, FName SeatId)
{
    const FMarburgumSeating Previous = Seating;
    Seating.Vehicle = Vehicle;
    Seating.SeatId = SeatId;
    ++Seating.Sequence;
    ApplySeating(Previous);
    ForceNetUpdate();
}

void AMarburgumCharacter::UnseatAuthority(const FVector& At, float Yaw)
{
    const FMarburgumSeating Previous = Seating;
    Seating.Vehicle = nullptr;
    Seating.SeatId = NAME_None;
    Seating.ExitLocation = At;
    Seating.ExitYaw = Yaw;
    ++Seating.Sequence;
    ApplySeating(Previous);
    ForceNetUpdate();
}

void AMarburgumCharacter::OnRep_Seating(const FMarburgumSeating& Previous)
{
    ApplySeating(Previous);
}

void AMarburgumCharacter::ApplySeating(const FMarburgumSeating& Previous)
{
    using namespace MarburgumII::Vehicles;
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    AMarburgumVehicle* Vehicle = Seating.Vehicle;
    const FVehicleDefinition* Definition = Vehicle ? Vehicle->GetDefinition() : nullptr;
    const FSeatDefinition* Seat = Definition ? FindSeat(*Definition, Seating.SeatId.ToString()) : nullptr;
    UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
    const bool bPresent = GetNetMode() != NM_DedicatedServer;
    // Walking prediction does not run in a seat. Moves saved before it, and the timestamps both ends hold,
    // are stale on either side of one: replayed after getting out, they put the owning client back where it
    // got in, and the server rejects them as expired.
    if (Previous.Vehicle != Vehicle)
    {
        if (GetLocalRole() == ROLE_AutonomousProxy)
        {
            Movement->ResetPredictionData_Client();
        }
        if (HasAuthority())
        {
            Movement->ResetPredictionData_Server();
        }
    }

    if (Vehicle && Seat)
    {
        // Still in the seat: the car carries the body; it neither walks nor collides.
        Movement->StopMovementImmediately();
        Movement->DisableMovement();
        Movement->SetComponentTickEnabled(false);
        SetActorEnableCollision(false);
        AttachToComponent(Vehicle->GetBody(), FAttachmentTransformRules::KeepWorldTransform);
        SetActorRelativeLocation(SeatPointCm(*Seat));
        SetActorRelativeRotation(FRotator::ZeroRotator);
        if (bPresent && Previous.Vehicle != Vehicle)
        {
            // The enter clip plays where the body stands at the door, facing the car; then it is hidden.
            const FVector Door = DoorPointCm(*Seat);
            const FVector SeatAt = SeatPointCm(*Seat);
            const FVector Offset(Door.X - SeatAt.X, Door.Y - SeatAt.Y, -RideHeightCm(*Definition) - SeatAt.Z);
            const float FaceYaw = (SeatAt - Door).GetSafeNormal2D().Rotation().Yaw;
            GetMesh()->SetRelativeLocationAndRotation(Offset, FRotator(0.f, FaceYaw + MeshRestRotation.Yaw, 0.f));
            GetMesh()->SetVisibility(true, true);
            EnterClipRemaining = 0.f;
            if (Anim && EnterVehicleClip)
            {
                Anim->StopAllMontages(VehicleClipBlendSeconds);
                Anim->PlaySlotAnimationAsDynamicMontage(EnterVehicleClip, VehicleSlot, VehicleClipBlendSeconds, VehicleClipBlendSeconds);
                EnterClipRemaining = EnterVehicleClip->GetPlayLength();
            }
            if (EnterClipRemaining <= 0.f)
            {
                GetMesh()->SetVisibility(false, true);
            }
        }
        return;
    }

    // On foot again (or never seated).
    const bool bWasSeated = Previous.Vehicle != nullptr || GetAttachParentActor() != nullptr;
    if (!bWasSeated)
    {
        return;
    }
    DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    StopJumping();
    SetActorEnableCollision(true);
    Movement->SetComponentTickEnabled(true);
    Movement->SetMovementMode(MOVE_Walking);
    if (!Seating.ExitLocation.IsZero())
    {
        SetActorLocationAndRotation(Seating.ExitLocation, FRotator(0.f, Seating.ExitYaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
    }
    EnterClipRemaining = 0.f;
    if (bPresent)
    {
        GetMesh()->SetRelativeLocationAndRotation(MeshRestOffset, MeshRestRotation);
        GetMesh()->SetVisibility(true, true);
        if (Anim && ExitVehicleClip)
        {
            Anim->StopAllMontages(VehicleClipBlendSeconds);
            Anim->PlaySlotAnimationAsDynamicMontage(ExitVehicleClip, VehicleSlot, VehicleClipBlendSeconds, VehicleClipBlendSeconds);
        }
    }
}

void AMarburgumCharacter::UpdateSeatPresentation(float DeltaSeconds)
{
    if (!Seating.Vehicle || EnterClipRemaining <= 0.f)
    {
        return;
    }
    EnterClipRemaining -= DeltaSeconds;
    if (EnterClipRemaining <= 0.f)
    {
        // In and the door shut behind: the original hid the body in the seat.
        GetMesh()->SetVisibility(false, true);
    }
}

void AMarburgumCharacter::UpdateDrivingCamera(float DeltaSeconds)
{
    using namespace MarburgumII::Vehicles;
    const AMarburgumVehicle* Vehicle = Seating.Vehicle;
    const FVehicleDefinition* Definition = Vehicle ? Vehicle->GetDefinition() : nullptr;
    const FSeatDefinition* Seat = Definition ? FindSeat(*Definition, Seating.SeatId.ToString()) : nullptr;

    // Driving framing (player_camera.cpp): 11 m back, the pivot a metre above the car's origin and led
    // ahead of the car a little at speed. On foot it returns to the walking framing exactly.
    float TargetArm = OnFootArmLength;
    FVector TargetBoom = OnFootBoomOffset;
    if (Seat)
    {
        TargetArm = static_cast<float>(CameraDistance * CentimetresPerMetre);
        const double Ahead = FMath::Clamp(Vehicle->GetForwardSpeed() * CameraLookAheadSeconds, -CameraMaxLookAhead, CameraMaxLookAhead);
        // The original's origin is CollisionLift below this car's actor origin; the seat is where this body is.
        TargetBoom = FVector(Ahead * CentimetresPerMetre, 0.0, (CameraFocusHeight - CollisionLift) * CentimetresPerMetre - SeatPointCm(*Seat).Z);
    }
    CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, TargetArm, DeltaSeconds, CameraFramingSpeed);
    CameraBoom->SetRelativeLocation(FMath::VInterpTo(CameraBoom->GetRelativeLocation(), TargetBoom, DeltaSeconds, CameraFramingSpeed));

    AController* Owner = GetController();
    FreeLookRemaining = FMath::Max(0.f, FreeLookRemaining - DeltaSeconds);
    if (!Seat || !Owner || FreeLookRemaining > 0.f)
    {
        return;
    }
    // Swing back round behind the car, and down to the driving pitch.
    const float Weight = 1.f - FMath::Exp(-static_cast<float>(CameraHeadingSharpness) * DeltaSeconds);
    const FRotator Current = Owner->GetControlRotation();
    const FRotator Wanted(-static_cast<float>(CameraPitchDegrees), Vehicle->GetActorRotation().Yaw, 0.f);
    const FRotator Delta = (Wanted - Current).GetNormalized();
    Owner->SetControlRotation(FRotator(Current.Pitch + Delta.Pitch * Weight, Current.Yaw + Delta.Yaw * Weight, 0.f));
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
    // The original's attack, heavy_attack and guard keys (project.godot).
    PlayerInputComponent->BindKey(EKeys::F, IE_Pressed, this, &AMarburgumCharacter::Jab);
    PlayerInputComponent->BindKey(EKeys::R, IE_Pressed, this, &AMarburgumCharacter::HeavySwing);
    PlayerInputComponent->BindKey(EKeys::G, IE_Pressed, this, &AMarburgumCharacter::StartGuard);
    PlayerInputComponent->BindKey(EKeys::G, IE_Released, this, &AMarburgumCharacter::StopGuard);
}

// ---- Fighting ----

void AMarburgumCharacter::Jab()
{
    UMarburgumCharacterMovement* Movement = Cast<UMarburgumCharacterMovement>(GetCharacterMovement());
    // In a car the keys mean nothing; the original refuses a swing from a seat before asking the server.
    if (Movement && !Seating.Vehicle)
    {
        Movement->RequestSwing(EMarburgumSwing::Jab);
    }
}

void AMarburgumCharacter::HeavySwing()
{
    UMarburgumCharacterMovement* Movement = Cast<UMarburgumCharacterMovement>(GetCharacterMovement());
    if (Movement && !Seating.Vehicle)
    {
        Movement->RequestSwing(EMarburgumSwing::Heavy);
    }
}

void AMarburgumCharacter::StartGuard()
{
    if (UMarburgumCharacterMovement* Movement = Cast<UMarburgumCharacterMovement>(GetCharacterMovement()))
    {
        Movement->SetWantsToGuard(true);
    }
}

void AMarburgumCharacter::StopGuard()
{
    if (UMarburgumCharacterMovement* Movement = Cast<UMarburgumCharacterMovement>(GetCharacterMovement()))
    {
        Movement->SetWantsToGuard(false);
    }
}

void AMarburgumCharacter::ResolveSwingAuthority(bool bHeavy, const TCHAR* Refusal)
{
    using namespace MarburgumII::Combat;
    FMarburgumCombatResult Result;
    Result.bHeavy = bHeavy;
    const AMarburgumPlayerState* State = GetPlayerState<AMarburgumPlayerState>();
    if (!Refusal && State && State->GetCharacterKey().IsEmpty())
    {
        // A connection without a character still moves, but it has nobody to fight with.
        Refusal = ErrorNoCharacter;
    }
    if (Refusal)
    {
        Result.Reason = Refusal;
    }
    else
    {
        Result.bOk = true;
        // melee_target(): the nearest standing fighter within reach and no more than the arc off the nose.
        AMarburgumFighter* Target = nullptr;
        double Best = FistReachCm;
        for (TActorIterator<AMarburgumFighter> It(GetWorld()); It; ++It)
        {
            double Distance = 0.0;
            if (!It->IsDown() && InSwingReach(GetActorLocation(), GetActorRotation().Yaw, It->GetActorLocation(), Distance)
                && Distance <= Best)
            {
                Best = Distance;
                Target = *It;
            }
        }
        if (!Target)
        {
            Result.Outcome = OutcomeMiss;
        }
        else
        {
            Result.Damage = SwingDamage(bHeavy);
            Result.Outcome = Target->ReceiveBlowAuthority(this, Result.Damage, SwingStaggerSeconds(bHeavy), bHeavy);
            Result.TargetId = Target->GetNpcId();
            Result.TargetName = Target->GetDisplayName();
            Result.TargetVitality = Target->GetVitalityPoints();
        }
        // A punch thrown in the street is a fight to the people in it, landed or not (ADR-044).
        if (UMarburgumStreetSubsystem* Street = GetWorld()->GetSubsystem<UMarburgumStreetSubsystem>())
        {
            Street->ReportFight(this, GetActorLocation(), true);
        }
    }
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=swing player=%d character=%s heavy=%d ok=%d reason=%s outcome=%s target=%s damage=%.1f target_vitality=%d"),
        CombatLogPrefix, State ? State->GetPlayerId() : -1, State && !State->GetCharacterKey().IsEmpty() ? *State->GetCharacterKey() : TEXT("-"),
        bHeavy ? 1 : 0, Result.bOk ? 1 : 0, WireWord(Result.Reason),
        WireWord(Result.Outcome), Result.TargetId.IsNone() ? TEXT("-") : *Result.TargetId.ToString(),
        Result.Damage, Result.TargetVitality);
    if (AMarburgumPlayerController* Owner = Cast<AMarburgumPlayerController>(GetController()))
    {
        Owner->ClientReceiveCombatResult(Result);
    }
}

void AMarburgumCharacter::MulticastImpact_Implementation(FVector_NetQuantize At, FName Outcome, bool bHeavy, bool bFloored)
{
    MarburgumFightSound::PlayImpact(GetWorld(), At, Outcome, bHeavy, bFloored);
}

FMarburgumFightPose AMarburgumCharacter::GetFightPose() const
{
    const UMarburgumCharacterMovement* Movement = Cast<UMarburgumCharacterMovement>(GetCharacterMovement());
    if (!Movement || (!HasAuthority() && !IsLocallyControlled()))
    {
        return FightPose;
    }
    FMarburgumFightPose Pose;
    Pose.SwingSeq = Movement->GetSwingSeq();
    Pose.bHeavy = Movement->WasLastSwingHeavy();
    Pose.bGuarding = Movement->IsGuarding();
    Pose.bDown = Movement->IsDown();
    return Pose;
}

void AMarburgumCharacter::UpdateFightPresentation()
{
    UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
    if (Seating.Vehicle)
    {
        FightAnimator.Stop(Anim);
        return;
    }
    // A body that drowned is the swim clip's until the server takes it out of the water.
    const bool bSwimming = GetCharacterMovement()->IsSwimming();
    FMarburgumFightPose Pose = GetFightPose();
    Pose.bDown = Pose.bDown && !bSwimming;
    FightAnimator.Update(Anim, Pose, GetVelocity().Size2D(), !bSwimming);
}

FName AMarburgumCharacter::GetPlayingFightClip() const
{
    return FightAnimator.GetPlaying(GetMesh() ? GetMesh()->GetAnimInstance() : nullptr);
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
    // In a car the same keys are its pedals and wheel, which the player controller reads.
    if (Value != 0.f && Controller && !Seating.Vehicle)
    {
        const FRotator Yaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
        AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Value);
    }
}

void AMarburgumCharacter::MoveRight(float Value)
{
    if (Value != 0.f && Controller && !Seating.Vehicle)
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
    if (Seating.Vehicle && FMath::Abs(Value) > 0.001f)
    {
        FreeLookRemaining = static_cast<float>(MarburgumII::Vehicles::CameraFreeLookSeconds);
    }
    AddControllerYawInput(Value);
}

void AMarburgumCharacter::LookUp(float Value)
{
    if (Seating.Vehicle && FMath::Abs(Value) > 0.001f)
    {
        FreeLookRemaining = static_cast<float>(MarburgumII::Vehicles::CameraFreeLookSeconds);
    }
    AddControllerPitchInput(-Value);
}
