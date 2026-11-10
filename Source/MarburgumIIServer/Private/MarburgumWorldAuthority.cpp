#include "MarburgumWorldAuthority.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumArea.h"
#include "MarburgumInteractable.h"
#include "MarburgumMissionRules.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumNpc.h"
#include "MarburgumPlayerController.h"
#include "MarburgumPlayerState.h"
#include "MarburgumWorldStore.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

using namespace MarburgumII::Gameplay;

namespace
{
    FString OperationKey(const FGuid& OperationId)
    {
        return OperationId.ToString(EGuidFormats::Digits);
    }

    TArray<FMarburgumItemStack> ToStacks(const TMap<FString, int32>* Owned)
    {
        TArray<FMarburgumItemStack> Stacks;
        if (Owned)
        {
            TArray<FString> Items;
            Owned->GetKeys(Items);
            Items.Sort();
            for (const FString& Item : Items)
            {
                if ((*Owned)[Item] > 0)
                {
                    Stacks.Add({ FName(*Item), (*Owned)[Item] });
                }
            }
        }
        return Stacks;
    }

    TArray<FMarburgumMissionState> ToMissions(const TMap<FString, FMarburgumStoredMission>* Owned)
    {
        TArray<FMarburgumMissionState> States;
        if (Owned)
        {
            TArray<FString> Ids;
            Owned->GetKeys(Ids);
            Ids.Sort();
            for (const FString& Id : Ids)
            {
                FMarburgumMissionState& State = States.AddDefaulted_GetRef();
                State.MissionId = FName(*Id);
                State.StageId = FName(*(*Owned)[Id].Stage);
                State.StartedRevision = (*Owned)[Id].StartedRevision;
            }
        }
        return States;
    }

    EMarburgumInteractionEffect EffectFromKind(const FString& Kind)
    {
        for (const EMarburgumInteractionEffect Effect : { EMarburgumInteractionEffect::ItemGranted,
                 EMarburgumInteractionEffect::MissionStarted, EMarburgumInteractionEffect::StageCompleted })
        {
            if (Kind == ToString(Effect))
            {
                return Effect;
            }
        }
        return EMarburgumInteractionEffect::None;
    }
}

bool UMarburgumWorldAuthority::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

void UMarburgumWorldAuthority::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    const ENetMode NetMode = InWorld.GetNetMode();
    // Only a networked authority owns durable world state; standalone front-end worlds never do.
    if (NetMode != NM_DedicatedServer && NetMode != NM_ListenServer)
    {
        return;
    }
    bActive = true;

    // Location does not depend on the store: it is derived from pawn positions and never persisted.
    InWorld.GetTimerManager().SetTimer(LocationTimer, FTimerDelegate::CreateUObject(this, &UMarburgumWorldAuthority::UpdateLocations),
        LocationUpdateSeconds, true);

    FString Path;
    if (!FParse::Value(FCommandLine::Get(), TEXT("MB2WorldStore="), Path) || Path.IsEmpty())
    {
        const FString MapName = UWorld::RemovePIEPrefix(InWorld.GetMapName());
        Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MarburgumII"), TEXT("WorldStore"), MapName + TEXT(".json"));
    }
    Path = FPaths::ConvertRelativePathToFull(Path);
    Store = MakeShared<FMarburgumWorldStore>(Path);
    if (FParse::Param(FCommandLine::Get(), TEXT("MB2StoreFaultInjection")))
    {
        Store->EnableFaultInjection();
    }

    FString Error;
    bStoreAvailable = Store->Load(Error);
    if (!bStoreAvailable)
    {
        // Never overwrite a store we cannot read: interactions are refused until an operator fixes it.
        UE_LOG(LogMarburgumNet, Error, TEXT("%s role=server event=store_unavailable path=\"%s\" error=\"%s\""),
            InteractLogPrefix, *Path, *Error);
        return;
    }

    ApplyStocks();
    PostLoginHandle = FGameModeEvents::GameModePostLoginEvent.AddUObject(this, &UMarburgumWorldAuthority::HandlePostLogin);
    // A listen-server host has already logged in by now.
    for (FConstPlayerControllerIterator It = InWorld.GetPlayerControllerIterator(); It; ++It)
    {
        if (AMarburgumPlayerState* State = It->Get() ? It->Get()->GetPlayerState<AMarburgumPlayerState>() : nullptr)
        {
            ApplyInventory(*State);
        }
    }

    const FMarburgumWorldRecord& Record = Store->Get();
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=store_loaded path=\"%s\" schema=%d revision=%lld characters=%d missions=%d receipts=%d"),
        InteractLogPrefix, *Path, Store->GetLoadedSchema(), Record.Revision, Record.Inventories.Num(), Record.Missions.Num(), Record.Receipts.Num());
    for (TActorIterator<AMarburgumInteractable> It(&InWorld); It; ++It)
    {
        if (!It->IsItemSource())
        {
            continue;
        }
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=stock target=%s stock=%d source=%s"),
            InteractLogPrefix, *It->InteractableId.ToString(), It->GetRemainingStock(),
            Record.Stocks.Contains(It->InteractableId.ToString()) ? TEXT("store") : TEXT("authored"));
    }
}

