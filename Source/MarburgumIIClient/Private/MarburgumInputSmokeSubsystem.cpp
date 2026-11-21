#include "MarburgumInputSmokeSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Pawn.h"
#include "GenericPlatform/GenericApplicationMessageHandler.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "MarburgumGameplayContract.h"
#include "MarburgumInteractable.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPhotography.h"
#include "MarburgumPlayerController.h"
#include "MarburgumPlayerState.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "UI/MarburgumChoicePanel.h"
#include "UI/MarburgumInputDevice.h"
#include "UI/MarburgumInteractionPrompt.h"
#include "UI/MarburgumUISubsystem.h"
#include "UI/MarburgumUIWidgets.h"
#include "UnrealClient.h"

#define MB2_INPUT_LOG(Format, ...) \
    UE_LOG(LogMarburgumNet, Log, TEXT("MB2INPUT role=client tag=%s " Format), *Role, ##__VA_ARGS__)

namespace MarburgumInputSmoke
{
    constexpr double InputSettleSeconds = 2.0;
    constexpr double InputShotTimeoutSeconds = 10.0;
    /** Prompts refresh at 10 Hz; three refreshes is plenty for a device switch to show. */
    constexpr double InputHintSwitchSeconds = 0.35;
    /** Long enough that a second answer, had one been sent, would have its receipt back from a local server. */
    constexpr double InputQuietSeconds = 1.5;
    /** Close enough to photograph a document on a counter: inside every subject's within_m (3.5 m) with room to spare. */
    constexpr float InputApproachCm = 200.f;
    const TCHAR* const InputPhotographySkill = TEXT("photography");

    const TCHAR* InputDeviceName(EMarburgumInputDevice Device)
    {
        return Device == EMarburgumInputDevice::Gamepad ? TEXT("gamepad") : TEXT("keyboard_mouse");
    }

    /** A push the pad's dead zone (0.25) passes, larger the further off the aim is, nothing once it is on. */
    float InputStickFor(float ErrorDegrees)
    {
        const float Magnitude = FMath::Abs(ErrorDegrees);
        return Magnitude < 0.6f ? 0.f : FMath::Sign(ErrorDegrees) * FMath::Clamp(0.32f + Magnitude / 25.f, 0.32f, 1.f);
    }
}

using namespace MarburgumInputSmoke;
using MarburgumII::Gameplay::ToString;

bool UMarburgumInputSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
    static_cast<void>(Outer);
    return false; // Test harnesses never run in a player build.
#else
    const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
    return GameInstance && !GameInstance->IsDedicatedServerInstance() && FParse::Param(FCommandLine::Get(), TEXT("MB2InputSmoke"));
#endif
}

void UMarburgumInputSmokeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const TCHAR* CommandLine = FCommandLine::Get();
    FParse::Value(CommandLine, TEXT("MB2InputRole="), Role);
    FParse::Value(CommandLine, TEXT("MB2InputShot="), ShotPrefix);
    FParse::Value(CommandLine, TEXT("MB2InputTimeout="), TimeoutSeconds);
    FString Text;
    if (FParse::Value(CommandLine, TEXT("MB2InputTarget="), Text)) TargetId = FName(*Text);
    if (FParse::Value(CommandLine, TEXT("MB2InputWrong="), Text)) WrongChoice = FName(*Text);
    if (FParse::Value(CommandLine, TEXT("MB2InputRight="), Text)) RightChoice = FName(*Text);
    Role = Role.ToUpper();
    StartSeconds = FPlatformTime::Seconds();
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMarburgumInputSmokeSubsystem::Tick), 0.f);
    MB2_INPUT_LOG("event=start target=%s wrong=%s right=%s shot=%d", *TargetId.ToString(), *WrongChoice.ToString(), *RightChoice.ToString(),
        ShotPrefix.IsEmpty() ? 0 : 1);
}

void UMarburgumInputSmokeSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    if (AMarburgumPlayerController* Bound = Controller.Get())
    {
        Bound->OnInteractionReceipt.RemoveAll(this);
    }
    Super::Deinitialize();
}

bool UMarburgumInputSmokeSubsystem::Bind()
{
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (!UMarburgumSessionSubsystem::IsAdmitted(World) || !UI() || !UI()->GetInteractionPrompt() || !UI()->GetChoicePanel())
    {
        return false;
    }
    AMarburgumPlayerController* Local = Cast<AMarburgumPlayerController>(World->GetFirstPlayerController());
    const AMarburgumPlayerState* State = Local ? Local->GetPlayerState<AMarburgumPlayerState>() : nullptr;
    if (!State || !State->IsInventoryKnown() || !State->AreMissionsKnown() || !State->IsProgressKnown() || !Local->GetPawn())
    {
        return false;
    }
    if (Controller.Get() != Local)
    {
        Local->OnInteractionReceipt.AddUObject(this, &UMarburgumInputSmokeSubsystem::HandleReceipt);
        Controller = Local;
        FString Missions;
        for (const FMarburgumMissionState& Mission : State->GetMissions())
        {
            if (!Mission.bCompleted)
            {
                Missions += FString::Printf(TEXT("%s:%s,"), *Mission.MissionId.ToString(), *Mission.StageId.ToString());
            }
        }
        const UGameViewportClient* GameViewport = World->GetGameViewport();
        const FIntPoint Size = GameViewport && GameViewport->Viewport ? GameViewport->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
        MB2_INPUT_LOG("event=ready map=%s player=%d open_missions=%s photography_xp=%lld money=%lld viewport=%dx%d culture=%s", *World->GetMapName(),
            State->GetPlayerId(), *Missions, State->GetSkillXp(InputPhotographySkill), State->GetMoney(), Size.X, Size.Y,
            *FInternationalization::Get().GetCurrentCulture()->GetName());
        BuildSteps();
        StepIndex = 0;
        bStepStarted = false;
    }
    return true;
}

UMarburgumUISubsystem* UMarburgumInputSmokeSubsystem::UI() const
{
    return GetGameInstance() ? GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>() : nullptr;
}

const AMarburgumPlayerState* UMarburgumInputSmokeSubsystem::LocalState() const
{
    const AMarburgumPlayerController* Local = Controller.Get();
    return Local ? Local->GetPlayerState<AMarburgumPlayerState>() : nullptr;
}

void UMarburgumInputSmokeSubsystem::HandleReceipt(const FMarburgumInteractionReceipt& Receipt)
{
    Receipts.Add(Receipt);
    MB2_INPUT_LOG("event=receipt n=%d target=%s outcome=%s reason=%s effect=%s mission=%s stage=%s choice=%s xp_awards=%d",
        Receipts.Num(), *Receipt.InteractableId.ToString(), ToString(Receipt.Outcome), ToString(Receipt.Reason), ToString(Receipt.Effect),
        *Receipt.MissionId.ToString(), *Receipt.MissionStage.ToString(), *Receipt.Choice.ToString(), Receipt.SkillXp.Num());
}

