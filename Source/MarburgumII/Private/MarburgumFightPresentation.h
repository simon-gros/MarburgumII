#pragma once

#include "CoreMinimal.h"
#include "MarburgumCombatTypes.h"
#include "UObject/Object.h"
#include "MarburgumFightPresentation.generated.h"

class UAnimSequenceBase;
class USoundBase;
class UWorld;

/**
 * The fight clips and impact sounds (ADR-041), referenced from this class's default object so the cook carries
 * them as it carries the swim clips the character references. Missing ones (before the combat content commandlet
 * has run) stay null and are simply not played.
 */
UCLASS()
class UMarburgumFightAssets : public UObject
{
    GENERATED_BODY()

public:
    UMarburgumFightAssets();

    UPROPERTY()
    TObjectPtr<UAnimSequenceBase> Guard;

    UPROPERTY()
    TObjectPtr<UAnimSequenceBase> Jab;

    UPROPERTY()
    TObjectPtr<UAnimSequenceBase> Heavy;

    UPROPERTY()
    TObjectPtr<UAnimSequenceBase> Down;

    UPROPERTY()
    TArray<TObjectPtr<USoundBase>> JabHits;

    UPROPERTY()
    TArray<TObjectPtr<USoundBase>> HeavyHits;

    UPROPERTY()
    TArray<TObjectPtr<USoundBase>> Blocked;

    UPROPERTY()
    TArray<TObjectPtr<USoundBase>> Floored;
};

namespace MarburgumFightSound
{
    /**
     * One blow's sound at a place: a hit (jab or heavy), a blocked blow, or a body going down. Picks one of the
     * pack's variants; does nothing on a dedicated server or before the sounds are imported. A miss is silent.
     */
    void PlayImpact(UWorld* World, const FVector& At, FName Outcome, bool bHeavy, bool bFloored);
}
