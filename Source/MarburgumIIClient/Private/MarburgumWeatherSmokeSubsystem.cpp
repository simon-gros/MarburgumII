#include "MarburgumWeatherSmokeSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "MarburgumCharacter.h"
#include "MarburgumFootsteps.h"
#include "MarburgumGameState.h"
#include "MarburgumGround.h"
#include "MarburgumNetworkContract.h"
#include "MarburgumPlayerController.h"
#include "MarburgumSurface.h"
#include "MarburgumVehicle.h"
#include "MarburgumWeather.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "Materials/MaterialInterface.h"
#include "Session/MarburgumSessionSubsystem.h"
#include "UI/MarburgumLocationBanner.h"
#include "UI/MarburgumUISubsystem.h"
#include "UnrealClient.h"

#define MB2_WEATHER_SMOKE_LOG(Format, ...) \
    UE_LOG(LogMarburgumNet, Log, TEXT("MB2WSMOKE role=client expect=%s " Format), *Expected, ##__VA_ARGS__)

namespace MarburgumWeatherSmoke
{
    namespace Surface = MarburgumII::Surface;

    const FName CarId(TEXT("lent_hatchback"));
    const FName DriverSeat(TEXT("driver"));
    constexpr double SettleSeconds = 2.0;
    constexpr double ShotTimeoutSeconds = 10.0;
    constexpr double AnswerTimeoutSeconds = 5.0;
    constexpr float Throttle = 0.8f;
    constexpr double ThrottleSeconds = 3.0;
    constexpr double BrakeFromMinSpeed = 3.0;
    constexpr double StoppedSpeed = 0.2;
    constexpr double StoppedHoldSeconds = 0.4;
    constexpr double ExitSettleSeconds = 1.0;
    constexpr double WalkSeconds = 4.0;
    constexpr int32 WalkMinSteps = 4;
}

using namespace MarburgumWeatherSmoke;

bool UMarburgumWeatherSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
    return GameInstance && !GameInstance->IsDedicatedServerInstance() && FParse::Param(FCommandLine::Get(), TEXT("MB2WeatherSmoke"));
}

void UMarburgumWeatherSmokeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const TCHAR* CommandLine = FCommandLine::Get();
    FParse::Value(CommandLine, TEXT("MB2WeatherExpect="), Expected);
    FParse::Value(CommandLine, TEXT("MB2WeatherShot="), ShotPrefix);
    FParse::Value(CommandLine, TEXT("MB2WeatherTimeout="), TimeoutSeconds);
    FParse::Value(CommandLine, TEXT("MB2WeatherWetAtLeast="), WetAtLeast);
    Expected = Expected.ToLower();
    StartSeconds = FPlatformTime::Seconds();
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMarburgumWeatherSmokeSubsystem::Tick), 0.f);
    MB2_WEATHER_SMOKE_LOG("event=start shot=%d", ShotPrefix.IsEmpty() ? 0 : 1);
}

void UMarburgumWeatherSmokeSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    Super::Deinitialize();
}

AMarburgumCharacter* UMarburgumWeatherSmokeSubsystem::Body() const
{
    const AMarburgumPlayerController* Local = Controller.Get();
    return Local ? Cast<AMarburgumCharacter>(Local->GetPawn()) : nullptr;
}

