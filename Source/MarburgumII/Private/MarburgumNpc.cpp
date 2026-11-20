#include "MarburgumNpc.h"

#include "Animation/AnimSequenceBase.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumNpcContract.h"
#include "MarburgumWater.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace MarburgumNpcLook
{
    namespace Npc = MarburgumII::Npc;

    // The collision stand-in: a person-sized cylinder, never drawn.
    constexpr float BodyHeight = 175.f;
    constexpr float BodyRadius = 25.f;
    const FName PaintTint(TEXT("Paint Tint"));
    const FLinearColor LineColor(0.55f, 0.6f, 0.5f);
    const FLinearColor FloatColor(0.85f, 0.18f, 0.08f);
    /** The float lands on the first water between these distances in front of the angler, and comes in with the reel. */
    constexpr double NearestCastCm = 500.0;
    constexpr double FurthestCastCm = 2600.0;
    constexpr double LineRadiusCm = 0.12;
    constexpr double FloatDiameterCm = 4.0;
    /** Talking hands blend in and out over this long. */
    constexpr float BlendSeconds = 0.25f;

    /** A little difference in height between people, the same on every machine. */
    float HeightScale(uint32 Seed)
    {
        return 0.95f + 0.08f * static_cast<float>(Seed % 101) / 100.f;
    }

    UStaticMeshComponent* MakePart(AActor& Owner, const TCHAR* Name, USceneComponent* Parent, UStaticMesh* Shape)
    {
        UStaticMeshComponent* Part = Owner.CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Part->SetupAttachment(Parent);
        Part->SetStaticMesh(Shape);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetGenerateOverlapEvents(false);
        Part->SetCastShadow(false);
        Part->SetMobility(EComponentMobility::Movable);
        return Part;
    }

    void Tint(UPrimitiveComponent* Part, const FLinearColor& Color, int32 Slot = 0, FName Parameter = TEXT("Color"))
    {
        if (UMaterialInstanceDynamic* Material = Part ? Part->CreateDynamicMaterialInstance(Slot) : nullptr)
        {
            Material->SetVectorParameterValue(Parameter, Color);
        }
    }

    UAnimSequenceBase* LoadClip(const TCHAR* Path)
    {
        return Path ? LoadObject<UAnimSequenceBase>(nullptr, Path) : nullptr;
    }
}

FVector FMarburgumNpcWalk::At(double ServerSeconds, bool& bOutWalking) const
{
    const double Distance = FVector::Dist(From, To);
    if (StartSeconds < 0.0 || Distance < 1.0)
    {
        bOutWalking = false;
        return To;
    }
    const double Alpha = FMath::Clamp((ServerSeconds - StartSeconds) * MarburgumII::Npc::StrollCmPerSecond / Distance, 0.0, 1.0);
    bOutWalking = Alpha < 1.0;
    return FMath::Lerp(FVector(From), FVector(To), Alpha);
}

AMarburgumNpc::AMarburgumNpc()
{
    namespace Look = MarburgumNpcLook;
    namespace Npc = MarburgumII::Npc;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> Manny(Npc::BodyMeshPath);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> RodMesh(Npc::RodMeshPath);

    // A feet-level root lets the interaction range and the prompt anchor use the person, not a cube.
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    // Movable: the server may stand a level-placed person somewhere better before play (the angler on the bank).
    Root->SetMobility(EComponentMobility::Movable);
    SetRootComponent(Root);

    // The base class's mesh is the collision stand-in: it blocks like a person and is never drawn.
    Mesh->SetupAttachment(Root);
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetStaticMesh(Cylinder.Object);
    Mesh->SetRelativeScale3D(FVector(Look::BodyRadius / 50.f, Look::BodyRadius / 50.f, Look::BodyHeight / 100.f));
    Mesh->SetRelativeLocation(FVector(0.f, 0.f, Look::BodyHeight * 0.5f));
    Mesh->SetHiddenInGame(true);
    Mesh->SetCastShadow(false);

    Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
    Body->SetupAttachment(Root);
    Body->SetMobility(EComponentMobility::Movable);
    if (Manny.Succeeded())
    {
        Body->SetSkeletalMesh(Manny.Object);
    }
    // The mannequin faces +Y; the person faces the actor's +X.
    Body->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetGenerateOverlapEvents(false);
    Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;

    Rod = Look::MakePart(*this, TEXT("Rod"), Body, RodMesh.Succeeded() ? RodMesh.Object : nullptr);
    Rod->SetupAttachment(Body, Npc::RodSocket);
    Rod->SetCastShadow(true);
    Rod->SetVisibility(false);

    Line = Look::MakePart(*this, TEXT("Line"), Root, Cylinder.Object);
    Line->SetVisibility(false);
    Float = Look::MakePart(*this, TEXT("Float"), Root, Sphere.Object);
    Float->SetRelativeScale3D(FVector(Look::FloatDiameterCm / 100.0));
    Float->SetVisibility(false);

    Verb = NSLOCTEXT("MarburgumNpc", "Talk", "Talk");
    bTestFixture = false;
    InteractionRange = 400.f;
    // Ticks only to draw: the body's clip, its turn and the line. A dedicated server switches it off.
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.f;
}

