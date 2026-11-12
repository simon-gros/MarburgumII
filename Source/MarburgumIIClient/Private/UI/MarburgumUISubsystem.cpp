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
#include "MarburgumCombatContract.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlayerController.h"
#include "MarburgumPlayerState.h"
#include "Session/MarburgumAccountSubsystem.h"
#include "UI/MarburgumAccountScreen.h"
#include "UI/MarburgumConnectionScreen.h"
#include "UI/MarburgumInteractionPrompt.h"
#include "MarburgumInteractable.h"
#include "MarburgumNpc.h"
#include "UI/MarburgumCharacterPanel.h"
#include "UI/MarburgumInventoryPanel.h"
#include "UI/MarburgumJournalPanel.h"
#include "UI/MarburgumChoicePanel.h"
#include "UI/MarburgumShopPanel.h"
#include "UI/MarburgumSkillsPanel.h"
#include "UI/MarburgumLocationBanner.h"
#include "UI/MarburgumPinnedObjective.h"
#include "UI/MarburgumPlayerCondition.h"
#include "UI/MarburgumVehicleStatus.h"
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
    Accounts = Collection.InitializeDependency<UMarburgumAccountSubsystem>();
    if (UMarburgumAccountSubsystem* AccountSubsystem = Accounts.Get())
    {
        AccountChangedHandle = AccountSubsystem->OnAccountChanged().AddUObject(this, &UMarburgumUISubsystem::HandleAccountChanged);
    }
    PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UMarburgumUISubsystem::HandlePreLoadMap);
    PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UMarburgumUISubsystem::HandlePostLoadMap);
    FishingHandle = AMarburgumPlayerState::OnFishingChanged().AddUObject(this, &UMarburgumUISubsystem::HandleFishingChanged);
}

void UMarburgumUISubsystem::Deinitialize()
{
    FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadMapHandle);
    FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
    AMarburgumPlayerState::OnFishingChanged().Remove(FishingHandle);
    if (UMarburgumSessionSubsystem* SessionSubsystem = Session.Get())
    {
        SessionSubsystem->OnSessionChanged().Remove(SessionChangedHandle);
    }
    if (UMarburgumAccountSubsystem* AccountSubsystem = Accounts.Get())
    {
        AccountSubsystem->OnAccountChanged().Remove(AccountChangedHandle);
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
    AccountScreen = nullptr;
    Feed = nullptr;
    Chat = nullptr;
    Prompt = nullptr;
    Inventory = nullptr;
    Journal = nullptr;
    Banner = nullptr;
    Objective = nullptr;
    Condition = nullptr;
    VehicleStatus = nullptr;
    CharacterPanel = nullptr;
    SkillsPanel = nullptr;
    ChoicePanel = nullptr;
    ShopPanel = nullptr;
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
        Previous->OnCombatResult.RemoveAll(this);
    }
    Controller->OnRequestResult.AddUObject(this, &UMarburgumUISubsystem::HandleRequestResult);
    Controller->OnInteractionReceipt.AddUObject(this, &UMarburgumUISubsystem::HandleInteractionReceipt);
    Controller->OnCombatResult.AddUObject(this, &UMarburgumUISubsystem::HandleCombatResult);
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
    Release(VehicleStatus);
    Release(CharacterPanel);
    Release(SkillsPanel);
    Release(ChoicePanel);
    Release(ShopPanel);
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
    if (!VehicleStatus)
    {
        VehicleStatus = CreateWidget<UMarburgumVehicleStatus>(Player, UMarburgumVehicleStatus::StaticClass());
        Root->AddToLayer(EMarburgumUILayer::Hud, VehicleStatus);
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
    if (!SkillsPanel)
    {
        SkillsPanel = CreateWidget<UMarburgumSkillsPanel>(Player, UMarburgumSkillsPanel::StaticClass());
        Root->AddToLayer(EMarburgumUILayer::Screens, SkillsPanel);
    }
    if (!ChoicePanel)
    {
        ChoicePanel = CreateWidget<UMarburgumChoicePanel>(Player, UMarburgumChoicePanel::StaticClass());
        Root->AddToLayer(EMarburgumUILayer::Screens, ChoicePanel);
    }
    if (!ShopPanel)
    {
        ShopPanel = CreateWidget<UMarburgumShopPanel>(Player, UMarburgumShopPanel::StaticClass());
        Root->AddToLayer(EMarburgumUILayer::Screens, ShopPanel);
    }
}

