#include "MarburgumSheetSmokeSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlayerController.h"
#include "MarburgumPlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "UI/MarburgumCharacterPanel.h"
#include "UI/MarburgumInventoryPanel.h"
#include "UI/MarburgumUISubsystem.h"
#include "UnrealClient.h"

#define MB2_SHEET_LOG(Format, ...) \
    UE_LOG(LogMarburgumNet, Log, TEXT("MB2SHEET role=client tag=%s " Format), *Role, ##__VA_ARGS__)

namespace MarburgumSheetSmoke
{
    constexpr double SettleSeconds = 1.5;
    constexpr double ShotTimeoutSeconds = 10.0;
    // The slot and the inventory replicate together, possibly a little after the receipt.
    constexpr double ReplicationSeconds = 5.0;
    // How long the peer keeps watching the other player's copy for leaks once it has seen one.
    constexpr double PeerWatchSeconds = 25.0;
    const FName Rod(TEXT("fishing_rod"));
    const FName Camera(TEXT("camera"));
    const FName RightHand(TEXT("right_hand"));
    const FName LeftHand(TEXT("left_hand"));
    const FName Head(TEXT("head"));
    const FName Bag(TEXT("bag"));
}

using namespace MarburgumSheetSmoke;
using MarburgumII::Gameplay::ToString;

bool UMarburgumSheetSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
    static_cast<void>(Outer);
    return false; // Test harnesses and capture tools never run in a player build.
#else
    const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
    return GameInstance && !GameInstance->IsDedicatedServerInstance() && FParse::Param(FCommandLine::Get(), TEXT("MB2SheetSmoke"));
#endif
}

void UMarburgumSheetSmokeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const TCHAR* CommandLine = FCommandLine::Get();
    FParse::Value(CommandLine, TEXT("MB2SheetRole="), Role);
    FParse::Value(CommandLine, TEXT("MB2SheetShot="), ShotPrefix);
    FParse::Value(CommandLine, TEXT("MB2SheetTimeout="), TimeoutSeconds);
    Role = Role.ToUpper();
    StartSeconds = FPlatformTime::Seconds();
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMarburgumSheetSmokeSubsystem::Tick), 0.f);
    MB2_SHEET_LOG("event=start shot=%d", ShotPrefix.IsEmpty() ? 0 : 1);
}

void UMarburgumSheetSmokeSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    if (AMarburgumPlayerController* Bound = Controller.Get())
    {
        Bound->OnInteractionReceipt.RemoveAll(this);
    }
    Super::Deinitialize();
}

bool UMarburgumSheetSmokeSubsystem::Bind()
{
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (!UMarburgumSessionSubsystem::IsAdmitted(World))
    {
        return false;
    }
    AMarburgumPlayerController* Local = Cast<AMarburgumPlayerController>(World->GetFirstPlayerController());
    const AMarburgumPlayerState* State = Local ? Local->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    if (!State || !State->IsInventoryKnown() || !State->AreAttributesKnown() || !Local->GetPawn())
    {
        return false;
    }
    if (Controller.Get() != Local)
    {
        Local->OnInteractionReceipt.AddUObject(this, &UMarburgumSheetSmokeSubsystem::HandleReceipt);
        Controller = Local;
        StepSeconds = FPlatformTime::Seconds();
        StartScores = Scores();
        MB2_SHEET_LOG("event=ready player=%d scores=%s %s", State->GetPlayerId(), *StartScores, *Snapshot());
    }
    return true;
}

void UMarburgumSheetSmokeSubsystem::HandleReceipt(const FMarburgumInteractionReceipt& Receipt)
{
    Receipts.Add(Receipt);
    MB2_SHEET_LOG("event=receipt op=%s outcome=%s reason=%s effect=%s slot=%s item=%s revision=%lld",
        *Receipt.OperationId.ToString(EGuidFormats::Digits), ToString(Receipt.Outcome), ToString(Receipt.Reason), ToString(Receipt.Effect),
        *Receipt.SlotId.ToString(), *Receipt.ItemId.ToString(), Receipt.WorldRevision);
}

