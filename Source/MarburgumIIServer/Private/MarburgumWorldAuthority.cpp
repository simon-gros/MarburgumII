#include "MarburgumWorldAuthority.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumArea.h"
#include "MarburgumCharacterMovement.h"
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

    FMarburgumStoredCondition ToStored(const FMarburgumCondition& Condition)
    {
        FMarburgumStoredCondition Stored;
        Stored.Vitality = Condition.Vitality;
        Stored.Stamina = Condition.Stamina;
        Stored.bWinded = Condition.bWinded;
        return Stored;
    }

    // Condition evidence lines share the movement component's prefix (scripts/condition-smoke-linux.sh).
    constexpr const TCHAR* ConditionLogPrefix = TEXT("MB2COND");

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
    // Players get a condition even without a store (a rested body that is never saved), so they can move.
    PostLoginHandle = FGameModeEvents::GameModePostLoginEvent.AddUObject(this, &UMarburgumWorldAuthority::HandlePostLogin);
    LogoutHandle = FGameModeEvents::GameModeLogoutEvent.AddUObject(this, &UMarburgumWorldAuthority::HandleLogout);
    TearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &UMarburgumWorldAuthority::HandleWorldTearDown);
    if (!bStoreAvailable)
    {
        // Never overwrite a store we cannot read: interactions are refused until an operator fixes it.
        UE_LOG(LogMarburgumNet, Error, TEXT("%s role=server event=store_unavailable path=\"%s\" error=\"%s\""),
            InteractLogPrefix, *Path, *Error);
        return;
    }

    ApplyStocks();
    // A listen-server host has already logged in by now.
    for (FConstPlayerControllerIterator It = InWorld.GetPlayerControllerIterator(); It; ++It)
    {
        if (AMarburgumPlayerState* State = It->Get() ? It->Get()->GetPlayerState<AMarburgumPlayerState>() : nullptr)
        {
            ApplyInventory(*State);
            RestoreCondition(*State);
        }
    }

    const FMarburgumWorldRecord& Record = Store->Get();
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=store_loaded path=\"%s\" schema=%d revision=%lld characters=%d missions=%d conditions=%d receipts=%d"),
        InteractLogPrefix, *Path, Store->GetLoadedSchema(), Record.Revision, Record.Inventories.Num(), Record.Missions.Num(),
        Record.Conditions.Num(), Record.Receipts.Num());
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
    FGameModeEvents::GameModeLogoutEvent.Remove(LogoutHandle);
    FWorldDelegates::OnWorldBeginTearDown.Remove(TearDownHandle);
    Store.Reset();
    Super::Deinitialize();
}

void UMarburgumWorldAuthority::HandlePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer)
{
    if (!GameMode || GameMode->GetWorld() != GetWorld() || !NewPlayer)
    {
        return;
    }
    AMarburgumPlayerState* State = NewPlayer->GetPlayerState<AMarburgumPlayerState>();
    if (!State)
    {
        return;
    }
    RestoreCondition(*State);
    if (bStoreAvailable)
    {
        ApplyInventory(*State);
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=inventory_restored player=%d character=%s stacks=%d missions=%d"),
            InteractLogPrefix, State->GetPlayerId(),
            State->GetCharacterKey().IsEmpty() ? TEXT("-") : *State->GetCharacterKey(), State->GetInventory().Num(),
            State->GetMissions().Num());
    }
}

void UMarburgumWorldAuthority::HandleLogout(AGameModeBase* GameMode, AController* Exiting)
{
    if (!GameMode || GameMode->GetWorld() != GetWorld() || !Exiting)
    {
        return;
    }
    // The pawn is already gone at logout; the player state holds the last simulated condition.
    if (const AMarburgumPlayerState* State = Exiting->GetPlayerState<AMarburgumPlayerState>())
    {
        SaveCondition(*State, TEXT("logout"));
    }
}

