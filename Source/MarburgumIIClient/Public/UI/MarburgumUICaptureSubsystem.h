#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarburgumUICaptureSubsystem.generated.h"

/**
 * Evidence capture driver, active only with -MB2UICapture=<dir>. Screenshots the real rendered client
 * UI (including UMG) each time the session settles in a new phase, drives the session through a
 * scripted scenario, then exits with 0 (scenario reached its expected end) or non-zero.
 *
 * -MB2UICaptureLabel=<name> names the screenshots (default: the script name).
 * -MB2UICaptureScript=frontend|fail|join|recover   (default frontend)
 *   frontend: capture the front-end (or gallery with -MB2UIGallery) and exit.
 *   fail:     connect to -MB2Server, expect a truthful error state.
 *   join:     connect to -MB2Server, expect admission; with -MB2UICaptureDrop, then drop the
 *             connection client-side and expect automatic recovery to admission.
 *   recover:  connect to -MB2Server, expect admission, then wait for the harness to stop and restart
 *             the server; expect reconnecting and automatic recovery to admission.
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumUICaptureSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    bool Tick(float DeltaTime);
    void RequestShot(const FMarburgumSessionView& View);
    void AfterShot(const FMarburgumSessionView& View);
    void Finish(int32 ExitCode, const TCHAR* Reason);

    FTSTicker::FDelegateHandle TickerHandle;
    TWeakObjectPtr<UMarburgumSessionSubsystem> Session;

    FString Directory;
    FString Script = TEXT("frontend");
    FString Label;
    bool bDropAfterReady = false;
    bool bDropped = false;
    bool bAwaitedLoss = false;
    bool bDone = false;

    double StartSeconds = 0.0;
    int32 CapturedRevision = -1;
    int32 ShotIndex = 0;
    FString PendingFile;
    double PendingSince = 0.0;
    FMarburgumSessionView PendingView;

    static constexpr double SettleSeconds = 1.2;
    static constexpr double FirstShotDelaySeconds = 2.5;
    static constexpr double ShotTimeoutSeconds = 10.0;
    static constexpr double ScenarioTimeoutSeconds = 150.0;
};
