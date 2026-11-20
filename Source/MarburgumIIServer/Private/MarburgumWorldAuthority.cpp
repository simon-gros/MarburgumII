#include "MarburgumWorldAuthority.h"

#include "MarburgumPhotography.h"

#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"
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
#include "MarburgumPhotography.h"
#include "MarburgumPlayerController.h"
#include "MarburgumPlaces.h"
#include "MarburgumClockAuthority.h"
#include "MarburgumWorldClock.h"
#include "MarburgumPedestrian.h"
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
                 EMarburgumInteractionEffect::ItemBought, EMarburgumInteractionEffect::ItemSold, EMarburgumInteractionEffect::Photographed })
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
    InWorld.GetTimerManager().SetTimer(PostsTimer, FTimerDelegate::CreateUObject(this, &UMarburgumWorldAuthority::UpdatePosts), 1.f, true);
    InWorld.GetTimerManager().SetTimer(ShopTimer, FTimerDelegate::CreateUObject(this, &UMarburgumWorldAuthority::ApplyShops),
        ShopHoursCheckSeconds, true);
    if (FParse::Param(FCommandLine::Get(), TEXT("MB2NetRate")))
    {
        // The engine only measures its rates with net stats on.
        if (IConsoleVariable* NetStats = IConsoleManager::Get().FindConsoleVariable(TEXT("net.EnableNetStats")))
        {
            NetStats->Set(1, ECVF_SetByCommandline);
        }
        InWorld.GetTimerManager().SetTimer(NetRateTimer, FTimerDelegate::CreateUObject(this, &UMarburgumWorldAuthority::LogNetRate),
            5.0f, true);
    }

    FString Path;
    if (!FParse::Value(FCommandLine::Get(), TEXT("MB2WorldStore="), Path) || Path.IsEmpty())
    {
        const FString MapName = UWorld::RemovePIEPrefix(InWorld.GetMapName());
        Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MarburgumII"), TEXT("WorldStore"), MapName + TEXT(".json"));
    }
    Path = FPaths::ConvertRelativePathToFull(Path);
    Store = MakeShared<FMarburgumWorldStore>(Path);
#if !UE_BUILD_SHIPPING
    // Test-only: a Shipping server never fails its own commits on request.
    if (FParse::Param(FCommandLine::Get(), TEXT("MB2StoreFaultInjection")))
    {
        Store->EnableFaultInjection();
    }
#endif

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

void UMarburgumWorldAuthority::LogNetRate()
{
    const UWorld* World = GetWorld();
    const UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;
    if (!Driver)
    {
        return;
    }
    int32 MaxConnectionOut = 0;
    for (const UNetConnection* Connection : Driver->ClientConnections)
    {
        MaxConnectionOut = Connection ? FMath::Max(MaxConnectionOut, Connection->OutBytesPerSecond) : MaxConnectionOut;
    }
    int32 People = 0;
    for (TActorIterator<AMarburgumPedestrian> It(World); It; ++It)
    {
        ++People;
    }
    UE_LOG(LogMarburgumNet, Log, TEXT("MB2NET role=server event=net_rate out_bytes_s=%u in_bytes_s=%u max_connection_out_bytes_s=%d connections=%d street_people=%d"),
        Driver->OutBytesPerSecond, Driver->InBytesPerSecond, MaxConnectionOut, Driver->ClientConnections.Num(), People);
}

void UMarburgumWorldAuthority::Deinitialize()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(LocationTimer);
        World->GetTimerManager().ClearTimer(FishingTimer);
        World->GetTimerManager().ClearTimer(ShopTimer);
        World->GetTimerManager().ClearTimer(PostsTimer);
        World->GetTimerManager().ClearTimer(NetRateTimer);
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
    LastCastOperation.Remove(Cast<AMarburgumPlayerController>(Exiting));
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

bool UMarburgumWorldAuthority::AdmitOperation(FMarburgumInteractionReceipt& Receipt, AMarburgumPlayerController& Requester,
    AMarburgumPlayerState*& OutState)
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

    OutState = State;
    return true;
}