const FMarburgumInteractionReceipt* UMarburgumSheetSmokeSubsystem::NextReceipt()
{
    return Receipts.IsValidIndex(ReceiptsSeen) ? &Receipts[ReceiptsSeen++] : nullptr;
}

void UMarburgumSheetSmokeSubsystem::Equip(FName Slot, FName Item, const TCHAR* Label)
{
    Op = FGuid::NewGuid();
    Before = Snapshot();
    MB2_SHEET_LOG("event=request label=%s op=%s slot=%s item=%s", Label, *Op.ToString(EGuidFormats::Digits), *Slot.ToString(), *Item.ToString());
    Controller->SubmitEquip(Op, Slot, Item);
}

const AMarburgumPlayerState* UMarburgumSheetSmokeSubsystem::LocalState() const
{
    const AMarburgumPlayerController* Local = Controller.Get();
    return Local ? Local->GetPlayerState<AMarburgumPlayerState>() : nullptr;
}

FString UMarburgumSheetSmokeSubsystem::Scores() const
{
    const AMarburgumPlayerState* State = LocalState();
    if (!State || !State->AreAttributesKnown())
    {
        return TEXT("unknown");
    }
    const FMarburgumAttributes& A = State->GetAttributes();
    return FString::Printf(TEXT("%d,%d,%d,%d"), A.Might, A.Endurance, A.Eye, A.Poise);
}

FString UMarburgumSheetSmokeSubsystem::Snapshot() const
{
    const AMarburgumPlayerState* State = LocalState();
    if (!State)
    {
        return TEXT("none");
    }
    TArray<FString> Slots;
    for (const FMarburgumEquippedItem& Entry : State->GetEquipment())
    {
        Slots.Add(FString::Printf(TEXT("%s:%s"), *Entry.SlotId.ToString(), *Entry.ItemId.ToString()));
    }
    return FString::Printf(TEXT("rod=%d camera=%d chub=%d slots=%s"), State->CountItem(Rod), State->CountItem(Camera),
        State->CountItem(TEXT("chub")), Slots.IsEmpty() ? TEXT("-") : *FString::Join(Slots, TEXT(",")));
}

void UMarburgumSheetSmokeSubsystem::ExpectRefused(const FMarburgumInteractionReceipt& R, EMarburgumInteractionReason Reason, const TCHAR* What)
{
    Expect(R.Outcome == EMarburgumRequestOutcome::Rejected && R.Reason == Reason && R.WorldRevision == 0,
        FString::Printf(TEXT("%s=%s/%s want %s"), What, ToString(R.Outcome), ToString(R.Reason), ToString(Reason)));
    Expect(Snapshot() == Before, FString::Printf(TEXT("%s_changed=%s was %s"), What, *Snapshot(), *Before));
}

void UMarburgumSheetSmokeSubsystem::CheckOthers()
{
    const UWorld* World = Controller.IsValid() ? Controller->GetWorld() : nullptr;
    const AGameStateBase* Game = World ? World->GetGameState() : nullptr;
    const AMarburgumPlayerState* Own = LocalState();
    if (!Game || !Own)
    {
        return;
    }
    for (const APlayerState* Player : Game->PlayerArray)
    {
        const AMarburgumPlayerState* Other = Cast<AMarburgumPlayerState>(Player);
        if (!Other || Other == Own)
        {
            continue;
        }
        if (!bSawOther)
        {
            bSawOther = true;
            MB2_SHEET_LOG("event=other_seen player=%d attributes_known=%d equipment=%d inventory_known=%d", Other->GetPlayerId(),
                Other->AreAttributesKnown() ? 1 : 0, Other->GetEquipment().Num(), Other->IsInventoryKnown() ? 1 : 0);
        }
        Expect(!Other->AreAttributesKnown() && Other->GetEquipment().IsEmpty() && Other->GetInventory().IsEmpty(),
            FString::Printf(TEXT("private_sheet_leaked player=%d attributes_known=%d equipment=%d"), Other->GetPlayerId(),
                Other->AreAttributesKnown() ? 1 : 0, Other->GetEquipment().Num()));
    }
}

