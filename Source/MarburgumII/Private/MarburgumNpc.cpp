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
    /** The float lands this far out in front of the angler, and comes in with the reel. */
    constexpr double CastReachCm = 1100.0;
    constexpr double LineRadiusCm = 0.12;
    constexpr double FloatDiameterCm = 4.0;
    /** Without a known surface the float rests this far under the feet (the Lent bank stands about this high). */
    constexpr double FallbackWaterDropCm = 180.0;
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

AMarburgumNpc::AMarburgumNpc()
{
    using namespace MarburgumNpcLook;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> Manny(Npc::BodyMeshPath);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> RodMesh(Npc::RodMeshPath);

    // A feet-level root lets the interaction range and the prompt anchor use the person, not a cube.
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Root->SetMobility(EComponentMobility::Static);
    SetRootComponent(Root);

    // The base class's mesh is the collision stand-in: it blocks like a person and is never drawn.
    Mesh->SetupAttachment(Root);
    Mesh->SetStaticMesh(Cylinder.Object);
    Mesh->SetRelativeScale3D(FVector(BodyRadius / 50.f, BodyRadius / 50.f, BodyHeight / 100.f));
    Mesh->SetRelativeLocation(FVector(0.f, 0.f, BodyHeight * 0.5f));
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

    Rod = MakePart(*this, TEXT("Rod"), Body, RodMesh.Succeeded() ? RodMesh.Object : nullptr);
    Rod->SetupAttachment(Body, Npc::RodSocket);
    Rod->SetCastShadow(true);
    Rod->SetVisibility(false);

    Line = MakePart(*this, TEXT("Line"), Root, Cylinder.Object);
    Line->SetVisibility(false);
    Float = MakePart(*this, TEXT("Float"), Root, Sphere.Object);
    Float->SetRelativeScale3D(FVector(FloatDiameterCm / 100.0));
    Float->SetVisibility(false);

    Verb = NSLOCTEXT("MarburgumNpc", "Talk", "Talk");
    bTestFixture = false;
    InteractionRange = 400.f;
    // Ticks only to draw: the body's clip, its turn and the line. A dedicated server switches it off.
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.f;
}

void AMarburgumNpc::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    Rod->SetVisibility(bHoldsRod);
}

void AMarburgumNpc::BeginPlay()
{
    Super::BeginPlay();
    using namespace MarburgumNpcLook;
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
    IdleClip = LoadClip(Npc::IdleClipPath);
    TalkClip = LoadClip(Npc::TalkClipPath);
    FishClips[static_cast<int32>(Npc::EAnglerPhase::Cast)] = LoadClip(Npc::FishCastClipPath);
    FishClips[static_cast<int32>(Npc::EAnglerPhase::Wait)] = LoadClip(Npc::FishWaitClipPath);
    FishClips[static_cast<int32>(Npc::EAnglerPhase::Reel)] = LoadClip(Npc::FishReelClipPath);
    FishClips[static_cast<int32>(Npc::EAnglerPhase::Hold)] = LoadClip(Npc::FishHoldClipPath);
    if (!bHoldsRod)
    {
        Body->SetRelativeScale3D(FVector(HeightScale(Seed)));
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
    using namespace MarburgumNpcLook;
    Rod->SetVisibility(bHoldsRod);
    if (GetNetMode() == NM_DedicatedServer)
    {
        return;
    }
    // The mannequin's two materials: the body takes the person's main colour, the joints and trim the second.
    const FLinearColor Colors[] = { PrimaryColor, SecondaryColor };
    for (int32 Slot = 0; Slot < FMath::Min(Body->GetNumMaterials(), 2); ++Slot)
    {
        Tint(Body, Colors[Slot], Slot, PaintTint);
    }
    Tint(Line, LineColor);
    Tint(Float, FloatColor);
}

void AMarburgumNpc::ConverseAuthority(APawn* With)
{
    check(HasAuthority());
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

void AMarburgumNpc::TurnBody(float DeltaSeconds, const APawn* Toward)
{
    using namespace MarburgumNpcLook;
    double Goal = 0.0;
    if (Toward)
    {
        const FVector Local = GetActorTransform().InverseTransformVectorNoScale(Toward->GetActorLocation() - GetActorLocation());
        Goal = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X)), -Npc::AttentionMaxTurnDegrees, Npc::AttentionMaxTurnDegrees);
    }
    const double Step = Npc::AttentionTurnDegreesPerSecond * DeltaSeconds;
    BodyTurn += FMath::Clamp(Goal - BodyTurn, -Step, Step);
    Body->SetRelativeRotation(FRotator(0.0, -90.0 + BodyTurn, 0.0));
}

void AMarburgumNpc::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    using namespace MarburgumNpcLook;
    if (GetNetMode() == NM_DedicatedServer || !Body)
    {
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
    TurnBody(DeltaSeconds, Toward);
    const bool bTalking = TalkingTo.IsValid() && GetWorld()->GetTimeSeconds() < TalkUntilSeconds;
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
    using namespace MarburgumNpcLook;
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
    using namespace MarburgumNpcLook;
    if (!bOut || !Rod->GetStaticMesh() || !Rod->DoesSocketExist(Npc::RodTipSocket))
    {
        Line->SetVisibility(false);
        Float->SetVisibility(false);
        return;
    }
    const FVector Tip = Rod->GetSocketLocation(Npc::RodTipSocket);
    if (!bFloatKnown)
    {
        FVector Out = GetActorLocation() + GetActorForwardVector() * CastReachCm;
        double Surface = GetActorLocation().Z - FallbackWaterDropCm;
        if (const UMarburgumWaterSubsystem* Water = GetWorld()->GetSubsystem<UMarburgumWaterSubsystem>())
        {
            Water->SurfaceAt(Out, Surface);
        }
        Out.Z = Surface;
        FloatAt = Out;
        bFloatKnown = true;
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
    Line->SetWorldScale3D(FVector(LineRadiusCm * 2.0 / 100.0, LineRadiusCm * 2.0 / 100.0, Span.Size() / 100.0));
    Line->SetVisibility(true);
}
