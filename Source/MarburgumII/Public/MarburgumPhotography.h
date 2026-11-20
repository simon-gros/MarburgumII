#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MarburgumPhotography.generated.h"

namespace MarburgumII::Gameplay
{
    struct FPhotoSubjectDefinition;
}

/**
 * Where the catalogue's photo subjects are in this world, and which one a camera is pointed at (ADR-080). The server
 * judges a photograph with it; the client asks the same question to show what [P] would photograph. Every machine
 * resolves the same aim points: the street level round the subject's plan position (the median of dry ground a few
 * metres out, so a tower's roof or the riverbed does not count) plus the subject's aim height.
 *
 * A subject is in frame when the eye is within its range, the view points within its cone of the aim point and the
 * first thing in the way is the subject itself (anything hit within a building's width of the aim point counts).
 * Of several in frame, the one nearest the middle of the view wins.
 */
UCLASS()
class MARBURGUMII_API UMarburgumPhotographySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

    struct FInFrame
    {
        const MarburgumII::Gameplay::FPhotoSubjectDefinition* Subject = nullptr;
        FVector AimPoint = FVector::ZeroVector;
        double OffAxisDegrees = 0.0;
        double DistanceCm = 0.0;
    };

    /** The subject Eye looking along View would photograph; Subject is null when nothing qualifies. */
    FInFrame FindInFrame(const FVector& Eye, const FRotator& View, const AActor* Photographer) const;

    /** A subject's resolved aim point in this world; false when the subject has no ground under it here. */
    bool GetAimPoint(const FString& SubjectId, FVector& OutAimPoint) const;

    /** Anything hit within this distance of the aim point is the subject itself (a tower's own wall). */
    static constexpr double SubjectBodyCm = 1200.0;

private:
    void Resolve() const;

    mutable TMap<FString, FVector> AimPoints;
    mutable bool bResolved = false;
    mutable double NextAttemptSeconds = 0.0;
};