void UMarburgumSheetSmokeSubsystem::Expect(bool bCondition, const FString& What)
{
    if (!bCondition && !Problems.Contains(What))
    {
        Problems.Add(What);
        MB2_SHEET_LOG("event=expectation_failed what=%s", *What);
    }
}

void UMarburgumSheetSmokeSubsystem::Shot(const TCHAR* Label)
{
    if (ShotPrefix.IsEmpty())
    {
        return;
    }
    PendingShot = FString::Printf(TEXT("%s-%s.png"), *ShotPrefix, Label);
    IFileManager::Get().Delete(*PendingShot, false, true, true);
    FScreenshotRequest::RequestScreenshot(PendingShot, true, false);
    ShotRequestedSeconds = FPlatformTime::Seconds();
}

void UMarburgumSheetSmokeSubsystem::Advance(int32 Next, double Now)
{
    Step = Next;
    StepSeconds = Now;
}

bool UMarburgumSheetSmokeSubsystem::Tick(float DeltaTime)
{
    if (bDone)
    {
        return true;
    }
    const double Now = FPlatformTime::Seconds();
    if (Now - StartSeconds > TimeoutSeconds)
    {
        Fail(FString::Printf(TEXT("timeout step=%d"), Step));
        return true;
    }
    if (!PendingShot.IsEmpty())
    {
        const bool bSaved = IFileManager::Get().FileSize(*PendingShot) > 0;
        if (!bSaved && Now - ShotRequestedSeconds < ShotTimeoutSeconds)
        {
            return true;
        }
        MB2_SHEET_LOG("event=shot file=%s saved=%d", *FPaths::GetCleanFilename(PendingShot), bSaved ? 1 : 0);
        Expect(bSaved, FString::Printf(TEXT("shot_not_saved=%s"), *FPaths::GetCleanFilename(PendingShot)));
        PendingShot.Reset();
    }
    if (!Bind())
    {
        return true;
    }
    CheckOthers();
    // Nothing in this run may move a score: not equipping, not a refusal, not a restart.
    Expect(Scores() == StartScores, FString::Printf(TEXT("scores_moved=%s was %s"), *Scores(), *StartScores));
    if (Role == TEXT("SHEET"))
    {
        TickSheet(Now);
    }
    else if (Role == TEXT("PEER"))
    {
        TickPeer(Now);
    }
    else if (Role == TEXT("RESTORED"))
    {
        TickRestored(Now);
    }
    else
    {
        Fail(FString::Printf(TEXT("unknown_role=%s"), *Role));
    }
    return true;
}