void UMarburgumWorldAuthority::Deinitialize()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(LocationTimer);
    }
    FGameModeEvents::GameModePostLoginEvent.Remove(PostLoginHandle);
    Store.Reset();
    Super::Deinitialize();
}

void UMarburgumWorldAuthority::HandlePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer)
{
    if (!GameMode || GameMode->GetWorld() != GetWorld() || !NewPlayer)
    {
        return;
    }
    if (AMarburgumPlayerState* State = NewPlayer->GetPlayerState<AMarburgumPlayerState>())
    {
        ApplyInventory(*State);
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=inventory_restored player=%d character=%s stacks=%d missions=%d"),
            InteractLogPrefix, State->GetPlayerId(),
            State->GetCharacterKey().IsEmpty() ? TEXT("-") : *State->GetCharacterKey(), State->GetInventory().Num(),
            State->GetMissions().Num());
    }
}

void UMarburgumWorldAuthority::ApplyInventory(AMarburgumPlayerState& State) const
{
    // Custody and mission progress always travel together, so a client never sees one without the other.
    const FString& Key = State.GetCharacterKey();
    State.SetInventoryAuthority(ToStacks(Key.IsEmpty() ? nullptr : Store->Get().Inventories.Find(Key)));
    State.SetMissionsAuthority(ToMissions(Key.IsEmpty() ? nullptr : Store->Get().Missions.Find(Key)));
}

void UMarburgumWorldAuthority::ApplyStocks() const
{
    for (TActorIterator<AMarburgumInteractable> It(GetWorld()); It; ++It)
    {
        if (It->IsItemSource())
        {
            It->SetRemainingStockAuthority(CurrentStock(**It));
        }
    }
}

void UMarburgumWorldAuthority::UpdateLocations()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* Player = It->Get();
        const APawn* Body = Player ? Player->GetPawn() : nullptr;
        AMarburgumPlayerState* State = Player ? Player->GetPlayerState<AMarburgumPlayerState>() : nullptr;
        if (!Body || !State)
        {
            continue;
        }
        const FName Area = AMarburgumArea::Resolve(World, Body->GetActorLocation());
        if (!State->IsLocationKnown() || State->GetAreaId() != Area)
        {
            State->SetAreaAuthority(Area);
            // The server's pawn position (metres) lets the Lent smoke bound how fast a client moved (ADR-019).
            const FVector Where = Body->GetActorLocation() / 100.f;
            UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=area player=%d character=%s area=%s x_m=%.2f y_m=%.2f"),
                InteractLogPrefix, State->GetPlayerId(),
                State->GetCharacterKey().IsEmpty() ? TEXT("-") : *State->GetCharacterKey(),
                Area.IsNone() ? TEXT("-") : *Area.ToString(), Where.X, Where.Y);
        }
    }
}

int32 UMarburgumWorldAuthority::CurrentStock(const AMarburgumInteractable& Target) const
{
    const int32* Stored = Store ? Store->Get().Stocks.Find(Target.InteractableId.ToString()) : nullptr;
    return Stored ? *Stored : Target.InitialStock;
}

AMarburgumInteractable* UMarburgumWorldAuthority::FindInteractable(const FString& Id) const
{
    for (TActorIterator<AMarburgumInteractable> It(GetWorld()); It; ++It)
    {
        if (It->InteractableId.ToString() == Id)
        {
            return *It;
        }
    }
    return nullptr;
}

