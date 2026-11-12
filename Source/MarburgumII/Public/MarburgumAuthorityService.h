#pragma once

#include "CoreMinimal.h"
#include "MarburgumInteractionTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "MarburgumAuthorityService.generated.h"

class AMarburgumInteractable;
class AMarburgumPlayerController;

/**
 * Boundary between the replicated runtime types (this module) and the server-only domain logic.
 * MarburgumIIServer provides the concrete subclass, so validation, commit and persistence code is
 * compiled into the Server target only; the core module merely routes requests to it. A world without
 * a concrete service (a client, or a build without the server module) refuses every interaction.
 */
UCLASS(Abstract)
class MARBURGUMII_API UMarburgumAuthorityService : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    /** Validate and, if allowed, durably commit one interaction for the requesting player. */
    virtual FMarburgumInteractionReceipt HandleInteraction(
        AMarburgumPlayerController& Requester, const FGuid& OperationId, AMarburgumInteractable* Target)
        PURE_VIRTUAL(UMarburgumAuthorityService::HandleInteraction, return FMarburgumInteractionReceipt(););

    /** Validate and, if allowed, durably commit the requesting player's decision at a mission choice stage. */
    virtual FMarburgumInteractionReceipt HandleMissionChoice(AMarburgumPlayerController& Requester, const FGuid& OperationId,
        AMarburgumInteractable* Target, FName MissionId, FName ChoiceId)
        PURE_VIRTUAL(UMarburgumAuthorityService::HandleMissionChoice, return FMarburgumInteractionReceipt(););

    /** Validate and, if allowed, durably commit a purchase or sale at the counter Target keeps (ADR-060). */
    virtual FMarburgumInteractionReceipt HandleTrade(AMarburgumPlayerController& Requester, const FGuid& OperationId,
        AMarburgumInteractable* Target, EMarburgumTradeAction Action, FName ItemId, int32 Quantity)
        PURE_VIRTUAL(UMarburgumAuthorityService::HandleTrade, return FMarburgumInteractionReceipt(););

    /** The active service of a world, if any. */
    static UMarburgumAuthorityService* Get(const UWorld* World);
};
