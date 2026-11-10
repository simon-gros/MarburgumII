#include "UI/MarburgumUISubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "MarburgumChatWidget.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlayerController.h"
#include "UI/MarburgumConnectionScreen.h"
#include "UI/MarburgumInteractionPrompt.h"
#include "MarburgumInteractable.h"
#include "MarburgumNpc.h"
#include "UI/MarburgumCharacterPanel.h"
#include "UI/MarburgumInventoryPanel.h"
#include "UI/MarburgumJournalPanel.h"
#include "UI/MarburgumLocationBanner.h"
#include "UI/MarburgumPinnedObjective.h"
#include "UI/MarburgumPlayerCondition.h"
#include "EngineUtils.h"
#include "UI/MarburgumNotificationFeed.h"
#include "UI/MarburgumUIGallery.h"
#include "UI/MarburgumUIRoot.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "MarburgumUISubsystem"

using MarburgumII::Session::EPhase;

bool UMarburgumUISubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
    return GameInstance && !GameInstance->IsDedicatedServerInstance() && FApp::CanEverRender();
}

void UMarburgumUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const TCHAR* CommandLine = FCommandLine::Get();
    bGalleryMode = FParse::Param(CommandLine, TEXT("MB2UIGallery"));
    bDiagnostics = FParse::Param(CommandLine, TEXT("MB2UIDiagnostics"));
    float Scale = 1.f;
    if (FParse::Value(CommandLine, TEXT("MB2UIScale="), Scale))
    {
        SetInterfaceScale(Scale);
    }

    Session = Collection.InitializeDependency<UMarburgumSessionSubsystem>();
    if (UMarburgumSessionSubsystem* SessionSubsystem = Session.Get())
    {
        SessionChangedHandle = SessionSubsystem->OnSessionChanged().AddUObject(this, &UMarburgumUISubsystem::HandleSessionChanged);
    }
    PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UMarburgumUISubsystem::HandlePreLoadMap);
    PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UMarburgumUISubsystem::HandlePostLoadMap);
}

void UMarburgumUISubsystem::Deinitialize()
{
    FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadMapHandle);
    FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
    if (UMarburgumSessionSubsystem* SessionSubsystem = Session.Get())
    {
        SessionSubsystem->OnSessionChanged().Remove(SessionChangedHandle);
    }
    TearDown();
    Super::Deinitialize();
}

void UMarburgumUISubsystem::SetInterfaceScale(float Scale)
{
    if (FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().SetApplicationScale(FMath::Clamp(Scale, 0.5f, 3.f));
    }
}

void UMarburgumUISubsystem::HandlePreLoadMap(const FString& MapName)
{
    // Widgets must not outlive the world whose actors they may reference.
    TearDown();
}

void UMarburgumUISubsystem::HandlePostLoadMap(UWorld* World)
{
    if (World && World->GetGameInstance() == GetGameInstance())
    {
        BuildForWorld(World);
    }
}

void UMarburgumUISubsystem::TearDown()
{
    if (AMarburgumPlayerController* Controller = ReceiptController.Get())
    {
        Controller->OnRequestResult.RemoveAll(this);
        Controller->OnInteractionReceipt.RemoveAll(this);
    }
    ReceiptController.Reset();
    if (Root)
    {
        Root->RemoveFromParent();
    }
    Root = nullptr;
    ConnectionScreen = nullptr;
    Feed = nullptr;
    Chat = nullptr;
    Prompt = nullptr;
    Inventory = nullptr;
    Journal = nullptr;
    Banner = nullptr;
    Objective = nullptr;
    Condition = nullptr;
    CharacterPanel = nullptr;
}

void UMarburgumUISubsystem::MountChat(APlayerController* Player)
{
    if (!Player || !Player->IsLocalController() || !Cast<AMarburgumPlayerController>(Player))
    {
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=chat_mount_skipped player=%d local=%d marburgum=%d"),
            MarburgumII::Session::UiLogPrefix, Player ? 1 : 0, Player && Player->IsLocalController() ? 1 : 0,
            Cast<AMarburgumPlayerController>(Player) ? 1 : 0);
        return;
    }
    if (!Root)
    {
        // The controller can replicate before the UI root exists; mount once the root is built.
        PendingChatOwner = Player;
        return;
    }
    if (Chat && Chat->GetOwningPlayer() == Player)
    {
        return;
    }
    if (Chat)
    {
        Chat->RemoveFromParent();
    }
    Chat = CreateWidget<UMarburgumChatWidget>(Player, UMarburgumChatWidget::StaticClass());
    if (Chat)
    {
        Root->AddToLayer(EMarburgumUILayer::Hud, Chat);
    }
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=chat_mounted ok=%d"), MarburgumII::Session::UiLogPrefix, Chat ? 1 : 0);
    PendingChatOwner.Reset();
}

