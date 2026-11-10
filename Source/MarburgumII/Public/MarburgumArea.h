#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MarburgumArea.generated.h"

class UBoxComponent;

/**
 * A named map area (ontology `place`), carried forward from the original project/data/areas.json:
 * an axis-aligned box with a stable AreaId registered in MarburgumII::Gameplay::Areas. Placed in maps,
 * never replicated; the server derives each player's area from these boxes (see
 * AMarburgumPlayerState::GetAreaId) and clients only display the result.
 */
UCLASS()
class MARBURGUMII_API AMarburgumArea : public AActor
{
    GENERATED_BODY()

public:
    AMarburgumArea();

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marburgum|Area")
    FName AreaId;

    /** Provenance shown under the name, e.g. that this map is a fallback blockout, not the licensed city. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marburgum|Area")
    FText Note;

    /** Lower wins where boxes overlap (the original list order). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Marburgum|Area")
    int32 Priority = 0;

    bool Contains(const FVector& Location) const;

    /** The area containing Location, or None when it is outside every area. */
    static FName Resolve(const UWorld* World, const FVector& Location);

    /** The placed actor for an area id, if this world has one. */
    static const AMarburgumArea* Find(const UWorld* World, FName Id);

private:
    UPROPERTY(VisibleAnywhere, Category = "Marburgum|Area")
    TObjectPtr<UBoxComponent> Box;
};
