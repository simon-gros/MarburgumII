#include "MarburgumFootsteps.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "MarburgumGameState.h"
#include "MarburgumGround.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumFootsteps, Log, All);

namespace
{
    namespace Surface = MarburgumII::Surface;

    /** Left and right foot under the UE mannequin's names, then a generic rig's. */
    const FName FeetBones[2][2] = { { TEXT("foot_l"), TEXT("LeftFoot") }, { TEXT("foot_r"), TEXT("RightFoot") } };
    /** A body never steps faster than this, whatever its feet seem to do (a fast stride is about 0.3 s per foot). */
    constexpr double MinimumStepSeconds = 0.16;
    /** The ground under a landing foot is looked for this far above and below it. */
    constexpr double TraceUpCm = 40.0;
    constexpr double TraceDownCm = 80.0;
    constexpr double PruneEverySeconds = 5.0;
}

bool UMarburgumFootstepSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return World && World->IsGameWorld() && !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

bool UMarburgumFootstepSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UMarburgumFootstepSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UMarburgumFootstepSubsystem, STATGROUP_Tickables);
}

void UMarburgumFootstepSubsystem::Deinitialize()
{
    Bodies.Reset();
    Sets.Reset();
    Super::Deinitialize();
}

bool UMarburgumFootstepSubsystem::ListenerLocation(FVector& OutLocation) const
{
    const APlayerController* Player = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
    if (!Player || !Player->IsLocalController())
    {
        return false;
    }
    if (const APawn* Pawn = Player->GetPawn())
    {
        OutLocation = Pawn->GetActorLocation();
        return true;
    }
    FRotator Unused;
    Player->GetPlayerViewPoint(OutLocation, Unused);
    return true;
}

void UMarburgumFootstepSubsystem::EnsureSettings()
{
    if (!Attenuation)
    {
        Attenuation = NewObject<USoundAttenuation>(this);
        FSoundAttenuationSettings& Settings = Attenuation->Attenuation;
        Settings.bAttenuate = true;
        Settings.bSpatialize = true;
        Settings.AttenuationShape = EAttenuationShape::Sphere;
        Settings.AttenuationShapeExtents = FVector(150.0, 0.0, 0.0);
        Settings.FalloffDistance = static_cast<float>(AudibleCm - 150.0);
        Settings.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
    }
    if (!Concurrency)
    {
        Concurrency = NewObject<USoundConcurrency>(this);
        Concurrency->Concurrency.MaxCount = MaxConcurrentSteps;
        Concurrency->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::StopFarthestThenOldest;
    }
}

const TArray<TObjectPtr<USoundBase>>& UMarburgumFootstepSubsystem::SoundsFor(FName Set)
{
    if (const FMarburgumFootstepSounds* Loaded = Sets.Find(Set))
    {
        return Loaded->Sounds;
    }
    FMarburgumFootstepSounds& Added = Sets.Add(Set);
    for (int32 Variant = 0; Variant < Surface::FootstepVariants; ++Variant)
    {
        if (USoundBase* Sound = LoadObject<USoundBase>(nullptr, *Surface::FootstepAsset(Set, Variant), nullptr, LOAD_NoWarn | LOAD_Quiet))
        {
            Added.Sounds.Add(Sound);
        }
    }
    UE_LOG(LogMarburgumFootsteps, Log, TEXT("MB2FOOTSTEP set=%s sounds=%d"), *Set.ToString(), Added.Sounds.Num());
    return Added.Sounds;
}