void UMarburgumSheetSmokeSubsystem::TickSheet(double Now)
{
    const AMarburgumPlayerState* State = LocalState();
    UMarburgumUISubsystem* UI = GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>();
    UMarburgumCharacterPanel* Sheet = UI ? UI->GetCharacterPanelForTest() : nullptr;
    UMarburgumInventoryPanel* Inventory = UI ? UI->GetInventoryPanelForTest() : nullptr;
    switch (Step)
    {
    case 0:
        if (Now - StepSeconds < SettleSeconds) return;
        // ATTR: the seeded record said `wind`: 14 is Endurance now, and the other three scores survived the rename.
        Expect(StartScores == TEXT("12,14,9,11"), FString::Printf(TEXT("migrated_scores=%s"), *StartScores));
        Expect(Snapshot() == TEXT("rod=1 camera=1 chub=2 slots=-"), FString::Printf(TEXT("start=%s"), *Snapshot()));
        Expect(UI && UI->ShowCharacterSlot(RightHand) && Sheet && Sheet->IsOpen(), TEXT("sheet_not_open"));
        Advance(1, Now);
        return;
    case 1:
        if (Now - StepSeconds < SettleSeconds) return;
        if (Sheet)
        {
            const TArray<int32> Shown = Sheet->GetShownScores();
            Expect(Shown == TArray<int32>({ 12, 14, 9, 11 }), FString::Printf(TEXT("sheet_scores=%d"), Shown.Num()));
            Expect(Sheet->GetShownSlotState(Head).ToString() == TEXT("NONE YET"),
                FString::Printf(TEXT("head_state=%s"), *Sheet->GetShownSlotState(Head).ToString()));
            Expect(Sheet->GetShownSlotState(RightHand).ToString() == TEXT("EMPTY"),
                FString::Printf(TEXT("right_hand_state=%s"), *Sheet->GetShownSlotState(RightHand).ToString()));
            MB2_SHEET_LOG("event=sheet scores=%d,%d,%d,%d head=%s right_hand=%s", Shown.IsValidIndex(0) ? Shown[0] : -1, Shown.IsValidIndex(1) ? Shown[1] : -1,
                Shown.IsValidIndex(2) ? Shown[2] : -1, Shown.IsValidIndex(3) ? Shown[3] : -1, *Sheet->GetShownSlotState(Head).ToString(),
                *Sheet->GetShownSlotState(RightHand).ToString());
        }
        Shot(TEXT("sheet-start"));
        Advance(2, Now);
        return;
    case 2:
        // CHAR: the rod into the right hand, through the sheet's own action.
        Before = Snapshot();
        Expect(Sheet && Sheet->GetSelectedSlot() == RightHand && Sheet->RequestForTest(Rod), TEXT("sheet_offers_no_rod"));
        Advance(3, Now);
        return;
    case 3:
    {
        const FMarburgumInteractionReceipt* R = NextReceipt();
        if (!R) return;
        Expect(R->Outcome == EMarburgumRequestOutcome::Applied && R->Effect == EMarburgumInteractionEffect::ItemEquipped && R->SlotId == RightHand
                && R->ItemId == Rod && R->WorldRevision > 0,
            FString::Printf(TEXT("equip_rod=%s/%s/%s"), ToString(R->Outcome), ToString(R->Reason), ToString(R->Effect)));
        FirstEquip = R->OperationId;
        Advance(4, Now);
        return;
    }
    case 4:
        if (Snapshot() != TEXT("rod=1 camera=1 chub=2 slots=right_hand:fishing_rod") && Now - StepSeconds < ReplicationSeconds) return;
        Expect(Snapshot() == TEXT("rod=1 camera=1 chub=2 slots=right_hand:fishing_rod"), FString::Printf(TEXT("after_equip=%s"), *Snapshot()));
        if (Now - StepSeconds < SettleSeconds) return;
        if (Sheet)
        {
            Expect(Sheet->GetShownSlotState(RightHand).ToString() == TEXT("IN USE"),
                FString::Printf(TEXT("right_hand_state_after=%s"), *Sheet->GetShownSlotState(RightHand).ToString()));
        }
        Shot(TEXT("sheet-equipped"));
        Advance(5, Now);
        return;
    case 5:
        // The same operation again: answered from the stored receipt, nothing moves.
        Op = FirstEquip;
        Before = Snapshot();
        MB2_SHEET_LOG("event=request label=replay op=%s slot=right_hand item=fishing_rod", *Op.ToString(EGuidFormats::Digits));
        Controller->SubmitEquip(Op, RightHand, Rod);
        Advance(6, Now);
        return;
    case 6:
    {
        const FMarburgumInteractionReceipt* R = NextReceipt();
        if (!R) return;
        Expect(R->Outcome == EMarburgumRequestOutcome::Duplicate && R->Effect == EMarburgumInteractionEffect::ItemEquipped && R->SlotId == RightHand,
            FString::Printf(TEXT("replay=%s/%s slot=%s"), ToString(R->Outcome), ToString(R->Effect), *R->SlotId.ToString()));
        Expect(Snapshot() == Before, FString::Printf(TEXT("replay_changed=%s"), *Snapshot()));
        Equip(LeftHand, Rod, TEXT("second_hand"));
        Advance(7, Now);
        return;
    }
    case 7:
    {
        const FMarburgumInteractionReceipt* R = NextReceipt();
        if (!R) return;
        ExpectRefused(*R, EMarburgumInteractionReason::InsufficientItems, TEXT("second_hand"));
        Equip(Head, Camera, TEXT("camera_on_head"));
        Advance(8, Now);
        return;
    }
    case 8:
    {
        const FMarburgumInteractionReceipt* R = NextReceipt();
        if (!R) return;
        ExpectRefused(*R, EMarburgumInteractionReason::NotEquippable, TEXT("camera_on_head"));
        Equip(FName(TEXT("pocket")), Camera, TEXT("unknown_slot"));
        Advance(9, Now);
        return;
    }
    case 9:
    {
        const FMarburgumInteractionReceipt* R = NextReceipt();
        if (!R) return;
        ExpectRefused(*R, EMarburgumInteractionReason::UnknownSlot, TEXT("unknown_slot"));
        Equip(RightHand, FName(TEXT("not_an_item")), TEXT("unknown_item"));
        Advance(10, Now);
        return;
    }
    case 10:
    {
        const FMarburgumInteractionReceipt* R = NextReceipt();
        if (!R) return;
        ExpectRefused(*R, EMarburgumInteractionReason::NotEquippable, TEXT("unknown_item"));
        Equip(Bag, NAME_None, TEXT("empty_bag"));
        Advance(11, Now);
        return;
    }
    case 11:
    {
        const FMarburgumInteractionReceipt* R = NextReceipt();
        if (!R) return;
        ExpectRefused(*R, EMarburgumInteractionReason::NothingToDo, TEXT("empty_bag"));
        // The camera into the left hand from the Inventory's card.
        Expect(UI && UI->ShowInventoryItem(Camera) && Inventory && Inventory->IsOpen(), TEXT("inventory_not_open"));
        Advance(12, Now);
        return;
    }
    case 12:
        if (Now - StepSeconds < SettleSeconds) return;
        Before = Snapshot();
        Expect(Inventory && Inventory->RequestForTest(LeftHand), TEXT("inventory_offers_no_left_hand"));
        Advance(13, Now);
        return;
    case 13:
    {
        const FMarburgumInteractionReceipt* R = NextReceipt();
        if (!R) return;
        Expect(R->Outcome == EMarburgumRequestOutcome::Applied && R->Effect == EMarburgumInteractionEffect::ItemEquipped && R->SlotId == LeftHand
                && R->ItemId == Camera,
            FString::Printf(TEXT("equip_camera=%s/%s/%s"), ToString(R->Outcome), ToString(R->Reason), ToString(R->Effect)));
        Advance(14, Now);
        return;
    }
    case 14:
    {
        const FString Want = TEXT("rod=1 camera=1 chub=2 slots=right_hand:fishing_rod,left_hand:camera");
        if (Snapshot() != Want && Now - StepSeconds < ReplicationSeconds) return;
        Expect(Snapshot() == Want, FString::Printf(TEXT("after_camera=%s"), *Snapshot()));
        if (Now - StepSeconds < SettleSeconds) return;
        Shot(TEXT("inventory-camera"));
        Advance(15, Now);
        return;
    }
    case 15:
        Expect(UI && UI->ShowCharacterSlot(LeftHand), TEXT("sheet_not_reopened"));
        Advance(16, Now);
        return;
    case 16:
        if (Now - StepSeconds < SettleSeconds) return;
        if (Sheet)
        {
            Expect(Sheet->GetShownSlotState(LeftHand).ToString() == TEXT("IN USE"),
                FString::Printf(TEXT("left_hand_state=%s"), *Sheet->GetShownSlotState(LeftHand).ToString()));
        }
        Shot(TEXT("sheet-both-hands"));
        Advance(17, Now);
        return;
    default:
        MB2_SHEET_LOG("event=final scores=%s %s", *Scores(), *Snapshot());
        Pass();
        return;
    }
}

