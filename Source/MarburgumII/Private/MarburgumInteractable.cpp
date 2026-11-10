#include "MarburgumInteractable.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AMarburgumInteractable::AMarburgumInteractable()
{
    bReplicates = true;
    bAlwaysRelevant = true; // Test maps are tiny; AOI-based relevance arrives with the city.
    PrimaryActorTick.bCanEverTick = false;
    SetNetUpdateFrequency(10.f);

    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    SetRootComponent(Mesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Cube.Succeeded())
    {
        Mesh->SetStaticMesh(Cube.Object);
    }
    Mesh->SetCollisionProfileName(TEXT("BlockAll"));
    Mesh->SetMobility(EComponentMobility::Static);
}

void AMarburgumInteractable::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMarburgumInteractable, RemainingStock);
}

void AMarburgumInteractable::BeginPlay()
{
    Super::BeginPlay();
    if (!HasAuthority() && RemainingStock >= 0)
    {
        OnChanged().Broadcast(this);
    }
}

void AMarburgumInteractable::SetRemainingStockAuthority(int32 NewStock)
{
    check(HasAuthority());
    RemainingStock = FMath::Max(NewStock, 0);
    ForceNetUpdate();
    OnChanged().Broadcast(this);
}

void AMarburgumInteractable::OnRep_RemainingStock()
{
    OnChanged().Broadcast(this);
}

FMarburgumInteractableChanged& AMarburgumInteractable::OnChanged()
{
    static FMarburgumInteractableChanged Event;
    return Event;
}
