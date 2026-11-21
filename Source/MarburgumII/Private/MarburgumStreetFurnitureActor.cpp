#include "MarburgumStreetFurnitureActor.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumStreetFurniture, Log, All);

namespace MarburgumStreetFurniture
{
    /** Ground is looked for from this far above the file's height, down to this far below it (as the lamps do). */
    constexpr double GroundAboveCm = 250.0;
    constexpr double GroundBelowCm = 400.0;
    constexpr double WalkableNormalZ = 0.7;

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

AMarburgumStreetFurniture::AMarburgumStreetFurniture()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = false;
    SetCanBeDamaged(false);
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Root->SetMobility(EComponentMobility::Static);
    SetRootComponent(Root);
}

void AMarburgumStreetFurniture::Build(const TArray<MarburgumII::StreetFurniture::FPiece>& Pieces)
{
    using namespace MarburgumII::StreetFurniture;
    constexpr int32 Count = static_cast<int32>(EPiece::Count);
    TArray<FTransform> ByPiece[Count];
    for (const FPiece& Piece : Pieces)
    {
        if (Piece.bStanding)
        {
            ByPiece[static_cast<int32>(Piece.Piece)].Add(FTransform(FRotator(0.0f, Piece.Yaw, 0.0f), Piece.Location));
        }
    }
    for (int32 Index = 0; Index < Count; ++Index)
    {
        if (ByPiece[Index].IsEmpty())
        {
            continue;
        }
        const EPiece Piece = static_cast<EPiece>(Index);
        UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath(Piece));
        if (!Mesh)
        {
            ++MissingMeshes;
            UE_LOG(LogMarburgumStreetFurniture, Warning, TEXT("MB2FURNITURE event=missing_mesh path=%s"), *MeshPath(Piece));
            continue;
        }
        UHierarchicalInstancedStaticMeshComponent* Component = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, *FString::Printf(TEXT("Furniture_%d"), Index));
        Component->SetupAttachment(Root);
        Component->SetMobility(EComponentMobility::Static);
        Component->SetStaticMesh(Mesh);
        // The mesh's own simple collision: people walk round a bench and into a shelter, not through its walls.
        Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
        Component->SetCanEverAffectNavigation(false);
        const float Cull = CullDistanceCm(Piece);
        Component->SetCullDistances(static_cast<int32>(Cull * 0.85f), static_cast<int32>(Cull));
        Component->RegisterComponent();
        Component->AddInstances(ByPiece[Index], false, true);
        Kinds.Add(Component);
    }
}

bool UMarburgumStreetFurnitureSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    return Super::ShouldCreateSubsystem(Outer) && !FParse::Param(FCommandLine::Get(), TEXT("MB2NoStreetFurniture"));
}

bool UMarburgumStreetFurnitureSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UMarburgumStreetFurnitureSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    using namespace MarburgumII::StreetFurniture;
    // Only Lent's map has its furniture, as only it has its streets.
    if (!InWorld.GetMapName().EndsWith(TEXT("lent")))
    {
        return;
    }
    const TCHAR* Role = MarburgumStreetFurniture::Role(InWorld);
    const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), FurnitureFile);
    FString Text;
    TArray<FPiece> All;
    FString Error;
    if (!FFileHelper::LoadFileToString(Text, *Path) || !Parse(Text, All, Error))
    {
        UE_LOG(LogMarburgumStreetFurniture, Warning, TEXT("MB2FURNITURE role=%s event=none reason=\"%s\""), Role, Error.IsEmpty() ? *FString::Printf(TEXT("cannot read %s"), *Path) : *Error);
        return;
    }
    Read = All.Num();

    // Each piece stands on what is there: the paving or the landscape nearest the walkable top in the file.
    const double Started = FPlatformTime::Seconds();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumStreetFurniture), true);
    const FCollisionObjectQueryParams Static(FCollisionObjectQueryParams::AllStaticObjects);
    int32 NoGround = 0;
    for (int32 Index = 0; Index < All.Num(); ++Index)
    {
        FPiece& Piece = All[Index];
        TArray<FHitResult> Hits;
        InWorld.LineTraceMultiByObjectType(Hits, Piece.Location + FVector(0.0, 0.0, MarburgumStreetFurniture::GroundAboveCm),
            Piece.Location - FVector(0.0, 0.0, MarburgumStreetFurniture::GroundBelowCm), Static, Params);
        const FHitResult* Ground = nullptr;
        for (const FHitResult& Hit : Hits)
        {
            const bool bWalkable = Hit.bBlockingHit && Hit.ImpactNormal.Z >= MarburgumStreetFurniture::WalkableNormalZ;
            if (bWalkable && (!Ground || FMath::Abs(Hit.ImpactPoint.Z - Piece.Location.Z) < FMath::Abs(Ground->ImpactPoint.Z - Piece.Location.Z)))
            {
                Ground = &Hit;
            }
        }
        if (!Ground)
        {
            FHitResult Single;
            if (InWorld.LineTraceSingleByObjectType(Single, Piece.Location + FVector(0.0, 0.0, MarburgumStreetFurniture::GroundAboveCm),
                    Piece.Location - FVector(0.0, 0.0, MarburgumStreetFurniture::GroundBelowCm), Static, Params))
            {
                // A wall along a bank steeper than walkable still stands on it.
                Piece.Location.Z = Single.ImpactPoint.Z;
                ++Grounded;
                continue;
            }
            if (NoGround < 8)
            {
                UE_LOG(LogMarburgumStreetFurniture, Log, TEXT("MB2FURNITURE role=%s event=no_ground piece=%d at=%s kind=%d"), Role, Index,
                    *Piece.Location.ToCompactString(), static_cast<int32>(Piece.Piece));
            }
            Piece.bStanding = false;
            ++NoGround;
            continue;
        }
        Piece.Location.Z = Ground->ImpactPoint.Z;
        ++Grounded;
    }

    FActorSpawnParameters Spawn;
    Spawn.Name = TEXT("MarburgumStreetFurniture");
    Spawn.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
    Spawn.ObjectFlags |= RF_Transient;
    Furniture = InWorld.SpawnActor<AMarburgumStreetFurniture>(Spawn);
    if (!Furniture)
    {
        UE_LOG(LogMarburgumStreetFurniture, Warning, TEXT("MB2FURNITURE role=%s event=none reason=spawn"), Role);
        return;
    }
    Furniture->Build(All);
    UE_LOG(LogMarburgumStreetFurniture, Log, TEXT("MB2FURNITURE role=%s event=placed pieces=%d grounded=%d no_ground=%d kinds=%d missing_meshes=%d seconds=%.2f"),
        Role, Read, Grounded, NoGround, Furniture->NumKinds(), Furniture->NumMissingMeshes(), FPlatformTime::Seconds() - Started);
}

void UMarburgumStreetFurnitureSubsystem::Deinitialize()
{
    if (Furniture)
    {
        Furniture->Destroy();
        Furniture = nullptr;
    }
    Super::Deinitialize();
}