bool UMarburgumUISubsystem::ToggleSkills()
{
    if (!SkillsPanel || LastPhase != EPhase::Ready)
    {
        return false;
    }
    CloseScreensExcept(SkillsPanel);
    SkillsPanel->Toggle();
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=skills_panel open=%d"),
        MarburgumII::Session::UiLogPrefix, SkillsPanel->IsOpen() ? 1 : 0);
    return true;
}

bool UMarburgumUISubsystem::IsSkillsOpen() const
{
    return SkillsPanel && SkillsPanel->IsOpen();
}

bool UMarburgumUISubsystem::ToggleChoice(AMarburgumInteractable* Target, FName MissionId)
{
    if (!ChoicePanel || LastPhase != EPhase::Ready)
    {
        return false;
    }
    if (ChoicePanel->IsOpen())
    {
        ChoicePanel->Close();
    }
    else
    {
        CloseScreensExcept(ChoicePanel);
        ChoicePanel->Open(Target, MissionId);
    }
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=choice_panel open=%d mission=%s choices=%d"),
        MarburgumII::Session::UiLogPrefix, ChoicePanel->IsOpen() ? 1 : 0, *MissionId.ToString(), ChoicePanel->GetShownChoiceCount());
    return true;
}

bool UMarburgumUISubsystem::ChooseOption(int32 Number)
{
    return ChoicePanel && ChoicePanel->Choose(Number);
}

bool UMarburgumUISubsystem::IsChoiceOpen() const
{
    return ChoicePanel && ChoicePanel->IsOpen();
}

bool UMarburgumUISubsystem::ToggleShop(AMarburgumInteractable* Keeper)
{
    if (!ShopPanel || LastPhase != EPhase::Ready)
    {
        return false;
    }
    if (ShopPanel->IsOpen())
    {
        ShopPanel->Close();
    }
    else if (Keeper)
    {
        CloseScreensExcept(ShopPanel);
        ShopPanel->Open(Keeper);
    }
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=shop_panel open=%d shop=%s lines=%d counter_open=%d"),
        MarburgumII::Session::UiLogPrefix, ShopPanel->IsOpen() ? 1 : 0, *ShopPanel->GetShownShop().ToString(),
        ShopPanel->GetShownLineCount(), ShopPanel->IsShownOpen() ? 1 : 0);
    return true;
}

bool UMarburgumUISubsystem::TradeAtShop(int32 Number, bool bSell)
{
    return ShopPanel && ShopPanel->Trade(Number, bSell);
}

bool UMarburgumUISubsystem::IsShopOpen() const
{
    return ShopPanel && ShopPanel->IsOpen();
}

