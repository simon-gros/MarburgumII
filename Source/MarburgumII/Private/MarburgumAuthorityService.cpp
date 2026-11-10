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
}
