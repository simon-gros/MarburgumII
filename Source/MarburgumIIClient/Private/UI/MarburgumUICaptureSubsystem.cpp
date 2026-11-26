#include "UI/MarburgumUICaptureSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UI/MarburgumSettingsPanel.h"
#include "UI/MarburgumUISubsystem.h"
#include "UnrealClient.h"

using namespace MarburgumII::Session;

#define MB2_CAPTURE_LOG(Format, ...) \
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=capture script=%s " Format), UiLogPrefix, *Script, ##__VA_ARGS__)

bool UMarburgumUICaptureSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
    static_cast<void>(Outer);
    return false; // Test harnesses and capture tools never run in a player build.
#else
    const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
    FString Ignored;
    return GameInstance && !GameInstance->IsDedicatedServerInstance()
        && FParse::Value(FCommandLine::Get(), TEXT("MB2UICapture="), Ignored);
#endif
}

void UMarburgumUICaptureSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Session = Collection.InitializeDependency<UMarburgumSessionSubsystem>();

    const TCHAR* CommandLine = FCommandLine::Get();
    FParse::Value(CommandLine, TEXT("MB2UICapture="), Directory);
    FParse::Value(CommandLine, TEXT("MB2UICaptureScript="), Script);
    Label = Script;
    FParse::Value(CommandLine, TEXT("MB2UICaptureLabel="), Label);
    bDropAfterReady = FParse::Param(CommandLine, TEXT("MB2UICaptureDrop"));
    FString Tour = TEXT("arrival,explore,interact,character,skills,inventory,journal,settings,chat,menu");
    FParse::Value(CommandLine, TEXT("MB2UITour="), Tour, false);
    Tour.ParseIntoArray(TourSteps, TEXT(","), true);
    Directory = FPaths::ConvertRelativePathToFull(Directory);
    IFileManager::Get().MakeDirectory(*Directory, true);

    StartSeconds = FPlatformTime::Seconds();
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UMarburgumUICaptureSubsystem::Tick), 0.1f);
    MB2_CAPTURE_LOG("stage=start dir=\"%s\" drop=%d", *Directory, bDropAfterReady ? 1 : 0);
}

void UMarburgumUICaptureSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    Super::Deinitialize();
}

bool UMarburgumUICaptureSubsystem::Tick(float DeltaTime)
{
    if (bDone)
    {
        return true;
    }
    const double Now = FPlatformTime::Seconds();
    if (Now - StartSeconds > ScenarioTimeoutSeconds)
    {
        Finish(2, TEXT("scenario_timeout"));
        return true;
    }
    UMarburgumSessionSubsystem* SessionSubsystem = Session.Get();
    if (!SessionSubsystem)
    {
        Finish(3, TEXT("no_session_subsystem"));
        return true;
    }

    if (!PendingFile.IsEmpty())
    {
        if (IFileManager::Get().FileSize(*PendingFile) > 0)
        {
            MB2_CAPTURE_LOG("stage=saved file=\"%s\" phase=%s fault=%s", *PendingFile, ToString(PendingView.Phase), ToString(PendingView.Fault));
            PendingFile.Reset();
            if (!PendingTourStep.IsEmpty())
            {
                const FString Step = MoveTemp(PendingTourStep);
                PendingTourStep.Reset();
                EndTourStep(Step);
                ++TourIndex;
                TourStepSince = Now;
                bTourStepShot = false;
                return true;
            }
            AfterShot(PendingView);
        }
        else if (Now - PendingSince > ShotTimeoutSeconds)
        {
            Finish(4, TEXT("screenshot_not_written"));
        }
        return true;
    }

    const FMarburgumSessionView& View = SessionSubsystem->GetView();
    if (TourIndex != INDEX_NONE)
    {
        if (View.Phase != EPhase::Ready)
        {
            Finish(1, TEXT("tour_lost_admission"));
            return true;
        }
        TickTour(Now);
        return true;
    }
    const bool bSettled = Now - View.PhaseStartedSeconds >= SettleSeconds;
    const bool bWarm = Now - StartSeconds >= FirstShotDelaySeconds;
    if (View.Revision != CapturedRevision && View.Phase != EPhase::Starting && bSettled && bWarm)
    {
        RequestShot(View);
    }
    return true;
}

