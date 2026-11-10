#include "MarburgumPlayerState.h"

#include "Net/UnrealNetwork.h"

void AMarburgumPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    // Custody is private: other players' clients never receive these properties.
    DOREPLIFETIME_CONDITION(AMarburgumPlayerState, bHasCharacter, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AMarburgumPlayerState, bInventoryKnown, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AMarburgumPlayerState, Inventory, COND_OwnerOnly);
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