bool UMarburgumWeatherSmokeSubsystem::Bind()
{
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (!UMarburgumSessionSubsystem::IsAdmitted(World))
    {
        return false;
    }
    AMarburgumPlayerController* Local = Cast<AMarburgumPlayerController>(World->GetFirstPlayerController());
    const AMarburgumCharacter* Character = Local ? Cast<AMarburgumCharacter>(Local->GetPawn()) : nullptr;
    if (!Character || !Local->PlayerState)
    {
        return false;
    }
    if (!Vehicle.IsValid())
    {
        TArray<AMarburgumVehicle*> Cars;
        AMarburgumVehicle::GetAll(World, Cars);
        for (AMarburgumVehicle* Candidate : Cars)
        {
            if (Candidate->VehicleId == CarId)
            {
                Vehicle = Candidate;
            }
        }
        if (!Vehicle.IsValid())
        {
            return false;
        }
    }
    if (Controller.Get() != Local)
    {
        Controller = Local;
        Local->OnVehicleResult.AddUObject(this, &UMarburgumWeatherSmokeSubsystem::OnVehicleResult);
        StepSeconds = FPlatformTime::Seconds();
        Local->SetControlRotation(FRotator(-12.f, Character->GetActorRotation().Yaw, 0.f));
        MB2_WEATHER_SMOKE_LOG("event=ready player=%d at=%s landscape_map=%d", Local->PlayerState->GetPlayerId(),
            *Character->GetActorLocation().ToString(), MarburgumII::Ground::HasLandscapeMap() ? 1 : 0);
    }
    return true;
}

void UMarburgumWeatherSmokeSubsystem::OnVehicleResult(bool bOk, FName Action, FName Reason)
{
    bAnswered = true;
    bAnswerOk = bOk;
    AnswerReason = Reason;
    MB2_WEATHER_SMOKE_LOG("event=answer action=%s ok=%d reason=%s", *Action.ToString(), bOk ? 1 : 0, Reason.IsNone() ? TEXT("-") : *Reason.ToString());
}

void UMarburgumWeatherSmokeSubsystem::Expect(bool bCondition, const FString& What)
{
    if (!bCondition)
    {
        Problems.Add(What);
        MB2_WEATHER_SMOKE_LOG("event=expectation_failed what=%s", *What);
    }
}

void UMarburgumWeatherSmokeSubsystem::Shot(const TCHAR* Label)
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

void UMarburgumWeatherSmokeSubsystem::Advance(int32 Next, double Now)
{
    Step = Next;
    StepSeconds = Now;
}

FString UMarburgumWeatherSmokeSubsystem::BannerWeather() const
{
    const UMarburgumUISubsystem* UI = GetGameInstance()->GetSubsystem<UMarburgumUISubsystem>();
    const UMarburgumLocationBanner* Banner = UI ? UI->GetLocationBanner() : nullptr;
    return Banner ? Banner->GetWeatherText() : FString(TEXT("-"));
}

FString UMarburgumWeatherSmokeSubsystem::SurfaceUnder(const FVector& At, bool bLog, FString* OutClass) const
{
    UWorld* World = GetGameInstance()->GetWorld();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MarburgumWeatherSmoke), false);
    Params.bReturnPhysicalMaterial = true;
    if (const AMarburgumCharacter* Character = Body())
    {
        Params.AddIgnoredActor(Character);
    }
    if (const AMarburgumVehicle* Car = Vehicle.Get())
    {
        Params.AddIgnoredActor(Car);
    }
    FHitResult Hit;
    if (!World || !World->LineTraceSingleByChannel(Hit, At + FVector(0.0, 0.0, 100.0), At - FVector(0.0, 0.0, 400.0), ECC_Visibility, Params))
    {
        return TEXT("none");
    }
    const UPrimitiveComponent* Component = Hit.GetComponent();
    if (OutClass)
    {
        *OutClass = Component ? Component->GetClass()->GetName() : FString(TEXT("-"));
    }
    if (!bLog)
    {
        return MarburgumII::Ground::SurfaceOf(Hit);
    }
    MB2_WEATHER_SMOKE_LOG("event=ground_hit at=%s component=%s class=%s owner=%s physical=%s face=%d", *Hit.ImpactPoint.ToString(),
        Component ? *Component->GetName() : TEXT("-"), Component ? *Component->GetClass()->GetName() : TEXT("-"),
        Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("-"), Hit.PhysMaterial.IsValid() ? *Hit.PhysMaterial->GetName() : TEXT("-"), Hit.FaceIndex);
    if (const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Component))
    {
        FString Materials;
        for (int32 Index = 0; Index < Mesh->GetNumMaterials(); ++Index)
        {
            const UMaterialInterface* Material = Mesh->GetMaterial(Index);
            const UPhysicalMaterial* Physical = Material ? Material->GetPhysicalMaterial() : nullptr;
            Materials += FString::Printf(TEXT("%s:%s;"), Material ? *Material->GetName() : TEXT("-"), Physical ? *Physical->GetName() : TEXT("-"));
        }
        MB2_WEATHER_SMOKE_LOG("event=ground_mesh mesh=%s complex_as_simple=%d materials=%s", Mesh->GetStaticMesh() ? *Mesh->GetStaticMesh()->GetName() : TEXT("-"),
            Mesh->GetStaticMesh() && Mesh->GetStaticMesh()->GetBodySetup() ? int32(Mesh->GetStaticMesh()->GetBodySetup()->CollisionTraceFlag) : -1, *Materials);
    }
    return MarburgumII::Ground::SurfaceOf(Hit);
}