void UMarburgumFootstepSubsystem::Tick(float DeltaTime)
{
    UWorld* World = GetWorld();
    if (!World || World->GetNetMode() == NM_DedicatedServer)
    {
        return;
    }
    Clock += DeltaTime;
    FVector Listener;
    if (!ListenerLocation(Listener))
    {
        return;
    }
    const APawn* LocalPawn = World->GetFirstPlayerController() ? World->GetFirstPlayerController()->GetPawn() : nullptr;

    for (TActorIterator<ACharacter> It(World); It; ++It)
    {
        ACharacter* Body = *It;
        USkeletalMeshComponent* Mesh = Body->GetMesh();
        const UCharacterMovementComponent* Movement = Body->GetCharacterMovement();
        if (!Mesh || !Mesh->GetSkeletalMeshAsset() || !Mesh->IsVisible() || Body->IsHidden()
            || FVector::DistSquared(Body->GetActorLocation(), Listener) > AudibleCm * AudibleCm)
        {
            continue;
        }
        FFeet& Feet = Bodies.FindOrAdd(Body);
        if (!Movement || !Movement->IsMovingOnGround())
        {
            Feet.Feet[0].Reset();
            Feet.Feet[1].Reset();
            continue;
        }
        const double Bottom = Body->GetActorLocation().Z - (Body->GetCapsuleComponent() ? Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.0);
        const double Speed = Body->GetVelocity().Size2D();
        for (int32 Side = 0; Side < 2; ++Side)
        {
            const FName Bone = Mesh->GetBoneIndex(FeetBones[Side][0]) != INDEX_NONE ? FeetBones[Side][0] : FeetBones[Side][1];
            if (Mesh->GetBoneIndex(Bone) == INDEX_NONE)
            {
                continue;
            }
            const FVector Foot = Mesh->GetBoneLocation(Bone);
            if (Feet.Feet[Side].Update(Foot.Z - Bottom, DeltaTime) && Speed >= MovingCmPerSecond
                && Clock - Feet.LastStepAt >= MinimumStepSeconds)
            {
                Feet.LastStepAt = Clock;
                Step(*Body, Foot, Speed, Body == LocalPawn);
            }
        }
    }

    if (Clock - PrunedAt >= PruneEverySeconds)
    {
        PrunedAt = Clock;
        for (auto It = Bodies.CreateIterator(); It; ++It)
        {
            if (!It.Key().ResolveObjectPtr())
            {
                It.RemoveCurrent();
            }
        }
    }
}

void UMarburgumFootstepSubsystem::Step(const ACharacter& Body, const FVector& Foot, double Speed, bool bLocal)
{
    UWorld* World = GetWorld();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumFootstep), false, &Body);
    Params.bReturnPhysicalMaterial = true;
    FHitResult Hit;
    const bool bHit = World->LineTraceSingleByChannel(Hit, Foot + FVector(0.0, 0.0, TraceUpCm), Foot - FVector(0.0, 0.0, TraceDownCm), ECC_Visibility, Params);
    const FString Ground = bHit ? MarburgumII::Ground::SurfaceOf(Hit) : FString(Surface::Unknown);
    const FMarburgumWeather* Weather = AMarburgumGameState::WeatherOf(World);
    const FName Set = Surface::FootstepSetFor(Ground, Weather ? Weather->Wetness : 0.0, Weather ? Weather->SnowDepthM : 0.0, Rolls.FRand());

    ++StepsPlayed;
    ++StepsBySet.FindOrAdd(Set);
    ++StepsBySurface.FindOrAdd(Ground);
    if (bLocal)
    {
        ++LocalSteps;
        ++LocalStepsBySet.FindOrAdd(Set);
        LocalSurface = Ground;
    }

    const TArray<TObjectPtr<USoundBase>>& Sounds = SoundsFor(Set);
    if (Sounds.Num() == 0)
    {
        ++StepsWithoutSound;
        return;
    }
    // Never the same variant twice in a row from one set.
    int32& Previous = LastVariant.FindOrAdd(Set, INDEX_NONE);
    int32 Variant = Rolls.RandRange(0, Sounds.Num() - 1);
    if (Sounds.Num() > 1 && Variant == Previous)
    {
        Variant = (Variant + 1 + Rolls.RandRange(0, Sounds.Num() - 2)) % Sounds.Num();
    }
    Previous = Variant;
    EnsureSettings();
    UGameplayStatics::PlaySoundAtLocation(World, Sounds[Variant], bHit ? Hit.ImpactPoint : Foot, FRotator::ZeroRotator,
        Surface::StepVolume(Speed), Rolls.FRandRange(0.94f, 1.06f), 0.f, Attenuation, Concurrency);
}
