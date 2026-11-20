#include "MarburgumDoor.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

namespace MarburgumDoorPrivate
{
    constexpr float LeafThicknessCm = 4.f;
    constexpr float SwingSeconds = 1.15f;
}

AMarburgumDoor::AMarburgumDoor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    bReplicates = true;
    SetReplicateMovement(false);
    SetNetUpdateFrequency(8.f);
    SetCanBeDamaged(false);

    HingeRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
    SetRootComponent(HingeRoot);
    Leaf = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Leaf"));
    Leaf->SetupAttachment(HingeRoot);
    Leaf->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
    Leaf->SetCanEverAffectNavigation(false);
    // Lit by the room lamps as well as the sun (ADR-076).
    Leaf->SetLightingChannels(true, true, false);
}

void AMarburgumDoor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMarburgumDoor, Motion);
    DOREPLIFETIME(AMarburgumDoor, Hinge);
    DOREPLIFETIME(AMarburgumDoor, ClosedYaw);
    DOREPLIFETIME(AMarburgumDoor, SwingDegrees);
    DOREPLIFETIME(AMarburgumDoor, WidthCm);
    DOREPLIFETIME(AMarburgumDoor, HeightCm);
    DOREPLIFETIME(AMarburgumDoor, BuildingId);
    DOREPLIFETIME(AMarburgumDoor, PlaceName);
    DOREPLIFETIME(AMarburgumDoor, bStreetDoor);
}

void AMarburgumDoor::BeginPlay()
{
    Super::BeginPlay();
    // A client that arrives while a door stands open sees it open, not swinging open in front of it.
    if (Motion == EMarburgumDoorMotion::Open || Motion == EMarburgumDoorMotion::Closed)
    {
        OpenAlpha = TargetAlpha();
    }
    else
    {
        SetActorTickEnabled(true);
    }
    EnsureLeaf();
    ApplySwing();
    ApplyCollision();
}

void AMarburgumDoor::InitAuthority(const FVector& InHinge, const FVector& ClosedDir, float InSwingDegrees, float InWidthCm, float InHeightCm,
    FName InBuildingId, const FString& InPlaceName, bool bInStreetDoor)
{
    Hinge = InHinge;
    const FVector Flat = FVector(ClosedDir.X, ClosedDir.Y, 0.f).GetSafeNormal();
    ClosedYaw = Flat.IsNearlyZero() ? 0.f : Flat.Rotation().Yaw;
    SwingDegrees = InSwingDegrees;
    WidthCm = InWidthCm;
    HeightCm = InHeightCm;
    BuildingId = InBuildingId;
    PlaceName = InPlaceName;
    bStreetDoor = bInStreetDoor;
    SetActorLocation(Hinge);
    SetActorRotation(FRotator(0.f, ClosedYaw, 0.f));
    EnsureLeaf();
    ApplySwing();
    ApplyCollision();
}

FVector AMarburgumDoor::GetDoorwayCentre() const
{
    const FVector Closed = FRotator(0.f, ClosedYaw, 0.f).Vector();
    return Hinge + Closed * (WidthCm * 0.5f) + FVector(0.f, 0.f, HeightCm * 0.5f);
}

bool AMarburgumDoor::IsWithinReach(const FVector& BodyLocation, float Slack) const
{
    const FVector Centre = GetDoorwayCentre();
    return FVector::DistSquared2D(BodyLocation, Centre) <= FMath::Square(InteractionRange + Slack)
        && FMath::Abs(BodyLocation.Z - Centre.Z) <= HeightCm * 0.5f + 120.f + Slack;
}

bool AMarburgumDoor::IsSwingClear() const
{
    UWorld* World = GetWorld();
    if (!World || WidthCm <= 0.f)
    {
        return true;
    }
    const FVector Closed = FRotator(0.f, ClosedYaw, 0.f).Vector();
    const float Sign = SwingDegrees >= 0.f ? 1.f : -1.f;
    const FVector Into = FRotator(0.f, ClosedYaw + 90.f * Sign, 0.f).Vector();
    const float Swept = FMath::DegreesToRadians(FMath::Abs(SwingDegrees) * OpenAlpha);
    for (TActorIterator<APawn> It(World); It; ++It)
    {
        const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(It->GetRootComponent());
        const float Radius = Capsule ? Capsule->GetScaledCapsuleRadius() : 34.f;
        const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.f;
        const FVector At = It->GetActorLocation();
        if (At.Z + HalfHeight < Hinge.Z || At.Z - HalfHeight > Hinge.Z + HeightCm)
        {
            continue;
        }
        const FVector Offset = At - Hinge;
        const float X = FVector::DotProduct(Offset, Closed);
        const float Y = FVector::DotProduct(Offset, Into);
        const float R = FMath::Sqrt(X * X + Y * Y);
        const float Reach = Radius + MarburgumDoorPrivate::LeafThicknessCm;
        if (R > WidthCm + Reach)
        {
            continue;
        }
        if (R <= Reach)
        {
            return false;
        }
        const float Angle = FMath::Atan2(Y, X);
        const float Margin = FMath::Asin(FMath::Min(1.f, Reach / R));
        if (Angle >= -Margin && Angle <= Swept + Margin)
        {
            return false;
        }
    }
    return true;
}

