#include "MarburgumMissionLogic.h"

using namespace MarburgumII::Gameplay;

namespace MarburgumII::Missions
{
    namespace
    {
        /** Whether the stage can be completed now; fills the satisfying or the missing item. */
        bool StageReady(const FMissionStageDefinition& Stage, FCountItem CountItem, FPlan& Plan)
        {
            for (int32 Index = 0; Index < Stage.RequiredItemCount; ++Index)
            {
                const FItemQuantity& Required = Stage.RequiredItems[Index];
                if (CountItem(Required.Id) < Required.Quantity)
                {
                    Plan.MissingItem = Required.Id;
                    return false;
                }
            }
            if (Stage.Objective == EObjectiveType::InventoryAny)
            {
                for (int32 Index = 0; Index < Stage.ObjectiveItemCount; ++Index)
                {
                    if (CountItem(Stage.ObjectiveItems[Index]) >= FMath::Max(Stage.ObjectiveQuantity, 1))
                    {
                        Plan.ObjectiveItem = Stage.ObjectiveItems[Index];
                        return true;
                    }
                }
                Plan.MissingItem = Stage.ObjectiveItemCount > 0 ? Stage.ObjectiveItems[0] : TEXT("");
                return false;
            }
            return true;
        }
    }

    const FMissionStageDefinition* NextStage(const FMissionDefinition& Mission, const FMissionStageDefinition& Stage)
    {
        const int32 Index = static_cast<int32>(&Stage - Mission.Stages);
        return Index >= 0 && Index + 1 < Mission.StageCount ? &Mission.Stages[Index + 1] : nullptr;
    }

    bool IsMissionTarget(const FString& TargetId)
    {
        for (const FMissionDefinition& Mission : Gameplay::Missions)
        {
            if (TargetId == Mission.OfferTarget)
            {
                return true;
            }
            for (int32 Index = 0; Index < Mission.StageCount; ++Index)
            {
                if (TargetId == Mission.Stages[Index].CompleteTarget)
                {
                    return true;
                }
            }
        }
        return false;
    }

    FPlan PlanInteraction(const FString& TargetId, FFindProgress FindProgress, FCountItem CountItem)
    {
        // 1. Work in hand that can be done here.
        FPlan Blocked;
        for (const FMissionDefinition& Mission : Gameplay::Missions)
        {
            const FProgressView* Progress = FindProgress(Mission.Id);
            if (!Progress || Progress->bCompleted)
            {
                continue;
            }
            const FMissionStageDefinition* Stage = FindStage(Mission, Progress->Stage);
            if (!Stage || TargetId != Stage->CompleteTarget)
            {
                continue;
            }
            FPlan Plan;
            Plan.Mission = &Mission;
            Plan.Stage = Stage;
            if (Stage->Objective == EObjectiveType::Choice)
            {
                Plan.Action = EPlanAction::ChoiceRequired;
            }
            else if (StageReady(*Stage, CountItem, Plan))
            {
                Plan.Action = EPlanAction::CompleteStage;
                return Plan;
            }
            else
            {
                Plan.Action = EPlanAction::RequirementNotMet;
            }
            if (Blocked.Action == EPlanAction::NothingToDo)
            {
                Blocked = Plan;
            }
        }

        // 2. A new mission offered here.
        FPlan Waiting;
        for (const FMissionDefinition& Mission : Gameplay::Missions)
        {
            if (TargetId != Mission.OfferTarget || Mission.StageCount <= 0 || FindProgress(Mission.Id))
            {
                continue;
            }
            FString Missing;
            for (int32 Index = 0; Index < Mission.RequiresCompletedCount && Missing.IsEmpty(); ++Index)
            {
                const FProgressView* Required = FindProgress(Mission.RequiresCompleted[Index]);
                if (!Required || !Required->bCompleted)
                {
                    Missing = Mission.RequiresCompleted[Index];
                }
            }
            if (Missing.IsEmpty())
            {
                FPlan Plan;
                Plan.Action = EPlanAction::StartMission;
                Plan.Mission = &Mission;
                Plan.Stage = &Mission.Stages[0];
                return Plan;
            }
            if (Waiting.Action == EPlanAction::NothingToDo)
            {
                Waiting.Action = EPlanAction::PrerequisiteIncomplete;
                Waiting.Mission = &Mission;
                Waiting.Prerequisite = Missing;
            }
        }

        // 3-4. Say why not.
        if (Blocked.Action != EPlanAction::NothingToDo)
        {
            return Blocked;
        }
        return Waiting;
    }
}
