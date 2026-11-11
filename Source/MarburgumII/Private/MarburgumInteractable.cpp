#include "MarburgumInteractable.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "MarburgumGameplayContract.h"
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
    DOREPLIFETIME_CONDITION(AMarburgumInteractable, CatalogueId, COND_InitialOnly);
}

void AMarburgumInteractable::SetCatalogueIdAuthority(FName Id)
{
    check(HasAuthority());
    CatalogueId = Id;
    ConfigureFromCatalogue();
}

void AMarburgumInteractable::OnRep_CatalogueId()
{
    ConfigureFromCatalogue();
    OnChanged().Broadcast(this);
}

void AMarburgumInteractable::ConfigureFromCatalogue()
{
    const MarburgumII::Gameplay::FInteractableDefinition* Entry =
        CatalogueId.IsNone() ? nullptr : MarburgumII::Gameplay::FindInteractable(CatalogueId.ToString());
    if (!Entry)
    {
        return;
    }
    InteractableId = CatalogueId;
    DisplayName = FText::FromString(Entry->DisplayName);
    Verb = FText::FromString(Entry->Verb);
    Dialogue = FText::FromString(Entry->Dialogue);
    InteractionRange = Entry->RangeCm;
    bTestFixture = false;
    // An authored place is a marker the body walks through, not an obstacle on the promenade.
    Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
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
