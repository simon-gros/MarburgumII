#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarburgumWeatherSmokeSubsystem.generated.h"

class AMarburgumCharacter;
class AMarburgumPlayerController;
class AMarburgumVehicle;

/**
 * Weather driver (ADR-061), active only with -MB2WeatherSmoke. One client against a real dedicated server on
 * /Game/World/lent, starting beside the authored lent_hatchback's driver door (?MB2Start=lent_hatchback); exits 0 when
 * its expectations held, else 1. See scripts/weather-smoke-linux.sh.
 *
 * Fixed weather (-MB2WeatherExpect=clear_dry or rain_wet): the replicated weather is that profile; the client gets in,
 * drives off at 0.8 throttle, brakes fully and reports the deceleration (the script compares dry with wet), gets out
 * and walks; its own footsteps are hard and dry on a dry street and splash on a wet one.
 *
 * Live weather (-MB2WeatherExpect=auto): the server runs the climate on a fast review clock. The client watches the
 * replicated weather turn: a new hourly state, rain setting in and the streets soaking to at least
 * -MB2WeatherWetAtLeast (default 0.5), with the HUD's weather line following; then it walks and its steps splash.
 *
 * Every phase also checks what the client draws (ADR-062): the weather effects actor's look, the rain curtain, the
 * clouds, the level's sun and fog against their own, and MPC_Weather's wetness for the ground.
 *
 * Look only (-MB2WeatherLookOnly=overcast, snow_lying or fog, with any -MB2WeatherExpect): once the weather has
 * arrived and settled, the client checks the look for that weather, takes a capture and passes.
 *
 * Options: -MB2WeatherExpect= -MB2WeatherShot=<path prefix> -MB2WeatherTimeout=<s> -MB2WeatherWetAtLeast=<0..1>
 * -MB2WeatherLookOnly=<look>
 */
UCLASS()
class MARBURGUMIICLIENT_API UMarburgumWeatherSmokeSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    bool Tick(float DeltaTime);
    bool Bind();
    void TickFixed(double Now);
    void TickLive(double Now);
    void TickLook(double Now);
    /** Logs the weather effects' state and expects the look of Want: clear, rain, overcast, snow_lying or fog. */
    void CheckLook(const TCHAR* Event, const FString& Want);
    /** Day and night (ADR-064): logs the world clock's daylight as drawn; checks it for the daylight looks. */
    void CheckDaylight(const TCHAR* Event, const FString& Want);
    static bool IsDaylightLook(const FString& Want);
    /** Walks away from the car; true once WalkSeconds have passed and the steps are checked. */
    bool Walk(double Now, bool bExpectWet);

    AMarburgumCharacter* Body() const;
    void OnVehicleResult(bool bOk, FName Action, FName Reason);
    void Expect(bool bCondition, const FString& What);
    void Shot(const TCHAR* Label);
    void Advance(int32 Next, double Now);
    void ReportWeather(const TCHAR* Event);
    FString BannerWeather() const;
    FString SurfaceUnder(const FVector& At, bool bLog = true, FString* OutClass = nullptr) const;
    /** Logs what the paving and the landscape report on a grid round a point; expects both to be classified. */
    void Survey(const FVector& Around);
    void Pass();
    void Fail(const FString& Reason);

    FTSTicker::FDelegateHandle TickerHandle;
    TWeakObjectPtr<AMarburgumPlayerController> Controller;
    TWeakObjectPtr<AMarburgumVehicle> Vehicle;

    FString Expected = TEXT("clear_dry");
    FString ShotPrefix;
    FString LookOnly;
    bool bLookTurned = false;
    /** clockrun: the world clock and the real clock at the first reading. */
    FDateTime ClockFirstUtc;
    double ClockFirstReal = 0.0;
    double ClockFirstElevation = 0.0;
    double ClockFirstAzimuth = 0.0;
    double TimeoutSeconds = 300.0;
    double WetAtLeast = 0.5;
    double StartSeconds = 0.0;

    int32 Step = 0;
    double StepSeconds = 0.0;
    FVector Mark = FVector::ZeroVector;
    double BrakeSpeed = 0.0;
    double StillSince = -1.0;
    bool bWalking = false;
    int32 StepsBefore = 0;
    TMap<FName, int32> SetsBefore;
    FName FirstState;
    float FirstWetness = 0.f;
    int32 StateChanges = 0;
    FName LastState;

    bool bAnswered = false;
    bool bAnswerOk = false;
    FName AnswerReason;

    FString PendingShot;
    double ShotRequestedSeconds = 0.0;
    bool bDone = false;
    TArray<FString> Problems;
};