FMarburgumInteractionReceipt UMarburgumWorldAuthority::Finish(FMarburgumInteractionReceipt Receipt, const AMarburgumPlayerController& Requester) const
{
    const AMarburgumPlayerState* State = Requester.GetPlayerState<AMarburgumPlayerState>();
    UE_LOG(LogMarburgumNet, Log,
        TEXT("%s role=server event=interaction player=%d character=%s op=%s target=%s outcome=%s reason=%s effect=%s mission=%s stage=%s item=%s quantity=%d stock=%d revision=%lld"),
        InteractLogPrefix,
        State ? State->GetPlayerId() : -1,
        State && !State->GetCharacterKey().IsEmpty() ? *State->GetCharacterKey() : TEXT("-"),
        *OperationKey(Receipt.OperationId),
        Receipt.InteractableId.IsNone() ? TEXT("-") : *Receipt.InteractableId.ToString(),
        ToString(Receipt.Outcome), ToString(Receipt.Reason), ToString(Receipt.Effect),
        Receipt.MissionId.IsNone() ? TEXT("-") : *Receipt.MissionId.ToString(),
        Receipt.MissionStage.IsNone() ? TEXT("-") : *Receipt.MissionStage.ToString(),
        Receipt.ItemId.IsNone() ? TEXT("-") : *Receipt.ItemId.ToString(),
        Receipt.Quantity, Receipt.StockRemaining, Receipt.WorldRevision);
    return Receipt;
}

FMarburgumInteractionReceipt UMarburgumWorldAuthority::HandleInteraction(
    AMarburgumPlayerController& Requester, const FGuid& OperationId, AMarburgumInteractable* Target)
{
    FMarburgumInteractionReceipt Receipt;
    Receipt.OperationId = OperationId;
    Receipt.Outcome = EMarburgumRequestOutcome::Rejected;
    auto Refuse = [this, &Receipt, &Requester](EMarburgumInteractionReason Reason)
    {
        Receipt.Reason = Reason;
        return Finish(Receipt, Requester);
    };

    if (!OperationId.IsValid())
    {
        return Refuse(EMarburgumInteractionReason::InvalidOperation);
    }
    if (!bActive)
    {
        return Refuse(EMarburgumInteractionReason::AuthorityUnavailable);
    }
    if (!bStoreAvailable || !Store)
    {
        return Refuse(EMarburgumInteractionReason::StoreUnavailable);
    }
    AMarburgumPlayerState* State = Requester.GetPlayerState<AMarburgumPlayerState>();
    if (!State || State->GetCharacterKey().IsEmpty())
    {
        return Refuse(EMarburgumInteractionReason::NoCharacter);
    }
    const FString& Character = State->GetCharacterKey();
    const FMarburgumWorldRecord& Current = Store->Get();

    // Idempotency survives restarts: committed receipts live in the durable record.
    if (const FMarburgumStoredReceipt* Prior = Current.Receipts.Find(OperationKey(OperationId)))
    {
        Receipt.InteractableId = FName(*Prior->Interactable);
        if (Prior->Character != Character)
        {
            return Refuse(EMarburgumInteractionReason::OperationConflict);
        }
        Receipt.Outcome = EMarburgumRequestOutcome::Duplicate;
        Receipt.Effect = EffectFromKind(Prior->Kind);
        Receipt.ItemId = Prior->Item.IsEmpty() ? NAME_None : FName(*Prior->Item);
        Receipt.Quantity = Prior->Quantity;
        Receipt.MissionId = Prior->Mission.IsEmpty() ? NAME_None : FName(*Prior->Mission);
        Receipt.MissionStage = Prior->Stage.IsEmpty() ? NAME_None : FName(*Prior->Stage);
        Receipt.WorldRevision = Prior->Revision;
        const AMarburgumInteractable* PriorTarget = FindInteractable(Prior->Interactable);
        Receipt.StockRemaining = PriorTarget && PriorTarget->IsItemSource() ? CurrentStock(*PriorTarget) : -1;
        return Finish(Receipt, Requester);
    }

    AMarburgumNpc* Npc = Cast<AMarburgumNpc>(Target);
    if (!Target || Target->GetWorld() != GetWorld() || Target->InteractableId.IsNone()
        || (!Npc && !FindItem(Target->GrantedItemId.ToString())))
    {
        return Refuse(EMarburgumInteractionReason::UnknownTarget);
    }
    Receipt.InteractableId = Target->InteractableId;
    const FString TargetId = Target->InteractableId.ToString();

    const APawn* Body = Requester.GetPawn();
    const float Reach = Target->InteractionRange + RangeToleranceCm;
    if (!Body || FVector::DistSquared(Body->GetActorLocation(), Target->GetActorLocation()) > FMath::Square(Reach))
    {
        return Refuse(EMarburgumInteractionReason::OutOfRange);
    }
    if (Npc)
    {
        return HandleNpc(Receipt, Requester, *State, *Npc);
    }

    const int32 Stock = CurrentStock(*Target);
    Receipt.StockRemaining = Stock;
    if (Stock <= 0)
    {
        return Refuse(EMarburgumInteractionReason::OutOfStock);
    }

    // One all-or-nothing transition: stock, custody and receipt change together.
    FMarburgumWorldRecord Next = Current;
    ++Next.Revision;
    Next.Stocks.Add(TargetId, Stock - 1);
    const FString Item = Target->GrantedItemId.ToString();
    Next.Inventories.FindOrAdd(Character).FindOrAdd(Item) += 1;
    FMarburgumStoredReceipt Stored;
    Stored.Character = Character;
    Stored.Kind = ToString(EMarburgumInteractionEffect::ItemGranted);
    Stored.Interactable = TargetId;
    Stored.Item = Item;
    Stored.Quantity = 1;
    Stored.Revision = Next.Revision;
    Stored.CommittedUtc = FDateTime::UtcNow().ToIso8601();
    Next.Receipts.Add(OperationKey(OperationId), Stored);

    FString Error;
    if (!Store->Commit(Next, Error))
    {
        UE_LOG(LogMarburgumNet, Warning, TEXT("%s role=server event=commit_failed op=%s error=\"%s\""),
            InteractLogPrefix, *OperationKey(OperationId), *Error);
        return Refuse(EMarburgumInteractionReason::StoreWriteFailed);
    }

    // Durable first, visible second.
    Target->SetRemainingStockAuthority(Stock - 1);
    ApplyInventory(*State);

    Receipt.Outcome = EMarburgumRequestOutcome::Applied;
    Receipt.Effect = EMarburgumInteractionEffect::ItemGranted;
    Receipt.ItemId = Target->GrantedItemId;
    Receipt.Quantity = 1;
    Receipt.StockRemaining = Stock - 1;
    Receipt.WorldRevision = Next.Revision;
    return Finish(Receipt, Requester);
}

