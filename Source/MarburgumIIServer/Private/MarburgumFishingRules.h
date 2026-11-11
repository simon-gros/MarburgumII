#pragma once

#include "CoreMinimal.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumWorldStore.h"

/** What landing a fish means for one character, decided without side effects (ADR-048). */
struct FMarburgumCatchDecision
{
    /** Applied: OutNext holds the complete next record (revision already incremented). */
    bool bApplied = false;
    FString Item;
    int64 Xp = 0;
};

namespace MarburgumII::Fishing
{
    /**
     * Seconds from a cast to the bite: the original's minimum wait plus a share of its random span, Roll being any
     * unsigned number (the server draws it). The purchasable rank that shortened the span is excluded (ADR-004).
     */
    double BiteDelaySeconds(uint32 Roll);

    /** The bite window: a strike this long after the bite still lands the fish. */
    double BiteWindowSeconds();

    /**
     * The species a strike lands (original Fishing::FCatalog::DrawSpecies): Roll, reduced modulo the spot's total
     * weight, walks the weighted list in order. Null for an unknown spot or one with no positive weight.
     */
    const Gameplay::FFishSpecies* DrawSpecies(const Gameplay::FFishingSpotDefinition& Spot, int64 Roll);

    /**
     * Land one fish at SpotId: the species from Roll, one of it into the character's custody and its XP into
     * Fishing, in one record. Not applied for an unknown spot, species or skill.
     */
    FMarburgumCatchDecision DecideCatch(const FMarburgumWorldRecord& Current, const FString& Character, const FString& SpotId,
        int64 Roll, FMarburgumWorldRecord& OutNext);
}
