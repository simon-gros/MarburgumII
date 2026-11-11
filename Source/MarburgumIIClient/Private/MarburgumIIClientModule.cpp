#include "Modules/ModuleManager.h"

#include "MarburgumClientUiHooks.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UI/MarburgumUISubsystem.h"

/**
 * Client-only event subscriber; the headless server never loads or references UMG.
 * Core raises UI hooks without knowing about widgets; the UI subsystem owns where they mount.
 */
class FMarburgumIIClientModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        ReadyHandle = FMarburgumClientUiHooks::OnLocalPlayerReady().AddRaw(
            this, &FMarburgumIIClientModule::HandlePlayerReady);
        FocusHandle = FMarburgumClientUiHooks::OnChatFocusRequested().AddRaw(
            this, &FMarburgumIIClientModule::HandleFocus);
        InventoryHandle = FMarburgumClientUiHooks::OnInventoryToggleRequested().AddRaw(
            this, &FMarburgumIIClientModule::HandleInventory);
        JournalHandle = FMarburgumClientUiHooks::OnJournalToggleRequested().AddRaw(
            this, &FMarburgumIIClientModule::HandleJournal);
        CharacterHandle = FMarburgumClientUiHooks::OnCharacterToggleRequested().AddRaw(
            this, &FMarburgumIIClientModule::HandleCharacter);
        SkillsHandle = FMarburgumClientUiHooks::OnSkillsToggleRequested().AddRaw(
            this, &FMarburgumIIClientModule::HandleSkills);
        ChoiceHandle = FMarburgumClientUiHooks::OnMissionChoiceRequested().AddRaw(
            this, &FMarburgumIIClientModule::HandleChoice);
        NumberHandle = FMarburgumClientUiHooks::OnNumberKeyPressed().AddRaw(
            this, &FMarburgumIIClientModule::HandleNumber);
    }

    virtual void ShutdownModule() override
    {
        FMarburgumClientUiHooks::OnLocalPlayerReady().Remove(ReadyHandle);
        FMarburgumClientUiHooks::OnChatFocusRequested().Remove(FocusHandle);
        FMarburgumClientUiHooks::OnInventoryToggleRequested().Remove(InventoryHandle);
        FMarburgumClientUiHooks::OnJournalToggleRequested().Remove(JournalHandle);
        FMarburgumClientUiHooks::OnCharacterToggleRequested().Remove(CharacterHandle);
        FMarburgumClientUiHooks::OnSkillsToggleRequested().Remove(SkillsHandle);
        FMarburgumClientUiHooks::OnMissionChoiceRequested().Remove(ChoiceHandle);
        FMarburgumClientUiHooks::OnNumberKeyPressed().Remove(NumberHandle);
    }

private:
    FDelegateHandle ReadyHandle;
    FDelegateHandle FocusHandle;
    FDelegateHandle InventoryHandle;
    FDelegateHandle JournalHandle;
    FDelegateHandle CharacterHandle;
    FDelegateHandle SkillsHandle;
    FDelegateHandle ChoiceHandle;
    FDelegateHandle NumberHandle;

    void HandleSkills(APlayerController* Player)
    {
        if (UMarburgumUISubsystem* UI = GetUI(Player))
        {
            UI->ToggleSkills();
        }
    }

    void HandleChoice(APlayerController* Player, AMarburgumInteractable* Target, FName MissionId)
    {
        if (UMarburgumUISubsystem* UI = GetUI(Player))
        {
            UI->ToggleChoice(Target, MissionId);
        }
    }

    void HandleNumber(APlayerController* Player, int32 Number)
    {
        if (UMarburgumUISubsystem* UI = GetUI(Player))
        {
            UI->ChooseOption(Number);
        }
    }

    static UMarburgumUISubsystem* GetUI(APlayerController* Player)
    {
        if (!Player || !Player->IsLocalController() || Player->GetNetMode() == NM_DedicatedServer ||
            !Player->GetWorld() || !Player->GetWorld()->IsGameWorld())
        {
            return nullptr;
        }
        const UGameInstance* GameInstance = Player->GetGameInstance();
        // Absent when the process cannot render (-nullrhi): there is no chat panel to mount.
        return GameInstance ? GameInstance->GetSubsystem<UMarburgumUISubsystem>() : nullptr;
    }

    void HandlePlayerReady(APlayerController* Player)
    {
        if (UMarburgumUISubsystem* UI = GetUI(Player))
        {
            UI->MountChat(Player);
        }
    }

    void HandleFocus(APlayerController* Player)
    {
        if (UMarburgumUISubsystem* UI = GetUI(Player))
        {
            UI->FocusChat();
        }
    }

    void HandleInventory(APlayerController* Player)
    {
        if (UMarburgumUISubsystem* UI = GetUI(Player))
        {
            UI->ToggleInventory();
        }
    }

    void HandleJournal(APlayerController* Player)
    {
        if (UMarburgumUISubsystem* UI = GetUI(Player))
        {
            UI->ToggleJournal();
        }
    }

    void HandleCharacter(APlayerController* Player)
    {
        if (UMarburgumUISubsystem* UI = GetUI(Player))
        {
            UI->ToggleCharacter();
        }
    }
};

IMPLEMENT_MODULE(FMarburgumIIClientModule, MarburgumIIClient)
