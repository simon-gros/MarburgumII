#include "MarburgumWorldAuthority.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumArea.h"
#include "MarburgumCharacter.h"
#include "MarburgumCharacterMovement.h"
#include "MarburgumCombatContract.h"
#include "MarburgumContentPad.h"
#include "MarburgumFishingRules.h"
#include "MarburgumInteractable.h"
#include "MarburgumMissionLogic.h"
#include "MarburgumMissionRules.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumNpc.h"
#include "MarburgumPlayerController.h"
#include "MarburgumPlaces.h"
#include "MarburgumClockAuthority.h"
#include "MarburgumWorldClock.h"
#include "MarburgumPlayerState.h"
#include "MarburgumRetail.h"
#include "MarburgumRetailRules.h"
#include "MarburgumWater.h"
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
                State.bCompleted = (*Owned)[Id].bCompleted;
                State.Choice = (*Owned)[Id].Choice.IsEmpty() ? NAME_None : FName(*(*Owned)[Id].Choice);
                State.Outcome = (*Owned)[Id].Outcome.IsEmpty() ? NAME_None : FName(*(*Owned)[Id].Outcome);
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
        Stored.RecoverySeconds = Condition.IsDown() ? Condition.RecoverySeconds : 0;
        return Stored;
    }

    TArray<FMarburgumSkillProgress> ToSkills(const TMap<FString, int64>* Owned)
    {
        TArray<FMarburgumSkillProgress> Skills;
        if (Owned)
        {
            TArray<FString> Ids;
            Owned->GetKeys(Ids);
            Ids.Sort();
            for (const FString& Id : Ids)
            {
                Skills.Add({ FName(*Id), (*Owned)[Id] });
            }
        }
        return Skills;
    }

    TArray<FMarburgumItemStack> ToStacks(const TMap<FString, int32>& Items)
    {
        return ToStacks(&Items);
    }

    TArray<FMarburgumSkillProgress> ToSkillAwards(const TMap<FString, int64>& Xp)
    {
        return ToSkills(&Xp);
    }

    FString Describe(const TArray<FMarburgumItemStack>& Stacks)
    {
        TArray<FString> Parts;
        for (const FMarburgumItemStack& Stack : Stacks)
        {
            Parts.Add(FString::Printf(TEXT("%s:%d"), *Stack.ItemId.ToString(), Stack.Quantity));
        }
        return Parts.IsEmpty() ? TEXT("-") : FString::Join(Parts, TEXT(","));
    }

    FString Describe(const TArray<FMarburgumSkillProgress>& Skills)
    {
        TArray<FString> Parts;
        for (const FMarburgumSkillProgress& Skill : Skills)
        {
            Parts.Add(FString::Printf(TEXT("%s:%lld"), *Skill.SkillId.ToString(), Skill.Xp));
        }
        return Parts.IsEmpty() ? TEXT("-") : FString::Join(Parts, TEXT(","));
    }

    // Condition evidence lines share the movement component's prefix (scripts/condition-smoke-linux.sh).
    constexpr const TCHAR* ConditionLogPrefix = TEXT("MB2COND");
    // Fishing evidence lines (scripts/mission-smoke-linux.sh).
    constexpr const TCHAR* FishLogPrefix = TEXT("MB2FISH");
    // How often the server advances casts: the original's 10 Hz world tick.
    constexpr float FishingTickSeconds = 0.1f;
    // Counter evidence lines (scripts/shop-smoke-linux.sh).
    constexpr const TCHAR* ShopLogPrefix = TEXT("MB2SHOP");
    // How often the server looks at its clock to open or shut the counters.
    constexpr float ShopHoursCheckSeconds = 10.f;

    /**
     * The world clock's Marburgum date and time (ADR-064): Ljubljana's hour on the mirrored 1990s date, so a counter
     * keeps the weekday the player is shown. Or the -MB2ClockHour / -MB2ClockWeekday development override.
     */
    MarburgumII::Retail::FCivilTime ClockNow()
    {
        const MarburgumII::Clock::FCivil World = MarburgumII::Clock::MarburgumAt(MarburgumII::ServerClock::Now());
        MarburgumII::Retail::FCivilTime Now;
        Now.Year = World.Year;
        Now.Month = World.Month;
        Now.Day = World.Day;
        Now.Weekday = World.Weekday;
        Now.Minute = World.MinuteOfDay();
        // Development and smoke overrides, as the original's MARBURGUM_CLOCK_HOUR / MARBURGUM_CLOCK_WEEKDAY:
        // -MB2ClockHour=<0-24, fractions allowed> and -MB2ClockWeekday=<0 Monday .. 6 Sunday>.
        float Hour = -1.f;
        if (FParse::Value(FCommandLine::Get(), TEXT("MB2ClockHour="), Hour) && FMath::IsFinite(Hour) && Hour >= 0.f)
        {
            Now.Minute = FMath::Clamp(static_cast<int32>(FMath::Fmod(Hour, 24.f) * 60.f), 0, 24 * 60 - 1);
        }
        int32 Weekday = -1;
        if (FParse::Value(FCommandLine::Get(), TEXT("MB2ClockWeekday="), Weekday) && Weekday >= 0 && Weekday <= 6)
        {
            Now.Weekday = Weekday;
        }
        return Now;
    }

    EMarburgumInteractionEffect EffectFromKind(const FString& Kind)
    {
        for (const EMarburgumInteractionEffect Effect : { EMarburgumInteractionEffect::ItemGranted,
                 EMarburgumInteractionEffect::MissionStarted, EMarburgumInteractionEffect::StageCompleted,
                 EMarburgumInteractionEffect::MissionCompleted, EMarburgumInteractionEffect::FishCaught,
                 EMarburgumInteractionEffect::ItemBought, EMarburgumInteractionEffect::ItemSold })
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
    InWorld.GetTimerManager().SetTimer(FishingTimer, FTimerDelegate::CreateUObject(this, &UMarburgumWorldAuthority::TickFishing),
        FishingTickSeconds, true);
    // Bites and catches are the server's dice; nothing a client sends feeds them.
    Rolls.Initialize(static_cast<int32>(FDateTime::UtcNow().GetTicks() & 0x7fffffff));
    SpawnAuthoredContent(InWorld);
    InWorld.GetTimerManager().SetTimer(ShopTimer, FTimerDelegate::CreateUObject(this, &UMarburgumWorldAuthority::ApplyShops),
        ShopHoursCheckSeconds, true);

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
    ApplyShops();
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
        World->GetTimerManager().ClearTimer(FishingTimer);
        World->GetTimerManager().ClearTimer(ShopTimer);
    }
    Casts.Reset();
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
    Casts.Remove(Cast<AMarburgumPlayerController>(Exiting));
    LastCastAt.Remove(Cast<AMarburgumPlayerController>(Exiting));
    PlaceTrackers.Remove(Exiting);
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
        Condition.RecoverySeconds = static_cast<uint8>(Saved->RecoverySeconds);
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
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=condition_saved why=%s character=%s vitality=%d stamina=%d winded=%d revision=%lld recovery_s=%d"),
        ConditionLogPrefix, Why, *Key, Next.Vitality, Next.Stamina, Next.bWinded ? 1 : 0, Record.Revision, Next.RecoverySeconds);
}