int32 UMarburgumInputSmokeSubsystem::ChoiceReceipts(FName Choice) const
{
    int32 Count = 0;
    for (const FMarburgumInteractionReceipt& Receipt : Receipts)
    {
        Count += !Receipt.Choice.IsNone() && (Choice.IsNone() || Receipt.Choice == Choice) ? 1 : 0;
    }
    return Count;
}

const FMarburgumInteractionReceipt* UMarburgumInputSmokeSubsystem::LatestReceipt() const
{
    return Receipts.IsEmpty() ? nullptr : &Receipts.Last();
}

bool UMarburgumInputSmokeSubsystem::IsWorldInput() const
{
    const AMarburgumPlayerController* Local = Controller.Get();
    const UWorld* World = Local ? Local->GetWorld() : nullptr;
    UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
    return Local && Viewport && !Viewport->IgnoreInput() && !Local->ShouldShowMouseCursor();
}

bool UMarburgumInputSmokeSubsystem::AimError(float& OutYaw, float& OutPitch, FString& OutInFrame, float& OutOffAxis) const
{
    const AMarburgumPlayerController* Local = Controller.Get();
    const APawn* Body = Local ? Local->GetPawn() : nullptr;
    const UMarburgumPhotographySubsystem* Photos = Body ? Body->GetWorld()->GetSubsystem<UMarburgumPhotographySubsystem>() : nullptr;
    FVector Aim;
    if (!Photos || !Photos->GetAimPoint(TargetId.ToString(), Aim))
    {
        return false;
    }
    const FVector Eye = Body->GetPawnViewLocation();
    const FRotator Want = (Aim - Eye).Rotation();
    const FRotator Have = Local->GetControlRotation();
    OutYaw = FRotator::NormalizeAxis(Want.Yaw - Have.Yaw);
    OutPitch = FRotator::NormalizeAxis(Want.Pitch - Have.Pitch);
    const UMarburgumPhotographySubsystem::FInFrame Frame = Photos->FindInFrame(Eye, Have, Body);
    OutInFrame = Frame.Subject ? Frame.Subject->Id : TEXT("-");
    OutOffAxis = Frame.OffAxisDegrees;
    return true;
}

// ---- The hands ----

void UMarburgumInputSmokeSubsystem::PadPress(FName Button, bool bRepeat)
{
    IPlatformInputDeviceMapper& Mapper = IPlatformInputDeviceMapper::Get();
    FSlateApplication::Get().OnControllerButtonPressed(Button, Mapper.GetPrimaryPlatformUser(), Mapper.GetDefaultInputDevice(), bRepeat);
    if (!bRepeat)
    {
        MB2_INPUT_LOG("event=pad_press button=%s", *Button.ToString());
    }
}

void UMarburgumInputSmokeSubsystem::PadRelease(FName Button)
{
    IPlatformInputDeviceMapper& Mapper = IPlatformInputDeviceMapper::Get();
    FSlateApplication::Get().OnControllerButtonReleased(Button, Mapper.GetPrimaryPlatformUser(), Mapper.GetDefaultInputDevice(), false);
}

void UMarburgumInputSmokeSubsystem::PadAxis(FName Axis, float Value)
{
    // Sent once at the end of the tick (SendAxes): a stick keeps its last sample, so two samples of one axis in one frame
    // would leave the first one standing.
    AxesThisTick.Add(Axis, FMath::IsNearlyZero(Value) ? 0.f : Value);
}

void UMarburgumInputSmokeSubsystem::SendAxes()
{
    IPlatformInputDeviceMapper& Mapper = IPlatformInputDeviceMapper::Get();
    FSlateApplication& App = FSlateApplication::Get();
    for (const TPair<FName, float>& Axis : AxesThisTick)
    {
        if (Axis.Value != 0.f || HeldAxes.Contains(Axis.Key))
        {
            App.OnControllerAnalog(Axis.Key, Mapper.GetPrimaryPlatformUser(), Mapper.GetDefaultInputDevice(), Axis.Value);
        }
        if (Axis.Value == 0.f)
        {
            HeldAxes.Remove(Axis.Key);
        }
        else
        {
            HeldAxes.Add(Axis.Key, Axis.Value);
        }
    }
    // A pad reports a stick that is off centre every frame.
    for (const TPair<FName, float>& Axis : HeldAxes)
    {
        if (!AxesThisTick.Contains(Axis.Key))
        {
            App.OnControllerAnalog(Axis.Key, Mapper.GetPrimaryPlatformUser(), Mapper.GetDefaultInputDevice(), Axis.Value);
        }
    }
    AxesThisTick.Reset();
}

void UMarburgumInputSmokeSubsystem::KeyDown(const FKey& Key, bool bRepeat)
{
    FSlateApplication& App = FSlateApplication::Get();
    App.ProcessKeyDownEvent(FKeyEvent(Key, App.GetModifierKeys(), 0, bRepeat, 0, 0));
    if (!bRepeat)
    {
        MB2_INPUT_LOG("event=key_down key=%s", *Key.ToString());
    }
}

void UMarburgumInputSmokeSubsystem::KeyUp(const FKey& Key)
{
    FSlateApplication& App = FSlateApplication::Get();
    App.ProcessKeyUpEvent(FKeyEvent(Key, App.GetModifierKeys(), 0, false, 0, 0));
}

void UMarburgumInputSmokeSubsystem::PointerMove(const FVector2D& Screen)
{
    FSlateApplication& App = FSlateApplication::Get();
    const FVector2D From = PointerAt.X < 0.0 ? Screen - FVector2D(12.0, 8.0) : PointerAt;
    PointerAt = Screen;
    App.ProcessMouseMoveEvent(FPointerEvent(App.GetUserIndexForMouse(), FSlateApplicationBase::CursorPointerIndex, Screen, From,
        PointerButtons, EKeys::Invalid, 0.f, App.GetModifierKeys()));
}

void UMarburgumInputSmokeSubsystem::PointerButton(bool bDown, bool bDoubleClick)
{
    FSlateApplication& App = FSlateApplication::Get();
    if (bDown)
    {
        PointerButtons.Add(EKeys::LeftMouseButton);
    }
    else
    {
        PointerButtons.Remove(EKeys::LeftMouseButton);
    }
    const FPointerEvent Event(App.GetUserIndexForMouse(), FSlateApplicationBase::CursorPointerIndex, PointerAt, PointerAt, PointerButtons,
        EKeys::LeftMouseButton, 0.f, App.GetModifierKeys());
    if (bDoubleClick)
    {
        App.ProcessMouseButtonDoubleClickEvent(nullptr, Event);
    }
    else if (bDown)
    {
        App.ProcessMouseButtonDownEvent(nullptr, Event);
    }
    else
    {
        App.ProcessMouseButtonUpEvent(Event);
    }
    MB2_INPUT_LOG("event=pointer_button down=%d double=%d x=%.0f y=%.0f", bDown ? 1 : 0, bDoubleClick ? 1 : 0, PointerAt.X, PointerAt.Y);
}