void UMarburgumUISubsystem::FocusChat()
{
    if (Chat && LastPhase == EPhase::Ready)
    {
        Chat->FocusComposer();
    }
}

void UMarburgumUISubsystem::BindReceipts()
{
    const UGameInstance* GameInstance = GetGameInstance();
    const UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
    AMarburgumPlayerController* Controller = World ? Cast<AMarburgumPlayerController>(World->GetFirstPlayerController()) : nullptr;
    if (!Controller || ReceiptController.Get() == Controller)
    {
        return;
    }
    if (AMarburgumPlayerController* Previous = ReceiptController.Get())
    {
        Previous->OnRequestResult.RemoveAll(this);
        Previous->OnInteractionReceipt.RemoveAll(this);
    }
    Controller->OnRequestResult.AddUObject(this, &UMarburgumUISubsystem::HandleRequestResult);
    Controller->OnInteractionReceipt.AddUObject(this, &UMarburgumUISubsystem::HandleInteractionReceipt);
    ReceiptController = Controller;
}

void UMarburgumUISubsystem::MountGameplayHud(APlayerController* Player)
{
    if (!Root || !Player || !Player->IsLocalController() || !Cast<AMarburgumPlayerController>(Player))
    {
        return;
    }
    if (Prompt && Prompt->GetOwningPlayer() != Player)
    {
        Prompt->RemoveFromParent();
        Prompt = nullptr;
    }
    // Widgets owned by an earlier controller (a reconnect) are rebuilt for the current one.
    auto Release = [Player](auto& Widget)
    {
        if (Widget && Widget->GetOwningPlayer() != Player)
        {
            Widget->RemoveFromParent();
            Widget = nullptr;
        }
    };
    Release(Inventory);
    Release(Journal);
    Release(Banner);
    Release(Objective);
    Release(Condition);
    Release(CharacterPanel);
    if (!Banner)
    {
        Banner = CreateWidget<UMarburgumLocationBanner>(Player, UMarburgumLocationBanner::StaticClass());
        Root->AddToLayer(EMarburgumUILayer::Hud, Banner);
    }
    if (!Objective)
    {
        Objective = CreateWidget<UMarburgumPinnedObjective>(Player, UMarburgumPinnedObjective::StaticClass());
        Root->AddToLayer(EMarburgumUILayer::Hud, Objective);
    }
    if (!Condition)
    {
        Condition = CreateWidget<UMarburgumPlayerCondition>(Player, UMarburgumPlayerCondition::StaticClass());
        Root->AddToLayer(EMarburgumUILayer::Hud, Condition);
    }
    if (!Prompt)
    {
        Prompt = CreateWidget<UMarburgumInteractionPrompt>(Player, UMarburgumInteractionPrompt::StaticClass());
        Root->AddToLayer(EMarburgumUILayer::Hud, Prompt);
    }
    if (!Inventory)
    {
        Inventory = CreateWidget<UMarburgumInventoryPanel>(Player, UMarburgumInventoryPanel::StaticClass());
        Root->AddToLayer(EMarburgumUILayer::Screens, Inventory);
    }
    if (!Journal)
    {
        Journal = CreateWidget<UMarburgumJournalPanel>(Player, UMarburgumJournalPanel::StaticClass());
        Root->AddToLayer(EMarburgumUILayer::Screens, Journal);
    }
    if (!CharacterPanel)
    {
        CharacterPanel = CreateWidget<UMarburgumCharacterPanel>(Player, UMarburgumCharacterPanel::StaticClass());
        Root->AddToLayer(EMarburgumUILayer::Screens, CharacterPanel);
    }
}