void UMarburgumWorldAuthority::ApplyInventory(AMarburgumPlayerState& State) const
{
    // Custody and mission progress always travel together, so a client never sees one without the other.
    const FString& Key = State.GetCharacterKey();
    State.SetInventoryAuthority(ToStacks(Key.IsEmpty() ? nullptr : Store->Get().Inventories.Find(Key)));
    State.SetMissionsAuthority(ToMissions(Key.IsEmpty() ? nullptr : Store->Get().Missions.Find(Key)));
    State.SetProgressAuthority(ToSkills(Key.IsEmpty() ? nullptr : Store->Get().Skills.Find(Key)),
        Key.IsEmpty() ? 0 : Store->Get().Money.FindRef(Key));
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
    // The places file is laid out in the launch map's frame; elsewhere only the areas are named (ADR-063).
    const bool bPlaces = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) == MarburgumII::Places::PlacesMap;
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
        const FVector Feet = Body->GetActorLocation();
        const MarburgumII::Places::FPlaceIds Ids = bPlaces ? PlaceTrackers.FindOrAdd(Player).Resolve(
            MarburgumII::Places::Get(), FVector2D(Feet.X, Feet.Y), !Area.IsNone()) : MarburgumII::Places::FPlaceIds();
        FMarburgumPlace Place;
        Place.District = Ids.District;
        Place.Street = Ids.Street;
        Place.Landmark = Ids.Landmark;
        const bool bAreaChanged = !State->IsLocationKnown() || State->GetAreaId() != Area;
        if (bAreaChanged || State->GetPlace() != Place)
        {
            State->SetAreaAuthority(Area, Place);
            if (bPlaces)
            {
                UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=place player=%d district=%s street=\"%s\" landmark=%s x_m=%.2f y_m=%.2f"),
                    InteractLogPrefix, State->GetPlayerId(), Place.District.IsEmpty() ? TEXT("-") : *Place.District,
                    *Place.Street, Place.Landmark.IsEmpty() ? TEXT("-") : *Place.Landmark, Feet.X / 100.f, Feet.Y / 100.f);
            }
        }
        if (bAreaChanged)
        {
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
        TEXT("%s role=server event=interaction player=%d character=%s op=%s target=%s outcome=%s reason=%s effect=%s mission=%s stage=%s item=%s quantity=%d stock=%d revision=%lld granted=%s choice=%s choice_outcome=%s money=%lld xp=%s"),
        InteractLogPrefix,
        State ? State->GetPlayerId() : -1,
        State && !State->GetCharacterKey().IsEmpty() ? *State->GetCharacterKey() : TEXT("-"),
        *OperationKey(Receipt.OperationId),
        Receipt.InteractableId.IsNone() ? TEXT("-") : *Receipt.InteractableId.ToString(),
        ToString(Receipt.Outcome), ToString(Receipt.Reason), ToString(Receipt.Effect),
        Receipt.MissionId.IsNone() ? TEXT("-") : *Receipt.MissionId.ToString(),
        Receipt.MissionStage.IsNone() ? TEXT("-") : *Receipt.MissionStage.ToString(),
        Receipt.ItemId.IsNone() ? TEXT("-") : *Receipt.ItemId.ToString(),
        Receipt.Quantity, Receipt.StockRemaining, Receipt.WorldRevision, *Describe(Receipt.ItemsGranted),
        Receipt.Choice.IsNone() ? TEXT("-") : *Receipt.Choice.ToString(),
        Receipt.ChoiceOutcome.IsNone() ? TEXT("-") : *Receipt.ChoiceOutcome.ToString(), Receipt.MoneyDelta, *Describe(Receipt.SkillXp));
    return Receipt;
}

