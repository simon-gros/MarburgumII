#include "Modules/ModuleManager.h"

#include "MarburgumChatWidget.h"
#include "MarburgumClientUiHooks.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Blueprint/UserWidget.h"

/** Client-only event subscriber; the headless server never loads or references UMG. */
class FMarburgumIIClientModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        ReadyHandle = FMarburgumClientUiHooks::OnLocalPlayerReady().AddRaw(
            this, &FMarburgumIIClientModule::HandlePlayerReady);
        FocusHandle = FMarburgumClientUiHooks::OnChatFocusRequested().AddRaw(
            this, &FMarburgumIIClientModule::HandleFocus);
    }

    virtual void ShutdownModule() override
    {
        FMarburgumClientUiHooks::OnLocalPlayerReady().Remove(ReadyHandle);
        FMarburgumClientUiHooks::OnChatFocusRequested().Remove(FocusHandle);
        ChatWidgets.Empty();
    }

private:
    FDelegateHandle ReadyHandle;
    FDelegateHandle FocusHandle;
    TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<UMarburgumChatWidget>> ChatWidgets;

    void HandlePlayerReady(APlayerController* Player)
    {
        if (!Player || !Player->IsLocalController() || Player->GetNetMode() == NM_DedicatedServer ||
            !Player->GetWorld() || !Player->GetWorld()->IsGameWorld())
        {
            return;
        }

        if (const TWeakObjectPtr<UMarburgumChatWidget>* Existing = ChatWidgets.Find(TWeakObjectPtr<APlayerController>(Player)))
        {
            if (Existing->IsValid())
            {
                return;
            }
        }
        UMarburgumChatWidget* Widget = CreateWidget<UMarburgumChatWidget>(
            Player, UMarburgumChatWidget::StaticClass());
        if (Widget)
        {
            Widget->AddToViewport(50);
            ChatWidgets.Add(TWeakObjectPtr<APlayerController>(Player), TWeakObjectPtr<UMarburgumChatWidget>(Widget));
        }
    }

    void HandleFocus(APlayerController* Player)
    {
        if (const TWeakObjectPtr<UMarburgumChatWidget>* Widget = ChatWidgets.Find(Player))
        {
            if (Widget->IsValid())
            {
                Widget->Get()->FocusComposer();
            }
        }
    }
};

IMPLEMENT_MODULE(FMarburgumIIClientModule, MarburgumIIClient)
