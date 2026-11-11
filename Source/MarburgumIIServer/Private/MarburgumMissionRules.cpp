#include "MarburgumMissionRules.h"

#include "MarburgumGameplayContract.h"
#include "MarburgumMissionLogic.h"

using namespace MarburgumII::Gameplay;

namespace MarburgumII::Missions
{
    namespace
    {
        // Far below int64 limits, so no sum of rewards or XP can overflow.
        constexpr int64 MaxAmount = int64(1) << 50;

        void AddItem(FMarburgumWorldRecord& Record, const FString& Character, const FString& Item, int32 Quantity)
        {
            Record.Inventories.FindOrAdd(Character).FindOrAdd(Item) += Quantity;
        }

        void RemoveItem(FMarburgumWorldRecord& Record, const FString& Character, const FString& Item, int32 Quantity)
        {
            TMap<FString, int32>& Items = Record.Inventories.FindOrAdd(Character);
            if ((Items.FindOrAdd(Item) -= Quantity) <= 0)
            {
                Items.Remove(Item);
            }
        }

        void SetFlags(FMarburgumStoredMission& State, const FMissionFlag* Flags, int32 Count)
        {
            for (int32 Index = 0; Index < Count; ++Index)
            {
                State.Flags.Add(Flags[Index].Id, Flags[Index].bValue);
            }
        }

        /** Move past Stage: to the next stage, or complete the mission (reward once, journal entry). */
        void Finish(const FMissionDefinition& Mission, const FMissionStageDefinition& Stage, FMarburgumWorldRecord& Next,
            const FString& Character, FMarburgumStoredMission& State, FMarburgumMissionDecision& Decision)
        {
            const FMissionStageDefinition* Following = NextStage(Mission, Stage);
            if (!Stage.bFinal && Following)
            {
                State.Stage = Following->Id;
                Decision.Effect = EMarburgumInteractionEffect::StageCompleted;
                Decision.Stage = State.Stage;
                return;
            }
            State.Stage = Stage.Id;
            State.bCompleted = true;
            State.JournalEntry = Mission.JournalEntryId;
            if (!State.bRewardPaid && Mission.RewardMoney > 0)
            {
                Next.Money.FindOrAdd(Character) += Mission.RewardMoney;
                Decision.MoneyDelta += Mission.RewardMoney;
                State.bRewardPaid = true;
            }
            Decision.Effect = EMarburgumInteractionEffect::MissionCompleted;
            Decision.Stage = State.Stage;
        }

        TMap<FString, FProgressView> Views(const FMarburgumWorldRecord& Current, const FString& Character)
        {
            TMap<FString, FProgressView> Out;
            if (const TMap<FString, FMarburgumStoredMission>* Owned = Current.Missions.Find(Character))
            {
                for (const TPair<FString, FMarburgumStoredMission>& Pair : *Owned)
                {
                    Out.Add(Pair.Key, { Pair.Value.Stage, Pair.Value.bCompleted });
                }
            }
            return Out;
        }
    }

    bool AddSkillXp(FMarburgumWorldRecord& Record, const FString& Character, const FString& Skill, int64 Xp)
    {
        if (!FindSkill(Skill) || Xp <= 0 || Xp > MaxAmount)
        {
            return false;
        }
        int64& Total = Record.Skills.FindOrAdd(Character).FindOrAdd(Skill);
        Total = FMath::Min(Total + Xp, MaxAmount);
        return true;
    }

