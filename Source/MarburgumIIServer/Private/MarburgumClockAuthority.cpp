#include "MarburgumClockAuthority.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "MarburgumGameState.h"
#include "MarburgumWorldClock.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarburgumClock, Log, All);

namespace
{
    /** How often the game state's copy is checked against the clock. */
    constexpr float CheckSeconds = 30.f;
    /** Republish once the copy is this many real seconds out (world time and the platform clock drift apart). */
    constexpr double DriftRealSeconds = 0.25;

    struct FProcessClock
    {
        FDateTime Start;
        double RealStart = 0.0;
        double Speed = 1.0;
        bool bReview = false;

        FProcessClock()
        {
            RealStart = FPlatformTime::Seconds();
            Start = FDateTime::UtcNow();
            const TCHAR* Command = FCommandLine::Get();
            FString Text;
            if (FParse::Value(Command, TEXT("MB2Clock="), Text) || FParse::Value(Command, TEXT("MB2WeatherClock="), Text))
            {
                FDateTime Parsed;
                if (FDateTime::ParseIso8601(*Text, Parsed))
                {
                    Start = Parsed;
                    bReview = true;
                }
                else
                {
                    UE_LOG(LogMarburgumClock, Error, TEXT("MB2CLOCK role=server event=bad_clock value=\"%s\": running on UTC"), *Text);
                }
            }
            double Value = 1.0;
            if ((FParse::Value(Command, TEXT("MB2ClockSpeed="), Value) || FParse::Value(Command, TEXT("MB2WeatherSpeed="), Value))
                && FMath::IsFinite(Value) && Value > 0.0)
            {
                Speed = Value;
                bReview = bReview || Speed != 1.0;
            }
        }

        FDateTime Now() const
        {
            return bReview ? Start + FTimespan::FromSeconds((FPlatformTime::Seconds() - RealStart) * Speed) : FDateTime::UtcNow();
        }
    };

    const FProcessClock& ProcessClock()
    {
        static const FProcessClock Clock;
        return Clock;
    }
}

namespace MarburgumII::ServerClock
{
    FDateTime Now()
    {
        return ProcessClock().Now();
    }

    double Speed()
    {
        return ProcessClock().Speed;
    }

    bool IsReview()
    {
        return ProcessClock().bReview;
    }
}

bool UMarburgumClockAuthority::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

void UMarburgumClockAuthority::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    const ENetMode NetMode = InWorld.GetNetMode();
    if ((NetMode != NM_DedicatedServer && NetMode != NM_ListenServer) || !InWorld.GetGameState<AMarburgumGameState>())
    {
        return;
    }
    Publish();
    InWorld.GetTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMarburgumClockAuthority::Publish), CheckSeconds, true);
}

void UMarburgumClockAuthority::Deinitialize()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(Timer);
    }
    Super::Deinitialize();
}

void UMarburgumClockAuthority::Publish()
{
    UWorld* World = GetWorld();
    AMarburgumGameState* GameState = World ? World->GetGameState<AMarburgumGameState>() : nullptr;
    if (!GameState)
    {
        return;
    }
    const FDateTime Now = MarburgumII::ServerClock::Now();
    const double Speed = MarburgumII::ServerClock::Speed();
    const double WorldSeconds = World->GetTimeSeconds();
    const FMarburgumClock& Current = GameState->Clock;
    const bool bWasKnown = Current.bKnown;
    if (bWasKnown)
    {
        const double Drift = FMath::Abs((Current.At(WorldSeconds) - Now).GetTotalSeconds()) / Speed;
        if (Drift < DriftRealSeconds)
        {
            return;
        }
    }
    FMarburgumClock Next;
    Next.bKnown = true;
    Next.UtcTicks = Now.GetTicks();
    Next.ServerSeconds = WorldSeconds;
    Next.Speed = Speed;
    GameState->SetClock(Next);

    const MarburgumII::Clock::FCivil Local = MarburgumII::Clock::MarburgumAt(Now);
    const MarburgumII::Clock::FSkyPosition Sun = MarburgumII::Clock::SunAt(Now);
    UE_LOG(LogMarburgumClock, Log, TEXT("MB2CLOCK role=server event=%s utc=%s marburgum=\"%s %s\" offset=%+d speed=%.3f review=%d sun_elevation=%.2f sun_azimuth=%.1f"),
        bWasKnown ? TEXT("republished") : TEXT("started"), *Now.ToIso8601(), *MarburgumII::Clock::DateLabel(Local),
        *MarburgumII::Clock::TimeLabel(Local), Local.UtcOffsetHours, Speed, MarburgumII::ServerClock::IsReview() ? 1 : 0,
        Sun.ElevationDeg, Sun.AzimuthDeg);
}
