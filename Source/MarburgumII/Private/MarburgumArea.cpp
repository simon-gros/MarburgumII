#include "MarburgumArea.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

AMarburgumArea::AMarburgumArea()
{
    PrimaryActorTick.bCanEverTick = false;
    Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
    Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Box->SetMobility(EComponentMobility::Static);
    Box->SetHiddenInGame(true);
    Box->ShapeColor = FColor(80, 170, 160);
    SetRootComponent(Box);
}

bool AMarburgumArea::Contains(const FVector& Location) const
{
    // Axis-aligned like the original: the box's extent in world space, ignoring rotation.
    const FVector Extent = Box->GetScaledBoxExtent();
    const FVector Delta = (Location - Box->GetComponentLocation()).GetAbs();
    return Delta.X <= Extent.X && Delta.Y <= Extent.Y && Delta.Z <= Extent.Z;
}

FName AMarburgumArea::Resolve(const UWorld* World, const FVector& Location)
{
    if (!World)
    {
        return NAME_None;
    }
    // A handful of areas per map; a spatial index arrives with the city (Stage 3).
    const AMarburgumArea* Best = nullptr;
    for (TActorIterator<AMarburgumArea> It(World); It; ++It)
    {
        if (!It->AreaId.IsNone() && It->Contains(Location) && (!Best || It->Priority < Best->Priority))
        {
            Best = *It;
        }
    }
    return Best ? Best->AreaId : NAME_None;
}

const AMarburgumArea* AMarburgumArea::Find(const UWorld* World, FName Id)
{
    if (!World || Id.IsNone())
    {
        return nullptr;
    }
    for (TActorIterator<AMarburgumArea> It(World); It; ++It)
    {
        if (It->AreaId == Id)
        {
            return *It;
        }
    }
    return nullptr;
}
