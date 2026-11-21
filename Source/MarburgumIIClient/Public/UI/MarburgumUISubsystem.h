#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "MarburgumGameState.h"
#include "MarburgumInteractionTypes.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UI/MarburgumManagementShell.h"
#include "MarburgumUISubsystem.generated.h"

class APlayerController;
class UMarburgumChatWidget;
class UMarburgumConnectionScreen;
class UMarburgumInteractionPrompt;
class UMarburgumInventoryPanel;
class UMarburgumJournalPanel;
class UMarburgumCharacterPanel;
class UMarburgumLocationBanner;
class UMarburgumPlayerCondition;
class UMarburgumVehicleStatus;
class UMarburgumPinnedObjective;
class UMarburgumNotificationFeed;
struct FMarburgumInteractionReceipt;
class UMarburgumUIRoot;
enum class EMarburgumChoiceClose : uint8;

/**
 * Client presentation owner. Builds the UI root for every loaded world and maps the session view onto
 * surfaces: the connection shell whenever the client is not admitted, the quiet HUD once it is.
 * Not created for dedicated servers or processes that cannot render (-nullrhi).
 *
 * Options: -MB2UIGallery shows the UI-01 component gallery on the front-end instead of the shell;
 * -MB2UIDiagnostics adds the labeled live-state diagnostic overlay; -MB2UIScale=<f> sets the
 * independent interface scale.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumUISubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    /** The account screen, for the first-launch smoke driver; null when accounts are off. */
    class UMarburgumAccountScreen* GetAccountScreen() const { return AccountScreen; }

    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /** Mount the chat_panel for a local Marburgum player controller (called from the client UI hook). */
    void MountChat(APlayerController* Player);
    /** Give keyboard focus to the chat composer when the session is admitted. */
    void FocusChat();
    /** Close the chat composer, keeping its draft (the caller decides where the keys go next). */
    void LeaveChat();

    /** Show or hide the inventory_view for the admitted local player (I key). False if not mounted yet. */
    bool ToggleInventory();
    /** Opens the Inventory (if it is not open) on ItemId's card. */
    bool ShowInventoryItem(FName ItemId);
    bool IsInventoryOpen() const;

    /** Show or hide the journal_panel (J key). Opening one management screen closes the others. */
    bool ToggleJournal();
    bool IsJournalOpen() const;

    /** Show or hide the character_panel (C key). */
    bool ToggleCharacter();
    bool IsCharacterOpen() const;

    /**
     * Escape in play: closes an open decision, counter or management screen; with none open, opens or closes the
     * system menu (back to the street, leave the world, quit).
     */
    void Back();
    /** The gamepad's B in play: closes an open decision or counter, and otherwise does nothing (no system menu). */
    void Cancel();
    bool IsSystemMenuOpen() const;

    /** Scroll the open management screen (inventory, journal, character or skills) by Lines; false if none is open. */
    bool ScrollOpenScreen(float Lines);

    /** Show or hide the skills_panel (K key, ADR-047). */
    bool ToggleSkills();
    bool IsSkillsOpen() const;

    /** Show or hide the settings_screen (O in a screen, Settings in the Escape menu). */
    bool ToggleSettings();
    bool IsSettingsOpen() const;

    /**
     * The management shell: open Screen in it (closing a decision, a counter or the Escape menu), or close it when
     * Screen is already showing. While it is open the HUD layer is hidden and the shell owns the keys (UI-only input);
     * closing hands mouse and keys back to the street.
     */
    bool ToggleScreen(EMarburgumScreen Screen);
    bool OpenScreen(EMarburgumScreen Screen);
    void CloseScreens();
    EMarburgumScreen GetOpenScreen() const { return Shell ? Shell->GetShown() : EMarburgumScreen::None; }

    /** Open the decision for Mission at Target, or close it when it is already open there (ADR-049). */
    bool ToggleChoice(class AMarburgumInteractable* Target, FName MissionId);
    /** Pick option Number in the open decision; false when none is open or it cannot be chosen. */
    bool ChooseOption(int32 Number);
    bool IsChoiceOpen() const;

    /** Open the counter Keeper keeps, or close the open one; a null keeper only closes (B key, ADR-060). */
    bool ToggleShop(class AMarburgumInteractable* Keeper);
    /** Buy one of line Number at the open counter, or sell one back with bSell; false when no counter is open. */
    bool TradeAtShop(int32 Number, bool bSell);
    bool IsShopOpen() const;

    /** Mounted HUD modules, for evidence capture; null until the session is admitted. */
    const UMarburgumLocationBanner* GetLocationBanner() const { return Banner; }
    const UMarburgumPinnedObjective* GetPinnedObjective() const { return Objective; }
    const UMarburgumPlayerCondition* GetPlayerCondition() const { return Condition; }
    const UMarburgumVehicleStatus* GetVehicleStatus() const { return VehicleStatus; }
    const UMarburgumCharacterPanel* GetCharacterPanel() const { return CharacterPanel; }
    const class UMarburgumSkillsPanel* GetSkillsPanel() const { return SkillsPanel; }
    class UMarburgumChoicePanel* GetChoicePanel() const { return ChoicePanel; }
    const UMarburgumInteractionPrompt* GetInteractionPrompt() const { return Prompt; }
    const class UMarburgumShopPanel* GetShopPanel() const { return ShopPanel; }
    class UMarburgumSettingsPanel* GetSettingsPanel() const { return SettingsPanel; }
    const UMarburgumManagementShell* GetShell() const { return Shell; }

    /** Independent UI scale on top of DPI scaling (0.5 - 3.0). */
    static void SetInterfaceScale(float Scale);