bool UMarburgumUISubsystem::ToggleInventory()
{
    if (!Inventory || LastPhase != EPhase::Ready)
    {
        return false;
    }
    CloseScreensExcept(Inventory);
    Inventory->Toggle();
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=inventory_view open=%d"),
        MarburgumII::Session::UiLogPrefix, Inventory->IsOpen() ? 1 : 0);
    return true;
}

bool UMarburgumUISubsystem::IsInventoryOpen() const
{
    return Inventory && Inventory->IsOpen();
}

bool UMarburgumUISubsystem::ToggleJournal()
{
    if (!Journal || LastPhase != EPhase::Ready)
    {
        return false;
    }
    CloseScreensExcept(Journal);
    Journal->Toggle();
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=journal_view open=%d"),
        MarburgumII::Session::UiLogPrefix, Journal->IsOpen() ? 1 : 0);
    return true;
}

bool UMarburgumUISubsystem::IsJournalOpen() const
{
    return Journal && Journal->IsOpen();
}

bool UMarburgumUISubsystem::ToggleCharacter()
{
    if (!CharacterPanel || LastPhase != EPhase::Ready)
    {
        return false;
    }
    CloseScreensExcept(CharacterPanel);
    CharacterPanel->Toggle();
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=character_view open=%d"),
        MarburgumII::Session::UiLogPrefix, CharacterPanel->IsOpen() ? 1 : 0);
    return true;
}

bool UMarburgumUISubsystem::IsCharacterOpen() const
{
    return CharacterPanel && CharacterPanel->IsOpen();
}

void UMarburgumUISubsystem::CloseScreensExcept(const UObject* Keep)
{
    // One management screen at a time.
    if (Inventory && Inventory.Get() != Keep && Inventory->IsOpen())
    {
        Inventory->Toggle();
    }
    if (Journal && Journal.Get() != Keep && Journal->IsOpen())
    {
        Journal->Toggle();
    }
    if (CharacterPanel && CharacterPanel.Get() != Keep && CharacterPanel->IsOpen())
    {
        CharacterPanel->Toggle();
    }
}

namespace
{
    const AMarburgumInteractable* FindPlaced(const UWorld* World, FName Id)
    {
        if (World && !Id.IsNone())
        {
            for (TActorIterator<AMarburgumInteractable> It(World); It; ++It)
            {
                if (It->InteractableId == Id)
                {
                    return *It;
                }
            }
        }
        return nullptr;
    }

    FText ItemDisplayName(FName ItemId)
    {
        const MarburgumII::Gameplay::FItemDefinition* Item = MarburgumII::Gameplay::FindItem(ItemId.ToString());
        return Item ? FText::FromString(Item->DisplayName) : FText::FromName(ItemId);
    }

    FText MissionDisplayName(FName MissionId)
    {
        const MarburgumII::Gameplay::FMissionDefinition* Mission = MarburgumII::Gameplay::FindMission(MissionId.ToString());
        return Mission ? FText::FromString(Mission->Name) : FText::FromName(MissionId);
    }

    /** "European chub, Common roach or European perch" for a stage's inventory_any requirement. */
    FText RequiredItemsText(FName MissionId, FName StageId)
    {
        using namespace MarburgumII::Gameplay;
        const FMissionDefinition* Mission = FindMission(MissionId.ToString());
        const FMissionStageDefinition* Stage = Mission ? FindStage(*Mission, StageId.ToString()) : nullptr;
        if (!Stage || Stage->RequiredAnyItemCount <= 0)
        {
            return FText::GetEmpty();
        }
        TArray<FString> Names;
        for (int32 Index = 0; Index < Stage->RequiredAnyItemCount; ++Index)
        {
            Names.Add(ItemDisplayName(FName(Stage->RequiredAnyItems[Index])).ToString());
        }
        FString Joined = Names.Pop();
        if (!Names.IsEmpty())
        {
            Joined = FString::Join(Names, TEXT(", ")) + TEXT(" or ") + Joined;
        }
        return FText::FromString(Joined);
    }
}

