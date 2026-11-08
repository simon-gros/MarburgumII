#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "MarburgumNetworkContract.h"
#include "Modules/ModuleManager.h"

// Server-specific services belong here; gameplay authority is also enforced by server-only GameMode.
// For now the module emits authority-side admission telemetry used as network smoke evidence.
class FMarburgumIIServerModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        WorldReadyHandle = FWorldDelegates::OnWorldInitializedActors.AddRaw(this, &FMarburgumIIServerModule::HandleWorldReady);
        PostLoginHandle = FGameModeEvents::GameModePostLoginEvent.AddRaw(this, &FMarburgumIIServerModule::HandlePostLogin);
        LogoutHandle = FGameModeEvents::GameModeLogoutEvent.AddRaw(this, &FMarburgumIIServerModule::HandleLogout);
    }

    virtual void ShutdownModule() override
    {
        FWorldDelegates::OnWorldInitializedActors.Remove(WorldReadyHandle);
        FGameModeEvents::GameModePostLoginEvent.Remove(PostLoginHandle);
        FGameModeEvents::GameModeLogoutEvent.Remove(LogoutHandle);
    }

private:
    void HandleWorldReady(const FActorsInitializedParams& Params)
    {
        const UWorld* World = Params.World;
        if (!World || !World->IsGameWorld() || World->GetNetMode() != NM_DedicatedServer)
        {
            return;
        }

        // UEngine::LoadMap starts listening before actors are initialized.
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=ready map=%s port=%d contract=%d"),
            MarburgumII::Network::SmokeLogPrefix,
            *World->GetMapName(),
            World->URL.Port,
            MarburgumII::Network::ContractVersion);
    }

    static bool IsNetworkedAuthority(const AGameModeBase* GameMode)
    {
        // Editor -game processes load server modules too; ignore their local standalone worlds.
        return GameMode && GameMode->GetNetMode() != NM_Standalone;
    }

    void HandlePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer)
    {
        if (!IsNetworkedAuthority(GameMode))
        {
            return;
        }
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=login player=%d players=%d"),
            MarburgumII::Network::SmokeLogPrefix,
            NewPlayer && NewPlayer->PlayerState ? NewPlayer->PlayerState->GetPlayerId() : -1,
            GameMode->GetNumPlayers());
    }

    void HandleLogout(AGameModeBase* GameMode, AController* Exiting)
    {
        if (!IsNetworkedAuthority(GameMode))
        {
            return;
        }
        // Called before the exiting controller is cleaned up, so it is still counted.
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=logout player=%d players=%d"),
            MarburgumII::Network::SmokeLogPrefix,
            Exiting && Exiting->PlayerState ? Exiting->PlayerState->GetPlayerId() : -1,
            FMath::Max(GameMode->GetNumPlayers() - 1, 0));
    }

    FDelegateHandle WorldReadyHandle;
    FDelegateHandle PostLoginHandle;
    FDelegateHandle LogoutHandle;
};

IMPLEMENT_MODULE(FMarburgumIIServerModule, MarburgumIIServer)