void UMarburgumInputSmokeSubsystem::RawMouse(int32 X, int32 Y)
{
    // What the platform layer reports for a mouse moved while the street has the cursor captured.
    FSlateApplication::Get().OnRawMouseMove(X, Y);
}

bool UMarburgumInputSmokeSubsystem::PointerToRow(int32 Number)
{
    const UMarburgumChoicePanel* Panel = UI() ? UI()->GetChoicePanel() : nullptr;
    const UMarburgumSelectable* Row = Panel ? Panel->GetRow(Number) : nullptr;
    if (!Row)
    {
        return false;
    }
    const FGeometry& Geometry = Row->GetCachedGeometry();
    const FVector2D Centre = FVector2D(Geometry.GetAbsolutePositionAtCoordinates(FVector2f(0.5f, 0.5f)));
    PointerMove(Centre);
    MB2_INPUT_LOG("event=pointer_move row=%d x=%.0f y=%.0f size=%.0fx%.0f", Number, Centre.X, Centre.Y, Geometry.GetAbsoluteSize().X,
        Geometry.GetAbsoluteSize().Y);
    return true;
}

// ---- Steps ----

void UMarburgumInputSmokeSubsystem::AddStep(const TCHAR* Name, TFunction<bool(double)> Run, double StepTimeout)
{
    Steps.Add({ Name, MoveTemp(Run), StepTimeout });
}

void UMarburgumInputSmokeSubsystem::AddWait(double Seconds)
{
    AddStep(TEXT("wait"), [Seconds](double Elapsed) { return Elapsed >= Seconds; }, Seconds + 5.0);
}

void UMarburgumInputSmokeSubsystem::AddShot(const TCHAR* Label)
{
    const FString Name = Label;
    AddStep(TEXT("shot"), [this, Name](double)
    {
        if (!ShotPrefix.IsEmpty())
        {
            PendingShot = FString::Printf(TEXT("%s-%s.png"), *ShotPrefix, *Name);
            IFileManager::Get().Delete(*PendingShot, false, true, true);
            FScreenshotRequest::RequestScreenshot(PendingShot, true, false);
            ShotRequestedSeconds = FPlatformTime::Seconds();
        }
        return true;
    });
}

void UMarburgumInputSmokeSubsystem::AddPadTap(FName Button)
{
    AddStep(TEXT("pad_down"), [this, Button](double) { PadPress(Button); return true; });
    AddWait(0.08);
    AddStep(TEXT("pad_up"), [this, Button](double) { PadRelease(Button); return true; });
    AddWait(0.15);
}

void UMarburgumInputSmokeSubsystem::AddKeyTap(const FKey& Key)
{
    AddStep(TEXT("key_down"), [this, Key](double) { KeyDown(Key); return true; });
    AddWait(0.08);
    AddStep(TEXT("key_up"), [this, Key](double) { KeyUp(Key); return true; });
    AddWait(0.15);
}

void UMarburgumInputSmokeSubsystem::AddOpenCard(bool bGamepad)
{
    AddStep(TEXT("before_open"), [this](double)
    {
        Expect(!UI()->IsChoiceOpen(), TEXT("card_open_before_open"));
        ReceiptsAtStep = Receipts.Num();
        return true;
    });
    if (bGamepad)
    {
        AddPadTap(FGamepadKeyNames::FaceButtonBottom);
    }
    else
    {
        AddKeyTap(EKeys::E);
    }
    AddStep(TEXT("card_open"), [this, bGamepad](double Elapsed)
    {
        const UMarburgumChoicePanel* Panel = UI()->GetChoicePanel();
        if (!UI()->IsChoiceOpen())
        {
            return false;
        }
        const EMarburgumInputDevice Want = bGamepad ? EMarburgumInputDevice::Gamepad : EMarburgumInputDevice::KeyboardMouse;
        MB2_INPUT_LOG("event=card_open mission=%s choices=%d focus=%d hints=%s world_input=%d", *Panel->GetShownMission().ToString(),
            Panel->GetShownChoiceCount(), Panel->GetFocusedNumber(), InputDeviceName(Panel->GetHintDevice()), IsWorldInput() ? 1 : 0);
        Expect(Panel->GetHintDevice() == Want, FString::Printf(TEXT("card_hints=%s"), InputDeviceName(Panel->GetHintDevice())));
        Expect(Panel->GetFocusedNumber() == 1, FString::Printf(TEXT("card_focus=%d"), Panel->GetFocusedNumber()));
        Expect(!IsWorldInput(), TEXT("card_open_but_world_has_input"));
        Expect(Receipts.Num() == ReceiptsAtStep, TEXT("opening_the_card_sent_something"));
        return true;
    }, 4.0);
}

void UMarburgumInputSmokeSubsystem::AddExpectWorldInput(const TCHAR* Label, bool bWalk)
{
    const FString Name = Label;
    AddStep(TEXT("world_input"), [this, Name](double Elapsed)
    {
        if (!IsWorldInput() && Elapsed < 1.0)
        {
            return false;
        }
        MB2_INPUT_LOG("event=world_input at=%s ok=%d card_open=%d", *Name, IsWorldInput() ? 1 : 0, UI()->IsChoiceOpen() ? 1 : 0);
        Expect(IsWorldInput() && !UI()->IsChoiceOpen(), FString::Printf(TEXT("world_input_not_restored_%s"), *Name));
        return true;
    });
    if (!bWalk)
    {
        return;
    }
    // The left stick walks again: the street really has the pad back. A short step forward and back, so the same
    // object stays the nearest one in reach. Out until 8 cm or a second, whichever comes first, so a frame hitch
    // cannot shorten the step; back for as long as it went out.
    AddStep(TEXT("stick_walk"), [this](double Elapsed)
    {
        const APawn* Body = Controller->GetPawn();
        if (Elapsed == 0.0 || PlaceBefore.IsZero())
        {
            PlaceBefore = Body->GetActorLocation();
            WalkTurnSeconds = -1.0;
        }
        if (WalkTurnSeconds < 0.0)
        {
            MovedCm = FMath::Max(MovedCm, static_cast<float>(FVector::Dist2D(Body->GetActorLocation(), PlaceBefore)));
            if (MovedCm >= 8.f || Elapsed >= 1.0)
            {
                WalkTurnSeconds = Elapsed;
            }
        }
        const double Back = WalkTurnSeconds < 0.0 ? 0.0 : Elapsed - WalkTurnSeconds;
        PadAxis(FGamepadKeyNames::LeftAnalogY, WalkTurnSeconds < 0.0 ? 0.6f : (Back < WalkTurnSeconds ? -0.6f : 0.f));
        if (WalkTurnSeconds < 0.0 || Back < WalkTurnSeconds + 0.4)
        {
            return false;
        }
        MB2_INPUT_LOG("event=stick_walk moved_cm=%.0f out_s=%.2f", MovedCm, WalkTurnSeconds);
        Expect(MovedCm >= 8.f, FString::Printf(TEXT("stick_did_not_walk moved_cm=%.0f"), MovedCm));
        PlaceBefore = FVector::ZeroVector;
        MovedCm = 0.f;
        WalkTurnSeconds = -1.0;
        return true;
    });
}