private:
    void HandlePreLoadMap(const FString& MapName);
    void HandlePostLoadMap(UWorld* World);
    void HandleSessionChanged(const FMarburgumSessionView& View);
    void HandleAccountChanged(const struct FMarburgumAccountView& View);
    /** Which front-end screen shows: the account screens until a character is ready, then the connection screen. */
    void ApplyFrontEnd();

    void BuildForWorld(UWorld* World);
    void TearDown();
    void ApplyInputMode(bool bScreenOwnsInput);
    void BindReceipts();
    void HandleRequestResult(const FGuid& OperationId, EMarburgumRequestOutcome Outcome);
    void HandleInteractionReceipt(const FMarburgumInteractionReceipt& Receipt);
    void HandleCombatResult(const struct FMarburgumCombatResult& Result);
    void HandleFishingChanged(class AMarburgumPlayerState* State);
    /** The decision card closed (chosen, cancelled or dismissed): the prompt comes back and the keys go to the street. */
    void HandleChoiceClosed(EMarburgumChoiceClose Why);
    void MountGameplayHud(APlayerController* Player);
    /** Closes every open management screen except Keep (nullptr closes all). */
    void CloseScreensExcept(const UObject* Keep);
    void SetSystemMenuOpen(bool bOpen);
    /** Collapses the exploration HUD under a screen or the Escape menu; shown again it takes the HUD opacity preference. */
    void SetHudHidden(bool bHidden);
    /** UI-only input focused on Focus (or Fallback), with the cursor; or the street's game-only input. */
    void GiveInputTo(UWidget* Focus, UWidget* Fallback);
    void ReturnInputToWorld();
    void LogScreen(EMarburgumScreen Screen) const;
    /** After a settings change that alters the theme tokens: rebuild every HUD and screen widget, re-open what was open. */
    void RebuildPresentation();

    UPROPERTY(Transient) TObjectPtr<UMarburgumUIRoot> Root;
    UPROPERTY(Transient) TObjectPtr<UMarburgumConnectionScreen> ConnectionScreen;
    UPROPERTY(Transient) TObjectPtr<class UMarburgumAccountScreen> AccountScreen;
    UPROPERTY(Transient) TObjectPtr<UMarburgumNotificationFeed> Feed;
    UPROPERTY(Transient) TObjectPtr<UMarburgumChatWidget> Chat;
    UPROPERTY(Transient) TObjectPtr<UMarburgumInteractionPrompt> Prompt;
    UPROPERTY(Transient) TObjectPtr<UMarburgumInventoryPanel> Inventory;
    UPROPERTY(Transient) TObjectPtr<UMarburgumJournalPanel> Journal;
    UPROPERTY(Transient) TObjectPtr<UMarburgumLocationBanner> Banner;
    UPROPERTY(Transient) TObjectPtr<UMarburgumPinnedObjective> Objective;
    UPROPERTY(Transient) TObjectPtr<UMarburgumPlayerCondition> Condition;
    UPROPERTY(Transient) TObjectPtr<UMarburgumVehicleStatus> VehicleStatus;
    UPROPERTY(Transient) TObjectPtr<UMarburgumCharacterPanel> CharacterPanel;
    UPROPERTY(Transient) TObjectPtr<class UMarburgumSkillsPanel> SkillsPanel;
    UPROPERTY(Transient) TObjectPtr<class UMarburgumChoicePanel> ChoicePanel;
    UPROPERTY(Transient) TObjectPtr<class UMarburgumShopPanel> ShopPanel;
    UPROPERTY(Transient) TObjectPtr<class UMarburgumSystemMenu> SystemMenu;
    UPROPERTY(Transient) TObjectPtr<UMarburgumManagementShell> Shell;
    UPROPERTY(Transient) TObjectPtr<class UMarburgumSettingsPanel> SettingsPanel;
    /** Settings was opened from the Escape menu, so closing it goes back there. */
    bool bSettingsFromMenu = false;
    FDelegateHandle FishingHandle;
    /** The shutter (ADR-081): a brief white flash over the world on the Notifications layer, fading on a ticker. */
    void HandlePhotographTaken(APlayerController* Player);
    FDelegateHandle PhotographHandle;
    FTSTicker::FDelegateHandle ShutterTicker;
    UPROPERTY(Transient) TObjectPtr<class UBorder> ShutterFlash;
    float ShutterLeft = 0.f;
    int32 LastFishingAttempt = 0;
    EMarburgumFishingEnd LastFishingEnd = EMarburgumFishingEnd::None;

    TWeakObjectPtr<APlayerController> PendingChatOwner;
    TWeakObjectPtr<class AMarburgumPlayerController> ReceiptController;

    TWeakObjectPtr<UMarburgumSessionSubsystem> Session;
    FDelegateHandle PreLoadMapHandle;
    FDelegateHandle PostLoadMapHandle;
    FDelegateHandle SessionChangedHandle;
    TWeakObjectPtr<class UMarburgumAccountSubsystem> Accounts;
    FDelegateHandle AccountChangedHandle;

    MarburgumII::Session::EPhase LastPhase = MarburgumII::Session::EPhase::Starting;
    bool bRecoveredFromLoss = false;
    bool bGalleryMode = false;
    bool bDiagnostics = false;
};
