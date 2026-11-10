#pragma once

#include "CoreMinimal.h"
#include "MarburgumInteractionTypes.h"
#include "MarburgumWorldStore.h"

/** What a request to talk to an NPC means for one character, decided without side effects. */
struct FMarburgumNpcDecision
{
    /** Applied: OutNext holds the complete next record (revision already incremented). */
    bool bApplied = false;
    EMarburgumInteractionReason Reason = EMarburgumInteractionReason::None;
    EMarburgumInteractionEffect Effect = EMarburgumInteractionEffect::None;
    /** The mission concerned, also on refusals where one applies (e.g. RequirementNotMet). */
    FString Mission;
    /** The mission's current stage after the decision. */
    FString Stage;
    /** Item granted (MissionStarted) or consumed (StageCompleted). */
    FString Item;
    int32 Quantity = 0;
};

namespace MarburgumII::Missions
{
    /**
     * Pure mission rules for an NPC interaction (no I/O, no actors), so they can be unit-tested:
     *
     *  1. The NPC offers a mission the character has never started: start it at its first stage and
     *     grant its start items in the same record.
     *  2. Otherwise the character's current stage targets this NPC: if the stage needs one of a set of
     *     items and the character holds one, consume it and move to the next registered stage;
     *     without the item, refuse with RequirementNotMet.
     *  3. Otherwise there is nothing to do here (NothingToDo).
     *
     * Range, character, store and replay checks belong to the caller (UMarburgumWorldAuthority).
     */
    FMarburgumNpcDecision DecideNpcInteraction(const FMarburgumWorldRecord& Current, const FString& Character,
        const FString& NpcId, FMarburgumWorldRecord& OutNext);
}
