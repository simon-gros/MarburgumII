#include "MarburgumNpc.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MarburgumGameplayContract.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    // Placeholder proportions, in centimetres above the actor origin at the feet.
    constexpr float BodyHeight = 145.f;
    constexpr float HeadDiameter = 26.f;
    const FLinearColor SkinTone(0.58f, 0.36f, 0.24f);
    const FLinearColor RodColor(0.12f, 0.07f, 0.03f);

    UStaticMeshComponent* MakePart(AActor& Owner, const TCHAR* Name, USceneComponent* Parent, UStaticMesh* Shape)
    {
        UStaticMeshComponent* Part = Owner.CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Part->SetupAttachment(Parent);
        Part->SetStaticMesh(Shape);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetMobility(EComponentMobility::Static);
        return Part;
    }
}

AMarburgumNpc::AMarburgumNpc()
{
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

    // A feet-level root lets the interaction range and the prompt anchor use the figure, not a cube.
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Root->SetMobility(EComponentMobility::Static);
    SetRootComponent(Root);

    Mesh->SetupAttachment(Root);
    Mesh->SetStaticMesh(Cylinder.Object);
    Mesh->SetRelativeScale3D(FVector(0.5f, 0.5f, BodyHeight / 100.f));
    Mesh->SetRelativeLocation(FVector(0.f, 0.f, BodyHeight * 0.5f));

    Head = MakePart(*this, TEXT("Head"), Root, Sphere.Object);
    Head->SetRelativeScale3D(FVector(HeadDiameter / 100.f));
    Head->SetRelativeLocation(FVector(0.f, 0.f, BodyHeight + HeadDiameter * 0.5f));

    Cap = MakePart(*this, TEXT("Cap"), Root, Cylinder.Object);
    Cap->SetRelativeScale3D(FVector(0.3f, 0.3f, 0.06f));
    Cap->SetRelativeLocation(FVector(4.f, 0.f, BodyHeight + HeadDiameter - 2.f));

    // A rod held out over the water in front of the figure (+X is the NPC's facing).
    Rod = MakePart(*this, TEXT("Rod"), Root, Cylinder.Object);
    Rod->SetRelativeScale3D(FVector(0.025f, 0.025f, 2.4f));
    Rod->SetRelativeRotation(FRotator(-55.f, 0.f, 0.f));
    Rod->SetRelativeLocation(FVector(110.f, 0.f, 175.f));
    Rod->SetVisibility(false);

    Verb = NSLOCTEXT("MarburgumNpc", "Talk", "Talk");
    bTestFixture = false;
    InteractionRange = 400.f;
}

void AMarburgumNpc::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    Rod->SetVisibility(bHoldsRod);
}

void AMarburgumNpc::BeginPlay()
{
    Super::BeginPlay();
    ApplyLook();
}

void AMarburgumNpc::ConfigureFromCatalogue()
{
    Super::ConfigureFromCatalogue();
    const MarburgumII::Gameplay::FInteractableDefinition* Entry =
        MarburgumII::Gameplay::FindInteractable(GetCatalogueId().ToString());
    if (!Entry)
    {
        return;
    }
    // A person stands in the way like the angler does; only places are walked through.
    Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
    PrimaryColor = FLinearColor(Entry->PrimaryColor[0], Entry->PrimaryColor[1], Entry->PrimaryColor[2]);
    SecondaryColor = FLinearColor(Entry->SecondaryColor[0], Entry->SecondaryColor[1], Entry->SecondaryColor[2]);
    if (HasActorBegunPlay())
    {
        ApplyLook();
    }
}

void AMarburgumNpc::ApplyLook()
{
    Rod->SetVisibility(bHoldsRod);
    if (GetNetMode() == NM_DedicatedServer)
    {
        return; // Nothing renders on a dedicated server.
    }
    Tint(Mesh, PrimaryColor);
    Tint(Head, SkinTone);
    Tint(Cap, SecondaryColor);
    Tint(Rod, RodColor);
}

void AMarburgumNpc::Tint(UStaticMeshComponent* Part, const FLinearColor& Color) const
{
    // The engine's basic-shape material exposes a "Color" parameter.
    if (UMaterialInstanceDynamic* Material = Part ? Part->CreateDynamicMaterialInstance(0) : nullptr)
    {
        Material->SetVectorParameterValue(TEXT("Color"), Color);
    }
}
