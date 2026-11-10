#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MarburgumChatTypes.h"
#include "MarburgumGameState.h"
#include "MarburgumInteractionTypes.h"
#include "MarburgumPlayerController.generated.h"

class AMarburgumInteractable;

DECLARE_MULTICAST_DELEGATE_TwoParams(FMarburgumRequestResultEvent, const FGuid& /*OperationId*/, EMarburgumRequestOutcome /*Outcome*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FMarburgumInteractionReceiptEvent, const FMarburgumInteractionReceipt& /*Receipt*/);

/**
 * Player-owned RPC boundary: the server derives sender identity from PlayerState,
 * never from the client's payload. Private chat is not replicated to bystanders.
 * Clients submit requests; only the server decides and commits outcomes.
 */
UCLASS()
class MARBURGUMII_API AMarburgumPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;

    /** Request a plain-text message for all connected players on this logical server. */
    UFUNCTION(BlueprintCallable, Category = "Marburgum|Chat")
    void SendGeneralChat(const FString& Text);

    /** RecipientPlayerId is session-scoped; use the connected player list, not display names. */
    UFUNCTION(BlueprintCallable, Category = "Marburgum|Chat")
    void SendPrivateChat(int32 RecipientPlayerId, const FString& Text);

    /** Submit an idempotent operation. Re-sending the same id must never apply twice. */
    UFUNCTION(Server, Reliable)
    void ServerSubmitOperation(FGuid OperationId);

    /** Ask the server to change the network contract version. Clients hold no such authority. */
    UFUNCTION(Server, Reliable)
    void ServerRequestContractVersion(int32 RequestedVersion);

    /** Client-local notification when the server answers a request. */
    FMarburgumRequestResultEvent OnRequestResult;

    /**
     * Client: the interactable the prompt offers and E would request — the nearest one whose range
     * contains this player's pawn. Proximity mirrors the original Marburgum interaction ranges.
     */
    AMarburgumInteractable* FindInteractionCandidate() const;

    /** Client: request the current candidate with a fresh operation id (bound to E). */
    void RequestInteract();

    /**
     * Client: submit an interaction request with an explicit operation id. Re-submitting the same id
     * is safe: the server answers Duplicate and changes nothing.
     */
    void SubmitInteraction(const FGuid& OperationId, AMarburgumInteractable* Target);

    /** Client: the request awaiting a receipt, if any (expires after ReceiptTimeoutSeconds). */
    bool GetPendingInteraction(FGuid& OutOperationId, TWeakObjectPtr<AMarburgumInteractable>& OutTarget) const;

    /** Client-local notification when the server answers an interaction request. */
    FMarburgumInteractionReceiptEvent OnInteractionReceipt;

    /** Server-approved deliveries only. Local history is bounded and nonpersistent. */
    UPROPERTY(BlueprintAssignable, Category = "Marburgum|Chat")
    FMarburgumChatMessageEvent OnChatReceived;

    UPROPERTY(BlueprintReadOnly, Transient, Category = "Marburgum|Chat")
    TArray<FMarburgumChatMessage> RecentChat;

protected:
    UFUNCTION(Client, Reliable)
    void ClientReceiveRequestResult(FGuid OperationId, EMarburgumRequestOutcome Outcome);

    /** The server decides; the client only names the operation and the target it chose. */
    UFUNCTION(Server, Reliable)
    void ServerRequestInteraction(FGuid OperationId, AMarburgumInteractable* Target);

    UFUNCTION(Client, Reliable)
    void ClientReceiveInteractionReceipt(const FMarburgumInteractionReceipt& Receipt);

private:
    AMarburgumGameState* GetMarburgumGameState() const;

    UFUNCTION(Server, Reliable)
    void ServerSendChat(EMarburgumChatChannel Channel, int32 RecipientPlayerId, const FString& RawText);

    UFUNCTION(Client, Reliable)
    void ClientReceiveChat(const FMarburgumChatMessage& Message);

    void SendSystemNotice(const FString& Notice);
    void RequestChatFocus();
    void RequestToggleInventory();

    FGuid PendingOperationId;
    TWeakObjectPtr<AMarburgumInteractable> PendingTarget;
    double PendingSinceSeconds = 0.0;

    double RequestWindowStartSeconds = 0.0;
    int32 RequestsInWindow = 0;

    double LastAcceptedAt = -10000.0;
    double LastSystemNoticeAt = -10000.0;
    TArray<double> AcceptedTimestamps;
    int32 NextMessageSequence = 0;
};