bool UMarburgumWorldAuthority::Admit(FMarburgumInteractionReceipt& Receipt, AMarburgumPlayerController& Requester,
    AMarburgumInteractable* Target, AMarburgumPlayerState*& OutState)
{
    Receipt.Outcome = EMarburgumRequestOutcome::Rejected;
    auto Refuse = [&Receipt](EMarburgumInteractionReason Reason)
    {
        Receipt.Reason = Reason;
        return false;
    };
    if (!Receipt.OperationId.IsValid())
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

    // Idempotency survives restarts: committed receipts live in the durable record.
    if (const FMarburgumStoredReceipt* Prior = Store->Get().Receipts.Find(OperationKey(Receipt.OperationId)))
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
        Receipt.ItemsGranted = ToStacks(Prior->Granted);
        Receipt.Choice = Prior->Choice.IsEmpty() ? NAME_None : FName(*Prior->Choice);
        Receipt.ChoiceOutcome = Prior->Outcome.IsEmpty() ? NAME_None : FName(*Prior->Outcome);
        Receipt.MoneyDelta = Prior->MoneyDelta;
        Receipt.SkillXp = ToSkillAwards(Prior->SkillXp);
        Receipt.WorldRevision = Prior->Revision;
        const AMarburgumInteractable* PriorTarget = FindInteractable(Prior->Interactable);
        Receipt.StockRemaining = PriorTarget && PriorTarget->IsItemSource() ? CurrentStock(*PriorTarget) : -1;
        if (const FShopDefinition* PriorShop = Prior->Shop.IsEmpty() ? nullptr : FindShop(Prior->Shop))
        {
            const FShopOffer* Offer = FindOffer(*PriorShop, Prior->Item);
            Receipt.StockRemaining = Offer ? MarburgumII::Retail::ShelfOf(*Offer, Store->Get().Shelves.Find(Prior->Shop)) : -1;
        }
        return false;
    }

    const bool bKnownTarget = Target && Target->GetWorld() == GetWorld() && !Target->InteractableId.IsNone()
        && (Target->IsItemSource() ? FindItem(Target->GrantedItemId.ToString()) != nullptr
                                   : Cast<AMarburgumNpc>(Target) || MarburgumII::Gameplay::FindInteractable(Target->InteractableId.ToString()));
    if (!bKnownTarget)
    {
        return Refuse(EMarburgumInteractionReason::UnknownTarget);
    }
    Receipt.InteractableId = Target->InteractableId;

    const APawn* Body = Requester.GetPawn();
    const float Reach = Target->InteractionRange + RangeToleranceCm;
    if (!Body || FVector::DistSquared(Body->GetActorLocation(), Target->GetActorLocation()) > FMath::Square(Reach))
    {
        return Refuse(EMarburgumInteractionReason::OutOfRange);
    }
    OutState = State;
    return true;
}

bool UMarburgumWorldAuthority::Commit(FMarburgumInteractionReceipt& Receipt, FMarburgumWorldRecord& Next, const FMarburgumStoredReceipt& Stored)
{
    FMarburgumStoredReceipt Entry = Stored;
    Entry.Revision = Next.Revision;
    Entry.CommittedUtc = FDateTime::UtcNow().ToIso8601();
    Next.Receipts.Add(OperationKey(Receipt.OperationId), Entry);
    FString Error;
    if (!Store->Commit(Next, Error))
    {
        UE_LOG(LogMarburgumNet, Warning, TEXT("%s role=server event=commit_failed op=%s error=\"%s\""),
            InteractLogPrefix, *OperationKey(Receipt.OperationId), *Error);
        Receipt.Outcome = EMarburgumRequestOutcome::Rejected;
        Receipt.Reason = EMarburgumInteractionReason::StoreWriteFailed;
        return false;
    }
    Receipt.WorldRevision = Next.Revision;
    return true;
}

FMarburgumInteractionReceipt UMarburgumWorldAuthority::HandleInteraction(
    AMarburgumPlayerController& Requester, const FGuid& OperationId, AMarburgumInteractable* Target)
{
    FMarburgumInteractionReceipt Receipt;
    Receipt.OperationId = OperationId;
    AMarburgumPlayerState* State = nullptr;
    if (!Admit(Receipt, Requester, Target, State))
    {
        return Finish(Receipt, Requester);
    }
    if (FindFishingSpot(Target->InteractableId.ToString()))
    {
        return HandleFishing(Receipt, Requester, *State, *Target);
    }
    if (!Target->IsItemSource())
    {
        return HandleMissionTarget(Receipt, Requester, *State, *Target);
    }

    const FString& Character = State->GetCharacterKey();
    const FString TargetId = Target->InteractableId.ToString();
    const int32 Stock = CurrentStock(*Target);
    Receipt.StockRemaining = Stock;
    if (Stock <= 0)
    {
        Receipt.Reason = EMarburgumInteractionReason::OutOfStock;
        return Finish(Receipt, Requester);
    }

    // One all-or-nothing transition: stock, custody and receipt change together.
    FMarburgumWorldRecord Next = Store->Get();
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
    if (!Commit(Receipt, Next, Stored))
    {
        return Finish(Receipt, Requester);
    }

    // Durable first, visible second.
    Target->SetRemainingStockAuthority(Stock - 1);
    ApplyInventory(*State);

    Receipt.Outcome = EMarburgumRequestOutcome::Applied;
    Receipt.Effect = EMarburgumInteractionEffect::ItemGranted;
    Receipt.ItemId = Target->GrantedItemId;
    Receipt.Quantity = 1;
    Receipt.StockRemaining = Stock - 1;
    return Finish(Receipt, Requester);
}

