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
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumViewShot, Log, All);

namespace MarburgumViewShot
{
    // The first frames after a map load are not representative (streaming, exposure, shaders).
    constexpr double FirstViewSettleSeconds = 6.0;
    // Each new view: let exposure and texture streaming catch up.
    constexpr double ViewSettleSeconds = 2.5;
    constexpr double ShotTimeoutSeconds = 15.0;
    constexpr double TotalTimeoutSeconds = 600.0;

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
}

using namespace MarburgumViewShot;

bool UMarburgumViewShotSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
    FString Ignored;
    return GameInstance && !GameInstance->IsDedicatedServerInstance()
        && FParse::Value(FCommandLine::Get(), TEXT("MB2ViewShots="), Ignored);
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
        if (Fields.Num() == 4 && ParseVector(Fields[1], View.Eye) && ParseVector(Fields[2], View.Target))
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
    StartSeconds = FPlatformTime::Seconds();
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMarburgumViewShotSubsystem::Tick), 0.f);
    UE_LOG(LogMarburgumViewShot, Display, TEXT("MB2VIEWSHOT start views=%d dir=%s"), Views.Num(), *Directory);
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
    if (Now - StartSeconds > TotalTimeoutSeconds)
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
        if (GEngine && GEngine->GameViewport)
        {
            GEngine->GameViewport->RemoveAllViewportWidgets();
        }
        Current = -1;
        bRequested = false;
    }
    if (bRequested)
    {
        if (!IFileManager::Get().FileExists(*PendingFile))
        {
            if (Now - ViewSeconds > ViewSettleSeconds + ShotTimeoutSeconds)
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
        if (Now - ViewSeconds > ViewSettleSeconds)
        {
            PendingFile = FPaths::Combine(Directory, Views[Current].Name + TEXT(".png"));
            IFileManager::Get().Delete(*PendingFile, false, true, true);
            FScreenshotRequest::RequestScreenshot(PendingFile, false, false);
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
    Camera->SetActorLocationAndRotation(View.Eye, (View.Target - View.Eye).Rotation());
    Camera->GetCameraComponent()->SetFieldOfView(View.Fov);
    Controller->SetViewTarget(Camera.Get());
    ViewSeconds = Now;
    return true;
}

void UMarburgumViewShotSubsystem::Finish(int32 Code, const TCHAR* Reason)
{
    bFinished = true;
    UE_LOG(LogMarburgumViewShot, Display, TEXT("MB2VIEWSHOT finished code=%d reason=%s shots=%d"), Code, Reason, FMath::Max(Current, 0));
    FPlatformMisc::RequestExitWithStatus(false, static_cast<uint8>(Code));
}