void UMarburgumUISubsystem::HandleInteractionReceipt(const FMarburgumInteractionReceipt& Receipt)
{
    if (!Feed)
    {
        return;
    }
    // Keyed by operation: a replayed request replaces its earlier receipt instead of stacking.
    const FString Op = Receipt.OperationId.ToString(EGuidFormats::Digits);
    const FName Key(*FString::Printf(TEXT("interaction.%s"), *Op));
    const FText ItemName = ItemDisplayName(Receipt.ItemId);
    const UGameInstance* GameInstance = GetGameInstance();
    const AMarburgumInteractable* Subject = FindPlaced(GameInstance ? GameInstance->GetWorld() : nullptr, Receipt.InteractableId);
    const FText SubjectName = Subject && !Subject->DisplayName.IsEmpty() ? Subject->DisplayName : FText::FromName(Receipt.InteractableId);

    if (Receipt.Outcome == EMarburgumRequestOutcome::Applied && Receipt.Effect == EMarburgumInteractionEffect::MissionStarted)
    {
        // Oldest first: the line spoken, the mission it starts, then what it hands over.
        if (const AMarburgumNpc* Npc = Cast<AMarburgumNpc>(Subject); Npc && !Npc->Dialogue.IsEmpty())
        {
            Feed->Push(FName(*(Key.ToString() + TEXT(".said"))), FText::FromString(SubjectName.ToString().ToUpper()),
                FText::Format(LOCTEXT("Said", "\u201C{0}\u201D"), Npc->Dialogue), MarburgumUI::ETone::Info);
        }
        Feed->Push(Key, LOCTEXT("MissionStartedKind", "MISSION STARTED"),
            FText::Format(LOCTEXT("MissionStarted", "{0}. Saved by the server; your objective is pinned, and [J] opens the journal."),
                MissionDisplayName(Receipt.MissionId)), MarburgumUI::ETone::Accent);
        if (!Receipt.ItemId.IsNone())
        {
            Feed->Push(FName(*(Key.ToString() + TEXT(".item"))), LOCTEXT("ItemKind", "ITEM RECEIVED"),
                FText::Format(LOCTEXT("MissionItem", "{0} \u00D7{1}, handed over for the mission."), ItemName, FText::AsNumber(Receipt.Quantity)),
                MarburgumUI::ETone::Success);
        }
        return;
    }
    if (Receipt.Outcome == EMarburgumRequestOutcome::Applied && Receipt.Effect == EMarburgumInteractionEffect::StageCompleted)
    {
        Feed->Push(Key, LOCTEXT("MissionUpdatedKind", "MISSION UPDATED"),
            Receipt.ItemId.IsNone()
                ? FText::Format(LOCTEXT("StageDone", "{0}: step complete. Saved by the server."), MissionDisplayName(Receipt.MissionId))
                : FText::Format(LOCTEXT("StageDoneItem", "{0}: step complete; {1} \u00D7{2} handed over. Saved by the server."),
                    MissionDisplayName(Receipt.MissionId), ItemName, FText::AsNumber(Receipt.Quantity)),
            MarburgumUI::ETone::Success);
        return;
    }
    if (Receipt.Outcome == EMarburgumRequestOutcome::Duplicate && Receipt.Effect != EMarburgumInteractionEffect::ItemGranted)
    {
        Feed->Push(Key, LOCTEXT("ReceiptKind", "SERVER RECEIPT"),
            FText::Format(LOCTEXT("MissionDuplicate", "Already done: {0}. The server ignored the repeat; nothing changed twice."),
                MissionDisplayName(Receipt.MissionId)), MarburgumUI::ETone::Info);
        return;
    }

    if (Receipt.Outcome == EMarburgumRequestOutcome::Applied)
    {
        Feed->Push(Key, LOCTEXT("ItemKind", "ITEM RECEIVED"),
            FText::Format(LOCTEXT("ItemApplied", "{0} \u00D7{1} added to your inventory. Confirmed and saved by the server."),
                ItemName, FText::AsNumber(Receipt.Quantity)), MarburgumUI::ETone::Success);
        return;
    }
    if (Receipt.Outcome == EMarburgumRequestOutcome::Duplicate)
    {
        Feed->Push(Key, LOCTEXT("ReceiptKind", "SERVER RECEIPT"),
            FText::Format(LOCTEXT("ItemDuplicate", "Already received: {0}. The server ignored the repeat; nothing was added twice."), ItemName),
            MarburgumUI::ETone::Info);
        return;
    }

    FText Message;
    MarburgumUI::ETone Tone = MarburgumUI::ETone::Warning;
    switch (Receipt.Reason)
    {
    case EMarburgumInteractionReason::RequirementNotMet:
    {
        const FText Needed = RequiredItemsText(Receipt.MissionId, Receipt.MissionStage);
        Message = Needed.IsEmpty()
            ? FText::Format(LOCTEXT("Requirement", "{0} needs something you do not have yet. Nothing was changed."), SubjectName)
            : FText::Format(LOCTEXT("RequirementItems", "{0} wants to see one {1} first. You have none, so nothing was changed."), SubjectName, Needed);
        Tone = MarburgumUI::ETone::Info;
        break;
    }
    case EMarburgumInteractionReason::NothingToDo:
        Message = FText::Format(LOCTEXT("NothingToDo", "{0} has nothing more for you right now. Nothing was changed."), SubjectName);
        Tone = MarburgumUI::ETone::Info;
        break;
    case EMarburgumInteractionReason::OutOfStock:
        Message = LOCTEXT("OutOfStock", "Nothing left to take. Someone else got there first; nothing was changed.");
        break;
    case EMarburgumInteractionReason::OutOfRange:
        Message = LOCTEXT("OutOfRange", "Too far away. Move closer and try again; nothing was changed.");
        break;
    case EMarburgumInteractionReason::NoCharacter:
        Message = LOCTEXT("NoCharacter", "This connection has no character, so it cannot hold items or take on missions. Nothing was changed.");
        break;
    case EMarburgumInteractionReason::StoreUnavailable:
    case EMarburgumInteractionReason::StoreWriteFailed:
        Message = LOCTEXT("StoreFailed", "The server could not save the result, so it did not apply it. Nothing was changed.");
        Tone = MarburgumUI::ETone::Danger;
        break;
    case EMarburgumInteractionReason::AuthorityUnavailable:
        Message = LOCTEXT("NoAuthority", "No authoritative server handled this request. Nothing was changed.");
        Tone = MarburgumUI::ETone::Danger;
        break;
    default:
        Message = LOCTEXT("Refused", "The server refused this request. Nothing was changed.");
        Tone = MarburgumUI::ETone::Danger;
        break;
    }
    // A mission step that is not possible yet is guidance, not an error.
    Feed->Push(Key, Tone == MarburgumUI::ETone::Info ? LOCTEXT("NotYetKind", "NOT YET") : LOCTEXT("RefusedKind", "REFUSED"), Message, Tone);
}