FMarburgumInteractionReceipt UMarburgumWorldAuthority::HandleMissionTarget(FMarburgumInteractionReceipt Receipt,
    const AMarburgumPlayerController& Requester, AMarburgumPlayerState& State, const AMarburgumInteractable& Target)
{
    const FString& Character = State.GetCharacterKey();
    FMarburgumWorldRecord Next;
    const FMarburgumMissionDecision Decision =
        MarburgumII::Missions::DecideInteraction(Store->Get(), Character, Target.InteractableId.ToString(), Next);
    Receipt.MissionId = Decision.Mission.IsEmpty() ? NAME_None : FName(*Decision.Mission);
    Receipt.MissionStage = Decision.Stage.IsEmpty() ? NAME_None : FName(*Decision.Stage);
    if (!Decision.bApplied)
    {
        Receipt.Reason = Decision.Reason;
        // On RequirementNotMet the item names what is missing; nothing was granted.
        Receipt.ItemId = Decision.Item.IsEmpty() ? NAME_None : FName(*Decision.Item);
        return Finish(Receipt, Requester);
    }

    // Mission progress and every item, coin and flag it moves are one all-or-nothing transition.
    FMarburgumStoredReceipt Stored;
    Stored.Character = Character;
    Stored.Kind = ToString(Decision.Effect);
    Stored.Interactable = Target.InteractableId.ToString();
    Stored.Item = Decision.Item;
    Stored.Quantity = Decision.Quantity;
    Stored.Mission = Decision.Mission;
    Stored.Stage = Decision.Stage;
    Stored.Granted = Decision.Granted;
    Stored.MoneyDelta = Decision.MoneyDelta;
    Stored.SkillXp = Decision.SkillXp;
    if (!Commit(Receipt, Next, Stored))
    {
        return Finish(Receipt, Requester);
    }

    // Durable first, visible second.
    ApplyInventory(State);

    Receipt.Outcome = EMarburgumRequestOutcome::Applied;
    Receipt.Effect = Decision.Effect;
    Receipt.ItemId = Decision.Item.IsEmpty() ? NAME_None : FName(*Decision.Item);
    Receipt.Quantity = Decision.Quantity;
    Receipt.ItemsGranted = ToStacks(Decision.Granted);
    Receipt.MoneyDelta = Decision.MoneyDelta;
    Receipt.SkillXp = ToSkillAwards(Decision.SkillXp);
    return Finish(Receipt, Requester);
}

FMarburgumInteractionReceipt UMarburgumWorldAuthority::HandleMissionChoice(AMarburgumPlayerController& Requester,
    const FGuid& OperationId, AMarburgumInteractable* Target, FName MissionId, FName ChoiceId)
{
    FMarburgumInteractionReceipt Receipt;
    Receipt.OperationId = OperationId;
    Receipt.MissionId = MissionId;
    Receipt.Choice = ChoiceId;
    AMarburgumPlayerState* State = nullptr;
    if (!Admit(Receipt, Requester, Target, State))
    {
        return Finish(Receipt, Requester);
    }
    const FString& Character = State->GetCharacterKey();
    FMarburgumWorldRecord Next;
    const FMarburgumMissionDecision Decision = MarburgumII::Missions::DecideChoice(
        Store->Get(), Character, Target->InteractableId.ToString(), MissionId.ToString(), ChoiceId.ToString(), Next);
    Receipt.MissionStage = Decision.Stage.IsEmpty() ? NAME_None : FName(*Decision.Stage);
    if (!Decision.bApplied)
    {
        Receipt.Reason = Decision.Reason;
        return Finish(Receipt, Requester);
    }

    FMarburgumStoredReceipt Stored;
    Stored.Character = Character;
    Stored.Kind = ToString(Decision.Effect);
    Stored.Interactable = Target->InteractableId.ToString();
    Stored.Mission = Decision.Mission;
    Stored.Stage = Decision.Stage;
    Stored.Choice = Decision.Choice;
    Stored.Outcome = Decision.Outcome;
    Stored.MoneyDelta = Decision.MoneyDelta;
    Stored.SkillXp = Decision.SkillXp;
    if (!Commit(Receipt, Next, Stored))
    {
        return Finish(Receipt, Requester);
    }
    ApplyInventory(*State);

    Receipt.Outcome = EMarburgumRequestOutcome::Applied;
    Receipt.Effect = Decision.Effect;
    Receipt.Choice = FName(*Decision.Choice);
    Receipt.ChoiceOutcome = FName(*Decision.Outcome);
    Receipt.MoneyDelta = Decision.MoneyDelta;
    Receipt.SkillXp = ToSkillAwards(Decision.SkillXp);
    return Finish(Receipt, Requester);
}

// ---- Shop counters (ADR-060) ----