void UMarburgumInputSmokeSubsystem::BuildSteps()
{
    Steps.Reset();
    AddWait(InputSettleSeconds);
    if (Role == TEXT("COUNTER"))
    {
        // Left stick: forward half a second, then back the same, so the counter stays in reach.
        AddStep(TEXT("walk"), [this](double Elapsed)
        {
            const APawn* Body = Controller->GetPawn();
            if (Elapsed == 0.0 || PlaceBefore.IsZero())
            {
                PlaceBefore = Body->GetActorLocation();
                MovedCm = 0.f;
            }
            PadAxis(FGamepadKeyNames::LeftAnalogY, Elapsed < 0.4 ? 0.8f : (Elapsed < 0.8 ? -0.8f : 0.f));
            if (Elapsed < 0.4)
            {
                MovedCm = FMath::Max(MovedCm, static_cast<float>(FVector::Dist2D(Body->GetActorLocation(), PlaceBefore)));
            }
            if (Elapsed < 1.2)
            {
                return false;
            }
            MB2_INPUT_LOG("event=stick_walk moved_cm=%.0f back_cm=%.0f device=%s", MovedCm,
                FVector::Dist2D(Body->GetActorLocation(), PlaceBefore), InputDeviceName(MarburgumUI::ActiveInputDevice()));
            Expect(MovedCm >= 20.f, FString::Printf(TEXT("left_stick_did_not_walk moved_cm=%.0f"), MovedCm));
            Expect(MarburgumUI::ActiveInputDevice() == EMarburgumInputDevice::Gamepad, TEXT("stick_not_noted_as_gamepad"));
            PlaceBefore = FVector::ZeroVector;
            MovedCm = 0.f;
            return true;
        }, 5.0);
        // The mouse turns the view (raw movement, as with the cursor captured by the street).
        AddStep(TEXT("mouse_look"), [this](double Elapsed)
        {
            if (Elapsed == 0.0)
            {
                YawBefore = Controller->GetControlRotation().Yaw;
                PitchBefore = FRotator::NormalizeAxis(Controller->GetControlRotation().Pitch);
            }
            if (Elapsed < 0.25)
            {
                // Right, and a little away from the player (screen up), as a hand moves a mouse.
                RawMouse(8, -2);
                return false;
            }
            if (Elapsed < 0.5)
            {
                return false;
            }
            const float Turned = FRotator::NormalizeAxis(Controller->GetControlRotation().Yaw - YawBefore);
            const float Raised = FRotator::NormalizeAxis(Controller->GetControlRotation().Pitch) - PitchBefore;
            MB2_INPUT_LOG("event=mouse_look turned_deg=%.1f pitch_change_deg=%.1f (mouse moved away from the player) device=%s", Turned, Raised,
                InputDeviceName(MarburgumUI::ActiveInputDevice()));
            Expect(FMath::Abs(Turned) > 1.f, FString::Printf(TEXT("mouse_did_not_turn turned=%.2f"), Turned));
            Expect(MarburgumUI::ActiveInputDevice() == EMarburgumInputDevice::KeyboardMouse, TEXT("mouse_not_noted"));
            return true;
        }, 3.0);
        // Right stick up looks up (the usual, non-inverted pad convention).
        AddStep(TEXT("stick_up"), [this](double Elapsed)
        {
            if (Elapsed == 0.0)
            {
                PitchBefore = FRotator::NormalizeAxis(Controller->GetControlRotation().Pitch);
            }
            PadAxis(FGamepadKeyNames::RightAnalogY, Elapsed < 0.3 ? 0.7f : 0.f);
            if (Elapsed < 0.5)
            {
                return false;
            }
            const float Raised = FRotator::NormalizeAxis(Controller->GetControlRotation().Pitch) - PitchBefore;
            MB2_INPUT_LOG("event=stick_up pitch_change_deg=%.1f", Raised);
            Expect(Raised > 1.f, FString::Printf(TEXT("stick_up_did_not_look_up change=%.1f"), Raised));
            return true;
        });
        // Up to the counter and the page framed, by eye: the right stick turns towards it, the left stick walks while it is
        // further than arm's length of the counter, then the right stick eases the frame onto it.
        AddStep(TEXT("stick_aim"), [this](double Elapsed)
        {
            float Yaw = 0.f, Pitch = 0.f, OffAxis = 0.f;
            FString InFrame;
            FVector Aim;
            const APawn* Body = Controller->GetPawn();
            const UMarburgumPhotographySubsystem* Photos = Body->GetWorld()->GetSubsystem<UMarburgumPhotographySubsystem>();
            if (!AimError(Yaw, Pitch, InFrame, OffAxis) || !Photos->GetAimPoint(TargetId.ToString(), Aim))
            {
                return false;
            }
            const float Distance = static_cast<float>(FVector::Dist(Body->GetPawnViewLocation(), Aim));
            const bool bFar = Distance > InputApproachCm;
            const bool bOn = !bFar && FMath::Abs(Yaw) < 0.8f && FMath::Abs(Pitch) < 0.8f && InFrame == TargetId.ToString();
            PadAxis(FGamepadKeyNames::LeftAnalogY, bFar && FMath::Abs(Yaw) < 20.f ? 0.6f : 0.f);
            PadAxis(FGamepadKeyNames::RightAnalogX, bOn ? 0.f : InputStickFor(Yaw));
            PadAxis(FGamepadKeyNames::RightAnalogY, bOn || bFar ? 0.f : InputStickFor(Pitch));
            if (static_cast<int32>(Elapsed) != AimLogSecond)
            {
                AimLogSecond = static_cast<int32>(Elapsed);
                const FRotator View = Controller->GetControlRotation();
                MB2_INPUT_LOG("event=aiming distance_m=%.2f yaw_err=%.1f pitch_err=%.1f view_yaw=%.1f view_pitch=%.1f in_frame=%s off_axis=%.1f",
                    Distance / 100.f, Yaw, Pitch, View.Yaw, FRotator::NormalizeAxis(View.Pitch), *InFrame, OffAxis);
            }
            if (!bOn)
            {
                return false;
            }
            MB2_INPUT_LOG("event=stick_aim subject=%s in_frame=%s off_axis=%.1f distance_m=%.2f yaw_err=%.2f pitch_err=%.2f after_s=%.1f",
                *TargetId.ToString(), *InFrame, OffAxis, Distance / 100.f, Yaw, Pitch, Elapsed);
            return true;
        }, 30.0);
        // The view reaches the server with the next moves.
        AddWait(1.0);
        AddStep(TEXT("prompt_pad"), [this](double)
        {
            const UMarburgumInteractionPrompt* Prompt = UI()->GetInteractionPrompt();
            MB2_INPUT_LOG("event=prompt device=gamepad shown=%d key=%s verb=%s subject=%s", Prompt->IsCardShown() ? 1 : 0, *Prompt->GetShownKey(),
                *Prompt->GetShownVerb(), *Prompt->GetShownSubject());
            Expect(Prompt->IsCardShown() && Prompt->GetShownKey() == TEXT("RT"), FString::Printf(TEXT("photo_prompt_pad key=%s"), *Prompt->GetShownKey()));
            return true;
        });
        AddShot(TEXT("prompt-pad"));
        // A nudge of the mouse in the middle of it: the same card names P.
        AddStep(TEXT("nudge_mouse"), [this](double) { RawMouse(1, 0); return true; });
        AddWait(InputHintSwitchSeconds);
        AddStep(TEXT("prompt_kbm"), [this](double)
        {
            const UMarburgumInteractionPrompt* Prompt = UI()->GetInteractionPrompt();
            MB2_INPUT_LOG("event=prompt device=keyboard_mouse shown=%d key=%s verb=%s subject=%s", Prompt->IsCardShown() ? 1 : 0,
                *Prompt->GetShownKey(), *Prompt->GetShownVerb(), *Prompt->GetShownSubject());
            Expect(Prompt->IsCardShown() && Prompt->GetShownKey() == TEXT("P"), FString::Printf(TEXT("photo_prompt_kbm key=%s"), *Prompt->GetShownKey()));
            return true;
        });
        AddShot(TEXT("prompt-kbm"));
        // A pad button that does nothing in the street: the card names RT again.
        AddPadTap(FGamepadKeyNames::DPadUp);
        AddWait(InputHintSwitchSeconds);
        AddStep(TEXT("prompt_pad_again"), [this](double)
        {
            const UMarburgumInteractionPrompt* Prompt = UI()->GetInteractionPrompt();
            Expect(Prompt->GetShownKey() == TEXT("RT"), FString::Printf(TEXT("photo_prompt_pad_again key=%s"), *Prompt->GetShownKey()));
            float Yaw = 0.f, Pitch = 0.f, OffAxis = 0.f;
            FString InFrame;
            AimError(Yaw, Pitch, InFrame, OffAxis);
            MB2_INPUT_LOG("event=prompt device=gamepad key=%s in_frame=%s off_axis=%.1f", *Prompt->GetShownKey(), *InFrame, OffAxis);
            XpBefore = LocalState()->GetSkillXp(InputPhotographySkill);
            ReceiptsAtStep = Receipts.Num();
            return true;
        });
        // RT: the trigger's travel and its click, as a pad reports a full pull.
        AddStep(TEXT("shutter"), [this](double Elapsed)
        {
            if (Elapsed == 0.0)
            {
                PadAxis(FGamepadKeyNames::RightTriggerAnalog, 1.f);
                PadPress(FGamepadKeyNames::RightTriggerThreshold);
                return false;
            }
            if (Elapsed < 0.12)
            {
                return false;
            }
            PadRelease(FGamepadKeyNames::RightTriggerThreshold);
            PadAxis(FGamepadKeyNames::RightTriggerAnalog, 0.f);
            return true;
        });
        AddStep(TEXT("photo_receipt"), [this](double)
        {
            if (Receipts.Num() <= ReceiptsAtStep)
            {
                return false;
            }
            const FMarburgumInteractionReceipt& R = Receipts.Last();
            Expect(R.Outcome == EMarburgumRequestOutcome::Applied && R.Effect == EMarburgumInteractionEffect::StageCompleted && R.InteractableId == TargetId,
                FString::Printf(TEXT("photo=%s/%s/%s subject=%s"), ToString(R.Outcome), ToString(R.Reason), ToString(R.Effect), *R.InteractableId.ToString()));
            return true;
        }, 8.0);
        AddWait(1.2);
        AddStep(TEXT("photo_state"), [this](double)
        {
            // The print is the subject's catalogue item, which need not be photo_<subject>.
            const MarburgumII::Gameplay::FPhotoSubjectDefinition* Subject = MarburgumII::Gameplay::FindPhotoSubject(TargetId.ToString());
            const FString Print = Subject ? FString(Subject->Item) : FString();
            MB2_INPUT_LOG("event=photo_state photography_xp=%lld before=%lld print=%d", LocalState()->GetSkillXp(InputPhotographySkill), XpBefore,
                LocalState()->CountItem(FName(*Print)));
            Expect(LocalState()->CountItem(FName(*Print)) == 1, TEXT("no_print_in_custody"));
            return true;
        });
        AddShot(TEXT("photo-taken"));
        // View opens the Journal, B closes it; Y opens the Inventory, B closes it.
        AddPadTap(FGamepadKeyNames::SpecialLeft);
        AddStep(TEXT("journal_open"), [this](double Elapsed)
        {
            if (!UI()->IsJournalOpen() && Elapsed < 1.5) return false;
            Expect(UI()->IsJournalOpen(), TEXT("view_did_not_open_journal"));
            return true;
        });
        AddWait(0.6);
        AddShot(TEXT("journal-pad"));
        AddPadTap(FGamepadKeyNames::FaceButtonRight);
        AddStep(TEXT("journal_closed"), [this](double Elapsed)
        {
            if (UI()->IsJournalOpen() && Elapsed < 1.5) return false;
            Expect(!UI()->IsJournalOpen() && IsWorldInput(), TEXT("b_did_not_close_journal"));
            return true;
        });
        AddPadTap(FGamepadKeyNames::FaceButtonTop);
        AddStep(TEXT("inventory_open"), [this](double Elapsed)
        {
            if (!UI()->IsInventoryOpen() && Elapsed < 1.5) return false;
            Expect(UI()->IsInventoryOpen(), TEXT("y_did_not_open_inventory"));
            return true;
        });
        AddWait(0.6);
        AddShot(TEXT("inventory-pad"));
        AddPadTap(FGamepadKeyNames::FaceButtonRight);
        AddExpectWorldInput(TEXT("inventory_closed"));
    }
    else if (Role == TEXT("LOUPE"))
    {
        AddStep(TEXT("prompt"), [this](double)
        {
            const UMarburgumInteractionPrompt* Prompt = UI()->GetInteractionPrompt();
            MB2_INPUT_LOG("event=prompt shown=%d key=%s verb=%s subject=%s", Prompt->IsCardShown() ? 1 : 0, *Prompt->GetShownKey(), *Prompt->GetShownVerb(),
                *Prompt->GetShownSubject());
            ReceiptsAtStep = Receipts.Num();
            return true;
        });
        AddPadTap(FGamepadKeyNames::DPadUp);
        AddWait(InputHintSwitchSeconds);
        AddStep(TEXT("prompt_pad"), [this](double)
        {
            const UMarburgumInteractionPrompt* Prompt = UI()->GetInteractionPrompt();
            Expect(Prompt->GetShownKey() == TEXT("A"), FString::Printf(TEXT("loupe_prompt_key=%s"), *Prompt->GetShownKey()));
            return true;
        });
        AddShot(TEXT("loupe-prompt-pad"));
        AddPadTap(FGamepadKeyNames::FaceButtonBottom);
        AddStep(TEXT("loupe_receipt"), [this](double)
        {
            if (Receipts.Num() <= ReceiptsAtStep) return false;
            const FMarburgumInteractionReceipt& R = Receipts.Last();
            Expect(R.Outcome == EMarburgumRequestOutcome::Applied && R.Effect == EMarburgumInteractionEffect::StageCompleted && R.InteractableId == TargetId,
                FString::Printf(TEXT("loupe=%s/%s/%s target=%s"), ToString(R.Outcome), ToString(R.Reason), ToString(R.Effect), *R.InteractableId.ToString()));
            return true;
        }, 8.0);
        AddWait(1.0);
        AddShot(TEXT("loupe-done"));
    }
    else if (Role == TEXT("CANCEL"))
    {
        // A pressed and held: the card opens on the press, the repeats and the release send nothing.
        AddStep(TEXT("before"), [this](double) { ReceiptsAtStep = Receipts.Num(); return true; });
        AddStep(TEXT("hold_a"), [this](double Elapsed)
        {
            if (Elapsed == 0.0)
            {
                PadPress(FGamepadKeyNames::FaceButtonBottom);
                return false;
            }
            if (Elapsed < 1.4)
            {
                // The platform's repeat for a held button.
                PadPress(FGamepadKeyNames::FaceButtonBottom, true);
                return false;
            }
            PadRelease(FGamepadKeyNames::FaceButtonBottom);
            return true;
        });
        AddWait(InputQuietSeconds);
        AddStep(TEXT("held_a_sent_nothing"), [this](double)
        {
            const UMarburgumChoicePanel* Panel = UI()->GetChoicePanel();
            MB2_INPUT_LOG("event=held_confirm card_open=%d receipts=%d focus=%d hints=%s", UI()->IsChoiceOpen() ? 1 : 0, Receipts.Num() - ReceiptsAtStep,
                Panel->GetFocusedNumber(), InputDeviceName(Panel->GetHintDevice()));
            Expect(UI()->IsChoiceOpen(), TEXT("held_a_did_not_open_or_closed_card"));
            Expect(Receipts.Num() == ReceiptsAtStep, TEXT("held_a_sent_an_answer"));
            Expect(Panel->GetHintDevice() == EMarburgumInputDevice::Gamepad && Panel->GetFocusedNumber() == 1, TEXT("card_state_after_hold"));
            return true;
        });
        AddShot(TEXT("card-pad"));
        AddPadTap(FGamepadKeyNames::DPadDown);
        AddStep(TEXT("focus_2"), [this](double Elapsed)
        {
            const int32 Focus = UI()->GetChoicePanel()->GetFocusedNumber();
            if (Focus != 2 && Elapsed < 1.0) return false;
            Expect(Focus == 2, FString::Printf(TEXT("dpad_down_focus=%d"), Focus));
            return true;
        });
        AddShot(TEXT("card-pad-focus2"));
        // The mouse moves over the card: its hints name the keyboard, the focus stays where the pad left it.
        AddStep(TEXT("pointer_over"), [this](double) { Expect(PointerToRow(3), TEXT("no_row_3")); return true; });
        AddWait(0.3);
        AddStep(TEXT("hints_kbm"), [this](double)
        {
            const UMarburgumChoicePanel* Panel = UI()->GetChoicePanel();
            MB2_INPUT_LOG("event=hint_switch device=%s focus=%d", InputDeviceName(Panel->GetHintDevice()), Panel->GetFocusedNumber());
            Expect(Panel->GetHintDevice() == EMarburgumInputDevice::KeyboardMouse, TEXT("pointer_did_not_switch_hints"));
            Expect(Receipts.Num() == ReceiptsAtStep, TEXT("hover_sent_an_answer"));
            return true;
        });
        AddShot(TEXT("card-switch-kbm"));
        AddPadTap(FGamepadKeyNames::DPadDown);
        AddStep(TEXT("hints_pad"), [this](double Elapsed)
        {
            const UMarburgumChoicePanel* Panel = UI()->GetChoicePanel();
            if ((Panel->GetHintDevice() != EMarburgumInputDevice::Gamepad || Panel->GetFocusedNumber() != 3) && Elapsed < 1.0) return false;
            MB2_INPUT_LOG("event=hint_switch device=%s focus=%d", InputDeviceName(Panel->GetHintDevice()), Panel->GetFocusedNumber());
            Expect(Panel->GetHintDevice() == EMarburgumInputDevice::Gamepad && Panel->GetFocusedNumber() == 3, TEXT("dpad_did_not_switch_back"));
            return true;
        });
        AddShot(TEXT("card-switch-pad"));
        // Down again from the last answer goes round to the first: the focus never leaves the card.
        AddPadTap(FGamepadKeyNames::DPadDown);
        AddStep(TEXT("focus_wraps"), [this](double Elapsed)
        {
            const int32 Focus = UI()->GetChoicePanel()->GetFocusedNumber();
            if (Focus != 1 && Elapsed < 1.0) return false;
            Expect(Focus == 1, FString::Printf(TEXT("dpad_wrap_focus=%d"), Focus));
            return true;
        });
        AddPadTap(FGamepadKeyNames::FaceButtonRight);
        AddExpectWorldInput(TEXT("pad_cancel"), false);
        // The keyboard: E pressed and held opens the card once; its repeats do not close it again; Escape does.
        AddStep(TEXT("hold_e"), [this](double Elapsed)
        {
            if (Elapsed == 0.0)
            {
                ReceiptsAtStep = Receipts.Num();
                KeyDown(EKeys::E);
                return false;
            }
            if (Elapsed < 1.2)
            {
                KeyDown(EKeys::E, true);
                return false;
            }
            KeyUp(EKeys::E);
            return true;
        });
        AddWait(0.3);
        AddStep(TEXT("held_e"), [this](double)
        {
            const UMarburgumChoicePanel* Panel = UI()->GetChoicePanel();
            MB2_INPUT_LOG("event=held_e card_open=%d receipts=%d hints=%s focus=%d", UI()->IsChoiceOpen() ? 1 : 0, Receipts.Num() - ReceiptsAtStep,
                InputDeviceName(Panel->GetHintDevice()), Panel->GetFocusedNumber());
            Expect(UI()->IsChoiceOpen() && Receipts.Num() == ReceiptsAtStep, TEXT("held_e_closed_the_card_or_sent"));
            Expect(Panel->GetHintDevice() == EMarburgumInputDevice::KeyboardMouse, TEXT("held_e_hints_not_keyboard"));
            return true;
        });
        AddShot(TEXT("card-kbm"));
        AddKeyTap(EKeys::Escape);
        AddExpectWorldInput(TEXT("escape_cancel"), true);
        AddWait(InputQuietSeconds);
        AddStep(TEXT("cancel_sent_nothing"), [this](double)
        {
            MB2_INPUT_LOG("event=cancelled receipts=%d choice_receipts=%d", Receipts.Num() - ReceiptsAtStep, ChoiceReceipts(NAME_None));
            Expect(ChoiceReceipts(NAME_None) == 0, TEXT("cancel_sent_an_answer"));
            return true;
        });
    }
    else if (Role == TEXT("MOUSEWRONG"))
    {
        AddOpenCard(false);
        AddStep(TEXT("pointer_to_wrong"), [this](double)
        {
            const int32 Number = UI()->GetChoicePanel()->FindNumber(WrongChoice);
            Expect(Number > 0 && PointerToRow(Number), FString::Printf(TEXT("wrong_answer_not_offered=%s"), *WrongChoice.ToString()));
            return true;
        });
        AddWait(0.25);
        AddShot(TEXT("card-hover"));
        // The platform's double-click: down, up, then a double-click event and up, 60 ms apart, each in its own tick.
        AddStep(TEXT("click_down"), [this](double) { PointerButton(true); return true; });
        AddWait(0.06);
        AddStep(TEXT("click_up"), [this](double) { PointerButton(false); return true; });
        AddWait(0.06);
        AddStep(TEXT("dbl_down"), [this](double) { PointerButton(true, true); return true; });
        AddWait(0.06);
        AddStep(TEXT("dbl_up"), [this](double) { PointerButton(false); return true; });
        AddStep(TEXT("wrong_receipt"), [this](double)
        {
            return ChoiceReceipts(NAME_None) > 0;
        }, 8.0);
        AddWait(0.9);
        AddShot(TEXT("refusal-mouse"));
        AddWait(InputQuietSeconds);
        AddStep(TEXT("one_answer"), [this](double)
        {
            const FMarburgumInteractionReceipt* R = LatestReceipt();
            MB2_INPUT_LOG("event=answered via=double_click choice_receipts=%d choice=%s outcome=%s reason=%s", ChoiceReceipts(NAME_None),
                R ? *R->Choice.ToString() : TEXT("-"), R ? ToString(R->Outcome) : TEXT("-"), R ? ToString(R->Reason) : TEXT("-"));
            Expect(ChoiceReceipts(NAME_None) == 1, FString::Printf(TEXT("double_click_answers=%d"), ChoiceReceipts(NAME_None)));
            Expect(R && R->Choice == WrongChoice && R->Outcome == EMarburgumRequestOutcome::Rejected && R->Reason == EMarburgumInteractionReason::WrongConclusion,
                TEXT("mouse_wrong_not_refused"));
            return true;
        });
        AddExpectWorldInput(TEXT("after_mouse_answer"));
    }
    else if (Role == TEXT("NUMBERWRONG"))
    {
        AddOpenCard(true);
        AddShot(TEXT("card-pad-number"));
        AddStep(TEXT("hold_number"), [this](double Elapsed)
        {
            // The number key of the wrong answer, held: one press and its repeats.
            static const FKey Numbers[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six };
            if (Elapsed == 0.0)
            {
                // Read while the card is up: the first press closes it.
                HeldNumber = UI()->GetChoicePanel()->FindNumber(WrongChoice);
                MB2_INPUT_LOG("event=number_key number=%d choice=%s", HeldNumber, *WrongChoice.ToString());
            }
            if (HeldNumber < 1 || HeldNumber > 6)
            {
                Expect(false, FString::Printf(TEXT("wrong_answer_not_offered=%s"), *WrongChoice.ToString()));
                return true;
            }
            const FKey Key = Numbers[HeldNumber - 1];
            if (Elapsed == 0.0) { KeyDown(Key); return false; }
            if (Elapsed < 0.8) { KeyDown(Key, true); return false; }
            KeyUp(Key);
            return true;
        });
        AddStep(TEXT("wrong_receipt"), [this](double) { return ChoiceReceipts(NAME_None) > 0; }, 8.0);
        AddWait(0.9);
        AddShot(TEXT("refusal-number"));
        AddWait(InputQuietSeconds);
        AddStep(TEXT("one_answer"), [this](double)
        {
            const FMarburgumInteractionReceipt* R = LatestReceipt();
            MB2_INPUT_LOG("event=answered via=number_key choice_receipts=%d choice=%s outcome=%s reason=%s", ChoiceReceipts(NAME_None),
                R ? *R->Choice.ToString() : TEXT("-"), R ? ToString(R->Outcome) : TEXT("-"), R ? ToString(R->Reason) : TEXT("-"));
            Expect(ChoiceReceipts(NAME_None) == 1, FString::Printf(TEXT("held_number_answers=%d"), ChoiceReceipts(NAME_None)));
            Expect(R && R->Choice == WrongChoice && R->Outcome == EMarburgumRequestOutcome::Rejected && R->Reason == EMarburgumInteractionReason::WrongConclusion,
                TEXT("number_wrong_not_refused"));
            return true;
        });
        AddExpectWorldInput(TEXT("after_number_answer"));
    }
    else if (Role == TEXT("PADRIGHT"))
    {
        AddStep(TEXT("before"), [this](double)
        {
            XpBefore = LocalState()->GetSkillXp(InputPhotographySkill);
            const FMarburgumMissionState* Progress = nullptr;
            for (const FMarburgumMissionState& Mission : LocalState()->GetMissions())
            {
                Progress = Mission.MissionId == FName(TEXT("nika_frame_37")) ? &Mission : Progress;
            }
            StageBefore = Progress ? Progress->StageId : NAME_None;
            return true;
        });
        AddOpenCard(true);
        // D-pad down until the right answer has the focus, one press at a time, as a player steps through the list.
        AddStep(TEXT("dpad_to_right"), [this](double Elapsed)
        {
            const UMarburgumChoicePanel* Panel = UI()->GetChoicePanel();
            const int32 Want = Panel->FindNumber(RightChoice);
            if (Want < 1)
            {
                Expect(false, FString::Printf(TEXT("right_answer_not_offered=%s"), *RightChoice.ToString()));
                return true;
            }
            if (Panel->GetFocusedNumber() == Want)
            {
                MB2_INPUT_LOG("event=focus_right number=%d after_s=%.1f", Want, Elapsed);
                return true;
            }
            const int32 Phase = static_cast<int32>(Elapsed / 0.25);
            if (Phase != DpadPhase)
            {
                DpadPhase = Phase;
                if (Phase % 2 == 0) PadPress(FGamepadKeyNames::DPadDown); else PadRelease(FGamepadKeyNames::DPadDown);
            }
            return false;
        }, 6.0);
        AddStep(TEXT("dpad_released"), [this](double) { PadRelease(FGamepadKeyNames::DPadDown); DpadPhase = -1; return true; });
        AddWait(0.3);
        AddShot(TEXT("card-pad-focus-right"));
        AddPadTap(FGamepadKeyNames::FaceButtonBottom);
        AddStep(TEXT("right_receipt"), [this](double) { return ChoiceReceipts(NAME_None) > 0; }, 8.0);
        AddStep(TEXT("xp_replicated"), [this](double Elapsed)
        {
            return LocalState()->GetSkillXp(InputPhotographySkill) != XpBefore || Elapsed > 3.0;
        });
        AddWait(0.6);
        AddShot(TEXT("deduced"));
        AddWait(InputQuietSeconds);
        AddStep(TEXT("advanced_once"), [this](double)
        {
            const FMarburgumInteractionReceipt* R = LatestReceipt();
            FName StageNow;
            for (const FMarburgumMissionState& Mission : LocalState()->GetMissions())
            {
                StageNow = Mission.MissionId == FName(TEXT("nika_frame_37")) ? Mission.StageId : StageNow;
            }
            const int64 Gained = LocalState()->GetSkillXp(InputPhotographySkill) - XpBefore;
            MB2_INPUT_LOG("event=deduced choice=%s outcome=%s effect=%s choice_receipts=%d stage_before=%s stage_now=%s photography_xp_gained=%lld",
                R ? *R->Choice.ToString() : TEXT("-"), R ? ToString(R->Outcome) : TEXT("-"), R ? ToString(R->Effect) : TEXT("-"), ChoiceReceipts(NAME_None),
                *StageBefore.ToString(), *StageNow.ToString(), Gained);
            Expect(ChoiceReceipts(NAME_None) == 1, TEXT("right_answer_sent_more_than_once"));
            Expect(R && R->Choice == RightChoice && R->Outcome == EMarburgumRequestOutcome::Applied && R->Effect == EMarburgumInteractionEffect::StageCompleted,
                TEXT("right_answer_not_applied"));
            Expect(Gained == 10, FString::Printf(TEXT("photography_xp_gained=%lld"), Gained));
            Expect(StageNow != StageBefore, TEXT("stage_did_not_advance"));
            return true;
        });
        AddExpectWorldInput(TEXT("after_right_answer"));
    }
    else
    {
        Fail(TEXT("unknown_role"));
    }
}

