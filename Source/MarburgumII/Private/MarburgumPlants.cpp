#include "MarburgumPlants.h"

#include "Algo/Transform.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "MarburgumWorldLoading.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumPlants, Log, All);

namespace MarburgumPlants
{
    /** Ground is looked for from this far above the file's height, down to this far below it (as the lamps do). */
    constexpr double GroundAboveCm = 250.0;
    constexpr double GroundBelowCm = 400.0;
    constexpr double WalkableNormalZ = 0.7;
    /** The engine cylinder the trunks block with; it is drawn nowhere. */
    const TCHAR* TrunkMesh = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

    const TCHAR* Role(const UWorld& World)
    {
        switch (World.GetNetMode())
        {
        case NM_DedicatedServer: return TEXT("server");
        case NM_ListenServer: return TEXT("listen");
        case NM_Client: return TEXT("client");
        default: return TEXT("standalone");
        }
    }
}

AMarburgumPlants::AMarburgumPlants()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = false;
    SetCanBeDamaged(false);
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Root->SetMobility(EComponentMobility::Static);
    SetRootComponent(Root);
    TrunkBlockers = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("TrunkBlockers"));
    TrunkBlockers->SetupAttachment(Root);
    TrunkBlockers->SetMobility(EComponentMobility::Static);
    TrunkBlockers->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
    TrunkBlockers->SetCanEverAffectNavigation(false);
    TrunkBlockers->SetHiddenInGame(true);
    TrunkBlockers->SetVisibility(false);
    TrunkBlockers->SetCastShadow(false);
    TrunkBlockers->bAffectDistanceFieldLighting = false;
    TrunkBlockers->bVisibleInRayTracing = false;
}

void AMarburgumPlants::Build(const TArray<MarburgumII::Vegetation::FSpecies>& Species, const TArray<MarburgumII::Vegetation::FPlant>& Plants, bool bDrawn)
{
    using namespace MarburgumII::Vegetation;
    TArray<TArray<FTransform>> ByKind;
    ByKind.SetNum(Species.Num());
    TArray<FTransform> TrunkXforms;
    for (const FPlant& Plant : Plants)
    {
        if (!Plant.bStanding)
        {
            continue;
        }
        const FSpecies& Kind = Species[Plant.Species];
        ByKind[Plant.Species].Add(FTransform(FRotator(0.0f, Plant.Yaw, 0.0f), Plant.Location, FVector(Plant.Spread, Plant.Spread, Plant.Grow)));
        if (Kind.bTree)
        {
            TrunkXforms.Add(TrunkTransform(Kind, Plant));
        }
    }

    TrunkBlockers->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, MarburgumPlants::TrunkMesh));
    TrunkBlockers->AddInstances(TrunkXforms, false, true);
    Trunks = TrunkXforms.Num();
    if (!bDrawn)
    {
        return;
    }

    for (int32 Index = 0; Index < Species.Num(); ++Index)
    {
        if (ByKind[Index].IsEmpty())
        {
            continue;
        }
        const FSpecies& Kind = Species[Index];
        UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath(Kind.Id));
        if (!Mesh)
        {
            ++MissingMeshes;
            UE_LOG(LogMarburgumPlants, Warning, TEXT("MB2PLANTS event=missing_mesh species=%s path=%s"), *Kind.Id, *MeshPath(Kind.Id));
            continue;
        }
        UHierarchicalInstancedStaticMeshComponent* Component = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, *FString::Printf(TEXT("Plants_%s"), *Kind.Id));
        Component->SetupAttachment(Root);
        Component->SetMobility(EComponentMobility::Static);
        Component->SetStaticMesh(Mesh);
        // Leaves and bushes never block; a tree's trunk blocks through TrunkBlockers.
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetCanEverAffectNavigation(false);
        const bool bShadow = Kind.NaturalHeightCm >= ShadowHeightCm;
        Component->SetCastShadow(bShadow);
        Component->bCastDynamicShadow = bShadow;
        // Bushes are too small and too many to pay for distance fields of their own.
        Component->bAffectDistanceFieldLighting = Kind.bTree;
        const float Cull = CullDistanceCm(Kind);
        Component->SetCullDistances(static_cast<int32>(Cull * (1.0f - FadeShare)), static_cast<int32>(Cull));
        Component->RegisterComponent();
        Component->AddInstances(ByKind[Index], false, true);
        Kinds.Add(Component);
        ++DrawnSpecies;
    }
}

bool UMarburgumPlantsSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    return Super::ShouldCreateSubsystem(Outer) && !FParse::Param(FCommandLine::Get(), TEXT("MB2NoPlants"));
}

