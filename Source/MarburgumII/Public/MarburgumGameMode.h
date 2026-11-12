#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MarburgumAdmissionService.h"
#include "MarburgumGameMode.generated.h"

class AMarburgumPlayerState;

/**
 * Server-authoritative entry point, also used by Editor/standalone development.
 *
 * Admission reads the development character key (?MB2Character=, ADR-014): a malformed key or a key
 * that is already in play on this server is refused, so one character never has two concurrent
 * sessions holding its inventory. A connection without a key is admitted but cannot interact.
 *
 * Account admission (ADR-055) replaces that whenever the server requires accounts (-MB2Accounts=, and always in
 * a shipping build): a join must bring a single-use ?MB2Ticket=, which the server spends at the account service
 * before the connection is admitted. The character it names becomes the player's store key, display name and
 * body; development keys are refused, and the client cannot rename itself.
 *
 * Spawn selection: a development join option ?MB2Start=<tag> picks a PlayerStart with that
 * PlayerStartTag; otherwise starts tagged `arrival`, otherwise any start. Among candidates the first
 * one nobody is standing on wins, so test clients land in predictable places.
 *
 * Fighting NPCs (ADR-041): on Lent the server spawns the original's fight cast (MarburgumII::Combat::Npcs) at
 * their original spots when play starts, each on the ground found there, with a start tagged fight_<id> a
 * few steps in front of them for test clients. They are not saved in the map, so the map stays the world's.
 */
UCLASS()
class MARBURGUMII_API AMarburgumGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AMarburgumGameMode();

    virtual void PreLoginAsync(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
        const FOnPreLoginCompleteDelegate& OnComplete) override;
    virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
    virtual void ChangeName(AController* Controller, const FString& NewName, bool bNameChange) override;
    virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal = TEXT("")) override;
    virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
    virtual void Logout(AController* Exiting) override;
    virtual void StartPlay() override;

private:
    /** Empty when the key may be admitted; otherwise the refusal shown to the client. */
    FString CheckCharacterKey(const FString& Key) const;
    AMarburgumPlayerState* FindPlayerByCharacterKey(const FString& Key) const;
    void SpawnFighters();
    /** Whether joins need an admission ticket (ADR-055). */
    bool AccountsRequired() const;
    FString InitAccountPlayer(APlayerController* NewPlayerController, const FString& Options);

    /** Characters whose ticket was spent in PreLoginAsync, by ticket, until InitNewPlayer takes them. */
    TMap<FString, FMarburgumAdmittedCharacter> Admitted;

    /** ?MB2Start= of players still being admitted; read by ChoosePlayerStart. */
    TMap<TObjectKey<AController>, FName> RequestedStarts;
};
