#pragma once

#include "CoreMinimal.h"
#include "MarburgumInteractionTypes.h"
#include "MarburgumWorldStore.h"

/** What a mission request means for one character, decided without side effects. */
struct FMarburgumMissionDecision
{
    /** Applied: OutNext holds the complete next record (revision already incremented). */
    bool bApplied = false;
    EMarburgumInteractionReason Reason = EMarburgumInteractionReason::None;
    EMarburgumInteractionEffect Effect = EMarburgumInteractionEffect::None;
    /** The mission concerned, also on refusals where one applies (e.g. RequirementNotMet). */
    FString Mission;
    /** The mission's current stage after the decision (the last stage once completed). */
    FString Stage;
    /** Item granted (MissionStarted) or consumed (StageCompleted). */
    FString Item;
    int32 Quantity = 0;
    /** Further items handed over: a stage's grant items, a mission's other start items. */
    TMap<FString, int32> Granted;
    FString Choice;
    FString Outcome;
    /** Money paid (reward) or spent (choice). */
    int64 MoneyDelta = 0;
    TMap<FString, int64> SkillXp;
};

namespace MarburgumII::Missions
{
    /**
     * Pure mission rules for interacting with a mission target, NPC or place (no I/O, no actors), so they can be
     * unit-tested. Which mission and stage is decided by PlanInteraction (MarburgumMissionLogic.h), the rule the
     * client's prompt mirrors. Then, in one all-or-nothing record:
     *  - StartMission: the mission starts at its first stage with its start items.
     *  - CompleteStage: an InventoryAny objective's item is consumed when the stage says so, required items stay
     *    in the pocket, grant items are handed over, flags are set, and the mission moves to the next stage or,
     *    after the final one, completes: the reward is paid once and the journal entry unlocked.
     * Refusals: ChoiceRequired, RequirementNotMet, PrerequisiteIncomplete, NothingToDo.
     *
     * Range, character, store and replay checks belong to the caller (UMarburgumWorldAuthority).
     */
    FMarburgumMissionDecision DecideInteraction(const FMarburgumWorldRecord& Current, const FString& Character,
        const FString& TargetId, FMarburgumWorldRecord& OutNext);

    /**
     * A decision at a choice stage (original MissionRules::Choose): the stage must be current and completed at
     * TargetId, the choice must exist there, and the character must afford its money. The choice's money, skill
     * XP, outcome and flags are recorded with the stage's flags; a final decision completes the mission.
     * Refusals: UnknownChoice, InsufficientMoney.
     */
    FMarburgumMissionDecision DecideChoice(const FMarburgumWorldRecord& Current, const FString& Character,
        const FString& TargetId, const FString& MissionId, const FString& ChoiceId, FMarburgumWorldRecord& OutNext);

    /** Add XP to a skill in a record; false (and nothing changed) for an unknown skill or a non-positive amount. */
    bool AddSkillXp(FMarburgumWorldRecord& Record, const FString& Character, const FString& Skill, int64 Xp);
}
