#pragma once

#include "CoreMinimal.h"
#include "MarburgumInteractionTypes.h"
#include "MarburgumWorldStore.h"

/** What a request to wear, hold or take off something means for one character, decided without side effects. */
struct FMarburgumEquipDecision
{
    /** Applied: OutNext holds the complete next record (revision already incremented). */
    bool bApplied = false;
    EMarburgumInteractionReason Reason = EMarburgumInteractionReason::None;
    EMarburgumInteractionEffect Effect = EMarburgumInteractionEffect::None;
    FString Slot;
    /** The item now in the slot (equip), or the item that left it (unequip). */
    FString Item;
    /** Equip into an occupied slot: the item that was there and is back in the bag. */
    FString Replaced;
};

namespace MarburgumII::Equipment
{
    /**
     * Pure equipment rules (ADR-085), so they can be unit-tested. An equipped item never leaves the character's custody:
     * the slot names one copy of something the character holds, and each copy fills one slot at most, so equipping can
     * neither create nor duplicate an item. In order, a request is refused when:
     *  - the slot is not one of the eight (UnknownSlot);
     *  - unequip: the slot is empty (NothingToDo);
     *  - equip: the item is unknown or does not fit the slot (NotEquippable), is already in that slot (NothingToDo), or
     *    every copy the character holds already fills another slot (InsufficientItems).
     * Equipping into an occupied slot swaps: the previous item stays in the inventory, just no longer in the slot.
     * Character, store, replay and body checks belong to the caller (UMarburgumWorldAuthority).
     */
    FMarburgumEquipDecision DecideEquip(const FMarburgumWorldRecord& Current, const FString& Character, const FString& Slot,
        const FString& Item, FMarburgumWorldRecord& OutNext);

    /** Copies of Item the character holds that no slot names yet. */
    int32 FreeCopies(const FMarburgumWorldRecord& Record, const FString& Character, const FString& Item);

    /**
     * Keep the equipment invariant after any other change to Character's inventory (a sale, a mission handing an item
     * over): slots naming more copies of an item than the character still holds are emptied, in reverse sheet order, until
     * the count fits. Returns the slots emptied.
     */
    TArray<FString> Reconcile(FMarburgumWorldRecord& Record, const FString& Character);
}