void UMarburgumSheetSmokeSubsystem::TickPeer(double Now)
{
    switch (Step)
    {
    case 0:
        if (Now - StepSeconds < SettleSeconds) return;
        // The mixed record held endurance 16 and wind 7: the canonical id won.
        Expect(StartScores == TEXT("10,16,10,10"), FString::Printf(TEXT("peer_scores=%s"), *StartScores));
        Expect(Snapshot() == TEXT("rod=1 camera=0 chub=0 slots=-"), FString::Printf(TEXT("peer_start=%s"), *Snapshot()));
        Equip(RightHand, Rod, TEXT("peer_rod"));
        Advance(1, Now);
        return;
    case 1:
    {
        const FMarburgumInteractionReceipt* R = NextReceipt();
        if (!R) return;
        Expect(R->Outcome == EMarburgumRequestOutcome::Applied && R->Effect == EMarburgumInteractionEffect::ItemEquipped,
            FString::Printf(TEXT("peer_equip=%s/%s"), ToString(R->Outcome), ToString(R->Reason)));
        Advance(2, Now);
        return;
    }
    case 2:
        // Watch the other player's copy while it equips: nothing private may arrive.
        if (!bSawOther || Now - StepSeconds < PeerWatchSeconds) return;
        Expect(Snapshot() == TEXT("rod=1 camera=0 chub=0 slots=right_hand:fishing_rod"), FString::Printf(TEXT("peer_after=%s"), *Snapshot()));
        Advance(3, Now);
        return;
    default:
        MB2_SHEET_LOG("event=final scores=%s %s saw_other=%d", *Scores(), *Snapshot(), bSawOther ? 1 : 0);
        Pass();
        return;
    }
}

