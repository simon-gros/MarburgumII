#include "MarburgumViewShotSubsystem.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "DynamicRHI.h"
#include "HAL/PlatformMemory.h"
#include "RHIStats.h"
#include "Render/MarburgumRenderAudit.h"
#include "RenderTimer.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumViewShot, Log, All);

namespace MarburgumViewShot
{
    // The first frames after a map load are not representative (streaming, exposure, shaders).
    constexpr double FirstViewSettleSeconds = 6.0;
    // Each new view: let exposure and texture streaming catch up (-MB2ViewShotSettle= for longer samples).
    constexpr double DefaultViewSettleSeconds = 2.5;
    constexpr double MaxViewSettleSeconds = 30.0;
    // Frame statistics are averaged over the settle window after this, once the camera cut has passed.
    constexpr double StatsAfterSeconds = 1.0;
    constexpr double ShotTimeoutSeconds = 15.0;
    constexpr double TotalTimeoutSeconds = 600.0;
    constexpr double MiB = 1024.0 * 1024.0;

    bool ParseVector(const FString& Text, FVector& Out)
    {
        TArray<FString> Parts;
        Text.ParseIntoArray(Parts, TEXT(","));
        if (Parts.Num() != 3)
        {
            return false;
        }
        Out = FVector(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2]));
        return true;
    }

    double Mean(const TArray<float>& Samples)
    {
        double Sum = 0.0;
        for (const float Sample : Samples)
        {
            Sum += Sample;
        }
        return Samples.IsEmpty() ? 0.0 : Sum / Samples.Num();
    }

    // Nearest rank: the smallest sample with at least Percent of the samples at or below it.
    double Percentile(TArray<float> Samples, double Percent)
    {
        if (Samples.IsEmpty())
        {
            return 0.0;
        }
        Samples.Sort();
        const int32 Rank = FMath::CeilToInt32(Percent / 100.0 * Samples.Num());
        return Samples[FMath::Clamp(Rank - 1, 0, Samples.Num() - 1)];
    }
}

using namespace MarburgumViewShot;

bool UMarburgumViewShotSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
    static_cast<void>(Outer);
    return false; // Test harnesses and capture tools never run in a player build.
#else
    const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
    FString Ignored;
    return GameInstance && !GameInstance->IsDedicatedServerInstance()
        && FParse::Value(FCommandLine::Get(), TEXT("MB2ViewShots="), Ignored);
#endif
}

void UMarburgumViewShotSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const TCHAR* CommandLine = FCommandLine::Get();
    FString Text;
    FParse::Value(CommandLine, TEXT("MB2ViewShots="), Text, false);
    FParse::Value(CommandLine, TEXT("MB2ViewShotDir="), Directory);
    Text.TrimQuotesInline();
    TArray<FString> Entries;
    Text.ParseIntoArray(Entries, TEXT(";"));
    for (const FString& Entry : Entries)
    {
        TArray<FString> Fields;
        Entry.ParseIntoArray(Fields, TEXT("@"));
        FView View;
        View.bPlayer = Fields.Num() == 4 && Fields[1] == TEXT("player");
        if (Fields.Num() == 4 && (View.bPlayer || (ParseVector(Fields[1], View.Eye) && ParseVector(Fields[2], View.Target))))
        {
            View.Name = Fields[0];
            View.Fov = FCString::Atof(*Fields[3]);
            Views.Add(View);
        }
        else
        {
            UE_LOG(LogMarburgumViewShot, Error, TEXT("MB2VIEWSHOT bad view '%s'"), *Entry);
        }
    }
    if (FParse::Value(CommandLine, TEXT("MB2ViewShotSettle="), SettleSeconds))
    {
        SettleSeconds = FMath::Clamp(SettleSeconds, DefaultViewSettleSeconds, MaxViewSettleSeconds);
    }
    else
    {
        SettleSeconds = DefaultViewSettleSeconds;
    }
    StartSeconds = FPlatformTime::Seconds();
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMarburgumViewShotSubsystem::Tick), 0.f);
    UE_LOG(LogMarburgumViewShot, Display, TEXT("MB2VIEWSHOT start views=%d settle_s=%.1f dir=%s"), Views.Num(), SettleSeconds, *Directory);
}

void UMarburgumViewShotSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    Super::Deinitialize();
}

bool UMarburgumViewShotSubsystem::Tick(float DeltaTime)
{
    if (bFinished)
    {
        return false;
    }
    const double Now = FPlatformTime::Seconds();
    if (Views.Num() == 0 || Directory.IsEmpty())
    {
        Finish(1, TEXT("no_views"));
        return false;
    }
    if (Now - StartSeconds > FMath::Max(TotalTimeoutSeconds, FirstViewSettleSeconds + Views.Num() * (SettleSeconds + ShotTimeoutSeconds)))
    {
        Finish(1, TEXT("timeout"));
        return false;
    }
    UWorld* World = GetGameInstance()->GetWorld();
    APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
    if (!Controller || Now - StartSeconds < FirstViewSettleSeconds)
    {
        return true;
    }
    if (!Camera.IsValid())
    {
        Camera = World->SpawnActor<ACameraActor>();
        Camera->GetCameraComponent()->bConstrainAspectRatio = false;
        Current = -1;
        bRequested = false;
    }
    if (!bAudited)
    {
        // Once, with the level settled and before any camera moves: what the renderer is configured to do.
        bAudited = true;
        MarburgumII::RenderAudit::WriteJson(*World, FPaths::Combine(Directory, TEXT("render_config.json")));
    }
    if (bRequested)
    {
        if (!IFileManager::Get().FileExists(*PendingFile))
        {
            if (Now - ViewSeconds > SettleSeconds + ShotTimeoutSeconds)
            {
                Finish(1, TEXT("shot_timeout"));
                return false;
            }
            return true;
        }
        UE_LOG(LogMarburgumViewShot, Display, TEXT("MB2VIEWSHOT wrote %s"), *PendingFile);
        bRequested = false;
    }
    else if (Current >= 0)
    {
        // Settling on the current view; then take its shot.
        if (Now - ViewSeconds > StatsAfterSeconds)
        {
            SampleStats(DeltaTime);
        }
        if (Now - ViewSeconds > SettleSeconds)
        {
            FlushStats();
            PendingFile = FPaths::Combine(Directory, Views[Current].Name + TEXT(".png"));
            IFileManager::Get().Delete(*PendingFile, false, true, true);
            FScreenshotRequest::RequestScreenshot(PendingFile, Views[Current].bPlayer, false);
            bRequested = true;
        }
        return true;
    }
    // Move to the next view.
    ++Current;
    if (Current >= Views.Num())
    {
        Finish(0, TEXT("done"));
        return false;
    }
    const FView& View = Views[Current];
    if (View.bPlayer && Controller->GetPawn())
    {
        if (bWidgetsRemoved)
        {
            UE_LOG(LogMarburgumViewShot, Warning, TEXT("MB2VIEWSHOT view=%s hud=removed: player views must come before fixed views"), *View.Name);
        }
        Controller->SetViewTarget(Controller->GetPawn());
    }
    else
    {
        if (View.bPlayer)
        {
            UE_LOG(LogMarburgumViewShot, Error, TEXT("MB2VIEWSHOT view=%s has no pawn; shooting from the last camera"), *View.Name);
        }
        else
        {
            Camera->SetActorLocationAndRotation(View.Eye, (View.Target - View.Eye).Rotation());
            Camera->GetCameraComponent()->SetFieldOfView(View.Fov);
        }
        if (!bWidgetsRemoved && GEngine && GEngine->GameViewport)
        {
            GEngine->GameViewport->RemoveAllViewportWidgets();
            bWidgetsRemoved = true;
        }
        Controller->SetViewTarget(Camera.Get());
    }
    ViewSeconds = Now;
    Stats = FStats();
    return true;
}

void UMarburgumViewShotSubsystem::SampleStats(float DeltaTime)
{
    Stats.FrameMs.Add(DeltaTime * 1000.f);
    Stats.GameMs.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
    Stats.RenderMs.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
    Stats.RhiMs.Add(FPlatformTime::ToMilliseconds(GRHIThreadTime));
    Stats.GpuMs.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)));
    Stats.Draws += GNumDrawCallsRHI[0];
    Stats.Primitives += GNumPrimitivesDrawnRHI[0];
}

