#pragma once

#include "CoreMinimal.h"
#include "MarburgumGameState.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarburgumUISubsystem.generated.h"

class APlayerController;
class UMarburgumChatWidget;
class UMarburgumConnectionScreen;
class UMarburgumInteractionPrompt;
class UMarburgumInventoryPanel;
class UMarburgumJournalPanel;
class UMarburgumLocationBanner;
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

    /** Show or hide the journal_panel (J key). Opening one management screen closes the other. */
    bool ToggleJournal();
    bool IsJournalOpen() const;

    /** Mounted HUD modules, for evidence capture; null until the session is admitted. */
    const UMarburgumLocationBanner* GetLocationBanner() const { return Banner; }
    const UMarburgumPinnedObjective* GetPinnedObjective() const { return Objective; }

    /** Independent UI scale on top of DPI scaling (0.5 - 3.0). */
    static void SetInterfaceScale(float Scale);

private:
    void HandlePreLoadMap(const FString& MapName);
    void HandlePostLoadMap(UWorld* World);
    void HandleSessionChanged(const FMarburgumSessionView& View);

    void BuildForWorld(UWorld* World);
    void TearDown();
    void ApplyInputMode(bool bScreenOwnsInput);
    void BindReceipts();
    void HandleRequestResult(const FGuid& OperationId, EMarburgumRequestOutcome Outcome);
    void HandleInteractionReceipt(const FMarburgumInteractionReceipt& Receipt);
    void MountGameplayHud(APlayerController* Player);

    UPROPERTY(Transient) TObjectPtr<UMarburgumUIRoot> Root;
    UPROPERTY(Transient) TObjectPtr<UMarburgumConnectionScreen> ConnectionScreen;
    UPROPERTY(Transient) TObjectPtr<UMarburgumNotificationFeed> Feed;
    UPROPERTY(Transient) TObjectPtr<UMarburgumChatWidget> Chat;
    UPROPERTY(Transient) TObjectPtr<UMarburgumInteractionPrompt> Prompt;
    UPROPERTY(Transient) TObjectPtr<UMarburgumInventoryPanel> Inventory;
    UPROPERTY(Transient) TObjectPtr<UMarburgumJournalPanel> Journal;
    UPROPERTY(Transient) TObjectPtr<UMarburgumLocationBanner> Banner;
    UPROPERTY(Transient) TObjectPtr<UMarburgumPinnedObjective> Objective;

    TWeakObjectPtr<APlayerController> PendingChatOwner;
    TWeakObjectPtr<class AMarburgumPlayerController> ReceiptController;

    TWeakObjectPtr<UMarburgumSessionSubsystem> Session;
    FDelegateHandle PreLoadMapHandle;
    FDelegateHandle PostLoadMapHandle;
    FDelegateHandle SessionChangedHandle;

    MarburgumII::Session::EPhase LastPhase = MarburgumII::Session::EPhase::Starting;
    bool bRecoveredFromLoss = false;
    bool bGalleryMode = false;
    bool bDiagnostics = false;
};
