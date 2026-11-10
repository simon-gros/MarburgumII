#pragma once

#include "CoreMinimal.h"
#include "MarburgumGameState.h"
#include "MarburgumInteractionTypes.generated.h"

/** Why the server refused an interaction. `None` accompanies Applied and Duplicate. */
UENUM(BlueprintType)
enum class EMarburgumInteractionReason : uint8
{
    None,
    InvalidOperation,     // Missing operation id.
    AuthorityUnavailable, // No authoritative interaction service in this world.
    StoreUnavailable,     // The durable world store could not be loaded; nothing can be committed.
    StoreWriteFailed,     // The commit could not be made durable, so it was not applied.
    NoCharacter,          // The connection has no development character identity.
    OperationConflict,    // The operation id already belongs to another character's receipt.
    UnknownTarget,        // Not an interactable in this world.
    OutOfRange,           // The player's pawn is too far from the target.
    OutOfStock            // The limited resource is exhausted.
};

/** A validated stack of one item definition in a character's custody (ontology `Inventory`). */
USTRUCT(BlueprintType)
struct FMarburgumItemStack
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Inventory")
    FName ItemId;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Inventory")
    int32 Quantity = 0;
};

/**
 * Authoritative answer to one interaction request (ontology `authoritative_receipt`). Applied means the
 * change was durably stored before it was replicated; Duplicate repeats the original committed grant of
 * the same operation id and changes nothing; Rejected changes nothing.
 */
USTRUCT(BlueprintType)
struct FMarburgumInteractionReceipt
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Interaction")
    FGuid OperationId;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Interaction")
    EMarburgumRequestOutcome Outcome = EMarburgumRequestOutcome::Rejected;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Interaction")
    EMarburgumInteractionReason Reason = EMarburgumInteractionReason::None;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Interaction")
    FName InteractableId;

    /** Item granted by the committed operation (Applied/Duplicate only). */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Interaction")
    FName ItemId;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Interaction")
    int32 Quantity = 0;

    /** Stock of the target after this decision; -1 when unknown. */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Interaction")
    int32 StockRemaining = -1;

    /** World-store revision that recorded the grant; 0 for refusals. */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Interaction")
    int64 WorldRevision = 0;
};

namespace MarburgumII::Gameplay
{
    MARBURGUMII_API const TCHAR* ToString(EMarburgumInteractionReason Reason);
    MARBURGUMII_API const TCHAR* ToString(EMarburgumRequestOutcome Outcome);
}