void UMarburgumViewShotSubsystem::FlushStats()
{
    const int32 Frames = Stats.FrameMs.Num();
    const double N = FMath::Max(Frames, 1);
    // Memory at the end of the view, once its textures have streamed in. VRAM comes from the RHI's own budget query
    // (VK_EXT_memory_budget on Vulkan): this process only, unlike nvidia-smi. -1 where the RHI cannot tell.
    FRHIMemoryStats Memory;
    RHIGetMemoryStats(Memory);
    FTextureMemoryStats Texture;
    RHIGetTextureMemoryStats(Texture);
    const double VramUsed = Memory.IsValid() ? Memory.UsedLocal / MiB : -1.0;
    const double VramBudget = Memory.IsValid() ? Memory.BudgetLocal / MiB : -1.0;
    const double ResidentMb = FPlatformMemory::GetStats().UsedPhysical / MiB;
    const FView& View = Views[Current];
    const FString Row = FString::Printf(TEXT("%s\t%d\t%.2f\t%.2f\t%.2f\t%.2f\t%.0f\t%.0f\t%s\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\t%d"),
        *View.Name, Frames, Mean(Stats.FrameMs), Mean(Stats.GameMs), Mean(Stats.RenderMs), Mean(Stats.GpuMs), Stats.Draws / N, Stats.Primitives / N,
        *FString::Printf(TEXT("%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f"),
            Percentile(Stats.FrameMs, 50), Percentile(Stats.FrameMs, 95), Percentile(Stats.FrameMs, 99),
            Percentile(Stats.GameMs, 95), Percentile(Stats.GameMs, 99), Percentile(Stats.RenderMs, 95), Percentile(Stats.RenderMs, 99),
            Mean(Stats.RhiMs), Percentile(Stats.RhiMs, 95), Percentile(Stats.GpuMs, 50), Percentile(Stats.GpuMs, 95), Percentile(Stats.GpuMs, 99)),
        VramUsed, VramBudget, Texture.StreamingMemorySize / MiB, Texture.NonStreamingMemorySize / MiB, ResidentMb, View.bPlayer ? 1 : 0);
    StatRows.Add(Row);
    UE_LOG(LogMarburgumViewShot, Display,
        TEXT("MB2VIEWSHOT stats view=%s frames=%d frame_ms=%.2f frame_p95=%.2f frame_p99=%.2f game_ms=%.2f render_ms=%.2f rhi_ms=%.2f gpu_ms=%.2f gpu_p95=%.2f gpu_p99=%.2f draws=%.0f primitives=%.0f vram_mb=%.0f"),
        *View.Name, Frames, Mean(Stats.FrameMs), Percentile(Stats.FrameMs, 95), Percentile(Stats.FrameMs, 99), Mean(Stats.GameMs), Mean(Stats.RenderMs),
        Mean(Stats.RhiMs), Mean(Stats.GpuMs), Percentile(Stats.GpuMs, 95), Percentile(Stats.GpuMs, 99), Stats.Draws / N, Stats.Primitives / N, VramUsed);
}

void UMarburgumViewShotSubsystem::Finish(int32 Code, const TCHAR* Reason)
{
    bFinished = true;
    if (!StatRows.IsEmpty() && !Directory.IsEmpty())
    {
        // The first eight columns are the original means, so older stats.tsv files still line up.
        StatRows.Insert(TEXT("view\tframes\tframe_ms\tgame_ms\trender_ms\tgpu_ms\tdraw_calls\tprimitives\t")
            TEXT("frame_p50\tframe_p95\tframe_p99\tgame_p95\tgame_p99\trender_p95\trender_p99\trhi_ms\trhi_p95\tgpu_p50\tgpu_p95\tgpu_p99\t")
            TEXT("vram_used_mb\tvram_budget_mb\ttexture_streaming_mb\ttexture_nonstreaming_mb\tresident_mb\tplayer_view"), 0);
        FFileHelper::SaveStringArrayToFile(StatRows, *FPaths::Combine(Directory, TEXT("stats.tsv")));
    }
    UE_LOG(LogMarburgumViewShot, Display, TEXT("MB2VIEWSHOT finished code=%d reason=%s shots=%d"), Code, Reason, FMath::Max(Current, 0));
    FPlatformMisc::RequestExitWithStatus(false, static_cast<uint8>(Code));
}
