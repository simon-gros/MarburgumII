#include "MarburgumWater.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PhysicsVolume.h"

namespace MarburgumWater
{
    // A body standing this high over the water still counts as walking into it when there is no ground under it:
    // off a quay, or off a bank, is into the river.
    constexpr double DropCm = 5000.0;
    // Ground over the water is looked for from this far above the feet, so a bridge rising ahead is found.
    constexpr double ClimbCm = 250.0;
    // Ground must be at least this far above the surface to stand on dry.
    constexpr double AboveSurfaceCm = 2.0;
    // Walkable ground faces up at least this much (as the walk graph).
    constexpr double WalkableNormalZ = 0.7;
    // The column's own test is made just under its surface.
    constexpr double UnderSurfaceCm = 2.0;
}

void FMarburgumWaterColumns::Reset()
{
    Columns.Reset();
    Bounds = FBox(ForceInit);
}

void FMarburgumWaterColumns::Add(const FBox& Box, APhysicsVolume* Volume)
{
    FColumn Column;
    Column.Box = Box;
    Column.Volume = Volume;
    Columns.Add(Column);
    Bounds += Box;
}

void FMarburgumWaterColumns::Under(const FVector& Feet, double DropCm, TArray<int32, TInlineAllocator<4>>& Out) const
{
    Out.Reset();
    if (!Bounds.IsValid || Feet.X < Bounds.Min.X || Feet.X > Bounds.Max.X || Feet.Y < Bounds.Min.Y || Feet.Y > Bounds.Max.Y)
    {
        return;
    }
    for (int32 Index = 0; Index < Columns.Num(); ++Index)
    {
        const FBox& Box = Columns[Index].Box;
        if (Feet.X >= Box.Min.X && Feet.X <= Box.Max.X && Feet.Y >= Box.Min.Y && Feet.Y <= Box.Max.Y && Feet.Z >= Box.Min.Z
            && Feet.Z <= Box.Max.Z + DropCm)
        {
            Out.Add(Index);
        }
    }
}

bool UMarburgumWaterSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UMarburgumWaterSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    bCollected = false;
    Collect();
}

void UMarburgumWaterSubsystem::Collect() const
{
    if (bCollected)
    {
        return;
    }
    bCollected = true;
    Columns.Reset();
    GroundParams = FCollisionQueryParams(SCENE_QUERY_STAT(MarburgumWater), false);
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }
    for (TActorIterator<APhysicsVolume> It(World); It; ++It)
    {
        if (!It->bWaterVolume)
        {
            continue;
        }
        FVector Origin;
        FVector Extent;
        It->GetActorBounds(false, Origin, Extent);
        Columns.Add(FBox(Origin - Extent, Origin + Extent), *It);
        GroundParams.AddIgnoredActor(*It);
    }
}

int32 UMarburgumWaterSubsystem::NumVolumes() const
{
    Collect();
    return Columns.Get().Num();
}

bool UMarburgumWaterSubsystem::IsWet(const FVector& Feet) const
{
    using namespace MarburgumWater;
    Collect();
    TArray<int32, TInlineAllocator<4>> Under;
    Columns.Under(Feet, DropCm, Under);
    for (const int32 Index : Under)
    {
        const FMarburgumWaterColumns::FColumn& Column = Columns.Get()[Index];
        const double Surface = Column.Box.Max.Z;
        // The bounds are the volume's box; a hand-placed volume need not fill them.
        const APhysicsVolume* Volume = Column.Volume.Get();
        if (Volume && !Volume->EncompassesPoint(FVector(Feet.X, Feet.Y, Surface - UnderSurfaceCm)))
        {
            continue;
        }
        const double Top = Feet.Z + ClimbCm;
        const double Dry = Surface + AboveSurfaceCm;
        if (Top <= Dry)
        {
            return true;
        }
        // Something to stand on over the surface (a bridge, a quay) keeps the feet dry.
        FHitResult Ground;
        if (GetWorld()->LineTraceSingleByObjectType(Ground, FVector(Feet.X, Feet.Y, Top), FVector(Feet.X, Feet.Y, Dry),
                FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllStaticObjects), GroundParams)
            && Ground.ImpactNormal.Z >= WalkableNormalZ)
        {
            continue;
        }
        return true;
    }
    return false;
}

bool UMarburgumWaterSubsystem::IsWetWay(const FVector& FromFeet, const FVector& ToFeet) const
{
    const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector::Dist2D(FromFeet, ToFeet) / WayStepCm));
    for (int32 Step = 1; Step <= Steps; ++Step)
    {
        if (IsWet(FMath::Lerp(FromFeet, ToFeet, static_cast<double>(Step) / Steps)))
        {
            return true;
        }
    }
    return false;
}