bool UMarburgumWorldAuthority::Admit(FMarburgumInteractionReceipt& Receipt, AMarburgumPlayerController& Requester,
    AMarburgumInteractable* Target, AMarburgumPlayerState*& OutState)
{
    auto Refuse = [&Receipt](EMarburgumInteractionReason Reason)
    {
        Receipt.Reason = Reason;
        return false;
    };
    AMarburgumPlayerState* State = nullptr;
    if (!AdmitOperation(Receipt, Requester, State))
    {
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
    // A body in a car seat or down on the ground takes no part in the world's exchanges (ADR-032, ADR-039): no
    // talk, choice or trade through a car window or from the ground. Fishing answers with its own reason.
    const AMarburgumCharacter* Rider = Cast<AMarburgumCharacter>(Body);
    if ((State->GetCondition().IsDown() || (Rider && Rider->GetSeatedVehicle()))
        && !FindFishingSpot(Target->InteractableId.ToString()))
    {
        return Refuse(EMarburgumInteractionReason::Incapacitated);
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
    // Somebody close enough to be spoken to answers whatever the request means: everyone nearby sees them turn
    // to the speaker and talk (ADR-081). Presentation only; the decision below is the server's alone.
    if (AMarburgumNpc* Person = Cast<AMarburgumNpc>(Target))
    {
        Person->ConverseAuthority(Requester.GetPawn());
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

FMarburgumInteractionReceipt UMarburgumWorldAuthority::HandlePhotograph(AMarburgumPlayerController& Requester, const FGuid& OperationId)
{
    FMarburgumInteractionReceipt Receipt;
    Receipt.OperationId = OperationId;
    AMarburgumPlayerState* State = nullptr;
    if (!AdmitOperation(Receipt, Requester, State))
    {
        return Finish(Receipt, Requester);
    }
    // The server's own copy of the body and its view decides what is in frame (ADR-081); the client only asks.
    const APawn* Body = Requester.GetPawn();
    const AMarburgumCharacter* Photographer = Cast<AMarburgumCharacter>(Body);
    if (!Body || State->GetCondition().IsDown() || (Photographer && Photographer->GetSeatedVehicle()))
    {
        Receipt.Reason = EMarburgumInteractionReason::Incapacitated;
        return Finish(Receipt, Requester);
    }
    const FString& Character = State->GetCharacterKey();
    const TMap<FString, int32>* Owned = Store->Get().Inventories.Find(Character);
    if (!Owned || Owned->FindRef(MarburgumII::Gameplay::CameraItem) <= 0)
    {
        Receipt.Reason = EMarburgumInteractionReason::NoCamera;
        return Finish(Receipt, Requester);
    }
    const UMarburgumPhotographySubsystem* Photography = GetWorld()->GetSubsystem<UMarburgumPhotographySubsystem>();
    const UMarburgumPhotographySubsystem::FInFrame Frame = Photography
        ? Photography->FindInFrame(Body->GetPawnViewLocation(), Requester.GetControlRotation(), Body)
        : UMarburgumPhotographySubsystem::FInFrame();
    if (!Frame.Subject)
    {
        Receipt.Reason = EMarburgumInteractionReason::NothingInFrame;
        return Finish(Receipt, Requester);
    }
    Receipt.InteractableId = FName(Frame.Subject->Id);

    FMarburgumWorldRecord Next;
    const FMarburgumMissionDecision Decision = MarburgumII::Missions::DecidePhotograph(Store->Get(), Character, Frame.Subject->Id, Next);
    Receipt.MissionId = Decision.Mission.IsEmpty() ? NAME_None : FName(*Decision.Mission);
    Receipt.MissionStage = Decision.Stage.IsEmpty() ? NAME_None : FName(*Decision.Stage);
    if (!Decision.bApplied)
    {
        Receipt.Reason = Decision.Reason;
        Receipt.ItemId = Decision.Item.IsEmpty() ? NAME_None : FName(*Decision.Item);
        return Finish(Receipt, Requester);
    }
    FMarburgumStoredReceipt Stored;
    Stored.Character = Character;
    Stored.Kind = ToString(Decision.Effect);
    Stored.Interactable = Frame.Subject->Id;
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
    ApplyInventory(*State);
    Receipt.Outcome = EMarburgumRequestOutcome::Applied;
    Receipt.Effect = Decision.Effect;
    Receipt.ItemId = Decision.Item.IsEmpty() ? NAME_None : FName(*Decision.Item);
    Receipt.Quantity = Decision.Quantity;
    Receipt.ItemsGranted = ToStacks(&Decision.Granted);
    Receipt.MoneyDelta = Decision.MoneyDelta;
    Receipt.SkillXp = ToSkillAwards(Decision.SkillXp);
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=photograph character=%s subject=%s off_axis=%.1f distance_m=%.1f"), InteractLogPrefix,
        *Character, Frame.Subject->Id, Frame.OffAxisDegrees, Frame.DistanceCm / 100.0);
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
    // A cast changes nothing durable, so no stored receipt answers its replay: the same id resent (a retry, a
    // duplicated packet) must not strike the line it cast, nor cast again once that line is over.
    if (const FGuid* CastOperation = LastCastOperation.Find(&Requester); CastOperation && *CastOperation == Receipt.OperationId)
    {
        Receipt.Outcome = EMarburgumRequestOutcome::Duplicate;
        Receipt.Effect = EMarburgumInteractionEffect::FishingCast;
        Receipt.WorldRevision = Store->Get().Revision;
        return Finish(Receipt, Requester);
    }
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
    LastCastOperation.Add(&Requester, Receipt.OperationId);

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

void UMarburgumWorldAuthority::UpdatePosts()
{
    // Named people keep their day by the Marburgum clock (ADR-081).
    const int32 Minute = MarburgumII::Clock::MarburgumAt(MarburgumII::ServerClock::Now()).MinuteOfDay();
    for (TActorIterator<AMarburgumNpc> It(GetWorld()); It; ++It)
    {
        const MarburgumII::Gameplay::FInteractableDefinition* Entry =
            It->GetCatalogueId().IsNone() ? nullptr : MarburgumII::Gameplay::FindInteractable(It->GetCatalogueId().ToString());
        if (!Entry || Entry->PostCount == 0 || It->GetPostCount() < 2)
        {
            continue;
        }
        const int32 Want = MarburgumII::Gameplay::DayPostAt(Entry->Posts, Entry->PostCount, Minute) + 1;
        if (Want != It->GetCurrentPost())
        {
            It->GoToPostAuthority(Want);
            if (It->GetCurrentPost() == Want)
            {
                UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=npc_post id=%s post=%s minute=%d"), InteractLogPrefix, Entry->Id,
                    Want == 0 ? TEXT("authored_spot") : Entry->Posts[Want - 1].Activity, Minute);
            }
        }
    }
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
    AMarburgumInteractable* Angler = FindInteractable(LentAnglerId);
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
    // The angler fishes from the bank (ADR-081): the level put him on the promenade, a road and a verge away from the
    // Drava. Walk him forward along his facing to the last dry ground before the water, before anything is laid out
    // round him, and move the development starts that look at him by the same step. His place replicates.
    if (Angler)
    {
        const FVector From = Angler->GetActorLocation();
        const FVector Ahead = Angler->GetActorForwardVector().GetSafeNormal2D();
        FHitResult LastDry;
        bool bDry = false;
        bool bWater = false;
        for (double Step = 100.0; Step <= 4500.0 && !bWater; Step += 50.0)
        {
            FHitResult Ground;
            if (!FindGround(From + Ahead * Step, Ground))
            {
                continue;
            }
            if (IsWet(Ground.ImpactPoint))
            {
                bWater = true;
            }
            else
            {
                LastDry = Ground;
                bDry = true;
            }
        }
        const FVector Bank = bDry ? LastDry.ImpactPoint - Ahead * 120.0 : From;
        FHitResult BankGround;
        if (bWater && bDry && FVector::Dist2D(Bank, From) > 300.0 && FindGround(Bank, BankGround) && !IsWet(BankGround.ImpactPoint))
        {
            const FVector Delta = BankGround.ImpactPoint - From;
            Angler->SetReplicateMovement(true);
            Angler->SetActorLocation(BankGround.ImpactPoint);
            for (TActorIterator<APlayerStart> It(&InWorld); It; ++It)
            {
                if (It->PlayerStartTag == TEXT("angler"))
                {
                    It->GetRootComponent()->SetMobility(EComponentMobility::Movable);
                    It->SetActorLocation(It->GetActorLocation() + FVector(Delta.X, Delta.Y, Delta.Z));
                }
            }
            UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=angler_to_bank moved_m=%.1f at=%.0f,%.0f,%.0f"), InteractLogPrefix,
                FVector::Dist2D(From, BankGround.ImpactPoint) / 100.0, BankGround.ImpactPoint.X, BankGround.ImpactPoint.Y, BankGround.ImpactPoint.Z);
        }
        else
        {
            UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=angler_to_bank moved_m=0 reason=%s"), InteractLogPrefix,
                !bWater ? TEXT("no_water_ahead") : TEXT("already_at_bank"));
        }
    }
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
        // Everyone faces the river (the original's -z in Unreal's -X) unless the angler shows otherwise; a person whose
        // original spot puts a wall within 15 m in their face turns to the clearest way instead (ADR-081).
        FRotator Facing = Angler ? Angler->GetActorRotation() : FRotator(0.0, 180.0, 0.0);
        if (bPerson)
        {
            const FVector Chest = Ground.ImpactPoint + FVector(0.0, 0.0, 140.0);
            const auto Clearance = [&InWorld, &Params, &Chest](const FRotator& Way)
            {
                FHitResult Hit;
                return InWorld.LineTraceSingleByChannel(Hit, Chest, Chest + Way.Vector() * 2500.0, ECC_Visibility, Params)
                    ? Hit.Distance : 2500.0;
            };
            if (Clearance(Facing) < 1500.0)
            {
                FRotator Best = Facing;
                double BestClear = Clearance(Facing);
                for (int32 Step = 1; Step < 16; ++Step)
                {
                    const FRotator Way(0.0, Facing.Yaw + Step * 22.5, 0.0);
                    const double Clear = Clearance(Way);
                    if (Clear > BestClear + 1.0)
                    {
                        BestClear = Clear;
                        Best = Way;
                    }
                }
                UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=content_turned id=%s from_yaw=%.0f to_yaw=%.0f clear_m=%.1f"), InteractLogPrefix,
                    Entry.Id, Facing.Yaw, Best.Yaw, BestClear / 100.0);
                Facing = Best;
            }
        }
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

        // A person's day (ADR-081): each post on dry, walkable ground, reached from the authored spot by a straight
        // stroll that crosses no water and no wall; a post that is not is tried mirrored, then dropped and reported.
        if (AMarburgumNpc* Person = Cast<AMarburgumNpc>(Placed); Person && Entry.PostCount > 0)
        {
            TArray<FVector> Points = { At };
            FCollisionQueryParams Sweep(SCENE_QUERY_STAT(MarburgumNpcPost), false, Person);
            for (int32 PostIndex = 0; PostIndex < Entry.PostCount; ++PostIndex)
            {
                const FDayPost& Post = Entry.Posts[PostIndex];
                bool bResolved = false;
                for (const double Mirror : { 1.0, -1.0 })
                {
                    // Original plan: east is Unreal +Y, south is Unreal -X.
                    const FVector PostPlanned = At + FVector(-Post.OffsetSouthM * Mirror, Post.OffsetEastM * Mirror, 0.0) * 100.0;
                    FHitResult PostGround;
                    if (!FindGround(PostPlanned, PostGround) || IsWet(PostGround.ImpactPoint) || PostGround.ImpactNormal.Z < 0.8
                        || FMath::Abs(PostGround.ImpactPoint.Z - At.Z) > 150.0 || (Water && Water->IsWetWay(At, PostGround.ImpactPoint)))
                    {
                        continue;
                    }
                    FHitResult Blocked;
                    if (InWorld.SweepSingleByChannel(Blocked, At + FVector(0.0, 0.0, 100.0), PostGround.ImpactPoint + FVector(0.0, 0.0, 100.0),
                            FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(30.0), Sweep))
                    {
                        continue;
                    }
                    Points.Add(PostGround.ImpactPoint);
                    bResolved = true;
                    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=npc_post_resolved id=%s post=%s at=%.0f,%.0f,%.0f mirrored=%d"),
                        InteractLogPrefix, Entry.Id, Post.Activity, PostGround.ImpactPoint.X, PostGround.ImpactPoint.Y, PostGround.ImpactPoint.Z,
                        Mirror < 0.0 ? 1 : 0);
                    break;
                }
                if (!bResolved)
                {
                    // Keep the indices aligned with the catalogue: an unreachable post is the authored spot.
                    Points.Add(At);
                    UE_LOG(LogMarburgumNet, Warning, TEXT("%s role=server event=npc_post_dropped id=%s post=%s reason=no_clear_dry_way"),
                        InteractLogPrefix, Entry.Id, Post.Activity);
                }
            }
            Person->SetPostsAuthority(Points);
        }

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
    // Photo subjects (ADR-081): a development start where each can be photographed from (?MB2Start=photo_<id>), found
    // round its aim point on dry ground in plain view; a subject with none is reported, never silently skipped.
    if (const UMarburgumPhotographySubsystem* Photography = InWorld.GetSubsystem<UMarburgumPhotographySubsystem>())
    {
        for (const FPhotoSubjectDefinition& Subject : PhotoSubjects)
        {
            FVector Aim;
            if (!Photography->GetAimPoint(Subject.Id, Aim))
            {
                UE_LOG(LogMarburgumNet, Warning, TEXT("%s role=server event=photo_unplaced id=%s reason=no_ground"), InteractLogPrefix, Subject.Id);
                continue;
            }
            const double Reach = FMath::Clamp(Subject.WithinCm * 0.4, 1200.0, 4000.0);
            bool bPlaced = false;
            for (const double Scale : { 1.0, 0.6, 1.4 })
            {
                for (int32 Step = 0; !bPlaced && Step < 24; ++Step)
                {
                    const double Angle = Step * UE_TWO_PI / 24.0;
                    const FVector Planned = Aim + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * Reach * Scale;
                    FHitResult Ground;
                    if (!FindGround(Planned, Ground) || IsWet(Ground.ImpactPoint) || Ground.ImpactNormal.Z < 0.8)
                    {
                        continue;
                    }
                    const FVector Eye = Ground.ImpactPoint + FVector(0.0, 0.0, 160.0);
                    if (FVector::Dist(Eye, Aim) > Subject.WithinCm * 0.95)
                    {
                        continue;
                    }
                    const UMarburgumPhotographySubsystem::FInFrame Frame = Photography->FindInFrame(Eye, (Aim - Eye).Rotation(), nullptr);
                    // By id: in a modular (editor) build each module has its own copy of the catalogue tables.
                    if (!Frame.Subject || FCString::Strcmp(Frame.Subject->Id, Subject.Id) != 0)
                    {
                        continue;
                    }
                    FActorSpawnParameters StartParams;
                    StartParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                    const FVector StartAt = Ground.ImpactPoint + FVector(0.0, 0.0, 96.0);
                    if (APlayerStart* Start = InWorld.SpawnActor<APlayerStart>(StartAt, (Aim - StartAt).GetSafeNormal2D().Rotation(), StartParams))
                    {
                        Start->PlayerStartTag = FName(*FString::Printf(TEXT("photo_%s"), Subject.Id));
                        bPlaced = true;
                        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=photo_start id=%s at=%.0f,%.0f,%.0f aim=%.0f,%.0f,%.0f distance_m=%.1f"),
                            InteractLogPrefix, Subject.Id, StartAt.X, StartAt.Y, StartAt.Z, Aim.X, Aim.Y, Aim.Z, FVector::Dist(Eye, Aim) / 100.0);
                    }
                }
                if (bPlaced)
                {
                    break;
                }
            }
            if (!bPlaced)
            {
                // Say what is in the way from the nearest candidate, so the subject's data can be corrected.
                FString Blocker = TEXT("-");
                FHitResult Ground;
                if (FindGround(Aim + FVector(Reach, 0.0, 0.0), Ground))
                {
                    FHitResult Hit;
                    const FVector Eye = Ground.ImpactPoint + FVector(0.0, 0.0, 160.0);
                    if (InWorld.LineTraceSingleByChannel(Hit, Eye, Aim, ECC_Visibility, FCollisionQueryParams(SCENE_QUERY_STAT(MarburgumPhotoWhy), true)))
                    {
                        Blocker = FString::Printf(TEXT("%s/%s at %.0f m from the aim"), Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("-"),
                            Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("-"), FVector::Dist(Hit.ImpactPoint, Aim) / 100.0);
                    }
                }
                UE_LOG(LogMarburgumNet, Warning, TEXT("%s role=server event=photo_unplaced id=%s reason=no_clear_view aim=%.0f,%.0f,%.0f blocker=\"%s\""),
                    InteractLogPrefix, Subject.Id, Aim.X, Aim.Y, Aim.Z, *Blocker);
            }
        }
    }
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=server event=content_ready spawned=%d"), InteractLogPrefix, Spawned);
}