    FMarburgumMissionDecision DecideInteraction(const FMarburgumWorldRecord& Current, const FString& Character,
        const FString& TargetId, FMarburgumWorldRecord& OutNext)
    {
        FMarburgumMissionDecision Decision;
        const TMap<FString, FProgressView> Progress = Views(Current, Character);
        const TMap<FString, int32>* Owned = Current.Inventories.Find(Character);
        const FPlan Plan = PlanInteraction(TargetId,
            [&Progress](const FString& Mission) { return Progress.Find(Mission); },
            [Owned](const FString& Item) { return Owned ? Owned->FindRef(Item) : 0; });
        if (Plan.Mission)
        {
            Decision.Mission = Plan.Mission->Id;
        }
        if (Plan.Stage)
        {
            Decision.Stage = Plan.Stage->Id;
        }

        switch (Plan.Action)
        {
        case EPlanAction::StartMission:
        {
            const FMissionDefinition& Mission = *Plan.Mission;
            OutNext = Current;
            ++OutNext.Revision;
            FMarburgumStoredMission& Started = OutNext.Missions.FindOrAdd(Character).Add(Mission.Id);
            Started.Stage = Mission.Stages[0].Id;
            Started.StartedRevision = OutNext.Revision;
            for (int32 Index = 0; Index < Mission.StartItemCount; ++Index)
            {
                const FItemQuantity& Start = Mission.StartItems[Index];
                if (!FindItem(Start.Id) || Start.Quantity <= 0)
                {
                    continue;
                }
                AddItem(OutNext, Character, Start.Id, Start.Quantity);
                if (Decision.Item.IsEmpty())
                {
                    Decision.Item = Start.Id;
                    Decision.Quantity = Start.Quantity;
                }
                else
                {
                    Decision.Granted.FindOrAdd(Start.Id) += Start.Quantity;
                }
            }
            Decision.bApplied = true;
            Decision.Effect = EMarburgumInteractionEffect::MissionStarted;
            Decision.Stage = Started.Stage;
            return Decision;
        }
        case EPlanAction::CompleteStage:
        {
            const FMissionDefinition& Mission = *Plan.Mission;
            const FMissionStageDefinition& Stage = *Plan.Stage;
            OutNext = Current;
            ++OutNext.Revision;
            FMarburgumStoredMission& State = OutNext.Missions.FindOrAdd(Character).FindOrAdd(Mission.Id);
            if (Stage.Objective == EObjectiveType::InventoryAny && Stage.bConsume && !Plan.ObjectiveItem.IsEmpty())
            {
                const int32 Quantity = FMath::Max(Stage.ObjectiveQuantity, 1);
                RemoveItem(OutNext, Character, Plan.ObjectiveItem, Quantity);
                Decision.Item = Plan.ObjectiveItem;
                Decision.Quantity = Quantity;
            }
            for (int32 Index = 0; Index < Stage.GrantItemCount; ++Index)
            {
                const FItemQuantity& Grant = Stage.GrantItems[Index];
                if (FindItem(Grant.Id) && Grant.Quantity > 0)
                {
                    AddItem(OutNext, Character, Grant.Id, Grant.Quantity);
                    Decision.Granted.FindOrAdd(Grant.Id) += Grant.Quantity;
                }
            }
            SetFlags(State, Stage.SetFlags, Stage.SetFlagCount);
            Finish(Mission, Stage, OutNext, Character, State, Decision);
            Decision.bApplied = true;
            return Decision;
        }
        case EPlanAction::ChoiceRequired:
            Decision.Reason = EMarburgumInteractionReason::ChoiceRequired;
            return Decision;
        case EPlanAction::RequirementNotMet:
            Decision.Reason = EMarburgumInteractionReason::RequirementNotMet;
            Decision.Item = Plan.MissingItem;
            return Decision;
        case EPlanAction::PrerequisiteIncomplete:
            Decision.Reason = EMarburgumInteractionReason::PrerequisiteIncomplete;
            return Decision;
        case EPlanAction::NothingToDo:
            break;
        }
        Decision.Reason = EMarburgumInteractionReason::NothingToDo;
        return Decision;
    }

    FMarburgumMissionDecision DecideChoice(const FMarburgumWorldRecord& Current, const FString& Character,
        const FString& TargetId, const FString& MissionId, const FString& ChoiceId, FMarburgumWorldRecord& OutNext)
    {
        FMarburgumMissionDecision Decision;
        Decision.Mission = MissionId;
        Decision.Choice = ChoiceId;
        const FMissionDefinition* Mission = FindMission(MissionId);
        const TMap<FString, FMarburgumStoredMission>* Owned = Current.Missions.Find(Character);
        const FMarburgumStoredMission* Saved = Owned ? Owned->Find(MissionId) : nullptr;
        const FMissionStageDefinition* Stage = Mission && Saved && !Saved->bCompleted ? FindStage(*Mission, Saved->Stage) : nullptr;
        const FMissionChoiceDefinition* Choice = Stage && Stage->Objective == EObjectiveType::Choice && TargetId == Stage->CompleteTarget
            ? FindChoice(*Stage, ChoiceId) : nullptr;
        if (Stage)
        {
            Decision.Stage = Stage->Id;
        }
        if (!Choice)
        {
            Decision.Reason = EMarburgumInteractionReason::UnknownChoice;
            return Decision;
        }
        const int64 Money = Current.Money.FindRef(Character);
        if (Money + Choice->MoneyDelta < 0)
        {
            Decision.Reason = EMarburgumInteractionReason::InsufficientMoney;
            return Decision;
        }

        OutNext = Current;
        ++OutNext.Revision;
        FMarburgumStoredMission& State = OutNext.Missions.FindOrAdd(Character).FindOrAdd(MissionId);
        if (Choice->MoneyDelta != 0)
        {
            int64& Held = OutNext.Money.FindOrAdd(Character);
            Held = FMath::Min(Held + Choice->MoneyDelta, MaxAmount);
            Decision.MoneyDelta += Choice->MoneyDelta;
        }
        for (int32 Index = 0; Index < Choice->SkillXpCount; ++Index)
        {
            const FSkillXp& Award = Choice->SkillXp[Index];
            if (AddSkillXp(OutNext, Character, Award.Skill, Award.Xp))
            {
                Decision.SkillXp.FindOrAdd(Award.Skill) += Award.Xp;
            }
        }
        State.Choice = Choice->Id;
        State.Outcome = Choice->Outcome;
        SetFlags(State, Stage->SetFlags, Stage->SetFlagCount);
        SetFlags(State, Choice->SetFlags, Choice->SetFlagCount);
        Decision.Outcome = State.Outcome;
        Finish(*Mission, *Stage, OutNext, Character, State, Decision);
        // Zero-money entries are not kept: a character with none is absent from the record.
        if (const int64* Held = OutNext.Money.Find(Character); Held && *Held == 0)
        {
            OutNext.Money.Remove(Character);
        }
        Decision.bApplied = true;
        return Decision;
    }
}