void UMarburgumWorldAuthority::HandleWorldTearDown(UWorld* World)
{
    if (World != GetWorld())
    {
        return;
    }
    // A server shutting down with players still connected saves them as a logout would.
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        if (const AMarburgumPlayerState* State = It->Get() ? It->Get()->GetPlayerState<AMarburgumPlayerState>() : nullptr)
        {
            SaveCondition(*State, TEXT("shutdown"));
        }
    }
}

void UMarburgumWorldAuthority::RestoreCondition(AMarburgumPlayerState& State) const
{
    using namespace MarburgumII::Gameplay;
    const FString& Key = State.GetCharacterKey();
    const FMarburgumStoredCondition* Saved =
        bStoreAvailable && Store && !Key.IsEmpty() ? Store->Get().Conditions.Find(Key) : nullptr;
    FMarburgumCondition Condition;
    if (Saved)
    {
        Condition.Vitality = static_cast<uint8>(Saved->Vitality);
        Condition.Stamina = static_cast<uint8>(Saved->Stamina);
        // Winded only means something below the breath a run resumes at.
        Condition.bWinded = Saved->bWinded && Saved->Stamina < RunResumeStamina;
    }
    State.RestoreConditionAuthority(Condition);
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=condition_restored player=%d character=%s source=%s vitality=%d stamina=%d winded=%d"),
        ConditionLogPrefix, State.GetPlayerId(), Key.IsEmpty() ? TEXT("-") : *Key, Saved ? TEXT("store") : TEXT("rested"),
        Condition.Vitality, Condition.Stamina, Condition.bWinded ? 1 : 0);
}

void UMarburgumWorldAuthority::SaveCondition(const AMarburgumPlayerState& State, const TCHAR* Why)
{
    const FString& Key = State.GetCharacterKey();
    if (!bStoreAvailable || !Store || Key.IsEmpty() || !State.IsConditionKnown())
    {
        return;
    }
    const FMarburgumStoredCondition Next = ToStored(State.GetCondition());
    const FMarburgumStoredCondition* Prior = Store->Get().Conditions.Find(Key);
    if (Next == (Prior ? *Prior : FMarburgumStoredCondition()))
    {
        // Nothing to record: a character that never ran (or has its breath back) causes no commit.
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=condition_unchanged why=%s character=%s stamina=%d winded=%d"),
            ConditionLogPrefix, Why, *Key, Next.Stamina, Next.bWinded ? 1 : 0);
        return;
    }
    FMarburgumWorldRecord Record = Store->Get();
    ++Record.Revision;
    if (Next == FMarburgumStoredCondition())
    {
        Record.Conditions.Remove(Key);
    }
    else
    {
        Record.Conditions.Add(Key, Next);
    }
    FString Error;
    if (!Store->Commit(Record, Error))
    {
        UE_LOG(LogMarburgumNet, Warning, TEXT("%s role=server event=condition_save_failed why=%s character=%s error=\"%s\""),
            ConditionLogPrefix, Why, *Key, *Error);
        return;
    }
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=condition_saved why=%s character=%s vitality=%d stamina=%d winded=%d revision=%lld"),
        ConditionLogPrefix, Why, *Key, Next.Vitality, Next.Stamina, Next.bWinded ? 1 : 0, Record.Revision);
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
            // The server's pawn position (metres) and running time let the Lent smoke bound how fast a
            // client moved (ADR-019, ADR-020).
            const FVector Where = Body->GetActorLocation() / 100.f;
            const ACharacter* Walker = Cast<ACharacter>(Body);
            const UMarburgumCharacterMovement* Movement =
                Walker ? Cast<UMarburgumCharacterMovement>(Walker->GetCharacterMovement()) : nullptr;
            UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=area player=%d character=%s area=%s x_m=%.2f y_m=%.2f run_s=%.2f"),
                InteractLogPrefix, State->GetPlayerId(),
                State->GetCharacterKey().IsEmpty() ? TEXT("-") : *State->GetCharacterKey(),
                Area.IsNone() ? TEXT("-") : *Area.ToString(), Where.X, Where.Y, Movement ? Movement->GetServerRunSeconds() : 0.0);
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