void UMarburgumWeatherSmokeSubsystem::Survey(const FVector& Around)
{
    // The ground on a 41 x 41 grid of 2 m steps round the start: what the paving and the landscape say they are.
    TMap<FString, int32> Paving;
    TMap<FString, int32> Landscape;
    int32 Other = 0;
    for (int32 Row = -20; Row <= 20; ++Row)
    {
        for (int32 Column = -20; Column <= 20; ++Column)
        {
            FString Class;
            const FString Ground = SurfaceUnder(Around + FVector(Row * 200.0, Column * 200.0, 0.0), false, &Class);
            if (Class == TEXT("LandscapeHeightfieldCollisionComponent"))
            {
                ++Landscape.FindOrAdd(Ground);
            }
            else if (Class == TEXT("StaticMeshComponent") && Ground != Surface::Unknown)
            {
                ++Paving.FindOrAdd(Ground);
            }
            else
            {
                ++Other;
            }
        }
    }
    const auto Join = [](const TMap<FString, int32>& Counts)
    {
        FString Text;
        for (const TPair<FString, int32>& Count : Counts)
        {
            Text += FString::Printf(TEXT("%s:%d,"), *Count.Key, Count.Value);
        }
        return Text.IsEmpty() ? FString(TEXT("-")) : Text.LeftChop(1);
    };
    MB2_WEATHER_SMOKE_LOG("event=survey paving=%s landscape=%s other=%d", *Join(Paving), *Join(Landscape), Other);
    Expect(Paving.Num() >= 2, FString::Printf(TEXT("survey_paving=%s"), *Join(Paving)));
    Expect(Landscape.Num() >= 1 && !Landscape.Contains(Surface::Unknown), FString::Printf(TEXT("survey_landscape=%s"), *Join(Landscape)));
}

void UMarburgumWeatherSmokeSubsystem::ReportWeather(const TCHAR* Event)
{
    const FMarburgumWeather* Weather = AMarburgumGameState::WeatherOf(GetGameInstance()->GetWorld());
    if (!Weather)
    {
        MB2_WEATHER_SMOKE_LOG("event=%s weather=none", Event);
        return;
    }
    MB2_WEATHER_SMOKE_LOG("event=%s known=%d source=%s state=%s type=%s precipitation=%.3f wetness=%.3f temperature=%.1f snow=%.3f cloud=%.3f wind=%.1f banner=\"%s\"",
        Event, Weather->bKnown ? 1 : 0, *Weather->Source.ToString(), *Weather->State.ToString(),
        Weather->PrecipitationType.IsNone() ? TEXT("none") : *Weather->PrecipitationType.ToString(),
        Weather->Precipitation, Weather->Wetness, Weather->TemperatureC, Weather->SnowDepthM, Weather->CloudCover, Weather->WindSpeed, *BannerWeather());
}