void AMarburgumNpc::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMarburgumNpc, Walk);
}

double AMarburgumNpc::ServerSeconds() const
{
    const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
    return GameState ? GameState->GetServerWorldTimeSeconds() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
}

bool AMarburgumNpc::IsWalking() const
{
    bool bWalking = false;
    Walk.At(ServerSeconds(), bWalking);
    return bWalking;
}

void AMarburgumNpc::OnRep_Walk()
{
    // A late joiner or a walk seen mid-way: start drawing it at once, wherever it has got to.
    SetActorTickEnabled(true);
}

void AMarburgumNpc::SetPostsAuthority(const TArray<FVector>& Points)
{
    check(HasAuthority());
    Posts = Points;
    CurrentPost = 0;
    // Only someone with somewhere to go needs the server's tick.
    SetActorTickEnabled(Posts.Num() > 1 || GetNetMode() != NM_DedicatedServer);
}

void AMarburgumNpc::GoToPostAuthority(int32 Index)
{
    check(HasAuthority());
    if (!Posts.IsValidIndex(Index) || ServerSeconds() < PausedUntilSeconds)
    {
        return;
    }
    bool bWalking = false;
    const FVector Here = Walk.StartSeconds < 0.0 ? GetActorLocation() : Walk.At(ServerSeconds(), bWalking);
    if (Index == CurrentPost && (bWalking || FVector::Dist2D(Here, Posts[Index]) < 5.0))
    {
        return;
    }
    CurrentPost = Index;
    Walk.From = Here;
    Walk.To = Posts[Index];
    Walk.StartSeconds = ServerSeconds();
    ForceNetUpdate();
}

void AMarburgumNpc::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    Rod->SetVisibility(bHoldsRod);
}

void AMarburgumNpc::BeginPlay()
{
    Super::BeginPlay();
    namespace Look = MarburgumNpcLook;
    namespace Npc = MarburgumII::Npc;
    Seed = GetTypeHash(InteractableId.IsNone() ? GetFName() : InteractableId);
    // Placed in a level before the body was a mannequin: whatever the level saved for the rod, it sits in the hand.
    Rod->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Npc::RodSocket);
    Rod->SetRelativeTransform(FTransform::Identity);
    if (GetNetMode() == NM_DedicatedServer)
    {
        // Nothing renders on a dedicated server: no pose to work out, no clips to load.
        Body->SetComponentTickEnabled(false);
        Body->SetUpdateAnimationInEditor(false);
        SetActorTickEnabled(false);
        return;
    }
    IdleClip = Look::LoadClip(Npc::IdleClipPath);
    TalkClip = Look::LoadClip(Npc::TalkClipPath);
    WalkClip = Look::LoadClip(Npc::WalkClipPath);
    FishClips[static_cast<int32>(Npc::EAnglerPhase::Cast)] = Look::LoadClip(Npc::FishCastClipPath);
    FishClips[static_cast<int32>(Npc::EAnglerPhase::Wait)] = Look::LoadClip(Npc::FishWaitClipPath);
    FishClips[static_cast<int32>(Npc::EAnglerPhase::Reel)] = Look::LoadClip(Npc::FishReelClipPath);
    FishClips[static_cast<int32>(Npc::EAnglerPhase::Hold)] = Look::LoadClip(Npc::FishHoldClipPath);
    if (!bHoldsRod)
    {
        Body->SetRelativeScale3D(FVector(Look::HeightScale(Seed)));
    }
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
    // The original profiles are picked colours (sRGB), like the street people's.
    const auto Picked = [](const float (&Rgb)[3])
    {
        return FLinearColor(FColor(FMath::RoundToInt32(FMath::Clamp(Rgb[0], 0.f, 1.f) * 255.f),
            FMath::RoundToInt32(FMath::Clamp(Rgb[1], 0.f, 1.f) * 255.f), FMath::RoundToInt32(FMath::Clamp(Rgb[2], 0.f, 1.f) * 255.f)));
    };
    PrimaryColor = Picked(Entry->PrimaryColor);
    SecondaryColor = Picked(Entry->SecondaryColor);
    if (HasActorBegunPlay())
    {
        ApplyLook();
    }
}