void UMarburgumUICaptureSubsystem::RequestShot(const FMarburgumSessionView& View)
{
    FIntPoint Size(0, 0);
    if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
    {
        Size = GEngine->GameViewport->Viewport->GetSizeXY();
    }
    FString Name = FString::Printf(TEXT("%02d-%s-%s"), ++ShotIndex, *Label, ToString(View.Phase));
    if (View.Fault != EFault::None)
    {
        Name += FString::Printf(TEXT("-%s"), ToString(View.Fault));
    }
    Name += FString::Printf(TEXT("-%dx%d.png"), Size.X, Size.Y);

    CapturedRevision = View.Revision;
    PendingView = View;
    PendingFile = FPaths::Combine(Directory, Name);
    PendingSince = FPlatformTime::Seconds();
    IFileManager::Get().Delete(*PendingFile, false, true, true);
    // bShowUI=true: capture the composited window, UMG included, exactly as a player sees it.
    FScreenshotRequest::RequestScreenshot(PendingFile, true, false);
    MB2_CAPTURE_LOG("stage=request file=\"%s\" phase=%s fault=%s", *PendingFile, ToString(View.Phase), ToString(View.Fault));
}

void UMarburgumUICaptureSubsystem::AfterShot(const FMarburgumSessionView& View)
{
    UMarburgumSessionSubsystem* SessionSubsystem = Session.Get();
    const FString Server = UMarburgumSessionSubsystem::DefaultAddress();

    if (Script == TEXT("frontend"))
    {
        Finish(View.Phase == EPhase::Idle ? 0 : 1, TEXT("frontend_captured"));
        return;
    }

    const bool bRecover = Script == TEXT("recover");
    const bool bTour = Script == TEXT("tour");
    const bool bJoin = Script == TEXT("join") || bRecover || bTour;
    if (Script != TEXT("fail") && !bJoin)
    {
        Finish(5, TEXT("unknown_script"));
        return;
    }

    switch (View.Phase)
    {
    case EPhase::Idle:
        MB2_CAPTURE_LOG("stage=connect server=%s", *Server);
        SessionSubsystem->Connect(Server);
        return;
    case EPhase::Error:
        Finish(bJoin ? 1 : 0, bJoin ? TEXT("unexpected_error") : TEXT("error_state_captured"));
        return;
    case EPhase::Ready:
        if (!bJoin)
        {
            Finish(1, TEXT("unexpected_admission"));
        }
        else if (bTour)
        {
            MB2_CAPTURE_LOG("stage=tour_start steps=%d", TourSteps.Num());
            TourIndex = 0;
            TourStepSince = FPlatformTime::Seconds();
            bTourStepShot = false;
        }
        else if (bRecover && !bAwaitedLoss)
        {
            // The harness stops the server now; the session must notice and recover by itself.
            bAwaitedLoss = true;
            MB2_CAPTURE_LOG("stage=await_loss");
        }
        else if (bDropAfterReady && !bDropped)
        {
            bDropped = true;
            MB2_CAPTURE_LOG("stage=drop method=client_disconnect_command");
            // Client-side simulation of an unexpected drop: the session subsystem was not asked to
            // disconnect, so it must treat this as a lost connection and recover on its own.
            GEngine->Exec(GetGameInstance()->GetWorld(), TEXT("disconnect"));
        }
        else
        {
            Finish(0, (bDropped || bAwaitedLoss) ? TEXT("recovered_admission_captured") : TEXT("admission_captured"));
        }
        return;
    default:
        return; // Intermediate phases are captured when they last long enough; no action needed.
    }
}

namespace
{
    /** How long each tour step settles before its screenshot: transitions finish and receipts arrive. */
    double TourSettleSeconds(const FString& Step)
    {
        if (Step == TEXT("arrival")) return 1.8;
        if (Step == TEXT("explore")) return 5.0;
        if (Step == TEXT("interact")) return 3.5;
        if (Step == TEXT("chat")) return 2.5;
        return 1.2;
    }
}

void UMarburgumUICaptureSubsystem::TickTour(double Now)
{
    if (!PendingFile.IsEmpty())
    {
        return;
    }
    if (!TourSteps.IsValidIndex(TourIndex))
    {
        Finish(0, TEXT("tour_captured"));
        return;
    }
    const FString& Step = TourSteps[TourIndex];
    if (!bTourStepShot)
    {
        bTourStepShot = true;
        TourStepSince = Now;
        if (!BeginTourStep(Step))
        {
            MB2_CAPTURE_LOG("stage=tour_skip step=%s", *Step);
            ++TourIndex;
            bTourStepShot = false;
        }
        return;
    }
    if (Now - TourStepSince >= TourSettleSeconds(Step))
    {
        RequestTourShot(Step);
    }
}

