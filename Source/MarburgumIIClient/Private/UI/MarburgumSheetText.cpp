#include "UI/MarburgumSheetText.h"

#include "MarburgumCharacterSheet.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumPlayerController.h"

#define LOCTEXT_NAMESPACE "MarburgumSheetText"

namespace MarburgumUI
{
    using namespace MarburgumII::Gameplay;

    FText AttributeName(int32 Index)
    {
        return Index >= 0 && Index < MarburgumII::CharacterSheet::AttributeCount ? FText::FromString(Localized(Attributes[Index].DisplayName))
                                                                                : FText::GetEmpty();
    }

    FText AttributeSummary(int32 Index)
    {
        return Index >= 0 && Index < MarburgumII::CharacterSheet::AttributeCount ? FText::FromString(Localized(Attributes[Index].Summary))
                                                                                : FText::GetEmpty();
    }

    FText SignedModifier(int32 Modifier)
    {
        if (Modifier > 0)
        {
            return FText::Format(LOCTEXT("Plus", "+{0}"), FText::AsNumber(Modifier));
        }
        if (Modifier < 0)
        {
            return FText::Format(LOCTEXT("Minus", "−{0}"), FText::AsNumber(-Modifier));
        }
        return LOCTEXT("Zero", "±0");
    }

    FText SlotName(FName SlotId)
    {
        const FEquipmentSlotDefinition* Slot = FindEquipmentSlot(SlotId.ToString());
        return Slot ? FText::FromString(Localized(Slot->DisplayName)) : FText::FromName(SlotId);
    }

    EMarburgumGlyph SlotGlyph(FName SlotId)
    {
        const FString Id = SlotId.ToString();
        if (Id == TEXT("right_hand") || Id == TEXT("left_hand") || Id == TEXT("hands"))
        {
            return EMarburgumGlyph::Use;
        }
        if (Id == TEXT("bag"))
        {
            return EMarburgumGlyph::Inventory;
        }
        return EMarburgumGlyph::Person;
    }

    FText ItemName(FName ItemId)
    {
        const FItemDefinition* Item = ItemId.IsNone() ? nullptr : FindItem(ItemId.ToString());
        return Item ? FText::FromString(Localized(Item->DisplayName)) : FText::FromName(ItemId);
    }

    namespace
    {
        bool IsHand(FName SlotId)
        {
            return SlotId == FName(TEXT("right_hand")) || SlotId == FName(TEXT("left_hand"));
        }
    }

    FText EquipVerb(FName SlotId)
    {
        if (IsHand(SlotId))
        {
            return FText::Format(LOCTEXT("HoldIn", "Hold in {0}"), SlotName(SlotId).ToLower());
        }
        if (SlotId == FName(TEXT("bag")))
        {
            return LOCTEXT("CarryAsBag", "Carry as your bag");
        }
        return FText::Format(LOCTEXT("WearOn", "Wear: {0}"), SlotName(SlotId).ToLower());
    }

    FText UnequipVerb(FName SlotId)
    {
        if (IsHand(SlotId))
        {
            return FText::Format(LOCTEXT("TakeOutOf", "Put away from {0}"), SlotName(SlotId).ToLower());
        }
        return FText::Format(LOCTEXT("TakeOff", "Take off: {0}"), SlotName(SlotId).ToLower());
    }

    FText EquipReceiptText(const FMarburgumInteractionReceipt& Receipt, ETone& OutTone)
    {
        const FText Slot = SlotName(Receipt.SlotId).ToLower();
        const FText Item = ItemName(Receipt.ItemId);
        const bool bEquip = Receipt.Effect == EMarburgumInteractionEffect::ItemEquipped;
        if (Receipt.Outcome == EMarburgumRequestOutcome::Applied)
        {
            OutTone = ETone::Success;
            return bEquip ? FText::Format(LOCTEXT("Equipped", "{0}: now in your {1}. Saved by the server; it is still in your inventory."), Item, Slot)
                          : FText::Format(LOCTEXT("Unequipped", "{0}: out of your {1} and back with your things. Saved by the server."), Item, Slot);
        }
        if (Receipt.Outcome == EMarburgumRequestOutcome::Duplicate)
        {
            OutTone = ETone::Info;
            return LOCTEXT("EquipDuplicate", "Already done. The server ignored the repeat; nothing changed twice.");
        }
        OutTone = ETone::Warning;
        switch (Receipt.Reason)
        {
        case EMarburgumInteractionReason::UnknownSlot:
            return LOCTEXT("UnknownSlot", "There is no such place to wear or hold something. Nothing was changed.");
        case EMarburgumInteractionReason::NotEquippable:
            return FText::Format(LOCTEXT("NotEquippable", "{0} cannot go in your {1}. Nothing was changed."), Item, Slot);
        case EMarburgumInteractionReason::InsufficientItems:
            OutTone = ETone::Info;
            return FText::Format(LOCTEXT("NoFreeCopy", "You have no {0} free for your {1}: every one you carry is already in use. Nothing was changed."),
                Item, Slot);
        case EMarburgumInteractionReason::NothingToDo:
            OutTone = ETone::Info;
            return Receipt.ItemId.IsNone() ? FText::Format(LOCTEXT("SlotEmpty", "Your {0} is already empty. Nothing was changed."), Slot)
                                           : FText::Format(LOCTEXT("AlreadyThere", "{0} is already in your {1}. Nothing was changed."), Item, Slot);
        case EMarburgumInteractionReason::Incapacitated:
            OutTone = ETone::Info;
            return LOCTEXT("EquipDown", "Not while you are down. Nothing was changed.");
        case EMarburgumInteractionReason::StoreUnavailable:
        case EMarburgumInteractionReason::StoreWriteFailed:
            OutTone = ETone::Danger;
            return LOCTEXT("EquipStore", "The server could not save the change, so it did not make it. Nothing was changed.");
        case EMarburgumInteractionReason::NoCharacter:
            return LOCTEXT("EquipNoCharacter", "This connection has no character, so nothing can be worn or held. Nothing was changed.");
        default:
            return FText::Format(LOCTEXT("EquipRefused", "The server refused ({0}). Nothing was changed."),
                FText::AsCultureInvariant(ToString(Receipt.Reason)));
        }
    }

    bool FEquipRequest::Submit(AMarburgumPlayerController* Controller, FName InSlot, FName InItem)
    {
        if (!Controller || IsPending())
        {
            return false;
        }
        Operation = FGuid::NewGuid();
        Slot = InSlot;
        Item = InItem;
        SentAt = FPlatformTime::Seconds();
        Controller->SubmitEquip(Operation, Slot, Item);
        return true;
    }

    bool FEquipRequest::IsPending() const
    {
        return Operation.IsValid();
    }

    bool FEquipRequest::Accept(const FMarburgumInteractionReceipt& Receipt)
    {
        if (!Operation.IsValid() || Receipt.OperationId != Operation)
        {
            return false;
        }
        Operation.Invalidate();
        return true;
    }

    bool FEquipRequest::Expire()
    {
        if (Operation.IsValid() && FPlatformTime::Seconds() - SentAt > ReceiptTimeoutSeconds)
        {
            Operation.Invalidate();
            return true;
        }
        return false;
    }
}

#undef LOCTEXT_NAMESPACE
