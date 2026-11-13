#include "MarburgumPlaceSmokeSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlaceRoute.h"
#include "MarburgumPlaces.h"
#include "MarburgumPlayerController.h"
#include "MarburgumPlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "UI/MarburgumLocationBanner.h"
#include "UI/MarburgumUISubsystem.h"
#include "UnrealClient.h"

#define MB2_PLACE_SMOKE_LOG(Format, ...) UE_LOG(LogMarburgumNet, Log, TEXT("MB2PSMOKE role=client " Format), ##__VA_ARGS__)

namespace MarburgumPlaceSmoke
{
    using namespace MarburgumII::PlaceRoute;

    /** How near the stop the body must be to have arrived: the server sets it down on the spot. */
    constexpr double ArrivedWithinCm = 300.0;
    /** How long the banner has to show the stop once the body is there: a few place ticks and replication. */
    constexpr double MatchSeconds = 3.5;
    /** After the match, how long to let the banner fade in and the camera settle before the capture. */
    constexpr double ShotDelaySeconds = 0.8;
    constexpr double ShotTimeoutSeconds = 10.0;

    int32 StopCount()
    {
        return UE_ARRAY_COUNT(Stops);
    }
}

using namespace MarburgumPlaceSmoke;

bool UMarburgumPlaceSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
    return GameInstance && !GameInstance->IsDedicatedServerInstance() && FParse::Param(FCommandLine::Get(), ClientOption);
}

void UMarburgumPlaceSmokeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    FParse::Value(FCommandLine::Get(), TEXT("MB2PlaceShot="), ShotPrefix);
    FParse::Value(FCommandLine::Get(), TEXT("MB2PlaceTimeout="), TimeoutSeconds);
    StartSeconds = FPlatformTime::Seconds();
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMarburgumPlaceSmokeSubsystem::Tick), 0.f);
    const MarburgumII::Places::FCatalogue& Places = MarburgumII::Places::Get();
    MB2_PLACE_SMOKE_LOG("event=start stops=%d shot=%d places_loaded=%d districts=%d streets=%d landmarks=%d", StopCount(),
        ShotPrefix.IsEmpty() ? 0 : 1, Places.IsLoaded() ? 1 : 0, Places.GetDistricts().Num(), Places.GetStreets().Num(), Places.GetLandmarks().Num());
}

void UMarburgumPlaceSmokeSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    Super::Deinitialize();
}

bool UMarburgumPlaceSmokeSubsystem::Bind()
{
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (!UMarburgumSessionSubsystem::IsAdmitted(World))
    {
        return false;
    }
    AMarburgumPlayerController* Local = Cast<AMarburgumPlayerController>(World->GetFirstPlayerController());
    if (!Local || !Local->GetPawn() || !Local->PlayerState || !Banner())
    {
        return false;
    }
    if (Controller.Get() != Local)
    {
        Controller = Local;
        MB2_PLACE_SMOKE_LOG("event=ready player=%d at=%s", Local->PlayerState->GetPlayerId(), *Local->GetPawn()->GetActorLocation().ToString());
    }
    return true;
}

const UMarburgumLocationBanner* UMarburgumPlaceSmokeSubsystem::Banner() const
{
    const UMarburgumUISubsystem* UI = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>() : nullptr;
    return UI ? UI->GetLocationBanner() : nullptr;
}

