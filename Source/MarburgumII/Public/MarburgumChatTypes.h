#pragma once

#include "CoreMinimal.h"
#include "MarburgumChatTypes.generated.h"

/** Gameplay chat uses separate server-routed channels, never global GameState replication. */
UENUM(BlueprintType)
enum class EMarburgumChatChannel : uint8
{
    General UMETA(DisplayName = "Server General"),
    Private UMETA(DisplayName = "Private Conversation"),
    System UMETA(DisplayName = "System")
};

USTRUCT(BlueprintType)
struct MARBURGUMII_API FMarburgumChatMessage
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Chat")
    EMarburgumChatChannel Channel = EMarburgumChatChannel::General;

    // Game-session PlayerState ID, NOT a permanent account ID.
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Chat")
    int32 SenderPlayerId = INDEX_NONE;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Chat")
    FString SenderName;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Chat")
    int32 RecipientPlayerId = INDEX_NONE;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Chat")
    FString Text;

    // Monotonic per-sender on the server. This is not a durable database ID.
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Chat")
    int32 SenderSequence = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMarburgumChatMessageEvent, const FMarburgumChatMessage&, Message);
