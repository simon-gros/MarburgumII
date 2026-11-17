#include "MarburgumStreetLamps.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumStreetLamps, Log, All);

namespace MarburgumStreetLamps
{
    namespace Lights = MarburgumII::StreetLights;

    /** Ground is looked for from this far above the surveyed top, down to this far below it (as the walk graph does). */
    constexpr double GroundAboveCm = 250.0;
    constexpr double GroundBelowCm = 400.0;
    constexpr double WalkableNormalZ = 0.7;
    /** Masts are drawn this far and lanterns this far; the heads glow further than a post can be told apart. */
    constexpr float MastCullCm = 60000.0f;
    constexpr float LanternCullCm = 30000.0f;

    UHierarchicalInstancedStaticMeshComponent* MakeKind(AActor* Owner, USceneComponent* Root, const TCHAR* Name)
    {
        UHierarchicalInstancedStaticMeshComponent* Kind = Owner->CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(Name);
        Kind->SetupAttachment(Root);
        Kind->SetMobility(EComponentMobility::Static);
        Kind->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
        Kind->SetCanEverAffectNavigation(false);
        Kind->NumCustomDataFloats = Lights::CustomDataFloats;
        Kind->bCastDynamicShadow = true;
        Kind->bAffectDistanceFieldLighting = false;
        return Kind;
    }

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

AMarburgumStreetLamps::AMarburgumStreetLamps()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = false;
    SetCanBeDamaged(false);
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Root->SetMobility(EComponentMobility::Static);
    SetRootComponent(Root);
    Masts = MarburgumStreetLamps::MakeKind(this, Root, TEXT("Masts"));
    Lanterns = MarburgumStreetLamps::MakeKind(this, Root, TEXT("Lanterns"));
}

void AMarburgumStreetLamps::Build(const TArray<MarburgumII::StreetLights::FLamp>& InLamps)
{
    using namespace MarburgumII::StreetLights;
    UStaticMesh* MastMeshAsset = LoadObject<UStaticMesh>(nullptr, MastMesh);
    UStaticMesh* LanternMeshAsset = LoadObject<UStaticMesh>(nullptr, LanternMesh);
    bHasMeshes = MastMeshAsset && LanternMeshAsset;
    Masts->SetStaticMesh(MastMeshAsset);
    Lanterns->SetStaticMesh(LanternMeshAsset);
    Masts->SetCullDistances(0, static_cast<int32>(MarburgumStreetLamps::MastCullCm));
    Lanterns->SetCullDistances(0, static_cast<int32>(MarburgumStreetLamps::LanternCullCm));

    Lamps = InLamps;
    Instance.Init(INDEX_NONE, Lamps.Num());
    TArray<FTransform> MastXforms;
    TArray<FTransform> LanternXforms;
    TArray<int32> MastLamps;
    TArray<int32> LanternLamps;
    for (int32 Index = 0; Index < Lamps.Num(); ++Index)
    {
        const FLamp& Lamp = Lamps[Index];
        const FTransform Xform(FRotator(0.0f, Lamp.Yaw, 0.0f), Lamp.Location);
        if (!Lamp.bStanding)
        {
            continue;
        }
        if (Lamp.Kind == EKind::Mast && MastMeshAsset)
        {
            MastXforms.Add(Xform);
            MastLamps.Add(Index);
        }
        else if (Lamp.Kind == EKind::Lantern && LanternMeshAsset)
        {
            LanternXforms.Add(Xform);
            LanternLamps.Add(Index);
        }
    }
    // World space; an HISM keeps the order it was given, so instance i is the i-th lamp of its kind.
    Masts->AddInstances(MastXforms, false, true);
    Lanterns->AddInstances(LanternXforms, false, true);
    for (int32 I = 0; I < MastLamps.Num(); ++I)
    {
        Instance[MastLamps[I]] = I;
    }
    for (int32 I = 0; I < LanternLamps.Num(); ++I)
    {
        Instance[LanternLamps[I]] = I;
    }
}

void AMarburgumStreetLamps::SetState(int32 Lamp, float Output, const FLinearColor& Colour)
{
    if (!Lamps.IsValidIndex(Lamp) || Instance[Lamp] == INDEX_NONE)
    {
        return;
    }
    using namespace MarburgumII::StreetLights;
    const bool bMast = Lamps[Lamp].Kind == EKind::Mast;
    UHierarchicalInstancedStaticMeshComponent* Kind = bMast ? Masts : Lanterns;
    const float Data[CustomDataFloats] = { Output, Colour.R, Colour.G, Colour.B };
    Kind->SetCustomData(Instance[Lamp], TArrayView<const float>(Data, CustomDataFloats), false);
    (bMast ? bMastsDirty : bLanternsDirty) = true;
}

void AMarburgumStreetLamps::FlushStates()
{
    if (bMastsDirty)
    {
        Masts->MarkRenderStateDirty();
    }
    if (bLanternsDirty)
    {
        Lanterns->MarkRenderStateDirty();
    }
    bMastsDirty = bLanternsDirty = false;
}

bool UMarburgumStreetLampsSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    return Super::ShouldCreateSubsystem(Outer) && !FParse::Param(FCommandLine::Get(), TEXT("MB2NoStreetLamps"));
}

bool UMarburgumStreetLampsSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UMarburgumStreetLampsSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    using namespace MarburgumII::StreetLights;
    // Only Lent's map has its lamps, as only it has a walk graph.
    if (!InWorld.GetMapName().EndsWith(TEXT("lent")))
    {
        return;
    }
    const TCHAR* Role = MarburgumStreetLamps::Role(InWorld);
    const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), LampsFile);
    FString Text;
    TArray<FLamp> All;
    FString Error;
    if (!FFileHelper::LoadFileToString(Text, *Path) || !Parse(Text, All, Error))
    {
        UE_LOG(LogMarburgumStreetLamps, Warning, TEXT("MB2LAMPS role=%s event=none reason=\"%s\""), Role, Error.IsEmpty() ? *FString::Printf(TEXT("cannot read %s"), *Path) : *Error);
        return;
    }
    Read = All.Num();

    // Each lamp stands on what is there: the paving or the landscape under its surveyed top.
    const double Started = FPlatformTime::Seconds();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumStreetLamps), true);
    const FCollisionObjectQueryParams Static(FCollisionObjectQueryParams::AllStaticObjects);
    TArray<FLamp> Standing;
    Standing.Reserve(All.Num());
    int32 NoGround = 0;
    int32 Masts = 0;
    for (const FLamp& Lamp : All)
    {
        TArray<FHitResult> Hits;
        InWorld.LineTraceMultiByObjectType(Hits, Lamp.Location + FVector(0.0, 0.0, MarburgumStreetLamps::GroundAboveCm),
            Lamp.Location - FVector(0.0, 0.0, MarburgumStreetLamps::GroundBelowCm), Static, Params);
        const FHitResult* Ground = nullptr;
        for (const FHitResult& Hit : Hits)
        {
            const bool bWalkable = Hit.bBlockingHit && Hit.ImpactNormal.Z >= MarburgumStreetLamps::WalkableNormalZ;
            // The surface nearest the survey's height, so an eave or a canopy over the pavement is not stood on.
            if (bWalkable && (!Ground || FMath::Abs(Hit.ImpactPoint.Z - Lamp.Location.Z) < FMath::Abs(Ground->ImpactPoint.Z - Lamp.Location.Z)))
            {
                Ground = &Hit;
            }
        }
        if (!Ground)
        {
            FHitResult Single;
            if (InWorld.LineTraceSingleByObjectType(Single, Lamp.Location + FVector(0.0, 0.0, MarburgumStreetLamps::GroundAboveCm),
                    Lamp.Location - FVector(0.0, 0.0, MarburgumStreetLamps::GroundBelowCm), Static, Params)
                && Single.ImpactNormal.Z >= MarburgumStreetLamps::WalkableNormalZ)
            {
                Hits = { Single };
                Ground = &Hits[0];
            }
        }
        FLamp& Placed = Standing.Add_GetRef(Lamp);
        if (Ground)
        {
            Placed.Location.Z = Ground->ImpactPoint.Z;
            ++Grounded;
        }
        else
        {
            // Not put up, rather than standing in the air or in the ground; kept in the list, so the file's indices stay
            // the lamps' indices, and counted and named, so it shows.
            if (NoGround < 8)
            {
                UE_LOG(LogMarburgumStreetLamps, Log, TEXT("MB2LAMPS role=%s event=no_ground lamp=%d at=%s kind=%d"), Role, Standing.Num() - 1,
                    *Lamp.Location.ToCompactString(), static_cast<int32>(Lamp.Kind));
            }
            Placed.bStanding = false;
            ++NoGround;
        }
        Masts += Lamp.Kind == EKind::Mast ? 1 : 0;
    }

    FActorSpawnParameters Spawn;
    Spawn.Name = TEXT("MarburgumStreetLamps");
    Spawn.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
    Spawn.ObjectFlags |= RF_Transient;
    Lamps = InWorld.SpawnActor<AMarburgumStreetLamps>(Spawn);
    if (!Lamps)
    {
        UE_LOG(LogMarburgumStreetLamps, Warning, TEXT("MB2LAMPS role=%s event=none reason=spawn"), Role);
        return;
    }
    Lamps->Build(Standing);
    UE_LOG(LogMarburgumStreetLamps, Log, TEXT("MB2LAMPS role=%s event=placed lamps=%d grounded=%d no_ground=%d masts=%d lanterns=%d meshes=%d seconds=%.2f"),
        Role, Read, Grounded, NoGround, Masts, Read - Masts, Lamps->HasMeshes() ? 1 : 0, FPlatformTime::Seconds() - Started);
}

void UMarburgumStreetLampsSubsystem::Deinitialize()
{
    if (Lamps)
    {
        Lamps->Destroy();
        Lamps = nullptr;
    }
    Super::Deinitialize();
}