void UMarburgumWorldAuthority::ApplyShops()
{
    using namespace MarburgumII::Retail;
    if (!bStoreAvailable || !Store)
    {
        return;
    }
    const FCivilTime Now = ClockNow();
    for (TActorIterator<AMarburgumInteractable> It(GetWorld()); It; ++It)
    {
        const FShopDefinition* Shop = It->GetShopId().IsNone() ? nullptr : FindShop(It->GetShopId().ToString());
        if (!Shop)
        {
            continue;
        }
        const TMap<FString, int32>* Shelves = Store->Get().Shelves.Find(Shop->Id);
        TArray<int32> Lines;
        for (int32 Index = 0; Index < Shop->OfferCount; ++Index)
        {
            Lines.Add(ShelfOf(Shop->Offers[Index], Shelves));
        }
        const bool bOpen = IsOpen(*Shop, Now.Weekday, Now.Minute);
        if (It->IsShopKnown() && It->IsShopOpen() == bOpen && It->GetShelves() == Lines)
        {
            continue;
        }
        It->SetShopAuthority(Lines, bOpen);
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=counter shop=%s keeper=%s open=%d weekday=%d minute=%d shelves=%s"),
            ShopLogPrefix, Shop->Id, *It->InteractableId.ToString(), bOpen ? 1 : 0, Now.Weekday, Now.Minute,
            *FString::JoinBy(Lines, TEXT(","), [](int32 Copies) { return FString::FromInt(Copies); }));
    }
}

FMarburgumInteractionReceipt UMarburgumWorldAuthority::HandleTrade(AMarburgumPlayerController& Requester, const FGuid& OperationId,
    AMarburgumInteractable* Target, EMarburgumTradeAction Action, FName ItemId, int32 Quantity)
{
    FMarburgumInteractionReceipt Receipt;
    Receipt.OperationId = OperationId;
    Receipt.ItemId = ItemId;
    Receipt.Quantity = Quantity;
    AMarburgumPlayerState* State = nullptr;
    if (!Admit(Receipt, Requester, Target, State))
    {
        return Finish(Receipt, Requester);
    }
    const FShopDefinition* Shop = Target->GetShopId().IsNone() ? nullptr : FindShop(Target->GetShopId().ToString());
    if (!Shop)
    {
        Receipt.Reason = EMarburgumInteractionReason::NoCounter;
        return Finish(Receipt, Requester);
    }

    const FString& Character = State->GetCharacterKey();
    const MarburgumII::Retail::FCivilTime Now = ClockNow();
    FMarburgumWorldRecord Next;
    const FMarburgumTradeDecision Decision =
        MarburgumII::Retail::DecideTrade(Store->Get(), Character, Shop->Id, Action, ItemId.ToString(), Quantity, Now, Next);
    Receipt.StockRemaining = Decision.Shelf;
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=trade character=%s shop=%s action=%s item=%s quantity=%d weekday=%d minute=%d decision=%s"),
        ShopLogPrefix, *Character, Shop->Id, ToString(Action), *ItemId.ToString(), Quantity, Now.Weekday, Now.Minute,
        Decision.bApplied ? ToString(Decision.Effect) : ToString(Decision.Reason));
    if (!Decision.bApplied)
    {
        Receipt.Reason = Decision.Reason;
        return Finish(Receipt, Requester);
    }

    // Shelf, money, goods and any first-purchase XP are one all-or-nothing transition.
    FMarburgumStoredReceipt Stored;
    Stored.Character = Character;
    Stored.Kind = ToString(Decision.Effect);
    Stored.Interactable = Target->InteractableId.ToString();
    Stored.Shop = Shop->Id;
    Stored.Item = Decision.Item;
    Stored.Quantity = Decision.Quantity;
    Stored.MoneyDelta = Decision.MoneyDelta;
    if (Decision.TradingXp > 0)
    {
        Stored.SkillXp.Add(TradingSkill, Decision.TradingXp);
    }
    if (!Commit(Receipt, Next, Stored))
    {
        Receipt.StockRemaining = -1;
        return Finish(Receipt, Requester);
    }

    // Durable first, visible second.
    ApplyInventory(*State);
    ApplyShops();

    Receipt.Outcome = EMarburgumRequestOutcome::Applied;
    Receipt.Effect = Decision.Effect;
    Receipt.MoneyDelta = Decision.MoneyDelta;
    Receipt.SkillXp = ToSkillAwards(Stored.SkillXp);
    return Finish(Receipt, Requester);
}

// ---- Fishing (ADR-048) ----

const TCHAR* UMarburgumWorldAuthority::WhyCannotFish(const AMarburgumPlayerController& Player) const
{
    const AMarburgumCharacter* Body = Cast<AMarburgumCharacter>(Player.GetPawn());
    const AMarburgumPlayerState* State = Player.GetPlayerState<AMarburgumPlayerState>();
    if (!Body || !State)
    {
        return TEXT("no_body");
    }
    if (Body->GetSeatedVehicle())
    {
        return TEXT("seated");
    }
    const FMarburgumCondition& Condition = State->GetCondition();
    if (Condition.IsDown())
    {
        return TEXT("down");
    }
    if (Condition.bSwimming)
    {
        return TEXT("swimming");
    }
    return nullptr;
}