FMarburgumInteractionReceipt UMarburgumWorldAuthority::HandleNpc(FMarburgumInteractionReceipt Receipt,
    const AMarburgumPlayerController& Requester, AMarburgumPlayerState& State, const AMarburgumNpc& Npc)
{
    const FString& Character = State.GetCharacterKey();
    FMarburgumWorldRecord Next;
    const FMarburgumNpcDecision Decision =
        MarburgumII::Missions::DecideNpcInteraction(Store->Get(), Character, Npc.InteractableId.ToString(), Next);
    Receipt.MissionId = Decision.Mission.IsEmpty() ? NAME_None : FName(*Decision.Mission);
    Receipt.MissionStage = Decision.Stage.IsEmpty() ? NAME_None : FName(*Decision.Stage);
    if (!Decision.bApplied)
    {
        Receipt.Reason = Decision.Reason;
        return Finish(Receipt, Requester);
    }

    // Mission progress and any item it grants or consumes are one all-or-nothing transition.
    FMarburgumStoredReceipt Stored;
    Stored.Character = Character;
    Stored.Kind = ToString(Decision.Effect);
    Stored.Interactable = Npc.InteractableId.ToString();
    Stored.Item = Decision.Item;
    Stored.Quantity = Decision.Quantity;
    Stored.Mission = Decision.Mission;
    Stored.Stage = Decision.Stage;
    Stored.Revision = Next.Revision;
    Stored.CommittedUtc = FDateTime::UtcNow().ToIso8601();
    Next.Receipts.Add(OperationKey(Receipt.OperationId), Stored);

    FString Error;
    if (!Store->Commit(Next, Error))
    {
        UE_LOG(LogMarburgumNet, Warning, TEXT("%s role=server event=commit_failed op=%s error=\"%s\""),
            InteractLogPrefix, *OperationKey(Receipt.OperationId), *Error);
        Receipt.Reason = EMarburgumInteractionReason::StoreWriteFailed;
        return Finish(Receipt, Requester);
    }

    // Durable first, visible second.
    ApplyInventory(State);

    Receipt.Outcome = EMarburgumRequestOutcome::Applied;
    Receipt.Effect = Decision.Effect;
    Receipt.ItemId = Decision.Item.IsEmpty() ? NAME_None : FName(*Decision.Item);
    Receipt.Quantity = Decision.Quantity;
    Receipt.WorldRevision = Next.Revision;
    return Finish(Receipt, Requester);
}
