#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MarburgumChatTypes.h"
#include "MarburgumPlayerController.generated.h"

/**
 * Player-owned RPC boundary: the server derives sender identity from PlayerState,
 * never from the client's payload. Private chat is not replicated to bystanders.
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

    /** Server-approved deliveries only. Local history is bounded and nonpersistent. */
    UPROPERTY(BlueprintAssignable, Category = "Marburgum|Chat")
    FMarburgumChatMessageEvent OnChatReceived;

    UPROPERTY(BlueprintReadOnly, Transient, Category = "Marburgum|Chat")
    TArray<FMarburgumChatMessage> RecentChat;

private:
    UFUNCTION(Server, Reliable)
    void ServerSendChat(EMarburgumChatChannel Channel, int32 RecipientPlayerId, const FString& RawText);

    UFUNCTION(Client, Reliable)
    void ClientReceiveChat(const FMarburgumChatMessage& Message);

    void SendSystemNotice(const FString& Notice);
    void RequestChatFocus();

    double LastAcceptedAt = -10000.0;
    double LastSystemNoticeAt = -10000.0;
    TArray<double> AcceptedTimestamps;
    int32 NextMessageSequence = 0;
};
