#pragma once

#include "CoreMinimal.h"
#include "MarburgumGameState.h"
#include "MarburgumInteractionTypes.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "Subsystems/GameInstanceSubsystem.h"
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

    /** Show or hide the inventory_view for the admitted local player (I key). False if not mounted yet. */
    bool ToggleInventory();
    bool IsInventoryOpen() const;

    /** Show or hide the journal_panel (J key). Opening one management screen closes the others. */
    bool ToggleJournal();
    bool IsJournalOpen() const;

    /** Show or hide the character_panel (C key). */
    bool ToggleCharacter();
    bool IsCharacterOpen() const;

    /** Show or hide the skills_panel (K key, ADR-047). */
    bool ToggleSkills();
    bool IsSkillsOpen() const;

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
    const class UMarburgumChoicePanel* GetChoicePanel() const { return ChoicePanel; }
    const class UMarburgumShopPanel* GetShopPanel() const { return ShopPanel; }

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
    void MountGameplayHud(APlayerController* Player);
    /** Closes every open management screen except Keep (nullptr closes all). */
    void CloseScreensExcept(const UObject* Keep);

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
    FDelegateHandle FishingHandle;
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