FMarburgumInteractionReceipt UMarburgumWorldAuthority::HandleFishing(FMarburgumInteractionReceipt Receipt,
    AMarburgumPlayerController& Requester, AMarburgumPlayerState& State, const AMarburgumInteractable& Spot)
{
    const FString& Character = State.GetCharacterKey();
    const FString SpotId = Spot.InteractableId.ToString();
    const double Now = GetWorld()->GetTimeSeconds();
    if (FCast* Line = Casts.Find(&Requester))
    {
        if (!Line->bBite)
        {
            // Struck at nothing: the fish, if any, is gone (the original's too_early).
            EndCast(Requester, EMarburgumFishingEnd::TooEarly, NAME_None, TEXT("too_early"));
            Receipt.Reason = EMarburgumInteractionReason::TooEarly;
            return Finish(Receipt, Requester);
        }
        const FString LineSpot = Line->Spot;
        FMarburgumWorldRecord Next;
        const FMarburgumCatchDecision Catch =
            MarburgumII::Fishing::DecideCatch(Store->Get(), Character, LineSpot, static_cast<int64>(Rolls.GetUnsignedInt()), Next);
        if (!Catch.bApplied)
        {
            EndCast(Requester, EMarburgumFishingEnd::Escaped, NAME_None, TEXT("no_species"));
            Receipt.Reason = EMarburgumInteractionReason::NothingToDo;
            return Finish(Receipt, Requester);
        }
        FMarburgumStoredReceipt Stored;
        Stored.Character = Character;
        Stored.Kind = ToString(EMarburgumInteractionEffect::FishCaught);
        Stored.Interactable = LineSpot;
        Stored.Item = Catch.Item;
        Stored.Quantity = 1;
        Stored.SkillXp.Add(FishingSkill, Catch.Xp);
        if (!Commit(Receipt, Next, Stored))
        {
            // The line stays in the water: a strike that could not be saved can be tried again inside the window.
            return Finish(Receipt, Requester);
        }
        ApplyInventory(State);
        EndCast(Requester, EMarburgumFishingEnd::Caught, FName(*Catch.Item), TEXT("caught"));
        Receipt.Outcome = EMarburgumRequestOutcome::Applied;
        Receipt.Effect = EMarburgumInteractionEffect::FishCaught;
        Receipt.ItemId = FName(*Catch.Item);
        Receipt.Quantity = 1;
        Receipt.SkillXp = { { FName(FishingSkill), Catch.Xp } };
        return Finish(Receipt, Requester);
    }

    const TMap<FString, int32>* Owned = Store->Get().Inventories.Find(Character);
    if (!Owned || Owned->FindRef(FishingRodItem) <= 0)
    {
        Receipt.Reason = EMarburgumInteractionReason::NoRod;
        return Finish(Receipt, Requester);
    }
    if (const TCHAR* Why = WhyCannotFish(Requester))
    {
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=cast_refused character=%s spot=%s why=%s"), FishLogPrefix, *Character, *SpotId, Why);
        Receipt.Reason = EMarburgumInteractionReason::CannotFishNow;
        return Finish(Receipt, Requester);
    }
    if (const double* Last = LastCastAt.Find(&Requester); Last && Now - *Last < FishingMinCastIntervalMs / 1000.0)
    {
        Receipt.Reason = EMarburgumInteractionReason::TooSoon;
        return Finish(Receipt, Requester);
    }

    FCast Line;
    Line.Spot = SpotId;
    Line.CastAt = Requester.GetPawn()->GetActorLocation();
    Line.BiteAt = Now + MarburgumII::Fishing::BiteDelaySeconds(Rolls.GetUnsignedInt());
    Casts.Add(&Requester, Line);
    LastCastAt.Add(&Requester, Now);

    FMarburgumFishingStatus Status = State.GetFishing();
    Status.SpotId = Spot.InteractableId;
    Status.Phase = EMarburgumFishingPhase::Waiting;
    ++Status.Attempt;
    State.SetFishingAuthority(Status);
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=cast character=%s spot=%s attempt=%d bite_in_s=%.2f"), FishLogPrefix,
        *Character, *SpotId, Status.Attempt, Line.BiteAt - Now);

    Receipt.Outcome = EMarburgumRequestOutcome::Applied;
    Receipt.Effect = EMarburgumInteractionEffect::FishingCast;
    Receipt.WorldRevision = Store->Get().Revision;
    return Finish(Receipt, Requester);
}

void UMarburgumWorldAuthority::EndCast(AMarburgumPlayerController& Player, EMarburgumFishingEnd End, FName Catch, const TCHAR* Why)
{
    Casts.Remove(&Player);
    AMarburgumPlayerState* State = Player.GetPlayerState<AMarburgumPlayerState>();
    if (!State)
    {
        return;
    }
    FMarburgumFishingStatus Status = State->GetFishing();
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=cast_end character=%s spot=%s attempt=%d end=%s why=%s catch=%s"), FishLogPrefix,
        State->GetCharacterKey().IsEmpty() ? TEXT("-") : *State->GetCharacterKey(), *Status.SpotId.ToString(), Status.Attempt,
        ToString(End), Why, Catch.IsNone() ? TEXT("-") : *Catch.ToString());
    Status.Phase = EMarburgumFishingPhase::None;
    Status.SpotId = NAME_None;
    Status.LastEnd = End;
    Status.LastCatch = Catch;
    State->SetFishingAuthority(Status);
}

