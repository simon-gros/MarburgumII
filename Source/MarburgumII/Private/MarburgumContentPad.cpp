#include "MarburgumContentPad.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AMarburgumContentPad::AMarburgumContentPad()
{
    bReplicates = true;
    bAlwaysRelevant = true; // Far outside the built windows, so distance relevance would drop it first.
    PrimaryActorTick.bCanEverTick = false;
    SetNetUpdateFrequency(1.f);

    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    SetRootComponent(Mesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Cube.Succeeded())
    {
        Mesh->SetStaticMesh(Cube.Object);
    }
    Mesh->SetCollisionProfileName(TEXT("BlockAll"));
    // Movable: the size arrives after spawn on clients.
    Mesh->SetMobility(EComponentMobility::Movable);
}

void AMarburgumContentPad::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(AMarburgumContentPad, Extent, COND_InitialOnly);
}

void AMarburgumContentPad::SetExtentAuthority(const FVector& InExtent)
{
    check(HasAuthority());
    Extent = InExtent;
    ApplyExtent();
}

void AMarburgumContentPad::OnRep_Extent()
{
    ApplyExtent();
}

void AMarburgumContentPad::ApplyExtent()
{
    // The engine cube is 100 cm on a side, centred on its origin.
    Mesh->SetRelativeScale3D(FVector(Extent) / 50.0);
}