void AMarburgumNpc::ApplyLook()
{
    namespace Look = MarburgumNpcLook;
    namespace Npc = MarburgumII::Npc;
    Rod->SetVisibility(bHoldsRod);
    if (GetNetMode() == NM_DedicatedServer)
    {
        return;
    }
    // The mannequin's two materials: the body takes the person's main colour, the joints and trim the second.
    const FLinearColor Colors[] = { PrimaryColor, SecondaryColor };
    for (int32 Slot = 0; Slot < FMath::Min(Body->GetNumMaterials(), 2); ++Slot)
    {
        Look::Tint(Body, Colors[Slot], Slot, Look::PaintTint);
    }
    Look::Tint(Line, Look::LineColor);
    Look::Tint(Float, Look::FloatColor);
}

void AMarburgumNpc::ConverseAuthority(APawn* With)
{
    check(HasAuthority());
    // Somebody on their way stops to talk; the next post check sets them off again afterwards.
    bool bWalking = false;
    const FVector Here = Walk.At(ServerSeconds(), bWalking);
    if (bWalking)
    {
        Walk.From = Here;
        Walk.To = Here;
        Walk.StartSeconds = ServerSeconds();
        CurrentPost = INDEX_NONE;
        SetActorLocation(Here);
        ForceNetUpdate();
    }
    PausedUntilSeconds = ServerSeconds() + MarburgumII::Npc::TalkSeconds;
    MulticastConverse(With);
}

void AMarburgumNpc::MulticastConverse_Implementation(APawn* With)
{
    if (GetNetMode() == NM_DedicatedServer)
    {
        return;
    }
    TalkingTo = With;
    TalkUntilSeconds = GetWorld()->GetTimeSeconds() + MarburgumII::Npc::TalkSeconds;
}

bool AMarburgumNpc::IsRodInHand() const
{
    return Rod && Rod->IsVisible() && Rod->GetStaticMesh() && Rod->GetAttachParent() == Body && Rod->GetAttachSocketName() == MarburgumII::Npc::RodSocket
        && Body->DoesSocketExist(MarburgumII::Npc::RodSocket);
}

bool AMarburgumNpc::IsLineOut() const
{
    return Float && Float->IsVisible();
}

void AMarburgumNpc::Play(UAnimSequenceBase* Clip, FName Name, bool bLoop, float Position)
{
    if (!Clip)
    {
        Clip = IdleClip;
        Name = TEXT("idle");
        bLoop = true;
    }
    if (!Clip)
    {
        return;
    }
    if (PlayingClip != Name)
    {
        Body->PlayAnimation(Clip, bLoop);
        PlayingClip = Name;
    }
    if (Position >= 0.f)
    {
        Body->SetPosition(Position, false);
    }
}

APawn* AMarburgumNpc::AttentionTarget() const
{
    const double Now = GetWorld()->GetTimeSeconds();
    if (TalkingTo.IsValid() && Now < TalkUntilSeconds)
    {
        return TalkingTo.Get();
    }
    // Otherwise the nearest player who has come close. Every client sees the same pawns where the server put
    // them, so they agree on who that is.
    APawn* Nearest = nullptr;
    double Best = FMath::Square(MarburgumII::Npc::AttentionRangeCm);
    for (TActorIterator<APawn> It(GetWorld()); It; ++It)
    {
        if (!It->IsPlayerControlled() && !It->GetPlayerState())
        {
            continue;
        }
        const double Distance = FVector::DistSquared2D(It->GetActorLocation(), GetActorLocation());
        if (Distance < Best)
        {
            Best = Distance;
            Nearest = *It;
        }
    }
    return Nearest;
}