void UMarburgumSheetSmokeSubsystem::TickRestored(double Now)
{
    UMarburgumUISubsystem* UI = GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>();
    UMarburgumCharacterPanel* Sheet = UI ? UI->GetCharacterPanelForTest() : nullptr;
    switch (Step)
    {
    case 0:
        if (Now - StepSeconds < SettleSeconds) return;
        Expect(StartScores == TEXT("12,14,9,11"), FString::Printf(TEXT("restored_scores=%s"), *StartScores));
        Expect(Snapshot() == TEXT("rod=1 camera=1 chub=2 slots=right_hand:fishing_rod,left_hand:camera"),
            FString::Printf(TEXT("restored=%s"), *Snapshot()));
        MB2_SHEET_LOG("event=restored scores=%s %s", *StartScores, *Snapshot());
        Expect(UI && UI->ShowCharacterSlot(RightHand) && Sheet && Sheet->IsOpen(), TEXT("sheet_not_open"));
        Advance(1, Now);
        return;
    case 1:
        if (Now - StepSeconds < SettleSeconds) return;
        Shot(TEXT("restored"));
        Advance(2, Now);
        return;
    case 2:
        Before = Snapshot();
        Expect(Sheet && Sheet->RequestForTest(NAME_None), TEXT("sheet_offers_no_unequip"));
        Advance(3, Now);
        return;
    case 3:
    {
        const FMarburgumInteractionReceipt* R = NextReceipt();
        if (!R) return;
        Expect(R->Outcome == EMarburgumRequestOutcome::Applied && R->Effect == EMarburgumInteractionEffect::ItemUnequipped && R->SlotId == RightHand
                && R->ItemId == Rod,
            FString::Printf(TEXT("unequip=%s/%s/%s"), ToString(R->Outcome), ToString(R->Reason), ToString(R->Effect)));
        Advance(4, Now);
        return;
    }
    case 4:
    {
        const FString Want = TEXT("rod=1 camera=1 chub=2 slots=left_hand:camera");
        if (Snapshot() != Want && Now - StepSeconds < ReplicationSeconds) return;
        Expect(Snapshot() == Want, FString::Printf(TEXT("after_unequip=%s"), *Snapshot()));
        if (Now - StepSeconds < SettleSeconds) return;
        Shot(TEXT("restored-unequipped"));
        Advance(5, Now);
        return;
    }
    default:
        MB2_SHEET_LOG("event=final scores=%s %s", *Scores(), *Snapshot());
        Pass();
        return;
    }
}

void UMarburgumSheetSmokeSubsystem::Pass()
{
    if (Problems.IsEmpty())
    {
        MB2_SHEET_LOG("event=pass");
        bDone = true;
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }
    else
    {
        Fail(TEXT("expectations"));
    }
}

void UMarburgumSheetSmokeSubsystem::Fail(const FString& Reason)
{
    if (bDone)
    {
        return;
    }
    Problems.Add(Reason);
    MB2_SHEET_LOG("event=fail reason=%s", *FString::Join(Problems, TEXT(",")));
    bDone = true;
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

#undef MB2_SHEET_LOG