bool UMarburgumWeatherSmokeSubsystem::Tick(float DeltaTime)
{
    if (bDone)
    {
        return true;
    }
    const double Now = FPlatformTime::Seconds();
    if (Now - StartSeconds > TimeoutSeconds)
    {
        ReportWeather(TEXT("timeout_state"));
        Fail(FString::Printf(TEXT("timeout step=%d"), Step));
        return true;
    }
    if (!Bind())
    {
        return true;
    }
    if (!PendingShot.IsEmpty())
    {
        const bool bSaved = IFileManager::Get().FileSize(*PendingShot) > 0;
        if (!bSaved && Now - ShotRequestedSeconds < ShotTimeoutSeconds)
        {
            return true;
        }
        MB2_WEATHER_SMOKE_LOG("event=shot file=%s saved=%d", *FPaths::GetCleanFilename(PendingShot), bSaved ? 1 : 0);
        Expect(bSaved, FString::Printf(TEXT("shot_not_saved=%s"), *FPaths::GetCleanFilename(PendingShot)));
        PendingShot.Reset();
    }
    if (Expected == TEXT("auto"))
    {
        TickLive(Now);
    }
    else
    {
        TickFixed(Now);
    }
    return true;
}

void UMarburgumWeatherSmokeSubsystem::TickFixed(double Now)
{
    AMarburgumCharacter* Character = Body();
    AMarburgumVehicle* Car = Vehicle.Get();
    AMarburgumPlayerController* Local = Controller.Get();
    const FMarburgumWeather* Weather = AMarburgumGameState::WeatherOf(GetGameInstance()->GetWorld());
    if (!Character || !Car || !Local || !Weather)
    {
        return;
    }
    const bool bWet = Expected == TEXT("rain_wet");
    switch (Step)
    {
    case 0:
        if (!Weather->bKnown || Now - StepSeconds < SettleSeconds)
        {
            if (Now - StepSeconds > 30.0) Fail(TEXT("weather_never_arrived"));
            return;
        }
        ReportWeather(TEXT("weather"));
        Expect(Weather->Source == FName(*Expected), FString::Printf(TEXT("source=%s"), *Weather->Source.ToString()));
        Expect(FMath::IsNearlyEqual(Weather->Wetness, bWet ? 1.f : 0.f), FString::Printf(TEXT("wetness=%.3f"), Weather->Wetness));
        Expect(Weather->PrecipitationType == (bWet ? FName(TEXT("rain")) : FName(TEXT("none"))), FString::Printf(TEXT("type=%s"), *Weather->PrecipitationType.ToString()));
        if (!ShotPrefix.IsEmpty())
        {
            Expect(BannerWeather().StartsWith(bWet ? TEXT("Rain") : TEXT("Clear")), FString::Printf(TEXT("banner_weather=%s"), *BannerWeather()));
        }
        MB2_WEATHER_SMOKE_LOG("event=surfaces body=%s car=%s", *SurfaceUnder(Character->GetActorLocation()), *SurfaceUnder(Car->GetActorLocation()));
        Survey(Character->GetActorLocation());
        Shot(TEXT("arrival"));
        bAnswered = false;
        Local->RequestEnterVehicle(Car, DriverSeat);
        Advance(1, Now);
        return;
    case 1:
        if (!bAnswered || Character->GetSeatedVehicle() != Car)
        {
            if (Now - StepSeconds > AnswerTimeoutSeconds) Fail(TEXT("never_seated"));
            return;
        }
        Expect(bAnswerOk, FString::Printf(TEXT("enter_refused=%s"), *AnswerReason.ToString()));
        Local->SetDrivingOverride(Throttle, 0.f, 0.f, false);
        Advance(2, Now);
        return;
    case 2:
        if (Now - StepSeconds < ThrottleSeconds) return;
        BrakeSpeed = Car->GetForwardSpeed();
        Mark = Car->GetActorLocation();
        Expect(BrakeSpeed >= BrakeFromMinSpeed, FString::Printf(TEXT("speed_at_brake=%.2f"), BrakeSpeed));
        MB2_WEATHER_SMOKE_LOG("event=brake speed=%.3f surface=%s", BrakeSpeed, *SurfaceUnder(Car->GetActorLocation()));
        Local->SetDrivingOverride(0.f, 1.f, 0.f, false);
        StillSince = -1.0;
        Advance(3, Now);
        return;
    case 3:
    {
        if (FMath::Abs(Car->GetForwardSpeed()) > StoppedSpeed)
        {
            StillSince = -1.0;
            if (Now - StepSeconds > 10.0) Fail(TEXT("never_stopped"));
            return;
        }
        if (StillSince < 0.0) StillSince = Now;
        if (Now - StillSince < StoppedHoldSeconds) return;
        const double StopM = FVector::Dist2D(Car->GetActorLocation(), Mark) / 100.0;
        const double Deceleration = StopM > 0.01 ? BrakeSpeed * BrakeSpeed / (2.0 * StopM) : 0.0;
        MB2_WEATHER_SMOKE_LOG("event=braked speed=%.3f stop_m=%.3f decel=%.3f seconds=%.2f surface=%s wetness=%.3f", BrakeSpeed, StopM, Deceleration,
            StillSince - StepSeconds, *SurfaceUnder(Car->GetActorLocation()), Weather->Wetness);
        Expect(StopM > 0.1, FString::Printf(TEXT("stop_m=%.3f"), StopM));
        Local->ClearDrivingOverride();
        bAnswered = false;
        Local->RequestExitVehicle();
        Advance(4, Now);
        return;
    }
    case 4:
        if (!bAnswered || Character->GetSeatedVehicle() || Now - StepSeconds < ExitSettleSeconds)
        {
            if (Now - StepSeconds > AnswerTimeoutSeconds + ExitSettleSeconds) Fail(TEXT("never_got_out"));
            return;
        }
        Expect(bAnswerOk, FString::Printf(TEXT("exit_refused=%s"), *AnswerReason.ToString()));
        Advance(5, Now);
        return;
    case 5:
        if (Walk(Now, bWet))
        {
            Advance(6, Now);
        }
        return;
    case 6:
        Pass();
        return;
    default:
        return;
    }
}

