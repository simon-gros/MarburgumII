#include "MarburgumAuthorityService.h"

#include "Engine/World.h"

UMarburgumAuthorityService* UMarburgumAuthorityService::Get(const UWorld* World)
{
    if (!World)
    {
        return nullptr;
    }
    // Abstract base: the collection only ever holds concrete (server-module) subclasses.
    const TArray<UMarburgumAuthorityService*> Services = World->GetSubsystemArrayCopy<UMarburgumAuthorityService>();
    return Services.IsEmpty() ? nullptr : Services[0];
}

namespace MarburgumII::Gameplay
{
    const TCHAR* ToString(EMarburgumInteractionReason Reason)
    {
        switch (Reason)
        {
        case EMarburgumInteractionReason::None: return TEXT("none");
        case EMarburgumInteractionReason::InvalidOperation: return TEXT("invalid_operation");
        case EMarburgumInteractionReason::AuthorityUnavailable: return TEXT("authority_unavailable");
        case EMarburgumInteractionReason::StoreUnavailable: return TEXT("store_unavailable");
        case EMarburgumInteractionReason::StoreWriteFailed: return TEXT("store_write_failed");
        case EMarburgumInteractionReason::NoCharacter: return TEXT("no_character");
        case EMarburgumInteractionReason::OperationConflict: return TEXT("operation_conflict");
        case EMarburgumInteractionReason::UnknownTarget: return TEXT("unknown_target");
        case EMarburgumInteractionReason::OutOfRange: return TEXT("out_of_range");
        case EMarburgumInteractionReason::OutOfStock: return TEXT("out_of_stock");
        case EMarburgumInteractionReason::RequirementNotMet: return TEXT("requirement_not_met");
        case EMarburgumInteractionReason::NothingToDo: return TEXT("nothing_to_do");
        case EMarburgumInteractionReason::ChoiceRequired: return TEXT("choice_required");
        case EMarburgumInteractionReason::UnknownChoice: return TEXT("unknown_choice");
        case EMarburgumInteractionReason::InsufficientMoney: return TEXT("insufficient_money");
        case EMarburgumInteractionReason::PrerequisiteIncomplete: return TEXT("prerequisite_incomplete");
        case EMarburgumInteractionReason::NoRod: return TEXT("no_rod");
        case EMarburgumInteractionReason::TooSoon: return TEXT("too_soon");
        case EMarburgumInteractionReason::CannotFishNow: return TEXT("cannot_fish_now");
        case EMarburgumInteractionReason::TooEarly: return TEXT("too_early");
        case EMarburgumInteractionReason::NoCounter: return TEXT("no_counter");
        case EMarburgumInteractionReason::ShopClosed: return TEXT("shop_closed");
        case EMarburgumInteractionReason::InvalidQuantity: return TEXT("invalid_quantity");
        case EMarburgumInteractionReason::NotOnCounter: return TEXT("not_on_counter");
        case EMarburgumInteractionReason::ItemNotAccepted: return TEXT("item_not_accepted");
        case EMarburgumInteractionReason::InsufficientItems: return TEXT("insufficient_items");
        }
        return TEXT("unknown");
    }

    const TCHAR* ToString(EMarburgumInteractionEffect Effect)
    {
        switch (Effect)
        {
        case EMarburgumInteractionEffect::None: return TEXT("none");
        case EMarburgumInteractionEffect::ItemGranted: return TEXT("item_granted");
        case EMarburgumInteractionEffect::MissionStarted: return TEXT("mission_started");
        case EMarburgumInteractionEffect::StageCompleted: return TEXT("stage_completed");
        case EMarburgumInteractionEffect::MissionCompleted: return TEXT("mission_completed");
        case EMarburgumInteractionEffect::FishingCast: return TEXT("fishing_cast");
        case EMarburgumInteractionEffect::FishCaught: return TEXT("fish_caught");
        case EMarburgumInteractionEffect::ItemBought: return TEXT("item_bought");
        case EMarburgumInteractionEffect::ItemSold: return TEXT("item_sold");
        }
        return TEXT("unknown");
    }

    const TCHAR* ToString(EMarburgumTradeAction Action)
    {
        switch (Action)
        {
        case EMarburgumTradeAction::Buy: return TEXT("buy");
        case EMarburgumTradeAction::Sell: return TEXT("sell");
        }
        return TEXT("unknown");
    }

    const TCHAR* ToString(EMarburgumRequestOutcome Outcome)
    {
        switch (Outcome)
        {
        case EMarburgumRequestOutcome::Applied: return TEXT("applied");
        case EMarburgumRequestOutcome::Duplicate: return TEXT("duplicate");
        case EMarburgumRequestOutcome::Rejected: return TEXT("rejected");
        }
        return TEXT("unknown");
    }

    const TCHAR* ToString(EMarburgumFishingPhase Phase)
    {
        switch (Phase)
        {
        case EMarburgumFishingPhase::None: return TEXT("none");
        case EMarburgumFishingPhase::Waiting: return TEXT("waiting");
        case EMarburgumFishingPhase::Bite: return TEXT("bite");
        }
        return TEXT("unknown");
    }

    const TCHAR* ToString(EMarburgumFishingEnd End)
    {
        switch (End)
        {
        case EMarburgumFishingEnd::None: return TEXT("none");
        case EMarburgumFishingEnd::Caught: return TEXT("caught");
        case EMarburgumFishingEnd::Escaped: return TEXT("escaped");
        case EMarburgumFishingEnd::TooEarly: return TEXT("too_early");
        case EMarburgumFishingEnd::Cancelled: return TEXT("cancelled");
        }
        return TEXT("unknown");
    }
}
