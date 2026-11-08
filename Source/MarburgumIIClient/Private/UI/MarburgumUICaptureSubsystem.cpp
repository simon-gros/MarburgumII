#include "UI/MarburgumUICaptureSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "MarburgumNetworkContract.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

using namespace MarburgumII::Session;

#define MB2_CAPTURE_LOG(Format, ...) \
    UE_LOG(LogMarburgumNet, Log, TEXT("%s role=client event=capture script=%s " Format), UiLogPrefix, *Script, ##__VA_ARGS__)

bool UMarburgumUICaptureSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
    FString Ignored;
    return GameInstance && !GameInstance->IsDedicatedServerInstance()
        && FParse::Value(FCommandLine::Get(), TEXT("MB2UICapture="), Ignored);
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
            AfterShot(PendingView);
        }
        else if (Now - PendingSince > ShotTimeoutSeconds)
        {
            Finish(4, TEXT("screenshot_not_written"));
        }
        return true;
    }

    const FMarburgumSessionView& View = SessionSubsystem->GetView();
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
    const bool bJoin = Script == TEXT("join") || bRecover;
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
