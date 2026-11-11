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

    /** True when some mission's stage targets TargetId (an authored mission target, as opposed to scenery). */
    MARBURGUMIISHARED_API bool IsMissionTarget(const FString& TargetId);

    /** The stage after Stage in Mission, or null at the end. */
    MARBURGUMIISHARED_API const Gameplay::FMissionStageDefinition* NextStage(
        const Gameplay::FMissionDefinition& Mission, const Gameplay::FMissionStageDefinition& Stage);
}