void UMarburgumWeatherSmokeSubsystem::TickLive(double Now)
{
    const FMarburgumWeather* Weather = AMarburgumGameState::WeatherOf(GetGameInstance()->GetWorld());
    if (!Weather || !Body())
    {
        return;
    }
    switch (Step)
    {
    case 0:
        if (!Weather->bKnown || Now - StepSeconds < SettleSeconds)
        {
            if (Now - StepSeconds > 30.0) Fail(TEXT("weather_never_arrived"));
            return;
        }
        ReportWeather(TEXT("weather"));
        Expect(Weather->Source == TEXT("auto"), FString::Printf(TEXT("source=%s"), *Weather->Source.ToString()));
        FirstState = Weather->State;
        FirstWetness = Weather->Wetness;
        LastState = Weather->State;
        Expect(FirstWetness < WetAtLeast, FString::Printf(TEXT("already_wet=%.3f"), FirstWetness));
        Shot(TEXT("live-start"));
        Advance(1, Now);
        return;
    case 1:
        if (Weather->State != LastState)
        {
            ++StateChanges;
            LastState = Weather->State;
            ReportWeather(TEXT("turned"));
        }
        if (Weather->Wetness < WetAtLeast || StateChanges == 0)
        {
            return;
        }
        ReportWeather(TEXT("soaked"));
        Expect(Weather->PrecipitationType == TEXT("rain") || Weather->Precipitation < 0.02, FString::Printf(TEXT("type=%s"), *Weather->PrecipitationType.ToString()));
        if (!ShotPrefix.IsEmpty())
        {
            const FString Want = MarburgumII::Weather::Summary(Weather->State.ToString(), Weather->TemperatureC, Weather->Wetness, Weather->SnowDepthM);
            Expect(BannerWeather() == Want, FString::Printf(TEXT("banner_weather=%s want=%s"), *BannerWeather(), *Want));
            Expect(BannerWeather().Contains(TEXT("wet streets")), FString::Printf(TEXT("banner_not_wet=%s"), *BannerWeather()));
        }
        Shot(TEXT("live-wet"));
        Advance(2, Now);
        return;
    case 2:
        if (Walk(Now, true))
        {
            Advance(3, Now);
        }
        return;
    case 3:
        Pass();
        return;
    default:
        return;
    }
}