void UMarburgumWorldAuthority::TickFishing()
{
    if (Casts.IsEmpty())
    {
        return;
    }
    const double Now = GetWorld()->GetTimeSeconds();
    TArray<TWeakObjectPtr<AMarburgumPlayerController>> Players;
    Casts.GetKeys(Players);
    for (const TWeakObjectPtr<AMarburgumPlayerController>& Weak : Players)
    {
        AMarburgumPlayerController* Player = Weak.Get();
        FCast* Line = Casts.Find(Weak);
        if (!Player || !Line)
        {
            Casts.Remove(Weak);
            continue;
        }
        const APawn* Body = Player->GetPawn();
        if (const TCHAR* Why = WhyCannotFish(*Player))
        {
            EndCast(*Player, EMarburgumFishingEnd::Cancelled, NAME_None, Why);
            continue;
        }
        // Fishing needs both feet on the bank: walking off ends it (the original's movement-intent rule).
        if (FVector::DistSquared2D(Body->GetActorLocation(), Line->CastAt) > FMath::Square(FishingMoveCancelCm))
        {
            EndCast(*Player, EMarburgumFishingEnd::Cancelled, NAME_None, TEXT("moved"));
            continue;
        }
        if (!Line->bBite && Now >= Line->BiteAt)
        {
            Line->bBite = true;
            Line->EscapeAt = Now + MarburgumII::Fishing::BiteWindowSeconds();
            if (AMarburgumPlayerState* State = Player->GetPlayerState<AMarburgumPlayerState>())
            {
                FMarburgumFishingStatus Status = State->GetFishing();
                Status.Phase = EMarburgumFishingPhase::Bite;
                State->SetFishingAuthority(Status);
                UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=bite character=%s spot=%s attempt=%d"), FishLogPrefix,
                    *State->GetCharacterKey(), *Line->Spot, Status.Attempt);
            }
            continue;
        }
        if (Line->bBite && Now >= Line->EscapeAt)
        {
            EndCast(*Player, EMarburgumFishingEnd::Escaped, NAME_None, TEXT("window_passed"));
        }
    }
}

// ---- Authored content (ADR-049) ----