void AMarburgumNpc::TurnBody(float DeltaSeconds, const APawn* Toward, bool bTalking)
{
    namespace Look = MarburgumNpcLook;
    namespace Npc = MarburgumII::Npc;
    double Goal = 0.0;
    if (Toward)
    {
        const FVector Local = GetActorTransform().InverseTransformVectorNoScale(Toward->GetActorLocation() - GetActorLocation());
        // Someone talking to you gets your face, wherever they stand; a passer-by only a turn of the head and shoulders.
        const double Limit = bTalking ? 180.0 : Npc::AttentionMaxTurnDegrees;
        Goal = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X)), -Limit, Limit);
    }
    const double Step = Npc::AttentionTurnDegreesPerSecond * DeltaSeconds;
    BodyTurn += FMath::Clamp(Goal - BodyTurn, -Step, Step);
    Body->SetRelativeRotation(FRotator(0.0, -90.0 + BodyTurn, 0.0));
}

void AMarburgumNpc::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    namespace Look = MarburgumNpcLook;
    namespace Npc = MarburgumII::Npc;
    // Everyone moves the body the walk says, the server included (its copy is what range checks use).
    bool bWalking = false;
    if (Walk.StartSeconds >= 0.0)
    {
        FVector At = Walk.At(ServerSeconds(), bWalking);
        if (GetNetMode() != NM_DedicatedServer)
        {
            // Feet on the ground the straight line crosses (steps, kerbs), not on the line itself.
            FHitResult Ground;
            FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumNpcStep), false, this);
            if (GetWorld()->LineTraceSingleByChannel(Ground, At + FVector(0.0, 0.0, 80.0), At - FVector(0.0, 0.0, 120.0), ECC_Visibility, Params))
            {
                At.Z = Ground.ImpactPoint.Z;
            }
        }
        if (!At.Equals(GetActorLocation(), 0.5))
        {
            SetActorLocation(At);
        }
    }
    if (GetNetMode() == NM_DedicatedServer || !Body)
    {
        return;
    }
    if (bWalking)
    {
        // Face the way they are going and stroll.
        const FVector Way = (FVector(Walk.To) - FVector(Walk.From)).GetSafeNormal2D();
        const FVector Local = GetActorTransform().InverseTransformVectorNoScale(Way);
        BodyTurn = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
        Body->SetRelativeRotation(FRotator(0.0, -90.0 + BodyTurn, 0.0));
        Play(WalkClip, TEXT("walk"), true, -1.f);
        return;
    }
    const AGameStateBase* GameState = GetWorld()->GetGameState();
    const double ServerSeconds = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
    if (bHoldsRod)
    {
        // The angler keeps his eyes and his rod on the water; somebody talking to him gets an answer, not a turn.
        UpdateAngler(ServerSeconds);
        return;
    }
    APawn* Toward = AttentionTarget();
    const bool bTalking = TalkingTo.IsValid() && GetWorld()->GetTimeSeconds() < TalkUntilSeconds;
    TurnBody(DeltaSeconds, Toward, bTalking);
    if (bTalking)
    {
        Play(TalkClip, TEXT("talk"), true, -1.f);
    }
    else
    {
        // Each person breathes at their own point of the idle, so a row of them does not move in step.
        const float Offset = IdleClip ? FMath::Fmod(static_cast<float>(Seed % 997) / 97.f, IdleClip->GetPlayLength()) : 0.f;
        Play(IdleClip, TEXT("idle"), true, PlayingClip == TEXT("idle") ? -1.f : Offset);
    }
}

