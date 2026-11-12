#include "UI/MarburgumCharacterPreview.h"

#include "Animation/AnimSequence.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "MarburgumAccountContract.h"
#include "MarburgumNetworkContract.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/MarburgumUIBuilder.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "MarburgumCharacterPreview"

using namespace MarburgumUI;

namespace
{
    /** Far below any map the front end may have loaded; the camera sees only the stage's own parts anyway. */
    const FVector StageLocation(0.0, 0.0, -200000.0);

    /** The picture is rendered at 1.5 times its pixels, so the body's edges stay smooth without a history. */
    constexpr float Supersample = 1.5f;
    constexpr int32 TargetMax = 2048;
    constexpr float DegreesPerPixel = 0.5f;
    constexpr float ZoomPerNotch = 0.25f;

    /** Framing, with a fixed 40° vertical view: the whole body at zoom 0, head and shoulders at zoom 1 (cm). */
    constexpr double VerticalFov = 40.0;
    constexpr double FarHalfHeight = 108.0;
    constexpr double FarCentre = 92.0;
    constexpr double NearHalfHeight = 52.0;
    constexpr double NearCentre = 146.0;

    /** The stage lights only what stands on it, and the world's lights leave it alone (lighting channel 2). */
    FLightingChannels StageChannel()
    {
        FLightingChannels Channels;
        Channels.bChannel0 = false;
        Channels.bChannel2 = true;
        return Channels;
    }

    UStaticMeshComponent* StagePart(AActor* Owner, USceneComponent* Parent, const TCHAR* Name, UStaticMesh* Mesh)
    {
        UStaticMeshComponent* Part = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Part->SetupAttachment(Parent);
        if (Mesh)
        {
            Part->SetStaticMesh(Mesh);
        }
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetLightingChannels(false, false, true);
        return Part;
    }
}