void UMarburgumUISubsystem::HandleFishingChanged(AMarburgumPlayerState* State)
{
    const UGameInstance* GameInstance = GetGameInstance();
    const UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
    const APlayerController* Local = World ? World->GetFirstPlayerController() : nullptr;
    if (!Feed || !State || !Local || Local->PlayerState != State)
    {
        return;
    }
    const FMarburgumFishingStatus& Fishing = State->GetFishing();
    if (Fishing.Phase == EMarburgumFishingPhase::Bite)
    {
        Feed->Push(FName(TEXT("fishing")), LOCTEXT("FishingKind", "FISHING"), LOCTEXT("Bite", "A bite! Press [E] to strike."), MarburgumUI::ETone::Accent);
        return;
    }
    // A cast that ended on its own: the receipt already told the catch and the too-early strike.
    if (Fishing.Phase == EMarburgumFishingPhase::None && (Fishing.Attempt != LastFishingAttempt || Fishing.LastEnd != LastFishingEnd))
    {
        LastFishingAttempt = Fishing.Attempt;
        LastFishingEnd = Fishing.LastEnd;
        if (Fishing.LastEnd == EMarburgumFishingEnd::Escaped)
        {
            Feed->Push(FName(TEXT("fishing")), LOCTEXT("FishingKind", "FISHING"), LOCTEXT("Escaped", "It got away. Strike sooner after the bite."),
                MarburgumUI::ETone::Warning);
        }
        else if (Fishing.LastEnd == EMarburgumFishingEnd::Cancelled)
        {
            Feed->Push(FName(TEXT("fishing")), LOCTEXT("FishingKind", "FISHING"), LOCTEXT("Cancelled", "You reeled in. Fishing needs you to stand still on the bank."),
                MarburgumUI::ETone::Neutral);
        }
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
    if (SkillsPanel && SkillsPanel.Get() != Keep && SkillsPanel->IsOpen())
    {
        SkillsPanel->Toggle();
    }
    if (ChoicePanel && ChoicePanel.Get() != Keep && ChoicePanel->IsOpen())
    {
        ChoicePanel->Close();
    }
    if (ShopPanel && ShopPanel.Get() != Keep && ShopPanel->IsOpen())
    {
        ShopPanel->Close();
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

    /** "European chub, Common roach or European perch" for a stage's inventory_any objective, or its required items. */
    FText RequiredItemsText(FName MissionId, FName StageId)
    {
        using namespace MarburgumII::Gameplay;
        const FMissionDefinition* Mission = FindMission(MissionId.ToString());
        const FMissionStageDefinition* Stage = Mission ? FindStage(*Mission, StageId.ToString()) : nullptr;
        if (!Stage || (Stage->ObjectiveItemCount <= 0 && Stage->RequiredItemCount <= 0))
        {
            return FText::GetEmpty();
        }
        TArray<FString> Names;
        for (int32 Index = 0; Index < Stage->ObjectiveItemCount; ++Index)
        {
            Names.Add(ItemDisplayName(FName(Stage->ObjectiveItems[Index])).ToString());
        }
        for (int32 Index = 0; Names.IsEmpty() && Index < Stage->RequiredItemCount; ++Index)
        {
            Names.Add(ItemDisplayName(FName(Stage->RequiredItems[Index].Id)).ToString());
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

    using namespace MarburgumII::Gameplay;
    const bool bApplied = Receipt.Outcome == EMarburgumRequestOutcome::Applied;
    const FMissionDefinition* Mission = Receipt.MissionId.IsNone() ? nullptr : FindMission(Receipt.MissionId.ToString());
    auto PushSaid = [&]()
    {
        if (Subject && !Subject->Dialogue.IsEmpty())
        {
            Feed->Push(FName(*(Key.ToString() + TEXT(".said"))), FText::FromString(SubjectName.ToString().ToUpper()),
                FText::Format(LOCTEXT("Said", "\u201C{0}\u201D"), Subject->Dialogue), MarburgumUI::ETone::Info);
        }
    };
    auto PushGranted = [&]()
    {
        TArray<FString> Parts;
        for (const FMarburgumItemStack& Stack : Receipt.ItemsGranted)
        {
            Parts.Add(FString::Printf(TEXT("%s \u00D7%d"), *ItemDisplayName(Stack.ItemId).ToString(), Stack.Quantity));
        }
        if (!Parts.IsEmpty())
        {
            Feed->Push(FName(*(Key.ToString() + TEXT(".granted"))), LOCTEXT("ItemsKind", "ITEMS RECEIVED"),
                FText::FromString(FString::Join(Parts, TEXT(", "))), MarburgumUI::ETone::Success);
        }
    };
    auto PushRewards = [&]()
    {
        if (Receipt.MoneyDelta != 0)
        {
            Feed->Push(FName(*(Key.ToString() + TEXT(".money"))), LOCTEXT("MoneyKind", "MONEY"),
                Receipt.MoneyDelta > 0 ? FText::Format(LOCTEXT("MoneyPaid", "+{0}. Saved by the server."), FText::AsNumber(Receipt.MoneyDelta))
                                       : FText::Format(LOCTEXT("MoneySpent", "\u2212{0}. Saved by the server."), FText::AsNumber(-Receipt.MoneyDelta)),
                MarburgumUI::ETone::Success);
        }
        for (const FMarburgumSkillProgress& Award : Receipt.SkillXp)
        {
            const FSkillDefinition* Skill = FindSkill(Award.SkillId.ToString());
            Feed->Push(FName(*(Key.ToString() + TEXT(".xp.") + Award.SkillId.ToString())), LOCTEXT("XpKind", "SKILL"),
                FText::Format(LOCTEXT("XpGained", "{0} +{1} XP. [K] Skills."), FText::FromString(Skill ? Skill->DisplayName : Award.SkillId.ToString()),
                    FText::AsNumber(Award.Xp)), MarburgumUI::ETone::Accent);
        }
    };

    if (bApplied && Receipt.Effect == EMarburgumInteractionEffect::FishingCast)
    {
        Feed->Push(FName(TEXT("fishing")), LOCTEXT("FishingKind", "FISHING"),
            LOCTEXT("Cast", "Line in the water. Wait for a bite, then press [E] to strike."), MarburgumUI::ETone::Info);
        return;
    }
    if (bApplied && Receipt.Effect == EMarburgumInteractionEffect::FishCaught)
    {
        Feed->Push(FName(TEXT("fishing")), LOCTEXT("FishingKind", "FISHING"),
            FText::Format(LOCTEXT("Caught", "You landed a {0}. Saved by the server."), ItemName), MarburgumUI::ETone::Success);
        PushRewards();
        return;
    }
    if (Receipt.Effect == EMarburgumInteractionEffect::ItemBought || Receipt.Effect == EMarburgumInteractionEffect::ItemSold)
    {
        // A counter (ADR-060): what changed hands, at the server's price.
        const bool bBought = Receipt.Effect == EMarburgumInteractionEffect::ItemBought;
        if (!bApplied)
        {
            Feed->Push(Key, LOCTEXT("ReceiptKind", "SERVER RECEIPT"),
                FText::Format(LOCTEXT("TradeDuplicate", "Already done: {0} ×{1}. The server ignored the repeat; nothing changed hands twice."),
                    ItemName, FText::AsNumber(Receipt.Quantity)), MarburgumUI::ETone::Info);
            return;
        }
        Feed->Push(Key, bBought ? LOCTEXT("BoughtKind", "BOUGHT") : LOCTEXT("SoldKind", "SOLD"),
            bBought ? FText::Format(LOCTEXT("Bought", "Petra takes the coins and slides {0} ×{1} across the counter. −{2} money, saved by the server."),
                          ItemName, FText::AsNumber(Receipt.Quantity), FText::AsNumber(-Receipt.MoneyDelta))
                    : FText::Format(LOCTEXT("Sold", "Petra counts out the coins and takes {0} ×{1} back. +{2} money, saved by the server."),
                          ItemName, FText::AsNumber(Receipt.Quantity), FText::AsNumber(Receipt.MoneyDelta)),
            MarburgumUI::ETone::Success);
        for (const FMarburgumSkillProgress& Award : Receipt.SkillXp)
        {
            const FSkillDefinition* Skill = FindSkill(Award.SkillId.ToString());
            Feed->Push(FName(*(Key.ToString() + TEXT(".xp.") + Award.SkillId.ToString())), LOCTEXT("XpKind", "SKILL"),
                FText::Format(LOCTEXT("FirstPurchaseXp", "First purchase here: {0} +{1} XP. [K] Skills."),
                    FText::FromString(Skill ? Skill->DisplayName : Award.SkillId.ToString()), FText::AsNumber(Award.Xp)), MarburgumUI::ETone::Accent);
        }
        return;
    }
    if (bApplied && Receipt.Effect == EMarburgumInteractionEffect::MissionStarted)
    {
        // Oldest first: the line spoken, the mission it starts, then what it hands over.
        PushSaid();
        Feed->Push(Key, LOCTEXT("MissionStartedKind", "MISSION STARTED"),
            FText::Format(LOCTEXT("MissionStarted", "{0}. Saved by the server; your objective is pinned, and [J] opens the journal."),
                MissionDisplayName(Receipt.MissionId)), MarburgumUI::ETone::Accent);
        if (!Receipt.ItemId.IsNone())
        {
            Feed->Push(FName(*(Key.ToString() + TEXT(".item"))), LOCTEXT("ItemKind", "ITEM RECEIVED"),
                FText::Format(LOCTEXT("MissionItem", "{0} \u00D7{1}, handed over for the mission."), ItemName, FText::AsNumber(Receipt.Quantity)),
                MarburgumUI::ETone::Success);
        }
        PushGranted();
        return;
    }
    if (bApplied && (Receipt.Effect == EMarburgumInteractionEffect::StageCompleted || Receipt.Effect == EMarburgumInteractionEffect::MissionCompleted))
    {
        // The stage just done is the one before the new current stage, or the last one on completion.
        const FMissionStageDefinition* Done = nullptr;
        if (Mission)
        {
            const FMissionStageDefinition* Now = FindStage(*Mission, Receipt.MissionStage.ToString());
            Done = Receipt.Effect == EMarburgumInteractionEffect::MissionCompleted ? Now : (Now && Now > Mission->Stages ? Now - 1 : nullptr);
        }
        const FMissionChoiceDefinition* Choice = Done && !Receipt.Choice.IsNone() ? FindChoice(*Done, Receipt.Choice.ToString()) : nullptr;
        const FString Told = Choice ? Choice->CompletionText : Done ? Done->CompletionText : TEXT("");
        if (!Choice)
        {
            PushSaid();
        }
        if (!Told.IsEmpty())
        {
            Feed->Push(FName(*(Key.ToString() + TEXT(".told"))), FText::FromString(MissionDisplayName(Receipt.MissionId).ToString().ToUpper()),
                FText::FromString(Told), MarburgumUI::ETone::Info);
        }
        if (Receipt.Effect == EMarburgumInteractionEffect::MissionCompleted)
        {
            Feed->Push(Key, LOCTEXT("MissionCompletedKind", "MISSION COMPLETED"),
                Mission && *Mission->JournalEntryId
                    ? FText::Format(LOCTEXT("MissionCompletedEntry", "{0}. Journal entry added: {1}."), MissionDisplayName(Receipt.MissionId),
                        FText::FromString(Mission->JournalEntryTitle))
                    : FText::Format(LOCTEXT("MissionCompleted", "{0}. Saved by the server."), MissionDisplayName(Receipt.MissionId)),
                MarburgumUI::ETone::Success);
        }
        else
        {
            Feed->Push(Key, LOCTEXT("MissionUpdatedKind", "MISSION UPDATED"),
                Receipt.ItemId.IsNone()
                    ? FText::Format(LOCTEXT("StageDone", "{0}: step complete. Saved by the server."), MissionDisplayName(Receipt.MissionId))
                    : FText::Format(LOCTEXT("StageDoneItem", "{0}: step complete; {1} \u00D7{2} handed over. Saved by the server."),
                        MissionDisplayName(Receipt.MissionId), ItemName, FText::AsNumber(Receipt.Quantity)),
                MarburgumUI::ETone::Success);
        }
        PushGranted();
        PushRewards();
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
        if (Subject && !Subject->Dialogue.IsEmpty())
        {
            // Talking to someone with no business for you still gets their line.
            Feed->Push(FName(*(Key.ToString() + TEXT(".said"))), FText::FromString(SubjectName.ToString().ToUpper()),
                FText::Format(LOCTEXT("Said", "\u201C{0}\u201D"), Subject->Dialogue), MarburgumUI::ETone::Info);
            return;
        }
        Message = FText::Format(LOCTEXT("NothingToDo", "{0} has nothing more for you right now. Nothing was changed."), SubjectName);
        Tone = MarburgumUI::ETone::Info;
        break;
    case EMarburgumInteractionReason::ChoiceRequired:
        Message = LOCTEXT("ChoiceRequired", "A decision is due here. Press [E] to see your choices.");
        Tone = MarburgumUI::ETone::Info;
        break;
    case EMarburgumInteractionReason::UnknownChoice:
        Message = LOCTEXT("UnknownChoice", "That decision is not open here any more. Nothing was changed.");
        break;
    case EMarburgumInteractionReason::InsufficientMoney:
        Message = LOCTEXT("InsufficientMoney", "You cannot afford that. Nothing was changed.");
        Tone = MarburgumUI::ETone::Info;
        break;
    case EMarburgumInteractionReason::PrerequisiteIncomplete:
        Message = FText::Format(LOCTEXT("Prerequisite", "{0} is not ready to ask more of you yet. Nothing was changed."), SubjectName);
        Tone = MarburgumUI::ETone::Info;
        break;
    case EMarburgumInteractionReason::NoRod:
        Message = LOCTEXT("NoRod", "You need a fishing rod to fish here.");
        Tone = MarburgumUI::ETone::Info;
        break;
    case EMarburgumInteractionReason::TooSoon:
        Message = LOCTEXT("TooSoon", "Give the line a moment before casting again.");
        Tone = MarburgumUI::ETone::Info;
        break;
    case EMarburgumInteractionReason::CannotFishNow:
        Message = LOCTEXT("CannotFish", "Fishing needs both feet on the bank.");
        Tone = MarburgumUI::ETone::Info;
        break;
    case EMarburgumInteractionReason::TooEarly:
        Feed->Push(FName(TEXT("fishing")), LOCTEXT("FishingKind", "FISHING"),
            LOCTEXT("TooEarly", "You struck too early. Whatever was there is gone; cast again."), MarburgumUI::ETone::Warning);
        return;
    case EMarburgumInteractionReason::OutOfStock:
        Message = Subject && !Subject->GetShopId().IsNone()
            ? FText::Format(LOCTEXT("SoldOut", "The last {0} has already gone. Nothing was changed."), ItemName)
            : LOCTEXT("OutOfStock", "Nothing left to take. Someone else got there first; nothing was changed.");
        break;
    case EMarburgumInteractionReason::ShopClosed:
    {
        const FShopDefinition* Shop = Subject && !Subject->GetShopId().IsNone() ? FindShop(Subject->GetShopId().ToString()) : nullptr;
        Message = Shop ? FText::FromString(Shop->ClosedMessage) : LOCTEXT("ShopClosed", "The counter is closed. Nothing was changed.");
        Tone = MarburgumUI::ETone::Info;
        break;
    }
    case EMarburgumInteractionReason::InvalidQuantity:
        Message = LOCTEXT("InvalidQuantity", "That quantity is not for sale. Nothing was changed.");
        break;
    case EMarburgumInteractionReason::NotOnCounter:
        Message = LOCTEXT("NotOnCounter", "That is not on the counter. Nothing was changed.");
        break;
    case EMarburgumInteractionReason::ItemNotAccepted:
        Message = FText::Format(LOCTEXT("ItemNotAccepted", "Petra will not take {0} back. Nothing was changed."), ItemName);
        Tone = MarburgumUI::ETone::Info;
        break;
    case EMarburgumInteractionReason::InsufficientItems:
        Message = FText::Format(LOCTEXT("InsufficientItems", "You have no {0} to sell. Nothing was changed."), ItemName);
        Tone = MarburgumUI::ETone::Info;
        break;
    case EMarburgumInteractionReason::NoCounter:
        Message = FText::Format(LOCTEXT("NoCounter", "{0} keeps no counter. Nothing was changed."), SubjectName);
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

void UMarburgumUISubsystem::HandleCombatResult(const FMarburgumCombatResult& Result)
{
    using namespace MarburgumII::Combat;
    if (!Feed)
    {
        return;
    }
    // The original's game_hud on_combat lines. One toast slot: a quick flurry refreshes it rather than stacking.
    const FName Key(TEXT("combat"));
    const FText Kind = LOCTEXT("FightKind", "FIGHT");
    if (!Result.bOk)
    {
        if (Result.Reason == ErrorExhausted)
        {
            Feed->Push(Key, Kind, LOCTEXT("FightExhausted", "Not enough breath."), MarburgumUI::ETone::Warning);
        }
        else if (Result.Reason == ErrorSeated)
        {
            Feed->Push(Key, Kind, LOCTEXT("FightSeated", "Get out before you swing."), MarburgumUI::ETone::Warning);
        }
        else if (Result.Reason == ErrorIncapacitated)
        {
            Feed->Push(Key, Kind, LOCTEXT("FightDown", "You cannot swing from the ground."), MarburgumUI::ETone::Warning);
        }
        // Too soon is the rhythm of a fight, not news; the original says nothing.
        return;
    }
    const FText Name = Result.TargetName.IsEmpty() ? LOCTEXT("FightSomeone", "Someone") : FText::FromString(Result.TargetName);
    if (Result.Outcome == OutcomeMiss)
    {
        Feed->Push(Key, Kind, Result.bHeavy ? LOCTEXT("FightHeavyMissed", "The heavy blow missed.") : LOCTEXT("FightJabMissed", "The jab missed."),
            MarburgumUI::ETone::Neutral);
    }
    else if (Result.Outcome == OutcomeBlocked)
    {
        Feed->Push(Key, Kind, FText::Format(LOCTEXT("FightBlocked", "{0} kept their hands up."), Name), MarburgumUI::ETone::Info);
    }
    else if (Result.TargetVitality == 0)
    {
        Feed->Push(Key, Kind, FText::Format(LOCTEXT("FightFloored", "{0} goes down."), Name), MarburgumUI::ETone::Warning);
    }
    else
    {
        Feed->Push(Key, Kind, FText::Format(LOCTEXT("FightHit", "{0}. {1}."), Name, FText::AsNumber(FMath::RoundToInt(Result.Damage))),
            MarburgumUI::ETone::Accent);
    }
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
    if (const UMarburgumAccountSubsystem* AccountSubsystem = Accounts.Get(); AccountSubsystem && AccountSubsystem->IsEnabled() && !bServerWorld)
    {
        AccountScreen = CreateWidget<UMarburgumAccountScreen>(GameInstance, UMarburgumAccountScreen::StaticClass());
        Root->AddToLayer(EMarburgumUILayer::Screens, AccountScreen);
        AccountScreen->ApplyView(AccountSubsystem->GetView());
        ConnectionScreen->ApplyAccount(AccountSubsystem->GetView());
    }

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
    ApplyFrontEnd();

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

void UMarburgumUISubsystem::HandleAccountChanged(const FMarburgumAccountView& View)
{
    if (AccountScreen)
    {
        AccountScreen->ApplyView(View);
    }
    if (ConnectionScreen)
    {
        ConnectionScreen->ApplyAccount(View);
    }
    ApplyFrontEnd();
}

void UMarburgumUISubsystem::ApplyFrontEnd()
{
    if (!ConnectionScreen)
    {
        return;
    }
    const bool bInWorld = LastPhase == EPhase::Ready;
    const UMarburgumAccountSubsystem* AccountSubsystem = Accounts.Get();
    const bool bAccountStep = AccountScreen && AccountSubsystem && AccountSubsystem->GetView().Step != EMarburgumAccountStep::Ready
        && AccountSubsystem->GetView().Step != EMarburgumAccountStep::Disabled;
    // A signed-out player, or one without a character, sees the account screens; connecting needs both.
    const bool bShowAccount = !bInWorld && bAccountStep && !MarburgumII::Session::IsAttemptInFlight(LastPhase);
    if (AccountScreen)
    {
        AccountScreen->SetVisibility(bShowAccount ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    }
    ConnectionScreen->SetVisibility(!bInWorld && !bShowAccount ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    ApplyInputMode(!bInWorld);
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
        const bool bAccount = AccountScreen && AccountScreen->GetVisibility() == ESlateVisibility::Visible;
        if (UWidget* Focus = bAccount ? AccountScreen->GetPreferredFocus() : ConnectionScreen->GetPreferredFocus())
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