TOptional<double> UMarburgumPlantsSubsystem::FindGround(const UWorld& World, const FVector& Surveyed)
{
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumPlants), true);
    const FCollisionObjectQueryParams Static(FCollisionObjectQueryParams::AllStaticObjects);
    const FVector From = Surveyed + FVector(0.0, 0.0, MarburgumPlants::GroundAboveCm);
    const FVector To = Surveyed - FVector(0.0, 0.0, MarburgumPlants::GroundBelowCm);
    TArray<FHitResult> Hits;
    World.LineTraceMultiByObjectType(Hits, From, To, Static, Params);
    const FHitResult* Ground = nullptr;
    for (const FHitResult& Hit : Hits)
    {
        const bool bWalkable = Hit.bBlockingHit && Hit.ImpactNormal.Z >= MarburgumPlants::WalkableNormalZ;
        if (bWalkable && (!Ground || FMath::Abs(Hit.ImpactPoint.Z - Surveyed.Z) < FMath::Abs(Ground->ImpactPoint.Z - Surveyed.Z)))
        {
            Ground = &Hit;
        }
    }
    if (Ground)
    {
        return Ground->ImpactPoint.Z;
    }
    // A grassy bank steeper than walkable is still ground for a plant.
    FHitResult Single;
    if (World.LineTraceSingleByObjectType(Single, From, To, Static, Params))
    {
        return Single.ImpactPoint.Z;
    }
    return TOptional<double>();
}

bool UMarburgumPlantsSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UMarburgumPlantsSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    using namespace MarburgumII::Vegetation;
    // Only Lent's map has its plants, as only it has its surveyed ground.
    if (!InWorld.GetMapName().EndsWith(TEXT("lent")))
    {
        return;
    }
    const TCHAR* Role = MarburgumPlants::Role(InWorld);
    const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), PlantsFile);
    FString Text;
    TArray<FSpecies> Species;
    TArray<FPlant> All;
    FString Error;
    if (!FFileHelper::LoadFileToString(Text, *Path) || !Parse(Text, Species, All, Error))
    {
        UE_LOG(LogMarburgumPlants, Warning, TEXT("MB2PLANTS role=%s event=none reason=\"%s\""), Role, Error.IsEmpty() ? *FString::Printf(TEXT("cannot read %s"), *Path) : *Error);
        return;
    }
    Read = All.Num();

    // Each plant stands on the ground under it: the walkable surface nearest the landscape height in the file. A client
    // on the World Partition map has only the cells round its player (ADR-091), so every role takes that from the
    // editor's ground file where there is one.
    const double Started = FPlatformTime::Seconds();
    TArray<FVector> Surveyed;
    Algo::Transform(All, Surveyed, [](const FPlant& Plant) { return Plant.Location; });
    bool bFromFile = false;
    const TArray<TOptional<double>> GroundCm = MarburgumII::WorldLoading::GroundOf(InWorld, PlantsFile, Text, Surveyed,
        &UMarburgumPlantsSubsystem::FindGround, TEXT("plants"), bFromFile);
    int32 NoGround = 0;
    for (int32 Index = 0; Index < All.Num(); ++Index)
    {
        FPlant& Plant = All[Index];
        if (!GroundCm[Index].IsSet())
        {
            if (NoGround < 8)
            {
                UE_LOG(LogMarburgumPlants, Log, TEXT("MB2PLANTS role=%s event=no_ground plant=%d at=%s species=%s"), Role, Index,
                    *Plant.Location.ToCompactString(), *Species[Plant.Species].Id);
            }
            Plant.bStanding = false;
            ++NoGround;
            continue;
        }
        Plant.Location.Z = *GroundCm[Index];
        ++Grounded;
    }

    FActorSpawnParameters Spawn;
    Spawn.Name = TEXT("MarburgumPlants");
    Spawn.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
    Spawn.ObjectFlags |= RF_Transient;
    Plants = InWorld.SpawnActor<AMarburgumPlants>(Spawn);
    if (!Plants)
    {
        UE_LOG(LogMarburgumPlants, Warning, TEXT("MB2PLANTS role=%s event=none reason=spawn"), Role);
        return;
    }
    Plants->Build(Species, All, InWorld.GetNetMode() != NM_DedicatedServer);
    UE_LOG(LogMarburgumPlants, Log, TEXT("MB2PLANTS role=%s event=placed plants=%d grounded=%d no_ground=%d trunks=%d species=%d missing_meshes=%d ground=%s seconds=%.2f"),
        Role, Read, Grounded, NoGround, Plants->NumTrunks(), Plants->NumDrawnSpecies(), Plants->NumMissingMeshes(),
        bFromFile ? TEXT("file") : TEXT("trace"), FPlatformTime::Seconds() - Started);
}

void UMarburgumPlantsSubsystem::Deinitialize()
{
    if (Plants)
    {
        Plants->Destroy();
        Plants = nullptr;
    }
    Super::Deinitialize();
}
