#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarburgumViewShotSubsystem.generated.h"

class ACameraActor;

/**
 * Fixed-camera screenshots of the loaded level, active only with -MB2ViewShots. Used as before/after
 * evidence for content changes such as rebuilt buildings.
 *
 *   -MB2ViewShots="name@eyeX,eyeY,eyeZ@targetX,targetY,targetZ@fov;..."  -MB2ViewShotDir=<abs dir>
 *
 * Coordinates are Unreal centimetres, fov is horizontal degrees. After the map has settled, the player
 * camera is switched to each view in turn, the HUD is removed, and one screenshot per view is written
 * as <dir>/<name>.png. The process exits 0 when every view was written, 1 otherwise.
 *
 * A view whose eye is `player` ("name@player@player@0") keeps the player's own third-person camera and the HUD, and
 * its screenshot includes the interface (GFX-00 character and HUD views). The HUD is removed for good at the first
 * fixed view, so player views go first.
 *
 * While each view settles, frame, game-thread, render-thread, RHI-thread and GPU times, RHI draw calls and
 * primitives are sampled from the second second on and logged as "MB2VIEWSHOT stats view=..." and written to
 * <dir>/stats.tsv: means first (the original columns), then p50/p95/p99, VRAM in use and its budget, texture memory
 * and the process's resident memory, so two builds can be compared view by view (editor -game numbers compare with
 * each other, not with a package). -MB2ViewShotSettle=<seconds> (default 2.5, at most 30) lengthens each view for
 * more samples. At the first view the renderer's configuration and the level's render inventory are written to
 * <dir>/render_config.json (MarburgumRenderAudit).
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumViewShotSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    struct FView
    {
        FString Name;
        FVector Eye;
        FVector Target;
        float Fov = 60.f;
        bool bPlayer = false;
    };

    bool Tick(float DeltaTime);
    void Finish(int32 Code, const TCHAR* Reason);
    void SampleStats(float DeltaTime);
    void FlushStats();

    TArray<FView> Views;
    FString Directory;
    FTSTicker::FDelegateHandle TickerHandle;
    TWeakObjectPtr<ACameraActor> Camera;
    int32 Current = -1;
    double StartSeconds = 0.0;
    double ViewSeconds = 0.0;
    double SettleSeconds = 2.5;
    bool bWidgetsRemoved = false;
    bool bAudited = false;
    FString PendingFile;
    bool bRequested = false;
    bool bFinished = false;

    struct FStats
    {
        TArray<float> FrameMs;
        TArray<float> GameMs;
        TArray<float> RenderMs;
        TArray<float> RhiMs;
        TArray<float> GpuMs;
        double Draws = 0.0;
        double Primitives = 0.0;
    };
    FStats Stats;
    TArray<FString> StatRows;
};
