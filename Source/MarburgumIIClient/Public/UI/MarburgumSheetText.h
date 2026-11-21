#pragma once

#include "CoreMinimal.h"
#include "MarburgumInteractionTypes.h"
#include "UI/MarburgumUITheme.h"
#include "UI/MarburgumUIWidgets.h"

class AMarburgumPlayerController;

/**
 * How the interface names the character sheet (ADR-085): the four attributes, the eight equipment slots and the
 * server's answers to equipping, so the Character sheet, the Inventory and the notifications never word them
 * differently. Catalogue names go through MarburgumII::Gameplay::Localized; the rest is LOCTEXT.
 */
namespace MarburgumUI
{
    /** "Might", "Endurance", "Eye", "Poise" by catalogue index. */
    MARBURGUMIICLIENT_API FText AttributeName(int32 Index);
    /** One sentence on what the score stands for (Endurance says it is not stamina). */
    MARBURGUMIICLIENT_API FText AttributeSummary(int32 Index);
    /** "+2", "−1", "±0". */
    MARBURGUMIICLIENT_API FText SignedModifier(int32 Modifier);

    /** "Right hand", from the catalogue; the id itself for an unknown slot. */
    MARBURGUMIICLIENT_API FText SlotName(FName SlotId);
    MARBURGUMIICLIENT_API EMarburgumGlyph SlotGlyph(FName SlotId);
    MARBURGUMIICLIENT_API FText ItemName(FName ItemId);

    /** "Hold in right hand" for a hand, "Wear on head" for clothes, "Carry in bag" for the bag. */
    MARBURGUMIICLIENT_API FText EquipVerb(FName SlotId);
    /** "Take out of right hand", "Take off head" … */
    MARBURGUMIICLIENT_API FText UnequipVerb(FName SlotId);

    /** What an equipping receipt means, in one sentence; Tone is set to how it should look. */
    MARBURGUMIICLIENT_API FText EquipReceiptText(const FMarburgumInteractionReceipt& Receipt, ETone& OutTone);

    /**
     * One equipping request in flight from a sheet: submits it, recognises its receipt, and times out the way every other
     * request does (ReceiptTimeoutSeconds). The sheet shows Pending/Refused/Done only from this, never from a guess.
     */
    struct MARBURGUMIICLIENT_API FEquipRequest
    {
        FGuid Operation;
        FName Slot;
        FName Item;
        double SentAt = 0.0;

        /** Send through Controller; false (and nothing sent) without one or while another request is in flight. */
        bool Submit(AMarburgumPlayerController* Controller, FName InSlot, FName InItem);
        bool IsPending() const;
        /** True when Receipt answers this request; the request is then finished. */
        bool Accept(const FMarburgumInteractionReceipt& Receipt);
        /** True once if a pending request has gone unanswered too long; the request is then finished. */
        bool Expire();
    };
}
