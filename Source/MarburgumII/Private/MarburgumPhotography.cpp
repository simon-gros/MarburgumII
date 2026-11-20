#include "MarburgumPhotography.h"

#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "MarburgumCombatContract.h"
#include "EngineUtils.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumInteractable.h"
#include "MarburgumWater.h"

namespace MarburgumPhotographyRules
{
    /** Street level is sampled this far out from the subject's plan position, all the way round. */
    constexpr double SampleRadiusCm = 2000.0;
    constexpr int32 Samples = 12;
    constexpr double TraceAboveCm = 6000.0;
    constexpr double TraceBelowCm = 6000.0;
}

bool UMarburgumPhotographySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UMarburgumPhotographySubsystem::Resolve() const
{
    using namespace MarburgumPhotographyRules;
    UWorld* World = GetWorld();
    // A client may ask before a server-spawned floor (the Melje pad) has arrived: what has no ground yet is tried
    // again a little later, what has ground is never moved.
    if (bResolved || !World || World->GetTimeSeconds() < NextAttemptSeconds)
    {
        return;
    }
    NextAttemptSeconds = World->GetTimeSeconds() + 2.0;
    const UMarburgumWaterSubsystem* Water = World->GetSubsystem<UMarburgumWaterSubsystem>();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumPhotoGround), true);
    for (const MarburgumII::Gameplay::FPhotoSubjectDefinition& Subject : MarburgumII::Gameplay::PhotoSubjects)
    {
        if (AimPoints.Contains(Subject.Id))
        {
            continue;
        }
        if (*Subject.Anchor)
        {
            // A document on a prop (ADR-084): the ground under the server-placed interactable, which every machine sees
            // in the same place. Until it has replicated there is nothing to aim at yet.
            for (TActorIterator<AMarburgumInteractable> It(World); It; ++It)
            {
                if (It->GetCatalogueId() != FName(Subject.Anchor))
                {
                    continue;
                }
                FCollisionQueryParams Under(SCENE_QUERY_STAT(MarburgumPhotoAnchor), true, *It);
                const FVector At = It->GetActorLocation();
                FHitResult Hit;
                if (World->LineTraceSingleByChannel(Hit, At + FVector(0.0, 0.0, 200.0), At - FVector(0.0, 0.0, 600.0), ECC_Visibility, Under))
                {
                    AimPoints.Add(Subject.Id, FVector(At.X, At.Y, Hit.ImpactPoint.Z + Subject.AimHeightCm));
                }
                break;
            }
            continue;
        }
        const FVector Plan = MarburgumII::Combat::GodotPositionToUnreal(Subject.GodotPosition);
        TArray<double> Levels;
        for (int32 Index = 0; Index < Samples; ++Index)
        {
            const double Angle = UE_TWO_PI * Index / Samples;
            const FVector At = Plan + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * SampleRadiusCm;
            FHitResult Hit;
            if (World->LineTraceSingleByChannel(Hit, FVector(At.X, At.Y, At.Z + TraceAboveCm), FVector(At.X, At.Y, At.Z - TraceBelowCm),
                    ECC_Visibility, Params)
                && !(Water && Water->IsWet(Hit.ImpactPoint)))
            {
                Levels.Add(Hit.ImpactPoint.Z);
            }
        }
        if (Levels.IsEmpty())
        {
            continue;
        }
        // The median: a roof or a stretch of riverbed among the samples does not move it.
        Levels.Sort();
        const double Street = Levels[Levels.Num() / 2];
        AimPoints.Add(Subject.Id, FVector(Plan.X, Plan.Y, Street + Subject.AimHeightCm));
    }
    bResolved = AimPoints.Num() == UE_ARRAY_COUNT(MarburgumII::Gameplay::PhotoSubjects);
}

bool UMarburgumPhotographySubsystem::GetAimPoint(const FString& SubjectId, FVector& OutAimPoint) const
{
    Resolve();
    if (const FVector* Found = AimPoints.Find(SubjectId))
    {
        OutAimPoint = *Found;
        return true;
    }
    return false;
}

UMarburgumPhotographySubsystem::FInFrame UMarburgumPhotographySubsystem::FindInFrame(const FVector& Eye, const FRotator& View,
    const AActor* Photographer) const
{
    Resolve();
    FInFrame Best;
    const UWorld* World = GetWorld();
    if (!World)
    {
        return Best;
    }
    const FVector Forward = View.Vector();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumPhotoSight), true, Photographer);
    for (const MarburgumII::Gameplay::FPhotoSubjectDefinition& Subject : MarburgumII::Gameplay::PhotoSubjects)
    {
        const FVector* Aim = AimPoints.Find(Subject.Id);
        if (!Aim)
        {
            continue;
        }
        const FVector Toward = *Aim - Eye;
        const double Distance = Toward.Size();
        if (Distance > Subject.WithinCm || Distance < 1.0)
        {
            continue;
        }
        const double OffAxis = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Forward, Toward / Distance), -1.0, 1.0)));
        if (OffAxis > Subject.ConeDegrees || (Best.Subject && OffAxis >= Best.OffAxisDegrees))
        {
            continue;
        }
        FHitResult Hit;
        if (World->LineTraceSingleByChannel(Hit, Eye, *Aim, ECC_Visibility, Params)
            && FVector::Dist(Hit.ImpactPoint, *Aim) > FMath::Min(SubjectBodyCm, Distance * 0.5))
        {
            continue; // Something else is in the way.
        }
        Best.Subject = &Subject;
        Best.AimPoint = *Aim;
        Best.OffAxisDegrees = OffAxis;
        Best.DistanceCm = Distance;
    }
    return Best;
}
