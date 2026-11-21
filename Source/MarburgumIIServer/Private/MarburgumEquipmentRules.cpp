#include "MarburgumEquipmentRules.h"

#include "MarburgumGameplayContract.h"

namespace MarburgumII::Equipment
{
    using namespace MarburgumII::Gameplay;

    int32 FreeCopies(const FMarburgumWorldRecord& Record, const FString& Character, const FString& Item)
    {
        const TMap<FString, int32>* Owned = Record.Inventories.Find(Character);
        const int32 Held = Owned ? FMath::Max(0, Owned->FindRef(Item)) : 0;
        int32 Filled = 0;
        if (const TMap<FString, FString>* Slots = Record.Equipment.Find(Character))
        {
            for (const TPair<FString, FString>& Pair : *Slots)
            {
                Filled += Pair.Value == Item ? 1 : 0;
            }
        }
        return Held - Filled;
    }

    FMarburgumEquipDecision DecideEquip(const FMarburgumWorldRecord& Current, const FString& Character, const FString& Slot,
        const FString& Item, FMarburgumWorldRecord& OutNext)
    {
        FMarburgumEquipDecision Decision;
        Decision.Slot = Slot;
        auto Refuse = [&Decision](EMarburgumInteractionReason Reason)
        {
            Decision.Reason = Reason;
            return Decision;
        };
        if (!FindEquipmentSlot(Slot))
        {
            return Refuse(EMarburgumInteractionReason::UnknownSlot);
        }
        const TMap<FString, FString>* Slots = Current.Equipment.Find(Character);
        const FString* Worn = Slots ? Slots->Find(Slot) : nullptr;

        if (Item.IsEmpty())
        {
            if (!Worn)
            {
                return Refuse(EMarburgumInteractionReason::NothingToDo);
            }
            OutNext = Current;
            ++OutNext.Revision;
            Decision.Item = *Worn;
            TMap<FString, FString>& NextSlots = OutNext.Equipment.FindChecked(Character);
            NextSlots.Remove(Slot);
            if (NextSlots.IsEmpty())
            {
                OutNext.Equipment.Remove(Character);
            }
            Decision.bApplied = true;
            Decision.Effect = EMarburgumInteractionEffect::ItemUnequipped;
            return Decision;
        }

        Decision.Item = Item;
        if (!FindItem(Item) || !FitsSlot(Item, Slot))
        {
            return Refuse(EMarburgumInteractionReason::NotEquippable);
        }
        if (Worn && *Worn == Item)
        {
            return Refuse(EMarburgumInteractionReason::NothingToDo);
        }
        if (FreeCopies(Current, Character, Item) < 1)
        {
            return Refuse(EMarburgumInteractionReason::InsufficientItems);
        }
        OutNext = Current;
        ++OutNext.Revision;
        Decision.Replaced = Worn ? *Worn : FString();
        OutNext.Equipment.FindOrAdd(Character).Add(Slot, Item);
        Decision.bApplied = true;
        Decision.Effect = EMarburgumInteractionEffect::ItemEquipped;
        return Decision;
    }

    TArray<FString> Reconcile(FMarburgumWorldRecord& Record, const FString& Character)
    {
        TArray<FString> Emptied;
        TMap<FString, FString>* Slots = Record.Equipment.Find(Character);
        if (!Slots)
        {
            return Emptied;
        }
        // Reverse sheet order: a bag or an off hand gives way before the right hand and the clothes.
        for (int32 Index = UE_ARRAY_COUNT(EquipmentSlots) - 1; Index >= 0; --Index)
        {
            const FString SlotId = EquipmentSlots[Index].Id;
            const FString* Worn = Slots->Find(SlotId);
            if (Worn && FreeCopies(Record, Character, *Worn) < 0)
            {
                Slots->Remove(SlotId);
                Emptied.Add(SlotId);
            }
        }
        if (Slots->IsEmpty())
        {
            Record.Equipment.Remove(Character);
        }
        return Emptied;
    }
}
