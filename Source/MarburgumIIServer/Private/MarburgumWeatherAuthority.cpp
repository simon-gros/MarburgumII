#include "MarburgumWeatherAuthority.h"

#include "Engine/World.h"
#include "MarburgumClockAuthority.h"
#include "MarburgumGameState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumWeather, Log, All);

namespace
{
    namespace Weather = MarburgumII::Weather;

    constexpr float UpdateSeconds = 1.f;
    /** A status line this often even when nothing named changes, so a smoke can read the drift. */
    constexpr int32 StatusEveryUpdates = 30;
    constexpr uint64 DefaultWorldSeed = 1996;

    float Round(double Value, double Step)
    {
        return static_cast<float>(FMath::RoundToDouble((FMath::IsFinite(Value) ? Value : 0.0) / Step) * Step);
    }
}

bool UMarburgumWeatherAuthority::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

void UMarburgumWeatherAuthority::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    const ENetMode NetMode = InWorld.GetNetMode();
    if ((NetMode != NM_DedicatedServer && NetMode != NM_ListenServer) || !InWorld.GetGameState<AMarburgumGameState>())
    {
        return;
    }

    const TCHAR* Command = FCommandLine::Get();
    FString Requested = TEXT("auto");
    FParse::Value(Command, TEXT("MB2Weather="), Requested);
    Requested = Requested.TrimStartAndEnd().ToLower();

    uint64 Seed = DefaultWorldSeed;
    FString SeedText;
    if (FParse::Value(Command, TEXT("MB2WorldSeed="), SeedText) && !SeedText.IsEmpty())
    {
        Seed = FCString::Strtoui64(*SeedText, nullptr, 10);
    }

    if (Requested == TEXT("auto"))
    {
        FString Error;
        const FString Directory = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Marburgum"), TEXT("Weather"));
        if (Weather::LoadClimate(Directory, Climate, Error))
        {
            Simulation.Configure(&Climate, Seed);
            Simulation.Reconstruct(WeatherUtc());
        }
        else
        {
            UE_LOG(LogMarburgumWeather, Warning, TEXT("MB2WEATHER role=server event=no_climate error=\"%s\": running clear_dry"), *Error);
            Fixed = Weather::DefaultProfile();
        }
    }
    else
    {
        Weather::FState Profile;
        if (Weather::TryProfile(Requested, Profile))
        {
            Fixed = Profile;
        }
        else
        {
            UE_LOG(LogMarburgumWeather, Error, TEXT("MB2WEATHER role=server event=unknown_profile value=\"%s\": running clear_dry"), *Requested);
            Fixed = Weather::DefaultProfile();
        }
    }

    UE_LOG(LogMarburgumWeather, Log, TEXT("MB2WEATHER role=server event=started source=%s seed=%llu clock=%s speed=%.3f review_clock=%d"),
        Fixed ? *Fixed->Source : TEXT("auto"), Seed, *WeatherUtc().ToIso8601(), MarburgumII::ServerClock::Speed(),
        MarburgumII::ServerClock::IsReview() ? 1 : 0);
    Update();
    InWorld.GetTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMarburgumWeatherAuthority::Update), UpdateSeconds, true);
}

void UMarburgumWeatherAuthority::Deinitialize()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(Timer);
    }
    Super::Deinitialize();
}

FDateTime UMarburgumWeatherAuthority::WeatherUtc() const
{
    return MarburgumII::ServerClock::Now();
}

FMarburgumWeather UMarburgumWeatherAuthority::ToReplicated(const Weather::FState& State)
{
    FMarburgumWeather Out;
    Out.bKnown = true;
    Out.Source = FName(*State.Source);
    Out.State = FName(*State.State);
    Out.PrecipitationType = FName(*State.PrecipitationType);
    Out.CloudCover = Round(State.CloudCover, 0.001);
    Out.Precipitation = Round(State.Precipitation, 0.001);
    Out.Wetness = Round(State.Wetness, 0.001);
    Out.TemperatureC = Round(State.TemperatureC, 0.1);
    Out.SnowDepthM = Round(State.SnowDepthM, 0.001);
    Out.Fog = Round(State.Fog, 0.001);
    Out.Thunder = Round(State.Thunder, 0.001);
    Out.WindSpeed = Round(State.WindSpeed, 0.1);
    Out.WindDirectionDegrees = Round(State.WindDirectionDegrees, 1.0);
    Out.Gust = Round(State.Gust, 0.01);
    return Out;
}

void UMarburgumWeatherAuthority::Update()
{
    AMarburgumGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AMarburgumGameState>() : nullptr;
    if (!GameState)
    {
        return;
    }
    const FDateTime Now = WeatherUtc();
    Weather::FState State;
    if (Fixed)
    {
        State = *Fixed;
    }
    else
    {
        Simulation.AdvanceTo(Now);
        State = Simulation.CurrentState();
    }
    GameState->SetWeather(ToReplicated(State));

    ++UpdatesSinceLog;
    if (State.State != LoggedState || State.PrecipitationType != LoggedType || UpdatesSinceLog >= StatusEveryUpdates)
    {
        const bool bChanged = State.State != LoggedState || State.PrecipitationType != LoggedType;
        UE_LOG(LogMarburgumWeather, Log, TEXT("MB2WEATHER role=server event=%s utc=%s source=%s state=%s type=%s precipitation=%.3f wetness=%.3f temperature=%.1f snow=%.3f cloud=%.2f wind=%.1f"),
            bChanged ? TEXT("changed") : TEXT("status"), *Now.ToIso8601(), *State.Source, *State.State, *State.PrecipitationType,
            State.Precipitation, State.Wetness, State.TemperatureC, State.SnowDepthM, State.CloudCover, State.WindSpeed);
        LoggedState = State.State;
        LoggedType = State.PrecipitationType;
        UpdatesSinceLog = 0;
    }
}