void AMarburgumNpc::UpdateAngler(double ServerSeconds)
{
    namespace Look = MarburgumNpcLook;
    namespace Npc = MarburgumII::Npc;
    const Npc::FAnglerMoment Moment = Npc::AnglerMomentAt(ServerSeconds, Seed);
    const int32 Phase = static_cast<int32>(Moment.Phase);
    const TCHAR* Names[] = { TEXT("fish_cast"), TEXT("fish_wait"), TEXT("fish_reel"), TEXT("fish_hold") };
    const bool bLoop = Moment.Phase != Npc::EAnglerPhase::Cast;
    const FName Name(Names[Phase]);
    if (PlayingClip != Name || (Moment.Phase == Npc::EAnglerPhase::Cast && PlayingCycle != Moment.Cycle))
    {
        // Join the phase where the server's clock has it, not from its start: a client that walks up mid-cast
        // sees the same frame as one that watched it begin.
        UAnimSequenceBase* Clip = FishClips[Phase];
        const float Length = Clip ? Clip->GetPlayLength() : 1.f;
        const float Position = bLoop ? FMath::Fmod(static_cast<float>(Moment.Elapsed), Length) : FMath::Min(static_cast<float>(Moment.Elapsed), Length);
        PlayingClip = NAME_None;
        Play(Clip, Name, bLoop, Position);
        PlayingCycle = Moment.Cycle;
        bFloatKnown = false;
    }
    // The line is out from the end of the cast's flick to the end of the reel; the reel brings the float in.
    const double CastOutAt = 1.2;
    double Reeled = 0.0;
    bool bOut = false;
    switch (Moment.Phase)
    {
    case Npc::EAnglerPhase::Cast:
        bOut = Moment.Elapsed >= CastOutAt;
        break;
    case Npc::EAnglerPhase::Wait:
        bOut = true;
        break;
    case Npc::EAnglerPhase::Reel:
        bOut = true;
        Reeled = Moment.Elapsed / Npc::AnglerPhaseSeconds[static_cast<int32>(Npc::EAnglerPhase::Reel)];
        break;
    default:
        break;
    }
    ShowLine(bOut, Reeled);
}

void AMarburgumNpc::ShowLine(bool bOut, double Reeled)
{
    namespace Look = MarburgumNpcLook;
    namespace Npc = MarburgumII::Npc;
    if (!bOut || !Rod->GetStaticMesh() || !Rod->DoesSocketExist(Npc::RodTipSocket))
    {
        Line->SetVisibility(false);
        Float->SetVisibility(false);
        return;
    }
    const FVector Tip = Rod->GetSocketLocation(Npc::RodTipSocket);
    if (!bFloatKnown)
    {
        // The float lands on the first water in front of him, never on the quay: the Lent promenade stands well back
        // from the Drava in places. With no water within reach the line stays in.
        bFloatKnown = true;
        bFloatOnWater = false;
        if (const UMarburgumWaterSubsystem* Water = GetWorld()->GetSubsystem<UMarburgumWaterSubsystem>())
        {
            for (double Reach = Look::NearestCastCm; Reach <= Look::FurthestCastCm && !bFloatOnWater; Reach += 100.0)
            {
                FVector Out = GetActorLocation() + GetActorForwardVector() * Reach;
                double Surface = 0.0;
                if (Water->SurfaceAt(Out, Surface))
                {
                    // A little further than the edge, where a cast actually lands.
                    Out = GetActorLocation() + GetActorForwardVector() * FMath::Min(Reach + 350.0, Look::FurthestCastCm);
                    Water->SurfaceAt(Out, Surface);
                    FloatAt = FVector(Out.X, Out.Y, Surface);
                    bFloatOnWater = true;
                }
            }
        }
    }
    if (!bFloatOnWater)
    {
        Line->SetVisibility(false);
        Float->SetVisibility(false);
        return;
    }
    // Reeling draws the float back to just under the tip.
    const FVector Under(Tip.X, Tip.Y, FloatAt.Z);
    const FVector At = FMath::Lerp(FloatAt, Under, FMath::Clamp(Reeled, 0.0, 0.92))
        + FVector(0.0, 0.0, 0.6 * FMath::Sin(GetWorld()->GetTimeSeconds() * 2.1 + Seed % 7));
    Float->SetWorldLocation(At);
    Float->SetVisibility(true);
    // The engine cylinder is 100 cm tall about its middle, along Z.
    const FVector Span = At - Tip;
    Line->SetWorldLocationAndRotation((Tip + At) * 0.5, FRotationMatrix::MakeFromZ(Span.GetSafeNormal()).Rotator());
    Line->SetWorldScale3D(FVector(Look::LineRadiusCm * 2.0 / 100.0, Look::LineRadiusCm * 2.0 / 100.0, Span.Size() / 100.0));
    Line->SetVisibility(true);
}
