#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "MarburgumChatTypes.h"
#include "MarburgumGameState.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarburgumChatSmokeSubsystem.generated.h"

class AMarburgumPlayerController;

/**
 * CHAT-001 three-client driver, active only with -MB2ChatSmoke. Each client process plays one role
 * against a real dedicated server and exits 0 (its expectations held) or 1:
 *
 *   A  sender: general, private to B, invalid recipient, invalid text, burst, end marker, then one
 *      idempotent operation submitted twice (receipt dedupe in the HUD).
 *   B  recipient: replies privately to A once A's private message arrives.
 *   C  bystander: must receive every general message once and no private or system message.
 *
 * Each client renames itself MB2ChatA|B|C through the engine's server-side ServerChangeName so the
 * others can learn its session ID; routing
 * still uses server-resolved PlayerState IDs. Message bodies are never logged: the log records
 * channel, sender, sequence and whether the body matched the expected text.
 *
 * Options: -MB2ChatRole=A|B|C -MB2ChatToken=<run token> -MB2ChatTimeout=<s> -MB2ChatShot=<png path>
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumChatSmokeSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    struct FReceived
    {
        FMarburgumChatMessage Message;
        double Seconds = 0.0;
    };

    bool Tick(float DeltaTime);
    void TickRoleA(double Now);
    void TickRoleB(double Now);

    UFUNCTION()
    void HandleChat(const FMarburgumChatMessage& Message);
    void HandleRequestResult(const FGuid& OperationId, EMarburgumRequestOutcome Outcome);

    bool ResolveRoster();
    int32 CountGeneral(const FString& Body) const;
    int32 CountPrivate(int32 SenderId, int32 RecipientId, const FString& Body) const;
    int32 CountChannel(EMarburgumChatChannel Channel) const;
    bool AnyBodyContains(const FString& Fragment) const;

    void Evaluate();
    void Fail(const FString& Reason);
    void Finish(int32 ExitCode);

    FString Body(const TCHAR* Kind) const;

    FTSTicker::FDelegateHandle TickerHandle;
    TWeakObjectPtr<AMarburgumPlayerController> Controller;

    FString Role = TEXT("C");
    FString Token = TEXT("token");
    FString ShotPath;
    double TimeoutSeconds = 120.0;
    double StartSeconds = 0.0;

    int32 IdA = INDEX_NONE;
    int32 IdB = INDEX_NONE;
    int32 IdC = INDEX_NONE;
    bool bRosterLogged = false;
    bool bRenameRequested = false;

    int32 Step = 0;
    double StepSeconds = 0.0;
    double EndSeenSeconds = 0.0;
    double ShotRequestedSeconds = 0.0;
    double LastHeartbeatLogSeconds = 0.0;
    bool bDone = false;
    bool bEvaluated = false;
    int32 PendingExit = 0;

    FGuid OperationId;
    TArray<EMarburgumRequestOutcome> Outcomes;
    TArray<FReceived> Received;
};
