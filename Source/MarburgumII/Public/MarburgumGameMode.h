#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MarburgumGameMode.generated.h"

class AMarburgumPlayerState;

/**
 * Server-authoritative entry point, also used by Editor/standalone development.
 *
 * Admission reads the development character key (?MB2Character=, ADR-014): a malformed key or a key
 * that is already in play on this server is refused, so one character never has two concurrent
 * sessions holding its inventory. A connection without a key is admitted but cannot interact.
 */
UCLASS()
class MARBURGUMII_API AMarburgumGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AMarburgumGameMode();

    virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
    virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal = TEXT("")) override;

private:
    /** Empty when the key may be admitted; otherwise the refusal shown to the client. */
    FString CheckCharacterKey(const FString& Key) const;
    AMarburgumPlayerState* FindPlayerByCharacterKey(const FString& Key) const;
};