AMarburgumCharacterPreviewStage::AMarburgumCharacterPreviewStage()
{
    PrimaryActorTick.bCanEverTick = false;
    SetReplicates(false);

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;
    Turntable = CreateDefaultSubobject<USceneComponent>(TEXT("Turntable"));
    Turntable->SetupAttachment(Root);

    Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
    Body->SetupAttachment(Turntable);
    // The roster bodies face +Y in mesh space, as on AMarburgumCharacter; this turns them toward the camera at +X.
    Body->SetRelativeRotation(FRotator(0.0, -90.0, 0.0));
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetLightingChannels(false, false, true);
    Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    for (const MarburgumII::Accounts::FCharacterModel& Model : MarburgumII::Accounts::CharacterModels)
    {
        ConstructorHelpers::FObjectFinder<USkeletalMesh> Found(Model.Mesh);
        ModelMeshes.Add(Found.Succeeded() ? Found.Object : nullptr);
    }
    static ConstructorHelpers::FObjectFinder<UAnimSequence> IdleFinder(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
    Idle = IdleFinder.Succeeded() ? IdleFinder.Object : nullptr;

    // A low plinth on a studio floor, before a backdrop: dark, so the body is what catches the eye.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Plain(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    PlainMaterial = Plain.Succeeded() ? Plain.Object : nullptr;
    Floor = StagePart(this, Root, TEXT("Floor"), Cylinder.Succeeded() ? Cylinder.Object : nullptr);
    Floor->SetRelativeLocation(FVector(0.0, 0.0, -1.0));
    Floor->SetRelativeScale3D(FVector(1.4, 1.4, 0.02));
    Backdrop = StagePart(this, Root, TEXT("Backdrop"), Plane.Succeeded() ? Plane.Object : nullptr);
    // The plane faces +Z; pitched back it faces the camera. Far enough back that only the key reaches it, so it
    // stays dark and the body stands out; wide and tall enough to fill the frame at any shape.
    Backdrop->SetRelativeLocation(FVector(-900.0, 0.0, 300.0));
    Backdrop->SetRelativeRotation(FRotator(-90.0, 0.0, 0.0));
    Backdrop->SetRelativeScale3D(FVector(12.0, 30.0, 1.0));
    Ground = StagePart(this, Root, TEXT("Ground"), Plane.Succeeded() ? Plane.Object : nullptr);
    Ground->SetRelativeLocation(FVector(0.0, 0.0, -2.0));
    Ground->SetRelativeScale3D(FVector(30.0, 30.0, 1.0));

    // Key from the front left and above, a soft fill from the right, a rim from behind for the silhouette.
    Key = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Key"));
    Key->SetupAttachment(Root);
    Key->SetRelativeRotation(FRotator(-38.0, 215.0, 0.0));
    Key->SetIntensity(3.5f);
    Key->LightingChannels = StageChannel();
    Key->SetAtmosphereSunLight(false);
    Key->SetCastShadows(true);

    Fill = CreateDefaultSubobject<UPointLightComponent>(TEXT("Fill"));
    Fill->SetupAttachment(Root);
    Fill->SetRelativeLocation(FVector(220.0, 200.0, 150.0));
    Fill->SetIntensityUnits(ELightUnits::Candelas);
    Fill->SetIntensity(18.f);
    Fill->SetAttenuationRadius(600.f);
    Fill->SetCastShadows(false);
    Fill->LightingChannels = StageChannel();

    Rim = CreateDefaultSubobject<UPointLightComponent>(TEXT("Rim"));
    Rim->SetupAttachment(Root);
    Rim->SetRelativeLocation(FVector(-160.0, -110.0, 230.0));
    Rim->SetIntensityUnits(ELightUnits::Candelas);
    Rim->SetIntensity(40.f);
    Rim->SetAttenuationRadius(450.f);
    Rim->SetCastShadows(false);
    Rim->SetLightColor(FLinearColor(1.f, 0.86f, 0.7f));
    Rim->LightingChannels = StageChannel();

    Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
    Capture->SetupAttachment(Root);
    Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->ShowOnlyComponents.Add(Body);
    Capture->ShowOnlyComponents.Add(Floor);
    Capture->ShowOnlyComponents.Add(Backdrop);
    Capture->ShowOnlyComponents.Add(Ground);
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    Capture->bAlwaysPersistRenderingState = true;
    Capture->ShowFlags.SetAtmosphere(false);
    Capture->ShowFlags.SetFog(false);
    Capture->ShowFlags.SetVolumetricFog(false);
    Capture->ShowFlags.SetMotionBlur(false);
    Capture->ShowFlags.SetEyeAdaptation(false);
    FPostProcessSettings& Post = Capture->PostProcessSettings;
    Capture->PostProcessBlendWeight = 1.f;
    // Fixed exposure: an almost empty frame would otherwise be brightened until the backdrop glows.
    Post.bOverride_AutoExposureMethod = true;
    Post.AutoExposureMethod = AEM_Manual;
    Post.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
    Post.AutoExposureApplyPhysicalCameraExposure = false;
    Post.bOverride_AutoExposureBias = true;
    Post.AutoExposureBias = 0.f;
    // Only the stage's own lights: no Lumen or screen-space lighting from a world the camera does not show.
    Post.bOverride_DynamicGlobalIlluminationMethod = true;
    Post.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
    Post.bOverride_ReflectionMethod = true;
    Post.ReflectionMethod = EReflectionMethod::None;
    Post.bOverride_VignetteIntensity = true;
    Post.VignetteIntensity = 0.f;
    Post.bOverride_BloomIntensity = true;
    Post.BloomIntensity = 0.f;
    SetFraming(2.f / 3.f, 0.f);
}

void AMarburgumCharacterPreviewStage::BeginPlay()
{
    Super::BeginPlay();
    if (!PlainMaterial)
    {
        return;
    }
    const auto Paint = [this](UStaticMeshComponent* Part, float Grey)
    {
        UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(PlainMaterial, this);
        Instance->SetVectorParameterValue(TEXT("Color"), FLinearColor(Grey, Grey, Grey * 1.08f));
        Part->SetMaterial(0, Instance);
    };
    Paint(Floor, 0.09f);
    for (UStaticMeshComponent* Part : { Backdrop.Get(), Ground.Get() })
    {
        if (Part)
        {
            Paint(Part, 0.035f);
        }
    }
}

bool AMarburgumCharacterPreviewStage::ShowModel(int32 Index)
{
    if (!ModelMeshes.IsValidIndex(Index) || !ModelMeshes[Index])
    {
        return false;
    }
    Body->SetSkeletalMesh(ModelMeshes[Index]);
    if (Idle)
    {
        Body->PlayAnimation(Idle, true);
    }
    return true;
}

void AMarburgumCharacterPreviewStage::SetYaw(float Degrees)
{
    Turntable->SetRelativeRotation(FRotator(0.0, Degrees, 0.0));
}

void AMarburgumCharacterPreviewStage::SetFraming(float Aspect, float Zoom)
{
    const double T = FMath::Clamp(static_cast<double>(Zoom), 0.0, 1.0);
    const double HalfHeight = FMath::Lerp(FarHalfHeight, NearHalfHeight, T);
    const double Centre = FMath::Lerp(FarCentre, NearCentre, T);
    const double HalfFov = FMath::DegreesToRadians(VerticalFov * 0.5);
    Capture->SetRelativeLocation(FVector(HalfHeight / FMath::Tan(HalfFov), 0.0, Centre));
    Capture->SetRelativeRotation(FRotator(0.0, 180.0, 0.0));
    // The capture's angle is horizontal; derive it so the vertical framing holds whatever the picture's shape.
    Capture->FOVAngle = FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(HalfFov) * FMath::Max(Aspect, 0.1f)));
}

