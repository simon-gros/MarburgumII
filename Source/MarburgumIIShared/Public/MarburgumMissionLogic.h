#pragma once

#include "CoreMinimal.h"
#include "MarburgumGameplayContract.h"

/**
 * What interacting with a mission target means for one character (ADR-049). Pure and engine-neutral, so the
 * server (deciding, over the durable record) and the client (choosing the prompt's words, over its replicated
 * copy) reach the same answer from the same rule. Only the server's answer changes anything.
 *
 * The rule, for a target:
 *  1. A mission under way whose current stage is completed here, and whose requirements are held: complete it.
 *     A decision due here is the same kind of work: it is answered before any new offer. Registry order decides
 *     between two such missions.
 *  2. Otherwise a mission offered here that the character has never started, whose prerequisites are complete:
 *     start it.
 *  3. Otherwise the first mission under way whose current stage is here but is missing something
 *     (RequirementNotMet).
 *  4. Otherwise an offer here waiting on another mission (PrerequisiteIncomplete), or nothing (NothingToDo).
 *
 * An offer gated on another mission's outcome (ADR-081) waits while that mission is unfinished and is simply not
 * made once it ended another way: a decision taken earlier closes the door for good, without a refusal to explain.
 * A Photograph stage is completed by photographing its subject (the server's photograph path), never by talking.
 *
 * A conditional stage (ADR-083) belongs to a character's route only when its gates hold (an earlier mission's recorded
 * outcome); the mission passes over it otherwise. Unique items are handed over once however many missions grant them.
 *
 * Work in hand comes before a new offer, so a character who reaches Petra Kralj while asking about the cashbox
 * address finishes that step before she offers Kiosk 67.
 */
namespace MarburgumII::Missions
{
    /** A character's progress in one mission, as either side holds it. */
    struct FProgressView
    {
        FString Stage;
        bool bCompleted = false;
        /** The outcome a decision recorded; empty when there was none. */
        FString Outcome;
    };

    enum class EPlanAction : uint8
    {
        NothingToDo,
        CompleteStage,
        StartMission,
        ChoiceRequired,
        RequirementNotMet,
        PrerequisiteIncomplete
    };

    struct FPlan
    {
        EPlanAction Action = EPlanAction::NothingToDo;
        const Gameplay::FMissionDefinition* Mission = nullptr;
        /** The stage completed, due, or blocked; the first stage for StartMission. */
        const Gameplay::FMissionStageDefinition* Stage = nullptr;
        /** InventoryAny: the item that satisfies (and may be consumed by) the stage. */
        FString ObjectiveItem;
        /** RequirementNotMet: the first thing missing. */
        FString MissingItem;
        /** PrerequisiteIncomplete: the mission that must be completed first. */
        FString Prerequisite;
    };

    using FFindProgress = TFunctionRef<const FProgressView*(const FString& MissionId)>;
    using FCountItem = TFunctionRef<int32(const FString& ItemId)>;

    MARBURGUMIISHARED_API FPlan PlanInteraction(const FString& TargetId, FFindProgress FindProgress, FCountItem CountItem);

    /** A photograph of SubjectId completes the current Photograph stage that names it (CompleteStage), or nothing. */
    MARBURGUMIISHARED_API FPlan PlanPhotograph(const FString& SubjectId, FFindProgress FindProgress, FCountItem CountItem);

    /**
     * Whether Mission's outcome gates allow it for this character: Open, Waiting (a gated mission is unfinished;
     * OutPrerequisite names it) or Closed (a gated mission ended in another outcome).
     */
    enum class EGate : uint8 { Open, Waiting, Closed };
    MARBURGUMIISHARED_API EGate CheckOutcomeGates(const Gameplay::FMissionDefinition& Mission, FFindProgress FindProgress, FString& OutPrerequisite);

    /** True when some mission's stage targets TargetId (an authored mission target, as opposed to scenery). */
    MARBURGUMIISHARED_API bool IsMissionTarget(const FString& TargetId);

    /**
     * Whether a conditional stage (ADR-083) is on this character's route: every gate's mission completed with one of its
     * outcomes. An unconditional stage always is.
     */
    MARBURGUMIISHARED_API bool StageApplies(const Gameplay::FMissionStageDefinition& Stage, FFindProgress FindProgress);

    /** The first stage of Mission on this character's route; null for a mission with none. */
    MARBURGUMIISHARED_API const Gameplay::FMissionStageDefinition* FirstStage(const Gameplay::FMissionDefinition& Mission, FFindProgress FindProgress);

    /** The stage after Stage in Mission on this character's route, passing over conditional stages that do not apply; null at the end. */
    MARBURGUMIISHARED_API const Gameplay::FMissionStageDefinition* NextStage(
        const Gameplay::FMissionDefinition& Mission, const Gameplay::FMissionStageDefinition& Stage, FFindProgress FindProgress);

    /** The stage before Stage on this character's route (the one just done when Stage became current); null at the start. */
    MARBURGUMIISHARED_API const Gameplay::FMissionStageDefinition* PreviousStage(
        const Gameplay::FMissionDefinition& Mission, const Gameplay::FMissionStageDefinition& Stage, FFindProgress FindProgress);
}