void UMarburgumWorldAuthority::SpawnAuthoredContent(UWorld& InWorld)
{
    using namespace MarburgumII::Gameplay;
    // The original's mission people and places stand on the original Lent level; the blockout and test maps
    // have none of them.
    if (UWorld::RemovePIEPrefix(InWorld.GetOutermost()->GetName()) != TEXT("/Game/World/lent"))
    {
        return;
    }
    const AMarburgumInteractable* Angler = FindInteractable(LentAnglerId);
    // The angler's original position: where the anchored content was laid out around him.
    const double AnglerOriginal[3] = { -127.0, 2.1, -58.0 };
    const FVector AnglerOriginalUe = MarburgumII::Combat::GodotPositionToUnreal(AnglerOriginal);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumAuthoredGround), true);
    // Places whose window is not built yet share a floor, so a mission can be stood on until the
    // ground reaches them (ADR-052). One floor per cluster, with room for the development start.
    TArray<TArray<FVector>> Pads;
    for (const FInteractableDefinition& Entry : MarburgumII::Gameplay::Interactables)
    {
        if (Entry.Placement != EPlacement::Pad)
        {
            continue;
        }
        const FVector At = MarburgumII::Combat::GodotPositionToUnreal(Entry.GodotPosition);
        TArray<FVector>* Cluster = nullptr;
        for (TArray<FVector>& Existing : Pads)
        {
            if (FVector::Dist2D(Existing[0], At) < 8000.0)
            {
                Cluster = &Existing;
                break;
            }
        }
        if (!Cluster)
        {
            Pads.AddDefaulted();
            Cluster = &Pads.Last();
        }
        Cluster->Add(At);
    }
    for (const TArray<FVector>& Cluster : Pads)
    {
        FVector Min = Cluster[0];
        FVector Max = Cluster[0];
        for (const FVector& At : Cluster)
        {
            Min = Min.ComponentMin(At);
            Max = Max.ComponentMax(At);
        }
        Min -= FVector(800.0, 800.0, 0.0);
        Max += FVector(800.0, 800.0, 0.0);
        const FVector Centre = (Min + Max) * 0.5;
        const FVector Extent((Max.X - Min.X) * 0.5, (Max.Y - Min.Y) * 0.5, 20.0);
        const FTransform PadAt(FVector(Centre.X, Centre.Y, Min.Z - Extent.Z));
        AMarburgumContentPad* Pad = InWorld.SpawnActorDeferred<AMarburgumContentPad>(AMarburgumContentPad::StaticClass(), PadAt,
            nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Pad)
        {
            continue;
        }
        Pad->SetExtentAuthority(Extent);
        Pad->FinishSpawning(PadAt);
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=content_pad points=%d at=%.0f,%.0f,%.0f extent=%.0f,%.0f"), InteractLogPrefix,
            Cluster.Num(), Centre.X, Centre.Y, Min.Z, Extent.X, Extent.Y);
    }
    const UMarburgumWaterSubsystem* Water = InWorld.GetSubsystem<UMarburgumWaterSubsystem>();
    const auto IsWet = [Water](const FVector& Feet) { return Water && Water->IsWet(Feet); };
    const auto FindGround = [&InWorld, &Params](const FVector& Planned, FHitResult& Out)
    {
        for (const double Above : { 300.0, 1500.0 })
        {
            if (InWorld.LineTraceSingleByChannel(Out, Planned + FVector(0.0, 0.0, Above), Planned - FVector(0.0, 0.0, 1500.0),
                    ECC_Visibility, Params))
            {
                return true;
            }
        }
        return false;
    };
    int32 Spawned = 0;
    for (const FInteractableDefinition& Entry : MarburgumII::Gameplay::Interactables)
    {
        if (Entry.Placement != EPlacement::Original && Entry.Placement != EPlacement::AnglerRelative && Entry.Placement != EPlacement::Pad)
        {
            continue;
        }
        FVector Planned = MarburgumII::Combat::GodotPositionToUnreal(Entry.GodotPosition);
        if (Entry.Placement == EPlacement::AnglerRelative)
        {
            if (!Angler)
            {
                UE_LOG(LogMarburgumNet, Warning, TEXT("%s role=server event=content_skipped id=%s reason=no_angler"), InteractLogPrefix, Entry.Id);
                continue;
            }
            // The map keeps the original axes, so the original offset from the angler carries over unrotated.
            Planned = Angler->GetActorLocation() + (Planned - AnglerOriginalUe);
        }
        FHitResult Ground;
        if (!FindGround(Planned, Ground))
        {
            UE_LOG(LogMarburgumNet, Warning, TEXT("%s role=server event=content_skipped id=%s reason=no_ground planned=%s"), InteractLogPrefix,
                Entry.Id, *Planned.ToString());
            continue;
        }
        // The new map's river does not follow the original's bank everywhere: a person or place whose original
        // spot is now in the Drava stands on the nearest dry ground instead (ADR-046, ADR-052).
        if (Entry.Kind != EInteractableKind::FishingSpot && IsWet(Ground.ImpactPoint))
        {
            const FVector Wet = Ground.ImpactPoint;
            bool bDry = false;
            for (double Radius = 200.0; !bDry && Radius <= 6000.0; Radius += 200.0)
            {
                for (int32 Step = 0; !bDry && Step < 16; ++Step)
                {
                    const double Angle = Step * UE_TWO_PI / 16.0;
                    FHitResult Candidate;
                    if (FindGround(Planned + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * Radius, Candidate)
                        && !IsWet(Candidate.ImpactPoint))
                    {
                        Ground = Candidate;
                        bDry = true;
                    }
                }
            }
            if (!bDry)
            {
                UE_LOG(LogMarburgumNet, Warning, TEXT("%s role=server event=content_skipped id=%s reason=in_water at=%s"), InteractLogPrefix,
                    Entry.Id, *Wet.ToString());
                continue;
            }
            UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=content_moved id=%s reason=in_water from=%.0f,%.0f,%.0f by=%.0f"),
                InteractLogPrefix, Entry.Id, Wet.X, Wet.Y, Wet.Z, FVector::Dist2D(Wet, Ground.ImpactPoint));
        }
        const bool bPerson = Entry.Kind == EInteractableKind::Npc;
        // People stand on their feet; a place marker sits half sunk so it reads as a post, crate or sign.
        const FVector At = Ground.ImpactPoint + FVector(0.0, 0.0, bPerson ? 0.0 : 25.0);
        // Everyone faces the river (the original's -z in Unreal's -X) unless the angler shows otherwise.
        const FRotator Facing = Angler ? Angler->GetActorRotation() : FRotator(0.0, 180.0, 0.0);
        const FTransform Spot(Facing, At, bPerson ? FVector::OneVector : FVector(0.5));
        UClass* Class = bPerson ? AMarburgumNpc::StaticClass() : AMarburgumInteractable::StaticClass();
        AMarburgumInteractable* Placed = InWorld.SpawnActorDeferred<AMarburgumInteractable>(Class, Spot, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Placed)
        {
            continue;
        }
        Placed->SetCatalogueIdAuthority(FName(Entry.Id));
        Placed->FinishSpawning(Spot);
        ++Spawned;

        // A development start a few steps inland of it, facing it (?MB2Start=mission_<id>), like the fight cast's.
        // Where inland is the river on the new map, the first dry side round it.
        FHitResult StartGround;
        bool bStart = false;
        for (int32 Step = 0; !bStart && Step < 8; ++Step)
        {
            const FVector Ahead = At - Facing.Vector().RotateAngleAxis(Step * 45.0, FVector::UpVector) * 250.0;
            bStart = InWorld.LineTraceSingleByChannel(StartGround, Ahead + FVector(0.0, 0.0, 300.0), Ahead - FVector(0.0, 0.0, 600.0),
                ECC_Visibility, Params) && !IsWet(StartGround.ImpactPoint);
        }
        if (bStart)
        {
            FActorSpawnParameters StartParams;
            StartParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            const FVector StartAt = StartGround.ImpactPoint + FVector(0.0, 0.0, 96.0);
            if (APlayerStart* Start = InWorld.SpawnActor<APlayerStart>(StartAt, (At - StartAt).GetSafeNormal2D().Rotation(), StartParams))
            {
                Start->PlayerStartTag = FName(*FString::Printf(TEXT("mission_%s"), Entry.Id));
            }
        }
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=content_spawned id=%s kind=%s at=%.0f,%.0f,%.0f placement=%s"), InteractLogPrefix,
            Entry.Id, bPerson ? TEXT("npc") : Entry.Kind == EInteractableKind::FishingSpot ? TEXT("fishing_spot") : TEXT("place"),
            At.X, At.Y, At.Z, Entry.Placement == EPlacement::AnglerRelative ? TEXT("angler_relative")
                : Entry.Placement == EPlacement::Pad ? TEXT("pad") : TEXT("original"));
    }
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=content_ready spawned=%d"), InteractLogPrefix, Spawned);
}
