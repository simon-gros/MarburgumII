#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MarburgumChatTypes.h"
#include "MarburgumCombatTypes.h"
#include "MarburgumGameState.h"
#include "MarburgumInteractionTypes.h"
#include "MarburgumPlayerController.generated.h"

class AMarburgumInteractable;
class AMarburgumVehicle;

DECLARE_MULTICAST_DELEGATE_TwoParams(FMarburgumRequestResultEvent, const FGuid& /*OperationId*/, EMarburgumRequestOutcome /*Outcome*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FMarburgumInteractionReceiptEvent, const FMarburgumInteractionReceipt& /*Receipt*/);
/** The server's answer to getting into or out of a car: action enter|exit, a wire reason when refused. */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FMarburgumVehicleResultEvent, bool /*bOk*/, FName /*Action*/, FName /*Reason*/);
/** The server's ruling on one of this player's swings (ADR-041). */
DECLARE_MULTICAST_DELEGATE_OneParam(FMarburgumCombatResultEvent, const FMarburgumCombatResult& /*Result*/);

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
    virtual void PlayerTick(float DeltaTime) override;

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

    // ---- Vehicles (ADR-039) ----

    /**
     * Client: the car E would get this player into, and which seat: the nearest car with a free seat whose
     * door is within reach (the driver's whenever it is). Null while seated or when none is in reach.
     */
    AMarburgumVehicle* FindVehicleCandidate(FName& OutSeat) const;

    /** Client: ask to get into a car (seat None = the driver's) or out of the one this pawn sits in. */
    void RequestEnterVehicle(AMarburgumVehicle* Vehicle, FName SeatId);
    void RequestExitVehicle();

    /** Client: true from a request until the server's answer (or ReceiptTimeoutSeconds). */
    bool IsVehicleRequestPending() const;

    /**
     * Client, test drivers only: hold these pedals instead of the keyboard's until cleared. Throttle -1..1,
     * brake 0..1, steer -1 (left)..1 (right). The server still decides what the car does.
     */
    void SetDrivingOverride(float Throttle, float Brake, float Steer, bool bHandbrake);
    void ClearDrivingOverride();

    /** Client-local notification when the server answers a vehicle request. */
    FMarburgumVehicleResultEvent OnVehicleResult;

    /** Client-local notification when the server rules on one of this player's swings. */
    FMarburgumCombatResultEvent OnCombatResult;

    /** Server -> owning client: how a swing went. Sent by the pawn, which rules it. */
    UFUNCTION(Client, Reliable)
    void ClientReceiveCombatResult(const FMarburgumCombatResult& Result);

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

    UFUNCTION(Server, Reliable)
    void ServerEnterVehicle(AMarburgumVehicle* Vehicle, FName SeatId);

    UFUNCTION(Server, Reliable)
    void ServerExitVehicle();

    /** The driver's held intent; unreliable and resent while held, so a lost packet only delays it. */
    UFUNCTION(Server, Unreliable)
    void ServerSetVehicleInput(float Throttle, float Brake, float Steer, bool bHandbrake);

    UFUNCTION(Client, Reliable)
    void ClientReceiveVehicleResult(bool bOk, FName Action, FName Reason);

private:
    AMarburgumGameState* GetMarburgumGameState() const;

    UFUNCTION(Server, Reliable)
    void ServerSendChat(EMarburgumChatChannel Channel, int32 RecipientPlayerId, const FString& RawText);

    UFUNCTION(Client, Reliable)
    void ClientReceiveChat(const FMarburgumChatMessage& Message);

    void SendSystemNotice(const FString& Notice);
    void RequestChatFocus();
    void RequestToggleInventory();
    void RequestToggleJournal();
    void RequestToggleCharacter();

    /** Shared request budget for interactions and vehicle requests (MaxRequestsPerSecond). */
    bool AdmitRequest(const TCHAR* What);
    void UpdateDriving();

    double VehicleRequestAt = -1000.0;
    bool bDrivingOverride = false;
    float OverrideThrottle = 0.f;
    float OverrideBrake = 0.f;
    float OverrideSteer = 0.f;
    bool bOverrideHandbrake = false;
    float SentThrottle = 0.f;
    float SentBrake = 0.f;
    float SentSteer = 0.f;
    bool bSentHandbrake = false;
    double SentAt = -1000.0;

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