void UMarburgumUISubsystem::HandleRequestResult(const FGuid& OperationId, EMarburgumRequestOutcome Outcome)
{
    if (!Feed)
    {
        return;
    }
    // Keyed by operation: a replayed request replaces its earlier receipt instead of stacking.
    const FName Key(*FString::Printf(TEXT("receipt.%s"), *OperationId.ToString(EGuidFormats::Digits)));
    switch (Outcome)
    {
    case EMarburgumRequestOutcome::Applied:
        Feed->Push(Key, LOCTEXT("ReceiptKind", "SERVER RECEIPT"), LOCTEXT("ReceiptApplied", "Request applied by the server."), MarburgumUI::ETone::Success);
        break;
    case EMarburgumRequestOutcome::Duplicate:
        Feed->Push(Key, LOCTEXT("ReceiptKind", "SERVER RECEIPT"), LOCTEXT("ReceiptDuplicate", "Already applied. The server ignored the repeat; nothing changed twice."), MarburgumUI::ETone::Info);
        break;
    case EMarburgumRequestOutcome::Rejected:
        Feed->Push(Key, LOCTEXT("RefusedKind", "REFUSED"), LOCTEXT("ReceiptRejected", "The server declined this request. Nothing was changed."), MarburgumUI::ETone::Danger);
        break;
    }
}

