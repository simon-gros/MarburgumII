#include "MarburgumWorldAuthority.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumInteractable.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlayerController.h"
#include "MarburgumPlayerState.h"
#include "MarburgumWorldStore.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

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
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=store_loaded path=\"%s\" revision=%lld characters=%d receipts=%d"),
        InteractLogPrefix, *Path, Record.Revision, Record.Inventories.Num(), Record.Receipts.Num());
    for (TActorIterator<AMarburgumInteractable> It(&InWorld); It; ++It)
    {
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=stock target=%s stock=%d source=%s"),
            InteractLogPrefix, *It->InteractableId.ToString(), It->GetRemainingStock(),
            Record.Stocks.Contains(It->InteractableId.ToString()) ? TEXT("store") : TEXT("authored"));
    }
}

void UMarburgumWorldAuthority::Deinitialize()
{
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
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=inventory_restored player=%d character=%s stacks=%d"),
            InteractLogPrefix, State->GetPlayerId(),
            State->GetCharacterKey().IsEmpty() ? TEXT("-") : *State->GetCharacterKey(), State->GetInventory().Num());
    }
}

void UMarburgumWorldAuthority::ApplyInventory(AMarburgumPlayerState& State) const
{
    const FString& Key = State.GetCharacterKey();
    State.SetInventoryAuthority(ToStacks(Key.IsEmpty() ? nullptr : Store->Get().Inventories.Find(Key)));
}

void UMarburgumWorldAuthority::ApplyStocks() const
{
    for (TActorIterator<AMarburgumInteractable> It(GetWorld()); It; ++It)
    {
        It->SetRemainingStockAuthority(CurrentStock(**It));
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
        TEXT("%s role=server event=interaction player=%d character=%s op=%s target=%s outcome=%s reason=%s item=%s quantity=%d stock=%d revision=%lld"),
        InteractLogPrefix,
        State ? State->GetPlayerId() : -1,
        State && !State->GetCharacterKey().IsEmpty() ? *State->GetCharacterKey() : TEXT("-"),
        *OperationKey(Receipt.OperationId),
        Receipt.InteractableId.IsNone() ? TEXT("-") : *Receipt.InteractableId.ToString(),
        ToString(Receipt.Outcome), ToString(Receipt.Reason),
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
        Receipt.ItemId = FName(*Prior->Item);
        Receipt.Quantity = Prior->Quantity;
        Receipt.WorldRevision = Prior->Revision;
        const AMarburgumInteractable* PriorTarget = FindInteractable(Prior->Interactable);
        Receipt.StockRemaining = PriorTarget ? CurrentStock(*PriorTarget) : -1;
        return Finish(Receipt, Requester);
    }

    if (!Target || Target->GetWorld() != GetWorld() || Target->InteractableId.IsNone()
        || !FindItem(Target->GrantedItemId.ToString()))
    {
        return Refuse(EMarburgumInteractionReason::UnknownTarget);
    }
    Receipt.InteractableId = Target->InteractableId;
    const FString TargetId = Target->InteractableId.ToString();
    const int32 Stock = CurrentStock(*Target);
    Receipt.StockRemaining = Stock;

    const APawn* Body = Requester.GetPawn();
    const float Reach = Target->InteractionRange + RangeToleranceCm;
    if (!Body || FVector::DistSquared(Body->GetActorLocation(), Target->GetActorLocation()) > FMath::Square(Reach))
    {
        return Refuse(EMarburgumInteractionReason::OutOfRange);
    }
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
    Receipt.ItemId = Target->GrantedItemId;
    Receipt.Quantity = 1;
    Receipt.StockRemaining = Stock - 1;
    Receipt.WorldRevision = Next.Revision;
    return Finish(Receipt, Requester);
}
