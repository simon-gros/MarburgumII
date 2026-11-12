#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MarburgumCombatTypes.h"
#include "MarburgumCharacter.generated.h"

class AMarburgumVehicle;
class UAnimMontage;
class UAnimSequenceBase;
class UCameraComponent;
class USpringArmComponent;

/** Which seat of which car this character sits in, and where it last stepped out (ADR-039). */
USTRUCT()
struct FMarburgumSeating
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<AMarburgumVehicle> Vehicle = nullptr;

    UPROPERTY()
    FName SeatId;

    /** Where the server put the body when it got out, so the owning client lands on the same spot. */
    UPROPERTY()
    FVector_NetQuantize10 ExitLocation = FVector::ZeroVector;

    UPROPERTY()
    float ExitYaw = 0.f;

    /** Changes on every seat change, so two identical states in a row still replicate. */
    UPROPERTY()
    uint8 Sequence = 0;
};

/**
 * Third-person player character for the Stage 1 interaction loop (ADR-023). Movement uses the engine's
 * networked CharacterMovementComponent, so the server knows the real pawn position it validates
 * interaction range against. UMarburgumCharacterMovement adds running (hold Shift) with the original
 * breath rules (ADR-020) and reports clients whose move timestamps run ahead of server time (ADR-019).
 * The body is the protagonist prototype on the mannequin skeleton, animated by the template's
 * ABP_Unarmed (ADR-021); the camera orbits on a spring arm and the body turns to face its movement.
 * In the water the body plays a swim clip through the template's DefaultSlot: the stroke, A_Swim
 * (ADR-032, ADR-033), lying along the water while it moves, and A_SwimTread, upright, while it does not
 * (ADR-036). That is presentation only and never runs on a dedicated server.
 *
 * In a car (ADR-039) the body rides in its seat: the server attaches it to the vehicle, turns its own
 * movement and collision off, and replicates the seating; every machine applies the same state. The body
 * plays the original's enter clip at the door, then is hidden in the seat as the original did, and plays the
 * exit clip where it steps out. The camera pulls back behind the car and swings round to follow it unless
 * the player has looked around in the last few seconds.
 *
 * Fighting (ADR-041): F throws a jab, R a committed swing and G holds a guard, as in the original. The
 * movement component carries them to the server, which rules each swing here: the nearest fighting NPC within
 * reach and arc in front of the body takes the blow (players never fight players), the player is told how it
 * went, and everyone nearby hears it. Other machines see swings, the guard and a body on the floor through a
 * replicated fight pose; the owner draws its own prediction.
 */
UCLASS()
class MARBURGUMII_API AMarburgumCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    explicit AMarburgumCharacter(const FObjectInitializer& ObjectInitializer);

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void OnPlayerStateChanged(APlayerState* NewPlayerState, APlayerState* OldPlayerState) override;

    /** Presentation evidence: the roster model this body wears (ADR-055); None until a signed-in character has one. */
    FName GetAppliedModel() const { return AppliedModel; }

    /** Presentation evidence: a swim clip (the stroke or treading water) is playing on this body. */
    bool IsSwimClipPlaying() const;

    /** Presentation evidence: the playing swim clip is treading water, upright, not the stroke. */
    bool IsTreadingWater() const;

    // ---- Vehicles (ADR-039) ----

    AMarburgumVehicle* GetSeatedVehicle() const { return Seating.Vehicle; }
    FName GetSeatId() const { return Seating.Vehicle ? Seating.SeatId : NAME_None; }
    /** In the driver's seat of a car. */
    bool IsDriving() const;

    /** A body that can move can get into a car; a drowned one cannot. */
    bool CanEnterVehicle() const;

    /** Server only (AMarburgumVehicle decides): sit in a seat, or step out at a spot. */
    void SeatAuthority(AMarburgumVehicle* Vehicle, FName SeatId);
    void UnseatAuthority(const FVector& At, float Yaw);

    /** Presentation evidence: the body is drawn (false while hidden in a seat). */
    bool IsBodyShown() const;

    // ---- Fighting (ADR-041) ----

    /**
     * Server only, from the movement component: this move threw a swing (Refusal null) or asked for one the
     * rules refused. Picks who it reaches, applies the blow and tells the player.
     */
    void ResolveSwingAuthority(bool bHeavy, const TCHAR* Refusal);

    /** Everyone: a blow landed on (or was blocked by) this body; play its sound here. */
    UFUNCTION(NetMulticast, Unreliable)
    void MulticastImpact(FVector_NetQuantize At, FName Outcome, bool bHeavy, bool bFloored);

    /** What other machines are told about this body's fighting; the owner reads its movement instead. */
    FMarburgumFightPose GetFightPose() const;

    /** Presentation evidence: the fight clip this body is playing (guard, jab, heavy, down), or None. */
    FName GetPlayingFightClip() const;

    /** Owning client, and the combat smoke: throw a jab or a committed swing, hold or drop the guard. */
    void Jab();
    void HeavySwing();
    void StartGuard();
    void StopGuard();

private:
    /** Wear the body of the signed-in character's chosen model, if it has one (ADR-055). */
    void ApplyModel();
    void HandleIdentityChanged(class AMarburgumPlayerState* State);

    /** The roster's bodies, referenced so they cook; index matches MarburgumII::Accounts::CharacterModels. */
    UPROPERTY()
    TArray<TObjectPtr<USkeletalMesh>> ModelMeshes;
    FName AppliedModel;
    FDelegateHandle IdentityHandle;

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
    void UpdateFightPresentation();

    UPROPERTY(Replicated)
    FMarburgumFightPose FightPose;

    /** Owns the fight montages on this machine's body; idle on a dedicated server. */
    FMarburgumFightAnimator FightAnimator;

    UPROPERTY()
    TObjectPtr<UAnimSequenceBase> SwimClip;

    UPROPERTY()
    TObjectPtr<UAnimSequenceBase> TreadClip;

    UPROPERTY(Transient)
    TObjectPtr<UAnimMontage> SwimMontage;

    FVector MeshRestOffset = FVector::ZeroVector;
    FRotator MeshRestRotation = FRotator::ZeroRotator;
    float SwimLift = 0.f;
    // The clip SwimMontage plays: treading water rather than the stroke.
    bool bTreading = false;

    // ---- Vehicles ----

    UFUNCTION()
    void OnRep_Seating(const FMarburgumSeating& Previous);

    /** Make this machine's body match Seating: attached and still in a seat, or free on its feet. */
    void ApplySeating(const FMarburgumSeating& Previous);
    void UpdateDrivingCamera(float DeltaSeconds);
    void UpdateSeatPresentation(float DeltaSeconds);

    UPROPERTY(ReplicatedUsing = OnRep_Seating)
    FMarburgumSeating Seating;

    UPROPERTY()
    TObjectPtr<UAnimSequenceBase> EnterVehicleClip;

    UPROPERTY()
    TObjectPtr<UAnimSequenceBase> ExitVehicleClip;

    /** Seconds left of the enter clip; the body is hidden in the seat when it runs out. */
    float EnterClipRemaining = 0.f;
    /** Seconds the player has left the driving camera alone after looking around. */
    float FreeLookRemaining = 0.f;
    float OnFootArmLength = 350.f;
    FVector OnFootBoomOffset = FVector::ZeroVector;
};