void UMarburgumUISubsystem::BuildForWorld(UWorld* World)
{
    TearDown();
    UGameInstance* GameInstance = GetGameInstance();
    Root = CreateWidget<UMarburgumUIRoot>(GameInstance, UMarburgumUIRoot::StaticClass());
    if (!Root)
    {
        return;
    }
    Root->AddToViewport(0);

    Feed = CreateWidget<UMarburgumNotificationFeed>(GameInstance, UMarburgumNotificationFeed::StaticClass());
    Root->AddToLayer(EMarburgumUILayer::Notifications, Feed);

    const bool bServerWorld = World->GetNetMode() == NM_Client;
    if (bDiagnostics && bServerWorld)
    {
        Root->AddToLayer(EMarburgumUILayer::Diagnostic,
            CreateWidget<UMarburgumDiagnosticsOverlay>(GameInstance, UMarburgumDiagnosticsOverlay::StaticClass()));
    }

    if (bGalleryMode && !bServerWorld)
    {
        Root->AddToLayer(EMarburgumUILayer::Screens, CreateWidget<UMarburgumUIGallery>(GameInstance, UMarburgumUIGallery::StaticClass()));
        return;
    }

    ConnectionScreen = CreateWidget<UMarburgumConnectionScreen>(GameInstance, UMarburgumConnectionScreen::StaticClass());
    Root->AddToLayer(EMarburgumUILayer::Screens, ConnectionScreen);

    if (APlayerController* Pending = PendingChatOwner.Get(); Pending && Pending->GetWorld() == World)
    {
        MountChat(Pending);
    }

    if (UMarburgumSessionSubsystem* SessionSubsystem = Session.Get())
    {
        HandleSessionChanged(SessionSubsystem->GetView());
    }
}

void UMarburgumUISubsystem::HandleSessionChanged(const FMarburgumSessionView& View)
{
    const EPhase PreviousPhase = LastPhase;
    LastPhase = View.Phase;
    if (View.Phase == EPhase::Reconnecting)
    {
        bRecoveredFromLoss = true;
    }
    else if (View.Phase == EPhase::Idle || View.Phase == EPhase::Error)
    {
        bRecoveredFromLoss = false;
    }

    if (!ConnectionScreen)
    {
        return;
    }
    if (View.Phase != EPhase::Ready)
    {
        CloseScreensExcept(nullptr); // No stale custody or condition view once the session is gone.
    }
    ConnectionScreen->ApplyView(View);
    const bool bShowScreen = View.Phase != EPhase::Ready;
    ConnectionScreen->SetVisibility(bShowScreen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    ApplyInputMode(bShowScreen);

    if (View.Phase == EPhase::Ready)
    {
        BindReceipts();
        // The controller's BeginPlay hook can fire before the engine marks it local on a client,
        // so admission (which guarantees a local controller) is the reliable mount point.
        const UGameInstance* GameInstance = GetGameInstance();
        const UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
        MountChat(World ? World->GetFirstPlayerController() : nullptr);
        MountGameplayHud(World ? World->GetFirstPlayerController() : nullptr);
    }
    if (View.Phase == EPhase::Ready && PreviousPhase != EPhase::Ready && Feed)
    {
        // One toast per admission; the dedupe key collapses repeated reports of the same event.
        const FText Message = FText::Format(
            bRecoveredFromLoss ? LOCTEXT("Reconnected", "Reconnected to {0}  ·  network contract v{1}")
                               : LOCTEXT("Connected", "Connected to {0}  ·  network contract v{1}"),
            FText::FromString(View.Address), FText::AsNumber(MarburgumII::Network::ContractVersion));
        Feed->Push(TEXT("session.admitted"), LOCTEXT("ConnectionKind", "CONNECTION"), Message, MarburgumUI::ETone::Success);
        bRecoveredFromLoss = false;
    }
}

void UMarburgumUISubsystem::ApplyInputMode(bool bScreenOwnsInput)
{
    const UGameInstance* GameInstance = GetGameInstance();
    const UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
    APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
    if (!Controller || !Controller->IsLocalController())
    {
        return;
    }
    if (bScreenOwnsInput && ConnectionScreen)
    {
        FInputModeUIOnly Mode;
        if (UWidget* Focus = ConnectionScreen->GetPreferredFocus())
        {
            Mode.SetWidgetToFocus(Focus->TakeWidget());
        }
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        Controller->SetInputMode(Mode);
        Controller->SetShowMouseCursor(true);
    }
    else
    {
        Controller->SetInputMode(FInputModeGameOnly());
        Controller->SetShowMouseCursor(false);
    }
}

#undef LOCTEXT_NAMESPACE