void AMarburgumCharacterPreviewStage::SetTarget(UTextureRenderTarget2D* Target)
{
    Capture->TextureTarget = Target;
}

void AMarburgumCharacterPreviewStage::SetCapturing(bool bCapturing)
{
    Capture->bCaptureEveryFrame = bCapturing;
    Body->SetComponentTickEnabled(bCapturing);
    Body->bPauseAnims = !bCapturing;
}

void UMarburgumCharacterPreview::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);

    Target = NewObject<UTextureRenderTarget2D>(this);
    Target->RenderTargetFormat = RTF_RGBA8_SRGB;
    Target->ClearColor = FLinearColor::Black;
    Target->InitAutoFormat(600, 900);
    Target->UpdateResourceImmediate(true);

    UOverlay* Layers = WidgetTree->ConstructWidget<UOverlay>();
    Picture = WidgetTree->ConstructWidget<UImage>();
    Picture->SetBrushResourceObject(Target);
    UOverlaySlot* PictureSlot = Layers->AddChildToOverlay(Picture);
    PictureSlot->SetHorizontalAlignment(HAlign_Fill);
    PictureSlot->SetVerticalAlignment(VAlign_Fill);
    UTextBlock* Hint = B.Text(LOCTEXT("DragHint", "Drag to turn  ·  scroll to look closer"), ETypeRole::Caption, T.Color.TextMuted);
    UOverlaySlot* HintSlot = Layers->AddChildToOverlay(Hint);
    HintSlot->SetHorizontalAlignment(HAlign_Center);
    HintSlot->SetVerticalAlignment(VAlign_Top); // over the dark backdrop, not the lit plinth
    HintSlot->SetPadding(FMargin(0.f, T.Space.L, 0.f, 0.f));

    WidgetTree->RootWidget = B.Surface(ESurfaceKind::Panel, FMargin(T.Space.XS), Layers);
    SetVisibility(ESlateVisibility::Visible); // receives the drag and the wheel
}

void UMarburgumCharacterPreview::NativeDestruct()
{
    if (AMarburgumCharacterPreviewStage* Existing = Stage.Get())
    {
        Existing->Destroy();
    }
    Stage.Reset();
    ShownModel = INDEX_NONE;
    bDragging = false;
    Super::NativeDestruct();
}

void UMarburgumCharacterPreview::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
    Super::NativeTick(Geometry, DeltaTime);
    if (!bActive || !Picture || !Target)
    {
        return;
    }
    // Render at the size the picture is drawn, so it is sharp at 720p and at 4K alike.
    const FVector2D Pixels = Picture->GetCachedGeometry().GetAbsoluteSize();
    if (Pixels.X < 16.0 || Pixels.Y < 16.0)
    {
        return;
    }
    const double Scale = FMath::Min(static_cast<double>(Supersample), TargetMax / FMath::Max(Pixels.X, Pixels.Y));
    const int32 Width = FMath::RoundToInt32(Pixels.X * Scale);
    const int32 Height = FMath::RoundToInt32(Pixels.Y * Scale);
    if (FMath::Abs(Width - Target->SizeX) > 8 || FMath::Abs(Height - Target->SizeY) > 8)
    {
        Target->ResizeTarget(Width, Height);
        Aspect = static_cast<float>(Pixels.X / Pixels.Y);
        if (AMarburgumCharacterPreviewStage* Current = Stage.Get())
        {
            Current->SetFraming(Aspect, Zoom);
        }
    }
}

AMarburgumCharacterPreviewStage* UMarburgumCharacterPreview::EnsureStage()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }
    if (AMarburgumCharacterPreviewStage* Existing = Stage.Get(); Existing && Existing->GetWorld() == World)
    {
        return Existing;
    }
    FActorSpawnParameters Parameters;
    Parameters.ObjectFlags = RF_Transient;
    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AMarburgumCharacterPreviewStage* Spawned = World->SpawnActor<AMarburgumCharacterPreviewStage>(StageLocation, FRotator::ZeroRotator, Parameters);
    if (Spawned)
    {
        Spawned->SetTarget(Target);
        Spawned->SetYaw(Yaw);
        Spawned->SetFraming(Aspect, Zoom);
    }
    Stage = Spawned;
    ShownModel = INDEX_NONE;
    return Spawned;
}

void UMarburgumCharacterPreview::ShowModel(int32 Index)
{
    WantedModel = Index;
    Refresh();
}