bool UMarburgumPlaceSmokeSubsystem::Tick(float DeltaTime)
{
    if (bDone)
    {
        return true;
    }
    const double Now = FPlatformTime::Seconds();
    if (Now - StartSeconds > TimeoutSeconds)
    {
        Fail(FString::Printf(TEXT("timeout_at_stop=%s"), Stop < StopCount() ? Stops[Stop].Id : TEXT("end")));
        return true;
    }
    if (!PendingShot.IsEmpty())
    {
        const bool bSaved = IFileManager::Get().FileSize(*PendingShot) > 0;
        if (!bSaved && Now - ShotRequestedSeconds < ShotTimeoutSeconds)
        {
            return true;
        }
        MB2_PLACE_SMOKE_LOG("event=shot file=%s saved=%d", *FPaths::GetCleanFilename(PendingShot), bSaved ? 1 : 0);
        Expect(bSaved, FString::Printf(TEXT("shot_not_saved=%s"), *FPaths::GetCleanFilename(PendingShot)));
        PendingShot.Reset();
    }
    if (!Bind())
    {
        return true;
    }
    if (Stop >= StopCount())
    {
        Pass();
        return true;
    }

    const FStop& Want = Stops[Stop];
    AMarburgumPlayerController* Local = Controller.Get();
    const FVector Body = Local->GetPawn()->GetActorLocation();
    const FVector2D Target = MarburgumII::Places::FromPlan(Want.East, Want.South);
    if (ArrivedAt < 0.0)
    {
        if (FVector2D::Distance(FVector2D(Body), Target) > ArrivedWithinCm)
        {
            return true;
        }
        ArrivedAt = Now;
        Local->SetControlRotation(FRotator(-8.f, Want.Yaw, 0.f));
        MB2_PLACE_SMOKE_LOG("event=arrived stop=%s at=%s", Want.Id, *Body.ToString());
    }

    const UMarburgumLocationBanner* Shown = Banner();
    const FString Place = Shown->GetPlaceText();
    const FString Street = Shown->GetStreetText();
    const FString Landmark = Shown->GetLandmarkText();
    const bool bMatch = Place == Want.Place && Street == Want.Street && Landmark == Want.Landmark;
    if (bMatch && MatchedAt < 0.0)
    {
        MatchedAt = Now;
    }
    const bool bTimedOut = Now - ArrivedAt > MatchSeconds;
    if (!(MatchedAt >= 0.0 && Now - MatchedAt >= ShotDelaySeconds) && !bTimedOut)
    {
        return true;
    }

    const AMarburgumPlayerState* State = Local->GetPlayerState<AMarburgumPlayerState>();
    const FMarburgumPlace& Ids = State ? State->GetPlace() : FMarburgumPlace();
    const int32 Announcements = Shown->GetAnnouncements();
    MB2_PLACE_SMOKE_LOG("event=stop stop=%s place=\"%s\" street=\"%s\" landmark=\"%s\" area=%s district=%s landmark_eid=%s showing=%d announcements=%d match=%d",
        Want.Id, *Place, *Street, *Landmark, State && !State->GetAreaId().IsNone() ? *State->GetAreaId().ToString() : TEXT("-"),
        Ids.District.IsEmpty() ? TEXT("-") : *Ids.District, Ids.Landmark.IsEmpty() ? TEXT("-") : *Ids.Landmark,
        Shown->IsShowing() ? 1 : 0, Announcements, bMatch ? 1 : 0);
    Expect(bMatch, FString::Printf(TEXT("%s_shows=%s|%s|%s"), Want.Id, *Place, *Street, *Landmark));
    // Every stop is a new place, street or landmark, so each one brings the banner up.
    Expect(Shown->IsShowing(), FString::Printf(TEXT("%s_banner_not_showing"), Want.Id));
    Expect(Announcements > AnnouncementsBefore, FString::Printf(TEXT("%s_not_announced"), Want.Id));
    AnnouncementsBefore = Announcements;
    Shot(Want.Id);
    ++Stop;
    ArrivedAt = -1.0;
    MatchedAt = -1.0;
    return true;
}

void UMarburgumPlaceSmokeSubsystem::Shot(const TCHAR* Label)
{
    if (ShotPrefix.IsEmpty())
    {
        return;
    }
    PendingShot = FString::Printf(TEXT("%s-%s.png"), *ShotPrefix, Label);
    IFileManager::Get().Delete(*PendingShot, false, true, true);
    FScreenshotRequest::RequestScreenshot(PendingShot, true, false);
    ShotRequestedSeconds = FPlatformTime::Seconds();
}

void UMarburgumPlaceSmokeSubsystem::Expect(bool bCondition, const FString& What)
{
    if (!bCondition)
    {
        Problems.Add(What);
        MB2_PLACE_SMOKE_LOG("event=expectation_failed what=%s", *What);
    }
}

void UMarburgumPlaceSmokeSubsystem::Pass()
{
    if (!Problems.IsEmpty())
    {
        Fail(TEXT("expectations"));
        return;
    }
    MB2_PLACE_SMOKE_LOG("event=pass stops=%d", StopCount());
    bDone = true;
    FPlatformMisc::RequestExitWithStatus(false, 0);
}

void UMarburgumPlaceSmokeSubsystem::Fail(const FString& Reason)
{
    if (bDone)
    {
        return;
    }
    Problems.Add(Reason);
    MB2_PLACE_SMOKE_LOG("event=fail reason=%s", *FString::Join(Problems, TEXT(",")));
    bDone = true;
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

#undef MB2_PLACE_SMOKE_LOG