bool UMarburgumWeatherSmokeSubsystem::Walk(double Now, bool bExpectWet)
{
    AMarburgumCharacter* Character = Body();
    const AMarburgumVehicle* Car = Vehicle.Get();
    UWorld* World = GetGameInstance()->GetWorld();
    UMarburgumFootstepSubsystem* Footsteps = World ? World->GetSubsystem<UMarburgumFootstepSubsystem>() : nullptr;
    if (!Character || !Footsteps)
    {
        Fail(TEXT("no_footsteps_subsystem"));
        return false;
    }
    if (!bWalking)
    {
        // First frame of the walk: count from here.
        bWalking = true;
        StepSeconds = Now;
        StepsBefore = Footsteps->GetLocalSteps();
        SetsBefore = Footsteps->GetLocalStepsBySet();
        Mark = Character->GetActorLocation();
    }
    const FVector Away = Car ? (Character->GetActorLocation() - Car->GetActorLocation()).GetSafeNormal2D() : Character->GetActorForwardVector();
    Character->AddMovementInput(Away.IsNearlyZero() ? Character->GetActorForwardVector() : Away, 1.f);
    if (Now - StepSeconds < WalkSeconds)
    {
        return false;
    }
    const auto Delta = [&](FName Set) { return Footsteps->GetLocalStepsBySet().FindRef(Set) - SetsBefore.FindRef(Set); };
    const int32 Steps = Footsteps->GetLocalSteps() - StepsBefore;
    const int32 Hard = Delta(Surface::FootstepHard);
    const int32 Wet = Delta(Surface::FootstepHardWet);
    const int32 Soft = Delta(Surface::FootstepSoft);
    const int32 Crunch = Delta(Surface::FootstepCrunch);
    const double Walked = FVector::Dist2D(Character->GetActorLocation(), Mark) / 100.0;
    MB2_WEATHER_SMOKE_LOG("event=walked metres=%.2f steps=%d hard=%d hard_wet=%d soft=%d crunch=%d surface=%s without_sound=%d all_steps=%d",
        Walked, Steps, Hard, Wet, Soft, Crunch, *Footsteps->GetLocalSurface(), Footsteps->GetStepsWithoutSound(), Footsteps->GetStepsPlayed());
    Expect(Walked > 4.0, FString::Printf(TEXT("walked_m=%.2f"), Walked));
    Expect(Steps >= WalkMinSteps, FString::Printf(TEXT("steps=%d"), Steps));
    Expect(Steps <= FMath::CeilToInt(WalkSeconds / 0.16), FString::Printf(TEXT("too_many_steps=%d"), Steps));
    Expect(Footsteps->GetStepsWithoutSound() == 0, FString::Printf(TEXT("steps_without_sound=%d"), Footsteps->GetStepsWithoutSound()));
    if (bExpectWet)
    {
        Expect(Hard == 0 || Hard < Wet, FString::Printf(TEXT("dry_steps_in_rain=%d"), Hard));
        Expect(Wet + Soft + Crunch > 0, TEXT("no_wet_or_soft_steps"));
    }
    else
    {
        Expect(Wet == 0, FString::Printf(TEXT("wet_steps_when_dry=%d"), Wet));
    }
    Shot(TEXT("walking"));
    return true;
}

void UMarburgumWeatherSmokeSubsystem::Pass()
{
    if (Problems.IsEmpty())
    {
        MB2_WEATHER_SMOKE_LOG("event=pass");
        bDone = true;
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }
    else
    {
        Fail(TEXT("expectations"));
    }
}

void UMarburgumWeatherSmokeSubsystem::Fail(const FString& Reason)
{
    if (bDone)
    {
        return;
    }
    Problems.Add(Reason);
    MB2_WEATHER_SMOKE_LOG("event=fail reason=%s", *FString::Join(Problems, TEXT(",")));
    bDone = true;
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

#undef MB2_WEATHER_SMOKE_LOG
