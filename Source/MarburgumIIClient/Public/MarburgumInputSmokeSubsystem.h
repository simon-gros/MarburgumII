#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "InputCoreTypes.h"
#include "MarburgumInteractionTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarburgumInputSmokeSubsystem.generated.h"

class AMarburgumPlayerController;
class AMarburgumPlayerState;
class UMarburgumUISubsystem;

/**
 * Keyboard, mouse and gamepad driver for the Foto Vidmar investigation (UI-02 input work), active only with
 * -MB2InputSmoke in a rendered client. It plays the player's hands, not the RPCs: every action is an input event handed
 * to Slate the way the platform layer hands over a real device's (gamepad buttons and sticks through the application
 * message handler, keys and the pointer through Slate's key and pointer entry points), so input preprocessors, focus,
 * widgets, the viewport and the player controller's bindings all run as for a person at the desk. Each process plays one
 * role against a real dedicated server and exits 0 (its expectations held) or 1. Roles (scripts/input-smoke-linux.sh):
 *
 *   COUNTER     Chapter VI's order-book page: the left stick walks, the mouse turns the view, the right stick aims at
 *               the page until the prompt offers the photograph, the prompt's key follows a mouse nudge and a pad button
 *               (P, RT, P...), RT takes the photograph and the server's receipt says the step completed; View and Y open
 *               the Journal and the Inventory, B closes them and the left stick walks again.
 *   LOUPE       A at the light table: Frame 37's loupe step.
 *   CANCEL      A opens the deduction and is held (no answer is sent), the D-pad moves the focus, a pointer move over the
 *               card switches its hints to the keyboard and the D-pad back, B cancels and the street has the keys again;
 *               then E opens it, a held E does not close it, and Escape does.
 *   MOUSEWRONG  E opens it and a double-click on a wrong answer sends exactly one answer, refused by the server.
 *   NUMBERWRONG A opens it and a held number key sends exactly one wrong answer.
 *   PADRIGHT    A opens it, the D-pad moves to the right answer and A confirms: the step completes once, with its XP.
 *
 * Options: -MB2InputRole= -MB2InputShot=<path prefix> -MB2InputTimeout=<s> -MB2InputTarget=<photo subject or
 * interactable> -MB2InputWrong=<choice> (MOUSEWRONG, NUMBERWRONG) -MB2InputRight=<choice> (PADRIGHT).
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumInputSmokeSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    /** One step of a role: runs every tick until it returns true; Elapsed is the time since the step began. */
    struct FStep
    {
        FString Name;
        TFunction<bool(double Elapsed)> Run;
        double TimeoutSeconds = 10.0;
    };

    bool Tick(float DeltaTime);
    bool Bind();
    void BuildSteps();
    void AddStep(const TCHAR* Name, TFunction<bool(double)> Run, double TimeoutSeconds = 10.0);
    void AddWait(double Seconds);
    void AddShot(const TCHAR* Label);
    void AddPadTap(FName Button);
    void AddKeyTap(const FKey& Key);
    /** Opens the decision card with Button (A) or Key (E), then expects it open on the given device's hints. */
    void AddOpenCard(bool bGamepad);
    /** The card is gone and the street has mouse and keys again; with bWalk, the left stick also walks a step. */
    void AddExpectWorldInput(const TCHAR* Label, bool bWalk = true);

    // The hands. Gamepad events go through FSlateApplication's message-handler entry points, as the platform's
    // controller code calls them; keys and the pointer through Slate's key and pointer event entry points.
    void PadPress(FName Button, bool bRepeat = false);
    void PadRelease(FName Button);
    void PadAxis(FName Axis, float Value);
    void SendAxes();
    void KeyDown(const FKey& Key, bool bRepeat = false);
    void KeyUp(const FKey& Key);
    void PointerMove(const FVector2D& Screen);
    void PointerButton(bool bDown, bool bDoubleClick = false);
    void RawMouse(int32 X, int32 Y);
    bool PointerToRow(int32 Number);

    UMarburgumUISubsystem* UI() const;
    const AMarburgumPlayerState* LocalState() const;
    int32 ChoiceReceipts(FName Choice) const;
    const FMarburgumInteractionReceipt* LatestReceipt() const;
    bool IsWorldInput() const;
    bool AimError(float& OutYaw, float& OutPitch, FString& OutInFrame, float& OutOffAxis) const;
    void HandleReceipt(const FMarburgumInteractionReceipt& Receipt);
    void Expect(bool bCondition, const FString& What);
    void Pass();
    void Fail(const FString& Reason);

    FTSTicker::FDelegateHandle TickerHandle;
    TWeakObjectPtr<AMarburgumPlayerController> Controller;

    FString Role;
    FString ShotPrefix;
    FName TargetId;
    FName WrongChoice;
    FName RightChoice;
    double TimeoutSeconds = 150.0;
    double StartSeconds = 0.0;

    TArray<FStep> Steps;
    int32 StepIndex = 0;
    double StepStartSeconds = 0.0;
    bool bStepStarted = false;
    FString PendingShot;
    double ShotRequestedSeconds = 0.0;
    bool bDone = false;
    TArray<FString> Problems;

    /** Sticks held this tick, re-sent every tick as a pad reports them. */
    TMap<FName, float> HeldAxes;
    TMap<FName, float> AxesThisTick;
    FVector2D PointerAt = FVector2D(-1.0, -1.0);
    TSet<FKey> PointerButtons;

    TArray<FMarburgumInteractionReceipt> Receipts;
    int32 ReceiptsAtStep = 0;
    int64 XpBefore = 0;
    FVector PlaceBefore = FVector::ZeroVector;
    float YawBefore = 0.f;
    float PitchBefore = 0.f;
    float MovedCm = 0.f;
    /** When the world-input walk turned back (seconds into the step), or a negative value while it is still going out. */
    double WalkTurnSeconds = -1.0;
    int32 DpadPhase = -1;
    int32 AimLogSecond = -1;
    int32 HeldNumber = 0;
    FName StageBefore;
};