bool AMarburgumDoor::ToggleAuthority()
{
    if (!HasAuthority())
    {
        return false;
    }
    if (Motion == EMarburgumDoorMotion::Closed || Motion == EMarburgumDoorMotion::Closing)
    {
        Motion = EMarburgumDoorMotion::Opening;
    }
    else
    {
        if (!IsSwingClear())
        {
            return false;
        }
        Motion = EMarburgumDoorMotion::Closing;
    }
    ApplyCollision();
    SetActorTickEnabled(true);
    ForceNetUpdate();
    return true;
}

void AMarburgumDoor::OnRep_Motion()
{
    if (!HasActorBegunPlay())
    {
        return;
    }
    ApplyCollision();
    SetActorTickEnabled(true);
}

void AMarburgumDoor::OnRep_Leaf()
{
    SetActorLocation(Hinge);
    EnsureLeaf();
    ApplySwing();
}

void AMarburgumDoor::EnsureLeaf()
{
    if (bLeafBuilt || WidthCm < 10.f || HeightCm < 10.f || !Leaf)
    {
        return;
    }
    if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
    {
        Leaf->SetStaticMesh(Cube);
    }
    if (UMaterialInterface* Frame = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Marburgum/Materials/Instances/building_envelope/MI_facade_frame.MI_facade_frame")))
    {
        // The façade's own door-frame brown, so leaf and jambs read as one door.
        Leaf->SetMaterial(0, Frame);
    }
    // The engine cube is 100 cm on a side and centred. Put the hinge at the actor origin, the leaf along +X.
    Leaf->SetRelativeScale3D(FVector(WidthCm / 100.f, MarburgumDoorPrivate::LeafThicknessCm / 100.f, HeightCm / 100.f));
    Leaf->SetRelativeLocation(FVector(WidthCm * 0.5f, 0.f, HeightCm * 0.5f));
    bLeafBuilt = true;
}

void AMarburgumDoor::ApplyCollision()
{
    if (!Leaf)
    {
        return;
    }
    // Server and clients agree, because Motion replicates: the shut leaf stops a body, any other does not.
    const ECollisionResponse ToPawn = Motion == EMarburgumDoorMotion::Closed ? ECR_Block : ECR_Ignore;
    Leaf->SetCollisionResponseToChannel(ECC_Pawn, ToPawn);
    Leaf->SetCollisionResponseToChannel(ECC_Camera, ToPawn);
}

float AMarburgumDoor::TargetAlpha() const
{
    return (Motion == EMarburgumDoorMotion::Opening || Motion == EMarburgumDoorMotion::Open) ? 1.f : 0.f;
}

void AMarburgumDoor::ApplySwing()
{
    SetActorLocation(Hinge);
    SetActorRotation(FRotator(0.f, ClosedYaw + SwingDegrees * OpenAlpha, 0.f));
}

void AMarburgumDoor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    // Somebody stepped into a closing door: it opens again rather than shutting on them.
    if (HasAuthority() && Motion == EMarburgumDoorMotion::Closing && !IsSwingClear())
    {
        Motion = EMarburgumDoorMotion::Opening;
        ForceNetUpdate();
    }
    const float Target = TargetAlpha();
    const float Step = DeltaSeconds / MarburgumDoorPrivate::SwingSeconds;
    if (OpenAlpha < Target)
    {
        OpenAlpha = FMath::Min(Target, OpenAlpha + Step);
    }
    else if (OpenAlpha > Target)
    {
        OpenAlpha = FMath::Max(Target, OpenAlpha - Step);
    }
    ApplySwing();
    if (!FMath::IsNearlyEqual(OpenAlpha, Target, 0.001f))
    {
        return;
    }
    OpenAlpha = Target;
    if (HasAuthority())
    {
        if (Motion == EMarburgumDoorMotion::Opening)
        {
            Motion = EMarburgumDoorMotion::Open;
        }
        else if (Motion == EMarburgumDoorMotion::Closing)
        {
            Motion = EMarburgumDoorMotion::Closed;
        }
        ApplyCollision();
    }
    else if (Motion == EMarburgumDoorMotion::Opening || Motion == EMarburgumDoorMotion::Closing)
    {
        // The client has finished the swing before the server's resting state arrives; keep ticking for it.
        return;
    }
    SetActorTickEnabled(false);
}