bool UMarburgumUICaptureSubsystem::BeginTourStep(const FString& Step)
{
    UGameInstance* GameInstance = GetGameInstance();
    UMarburgumUISubsystem* UI = GameInstance ? GameInstance->GetSubsystem<UMarburgumUISubsystem>() : nullptr;
    UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
    AMarburgumPlayerController* Controller = World ? Cast<AMarburgumPlayerController>(World->GetFirstPlayerController()) : nullptr;
    if (!UI || !Controller)
    {
        return false;
    }
    MB2_CAPTURE_LOG("stage=tour_step step=%s", *Step);
    if (Step == TEXT("arrival") || Step == TEXT("explore"))
    {
        return true;
    }
    if (Step == TEXT("interact"))
    {
        Controller->RequestInteract();
        return true;
    }
    if (Step == TEXT("character")) return UI->ToggleCharacter();
    // `character:<slot>`: the Character sheet on one equipment slot's card (ADR-085).
    if (Step.StartsWith(TEXT("character:"))) return UI->ShowCharacterSlot(FName(*Step.RightChop(10)));
    // `equip:<slot>:<item>`: ask the server to put a carried item in a slot, as the sheet would (ADR-085). The next step
    // shows whatever the server decided; this one takes no picture of its own worth keeping.
    if (Step.StartsWith(TEXT("equip:")))
    {
        FString Slot, Item;
        if (!Step.RightChop(6).Split(TEXT(":"), &Slot, &Item))
        {
            return false;
        }
        Controller->SubmitEquip(FGuid::NewGuid(), FName(*Slot), FName(*Item));
        return true;
    }
    if (Step == TEXT("skills")) return UI->ToggleSkills();
    if (Step == TEXT("inventory")) return UI->ToggleInventory();
    // `inventory:<item>`: the Inventory on one item's card (evidence classes and descriptions, ADR-084).
    if (Step.StartsWith(TEXT("inventory:"))) return UI->ShowInventoryItem(FName(*Step.RightChop(10)));
    if (Step == TEXT("journal")) return UI->ToggleJournal();
    if (Step == TEXT("settings")) return UI->ToggleSettings();
    // `settings:graphics`: Settings on the DLSS 5 neural rendering row, with the reason it does or does not run here.
    if (Step == TEXT("settings:graphics"))
    {
        UMarburgumSettingsPanel* Panel = UI->ToggleSettings() ? UI->GetSettingsPanel() : nullptr;
        return Panel && Panel->FocusSetting(EMarburgumSetting::NeuralRendering);
    }
    if (Step == TEXT("chat"))
    {
        Controller->SendGeneralChat(TEXT("Dober dan from the tour: one general line, as every player on this server sees it."));
        UI->FocusChat();
        return true;
    }
    if (Step == TEXT("menu"))
    {
        UI->Back();
        return UI->IsSystemMenuOpen();
    }
    return false;
}

void UMarburgumUICaptureSubsystem::EndTourStep(const FString& Step)
{
    UGameInstance* GameInstance = GetGameInstance();
    UMarburgumUISubsystem* UI = GameInstance ? GameInstance->GetSubsystem<UMarburgumUISubsystem>() : nullptr;
    UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
    APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
    if (!UI || !Controller)
    {
        return;
    }
    if (Step == TEXT("character") || Step == TEXT("skills") || Step == TEXT("inventory") || Step == TEXT("journal")
        || Step == TEXT("settings") || Step == TEXT("settings:graphics") || Step == TEXT("menu"))
    {
        UI->Back(); // Escape closes what the step opened.
    }
    else if (Step == TEXT("chat"))
    {
        UI->LeaveChat();
        Controller->SetInputMode(FInputModeGameOnly());
        Controller->SetShowMouseCursor(false);
        if (FSlateApplication::IsInitialized())
        {
            FSlateApplication::Get().SetAllUserFocusToGameViewport();
        }
    }
}

void UMarburgumUICaptureSubsystem::RequestTourShot(const FString& Step)
{
    FIntPoint Size(0, 0);
    if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
    {
        Size = GEngine->GameViewport->Viewport->GetSizeXY();
    }
    const FString Name = FString::Printf(TEXT("%02d-%s-%s-%dx%d.png"), ++ShotIndex, *Label, *Step.Replace(TEXT(":"), TEXT("-")), Size.X, Size.Y);
    PendingTourStep = Step;
    PendingView = Session.IsValid() ? Session->GetView() : FMarburgumSessionView();
    PendingFile = FPaths::Combine(Directory, Name);
    PendingSince = FPlatformTime::Seconds();
    IFileManager::Get().Delete(*PendingFile, false, true, true);
    FScreenshotRequest::RequestScreenshot(PendingFile, true, false);
    MB2_CAPTURE_LOG("stage=request file=\"%s\" step=%s", *PendingFile, *Step);
}

void UMarburgumUICaptureSubsystem::Finish(int32 ExitCode, const TCHAR* Reason)
{
    if (bDone)
    {
        return;
    }
    bDone = true;
    MB2_CAPTURE_LOG("stage=finish exit=%d reason=%s shots=%d", ExitCode, Reason, ShotIndex);
    FPlatformMisc::RequestExitWithStatus(false, static_cast<uint8>(ExitCode));
}

#undef MB2_CAPTURE_LOG