bool UMarburgumInputSmokeSubsystem::Tick(float DeltaTime)
{
    if (bDone)
    {
        return true;
    }
    const double Now = FPlatformTime::Seconds();
    if (Now - StartSeconds > TimeoutSeconds)
    {
        Fail(FString::Printf(TEXT("timeout step=%d"), StepIndex));
        return true;
    }
    if (!FSlateApplication::IsInitialized())
    {
        return true;
    }
    if (!PendingShot.IsEmpty())
    {
        const bool bSaved = IFileManager::Get().FileSize(*PendingShot) > 0;
        if (!bSaved && Now - ShotRequestedSeconds < InputShotTimeoutSeconds)
        {
            return true;
        }
        MB2_INPUT_LOG("event=shot file=%s saved=%d", *FPaths::GetCleanFilename(PendingShot), bSaved ? 1 : 0);
        Expect(bSaved, FString::Printf(TEXT("shot_not_saved=%s"), *FPaths::GetCleanFilename(PendingShot)));
        PendingShot.Reset();
    }
    if (!Bind())
    {
        return true;
    }
    if (bDone)
    {
        return true;
    }
    if (!Steps.IsValidIndex(StepIndex))
    {
        Pass();
        return true;
    }
    FStep& Current = Steps[StepIndex];
    if (!bStepStarted)
    {
        bStepStarted = true;
        StepStartSeconds = Now;
        if (Current.Name != TEXT("wait"))
        {
            MB2_INPUT_LOG("event=step n=%d name=%s", StepIndex, *Current.Name);
        }
    }
    const double Elapsed = Now - StepStartSeconds;
    // The first call of a step sees exactly zero, so steps can set themselves up.
    if (Current.Run(Elapsed == 0.0 ? 0.0 : FMath::Max(Elapsed, 1e-6)))
    {
        ++StepIndex;
        bStepStarted = false;
    }
    else if (Elapsed > Current.TimeoutSeconds)
    {
        Fail(FString::Printf(TEXT("step_timeout n=%d name=%s"), StepIndex, *Current.Name));
    }
    SendAxes();
    return true;
}

void UMarburgumInputSmokeSubsystem::Expect(bool bCondition, const FString& What)
{
    if (!bCondition)
    {
        Problems.Add(What);
        MB2_INPUT_LOG("event=expectation_failed what=%s", *What);
    }
}

void UMarburgumInputSmokeSubsystem::Pass()
{
    if (Problems.IsEmpty())
    {
        MB2_INPUT_LOG("event=pass");
        bDone = true;
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }
    else
    {
        Fail(TEXT("expectations"));
    }
}

void UMarburgumInputSmokeSubsystem::Fail(const FString& Reason)
{
    if (bDone)
    {
        return;
    }
    Problems.Add(Reason);
    MB2_INPUT_LOG("event=fail reason=%s", *FString::Join(Problems, TEXT(",")));
    bDone = true;
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

#undef MB2_INPUT_LOG
