#include "MarburgumMissionRules.h"

#include "MarburgumGameplayContract.h"

using namespace MarburgumII::Gameplay;

namespace MarburgumII::Missions
{
    FMarburgumNpcDecision DecideNpcInteraction(const FMarburgumWorldRecord& Current, const FString& Character,
        const FString& NpcId, FMarburgumWorldRecord& OutNext)
    {
        FMarburgumNpcDecision Decision;
        const TMap<FString, FMarburgumStoredMission>* Progress = Current.Missions.Find(Character);

        // 1. A mission offer this character has never taken up.
        if (const FMissionDefinition* Offer = FindMissionOfferedBy(NpcId);
            Offer && Offer->StageCount > 0 && !(Progress && Progress->Contains(Offer->Id)))
        {
            OutNext = Current;
            ++OutNext.Revision;
            FMarburgumStoredMission& Started = OutNext.Missions.FindOrAdd(Character).Add(Offer->Id);
            Started.Stage = Offer->Stages[0].Id;
            Started.StartedRevision = OutNext.Revision;
            if (Offer->StartItem && FindItem(Offer->StartItem) && Offer->StartItemQuantity > 0)
            {
                OutNext.Inventories.FindOrAdd(Character).FindOrAdd(Offer->StartItem) += Offer->StartItemQuantity;
                Decision.Item = Offer->StartItem;
                Decision.Quantity = Offer->StartItemQuantity;
            }
            Decision.bApplied = true;
            Decision.Effect = EMarburgumInteractionEffect::MissionStarted;
            Decision.Mission = Offer->Id;
            Decision.Stage = Started.Stage;
            return Decision;
        }

        // 2. The current stage of a mission under way targets this NPC (registry order decides ties).
        for (const FMissionDefinition& Mission : MarburgumII::Gameplay::Missions)
        {
            const FMarburgumStoredMission* State = Progress ? Progress->Find(Mission.Id) : nullptr;
            const FMissionStageDefinition* Stage = State ? FindStage(Mission, State->Stage) : nullptr;
            if (!Stage || NpcId != Stage->CompleteTarget)
            {
                continue;
            }
            Decision.Mission = Mission.Id;
            Decision.Stage = State->Stage;
            const int32 StageIndex = static_cast<int32>(Stage - Mission.Stages);
            if (StageIndex + 1 >= Mission.StageCount)
            {
                // The rest of the mission is not registered in this build; never invent an ending.
                Decision.Reason = EMarburgumInteractionReason::NothingToDo;
                return Decision;
            }

            FString Consumed;
            if (Stage->RequiredAnyItemCount > 0)
            {
                const TMap<FString, int32>* Owned = Current.Inventories.Find(Character);
                for (int32 Index = 0; Index < Stage->RequiredAnyItemCount && Consumed.IsEmpty(); ++Index)
                {
                    const int32* Held = Owned ? Owned->Find(Stage->RequiredAnyItems[Index]) : nullptr;
                    if (Held && *Held > 0)
                    {
                        Consumed = Stage->RequiredAnyItems[Index];
                    }
                }
                if (Consumed.IsEmpty())
                {
                    Decision.Reason = EMarburgumInteractionReason::RequirementNotMet;
                    return Decision;
                }
            }

            OutNext = Current;
            ++OutNext.Revision;
            if (!Consumed.IsEmpty())
            {
                TMap<FString, int32>& Items = OutNext.Inventories.FindOrAdd(Character);
                if (--Items.FindOrAdd(Consumed) <= 0)
                {
                    Items.Remove(Consumed);
                }
                Decision.Item = Consumed;
                Decision.Quantity = 1;
            }
            OutNext.Missions.FindOrAdd(Character).FindOrAdd(Mission.Id).Stage = Mission.Stages[StageIndex + 1].Id;
            Decision.bApplied = true;
            Decision.Effect = EMarburgumInteractionEffect::StageCompleted;
            Decision.Stage = Mission.Stages[StageIndex + 1].Id;
            return Decision;
        }

        // 3. Nothing this NPC offers the character right now.
        Decision.Reason = EMarburgumInteractionReason::NothingToDo;
        return Decision;
    }
}
