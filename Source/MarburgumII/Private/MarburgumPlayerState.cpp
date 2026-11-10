#include "MarburgumPlayerState.h"

#include "Net/UnrealNetwork.h"

void AMarburgumPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    // Custody is private: other players' clients never receive these properties.
    DOREPLIFETIME_CONDITION(AMarburgumPlayerState, bHasCharacter, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AMarburgumPlayerState, bInventoryKnown, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AMarburgumPlayerState, Inventory, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AMarburgumPlayerState, bMissionsKnown, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AMarburgumPlayerState, Missions, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AMarburgumPlayerState, bLocationKnown, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AMarburgumPlayerState, AreaId, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AMarburgumPlayerState, bConditionKnown, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AMarburgumPlayerState, Condition, COND_OwnerOnly);
}

void AMarburgumPlayerState::SetCharacterKeyAuthority(const FString& Key)
{
    check(HasAuthority());
    CharacterKey = Key;
    bHasCharacter = !Key.IsEmpty();
    ForceNetUpdate();
}

int32 AMarburgumPlayerState::CountItem(FName ItemId) const
{
    int32 Count = 0;
    for (const FMarburgumItemStack& Stack : Inventory)
    {
        Count += Stack.ItemId == ItemId ? Stack.Quantity : 0;
    }
    return Count;
}

void AMarburgumPlayerState::SetInventoryAuthority(const TArray<FMarburgumItemStack>& Items)
{
    check(HasAuthority());
    Inventory = Items;
    bInventoryKnown = true;
    ForceNetUpdate();
    OnInventoryChanged().Broadcast(this);
}

void AMarburgumPlayerState::OnRep_Inventory()
{
    OnInventoryChanged().Broadcast(this);
}

FMarburgumInventoryChanged& AMarburgumPlayerState::OnInventoryChanged()
{
    static FMarburgumInventoryChanged Event;
    return Event;
}

const FMarburgumMissionState* AMarburgumPlayerState::FindMission(FName MissionId) const
{
    return Missions.FindByPredicate([MissionId](const FMarburgumMissionState& State) { return State.MissionId == MissionId; });
}

void AMarburgumPlayerState::SetMissionsAuthority(const TArray<FMarburgumMissionState>& InMissions)
{
    check(HasAuthority());
    Missions = InMissions;
    bMissionsKnown = true;
    ForceNetUpdate();
    OnMissionsChanged().Broadcast(this);
}

void AMarburgumPlayerState::OnRep_Missions()
{
    OnMissionsChanged().Broadcast(this);
}

FMarburgumPlayerStateChanged& AMarburgumPlayerState::OnMissionsChanged()
{
    static FMarburgumPlayerStateChanged Event;
    return Event;
}

void AMarburgumPlayerState::SetAreaAuthority(FName InAreaId)
{
    check(HasAuthority());
    if (bLocationKnown && AreaId == InAreaId)
    {
        return;
    }
    AreaId = InAreaId;
    bLocationKnown = true;
    ForceNetUpdate();
    OnAreaChanged().Broadcast(this);
}

void AMarburgumPlayerState::OnRep_Area()
{
    OnAreaChanged().Broadcast(this);
}

FMarburgumPlayerStateChanged& AMarburgumPlayerState::OnAreaChanged()
{
    static FMarburgumPlayerStateChanged Event;
    return Event;
}

void AMarburgumPlayerState::RestoreConditionAuthority(const FMarburgumCondition& Restored)
{
    check(HasAuthority());
    Condition = Restored;
    bConditionKnown = true;
    ++ConditionRestoreCount;
    ForceNetUpdate();
    OnConditionChanged().Broadcast(this);
}

void AMarburgumPlayerState::UpdateConditionAuthority(const FMarburgumCondition& Simulated)
{
    check(HasAuthority());
    if (!bConditionKnown || Condition == Simulated)
    {
        return;
    }
    // A whole point of stamina changes ten times a second while running; that rides the normal update
    // rate. The states the player acts on (running, winded, in the water, under it, down) go out at once.
    const bool bStateChanged = Condition.bWinded != Simulated.bWinded || Condition.bRunning != Simulated.bRunning
        || Condition.bSwimming != Simulated.bSwimming || Condition.bSubmerged != Simulated.bSubmerged
        || Condition.IsDown() != Simulated.IsDown();
    Condition = Simulated;
    if (bStateChanged)
    {
        ForceNetUpdate();
    }
    OnConditionChanged().Broadcast(this);
}

void AMarburgumPlayerState::OnRep_Condition()
{
    OnConditionChanged().Broadcast(this);
}

FMarburgumPlayerStateChanged& AMarburgumPlayerState::OnConditionChanged()
{
    static FMarburgumPlayerStateChanged Event;
    return Event;
}