void UMarburgumCharacterPreview::SetActive(bool bInActive)
{
    bActive = bInActive;
    if (!bActive)
    {
        bDragging = false;
    }
    Refresh();
}

void UMarburgumCharacterPreview::Refresh()
{
    if (!bActive)
    {
        if (AMarburgumCharacterPreviewStage* Existing = Stage.Get())
        {
            Existing->SetCapturing(false);
        }
        return;
    }
    AMarburgumCharacterPreviewStage* Current = EnsureStage();
    if (!Current)
    {
        return;
    }
    if (WantedModel != ShownModel)
    {
        const bool bShown = Current->ShowModel(WantedModel);
        ShownModel = bShown ? WantedModel : INDEX_NONE;
        UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=preview_model model=%s shown=%d"),
            MarburgumII::Accounts::AdmissionLogPrefix, MarburgumII::Accounts::CharacterModels[WantedModel].Id, bShown ? 1 : 0);
    }
    Current->SetCapturing(true);
}

void UMarburgumCharacterPreview::Turn(float Pixels)
{
    Yaw = FRotator::NormalizeAxis(Yaw + Pixels * DegreesPerPixel);
    if (AMarburgumCharacterPreviewStage* Current = Stage.Get())
    {
        Current->SetYaw(Yaw);
    }
}

void UMarburgumCharacterPreview::ZoomBy(float Notches)
{
    Zoom = FMath::Clamp(Zoom + Notches * ZoomPerNotch, 0.f, 1.f);
    if (AMarburgumCharacterPreviewStage* Current = Stage.Get())
    {
        Current->SetFraming(Aspect, Zoom);
    }
}

FReply UMarburgumCharacterPreview::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
    if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return Super::NativeOnMouseButtonDown(Geometry, Event);
    }
    bDragging = true;
    return FReply::Handled().CaptureMouse(TakeWidget());
}

FReply UMarburgumCharacterPreview::NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
    if (!bDragging)
    {
        return Super::NativeOnMouseMove(Geometry, Event);
    }
    Turn(Event.GetCursorDelta().X);
    return FReply::Handled();
}

FReply UMarburgumCharacterPreview::NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
    if (!bDragging || Event.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return Super::NativeOnMouseButtonUp(Geometry, Event);
    }
    bDragging = false;
    return FReply::Handled().ReleaseMouseCapture();
}

FReply UMarburgumCharacterPreview::NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
    ZoomBy(Event.GetWheelDelta());
    return FReply::Handled();
}

void UMarburgumModelCard::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    const FTokens& T = Tokens();
    FBuilder B(*WidgetTree);
    Button = WidgetTree->ConstructWidget<UButton>();
    UVerticalBox* Lines = B.Column();
    NameText = B.Text(FText::GetEmpty(), ETypeRole::BodyStrong);
    FBuilder::Stack(Lines, NameText, T.Space.XXS);
    StateText = B.Text(LOCTEXT("Selected", "SELECTED"), ETypeRole::Overline, T.Color.Accent);
    FBuilder::Stack(Lines, StateText, 0.f);
    Button->SetContent(Lines);
    Button->OnClicked.AddDynamic(this, &UMarburgumModelCard::HandleClicked);
    WidgetTree->RootWidget = Button;
    SetSelected(false);
}

void UMarburgumModelCard::Setup(int32 InIndex, const FText& Name)
{
    Index = InIndex;
    NameText->SetText(Name);
}

void UMarburgumModelCard::SetSelected(bool bSelected)
{
    const FTokens& T = Tokens();
    const FColors& C = T.Color;
    const float R = T.Radius.Medium;
    // The chosen card is filled and outlined in copper and says so; the others are quiet outlines.
    const FLinearColor Outline = bSelected ? C.Accent : C.BorderStrong;
    const float Width = bSelected ? T.FocusWidth : T.BorderWidth;
    const FLinearColor Fill = bSelected ? C.Raised : FLinearColor::Transparent;
    FButtonStyle Style;
    Style.SetNormal(RoundedBrush(Fill, R, Outline, Width))
        .SetHovered(RoundedBrush(C.Raised, R, C.Accent, Width))
        .SetPressed(RoundedBrush(C.Panel, R, C.AccentPressed, Width))
        .SetDisabled(RoundedBrush(FLinearColor::Transparent, R, C.Disabled, T.BorderWidth));
    const FMargin Padding(T.Space.L, T.Space.M);
    Style.SetNormalPadding(Padding).SetPressedPadding(Padding);
    Button->SetStyle(Style);
    StateText->SetVisibility(bSelected ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
}

void UMarburgumModelCard::HandleClicked()
{
    OnPicked.Broadcast(Index);
}

#undef LOCTEXT_NAMESPACE
