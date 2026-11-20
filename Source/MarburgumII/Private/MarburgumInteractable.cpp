#include "MarburgumInteractable.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Internationalization/Internationalization.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumStoryProps.h"
#include "Materials/MaterialInterface.h"
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

    // Story furniture (ADR-084) is full size whatever the marker's scale.
    PropRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PropRoot"));
    PropRoot->SetupAttachment(Mesh);
    PropRoot->SetUsingAbsoluteScale(true);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Plain(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    PropCylinder = Cylinder.Object;
    PropMaterial = Plain.Object;
}

void AMarburgumInteractable::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMarburgumInteractable, RemainingStock);
    DOREPLIFETIME_CONDITION(AMarburgumInteractable, CatalogueId, COND_InitialOnly);
    // Shelves are public: everyone at the kiosk sees the same counter (ADR-060).
    DOREPLIFETIME(AMarburgumInteractable, Shelves);
    DOREPLIFETIME(AMarburgumInteractable, bShopOpen);
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
    DisplayName = FText::FromString(MarburgumII::Gameplay::Localized(Entry->DisplayName));
    Verb = FText::FromString(MarburgumII::Gameplay::Localized(Entry->Verb));
    Dialogue = FText::FromString(MarburgumII::Gameplay::Localized(Entry->Dialogue));
    InteractionRange = Entry->RangeCm;
    bTestFixture = false;
    const MarburgumII::Gameplay::FShopDefinition* Shop = MarburgumII::Gameplay::FindShopKeptBy(CatalogueId.ToString());
    ShopId = Shop ? FName(Shop->Id) : NAME_None;
    // An authored place is a marker the body walks through, not an obstacle on the promenade.
    Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
    if (Entry->Prop != MarburgumII::Gameplay::EPropKind::None)
    {
        // The furniture is the place: the marker cube neither shows nor gets in the camera's way.
        Mesh->SetVisibility(false);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    EnsureProp();
}

void AMarburgumInteractable::EnsureProp()
{
    const MarburgumII::Gameplay::FInteractableDefinition* Entry =
        CatalogueId.IsNone() ? nullptr : MarburgumII::Gameplay::FindInteractable(CatalogueId.ToString());
    if (bPropBuilt || !Entry || Entry->Prop == MarburgumII::Gameplay::EPropKind::None || !HasActorBegunPlay() || !PropRoot)
    {
        return;
    }
    bPropBuilt = true;
    MarburgumStoryProps::FPrimitives Primitives;
    Primitives.Cube = Mesh->GetStaticMesh();
    Primitives.Cylinder = PropCylinder;
    Primitives.Plain = PropMaterial;
    MarburgumStoryProps::Build(*this, *PropRoot, Entry->Prop, Primitives);
}

void AMarburgumInteractable::BeginPlay()
{
    Super::BeginPlay();
    EnsureProp();
    // The interface language can change in Settings while playing: names, verbs and lines follow it (ADR-081).
    CultureHandle = FInternationalization::Get().OnCultureChanged().AddWeakLambda(this, [this]()
    {
        if (!CatalogueId.IsNone())
        {
            ConfigureFromCatalogue();
            OnChanged().Broadcast(this);
        }
    });
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

void AMarburgumInteractable::SetShopAuthority(const TArray<int32>& NewShelves, bool bNewOpen)
{
    check(HasAuthority());
    if (Shelves == NewShelves && bShopOpen == bNewOpen)
    {
        return;
    }
    Shelves = NewShelves;
    bShopOpen = bNewOpen;
    ForceNetUpdate();
    OnChanged().Broadcast(this);
}

void AMarburgumInteractable::OnRep_Shop()
{
    OnChanged().Broadcast(this);
}

void AMarburgumInteractable::OnRep_RemainingStock()
{
    OnChanged().Broadcast(this);
}

void AMarburgumInteractable::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    FInternationalization::Get().OnCultureChanged().Remove(CultureHandle);
    Super::EndPlay(EndPlayReason);
}

FMarburgumInteractableChanged& AMarburgumInteractable::OnChanged()
{
    static FMarburgumInteractableChanged Event;
    return Event;
}
