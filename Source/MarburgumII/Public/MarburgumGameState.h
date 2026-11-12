#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "MarburgumGameState.generated.h"

/** Outcome of a client request as decided by the authoritative server. */
UENUM(BlueprintType)
enum class EMarburgumRequestOutcome : uint8
{
    Applied,
    Duplicate,
    Rejected
};

/**
 * The world's weather as the server runs it (MarburgumWeather.h, ADR-061). Clients only read it: tyres and feet take
 * Wetness and SnowDepthM, the HUD the rest. Values are rounded on the server so a slow drift does not resend the struct
 * every second.
 */
USTRUCT(BlueprintType)
struct FMarburgumWeather
{
    GENERATED_BODY()

    /** False until the server has set the weather once (and on a server that runs none). */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Weather")
    bool bKnown = false;

    /** "auto" (the Maribor climate) or a fixed developer profile. */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Weather")
    FName Source;

    /** The hourly state now: clear, partly_cloudy, overcast, light_rain, steady_rain, heavy_rain, thunderstorm, fog, mist. */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Weather")
    FName State;

    /** rain, sleet or snow; None (FName's own "none") when nothing falls. */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Weather")
    FName PrecipitationType;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Weather")
    float CloudCover = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Weather")
    float Precipitation = 0.f;

    /** 0..1: the one value road grip and footsteps follow. */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Weather")
    float Wetness = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Weather")
    float TemperatureC = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Weather")
    float SnowDepthM = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Weather")
    float Fog = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Weather")
    float Thunder = 0.f;

    /** Mean wind, m/s, from WindDirectionDegrees (clockwise from north). */
    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Weather")
    float WindSpeed = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Weather")
    float WindDirectionDegrees = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Marburgum|Weather")
    float Gust = 0.f;

    bool operator==(const FMarburgumWeather& Other) const = default;
};

/**
 * Server-authored, replicated world state shared by every connected client.
 * Clients only read these values; all mutation entry points are authority-only.
 */
UCLASS()
class MARBURGUMII_API AMarburgumGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    AMarburgumGameState();

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Marburgum|Network")
    int32 NetworkContractVersion = 0;

    /** Incremented by the server on a fixed interval; proves a live authority-to-client data path. */
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Marburgum|Network")
    int32 AuthorityHeartbeat = 0;

    /** Distinct client operations committed by the server (each operation id counts once). */
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Marburgum|Network")
    int32 AppliedRequestCount = 0;

    /** Client requests refused by server policy; refusals never change other state. */
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Marburgum|Network")
    int32 RejectedRequestCount = 0;

    /** The weather, set by the server's weather authority once a second (ADR-061). */
    UPROPERTY(ReplicatedUsing = OnRep_Weather, BlueprintReadOnly, Category = "Marburgum|Weather")
    FMarburgumWeather Weather;

    /** The world's weather, or nullptr before a Marburgum game state exists. */
    static const FMarburgumWeather* WeatherOf(const UWorld* World);

    /** Authority only: replicates a new weather when it differs from the current one. */
    void SetWeather(const FMarburgumWeather& Next);

    DECLARE_EVENT_OneParam(AMarburgumGameState, FWeatherChanged, const FMarburgumWeather&);
    /** Fires on the server and on clients whenever the weather changes. */
    static FWeatherChanged& OnWeatherChanged();

    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** Authority only. Commits an operation once; replays of the same id return Duplicate. */
    EMarburgumRequestOutcome CommitOperation(const FGuid& OperationId);

    /** Authority only. Records a refused request without applying it. */
    void RecordRejectedRequest();

private:
    void AdvanceHeartbeat();

    UFUNCTION()
    void OnRep_Weather();

    FTimerHandle HeartbeatTimer;

    /** Server-side idempotency ledger; process lifetime only until persistence exists. */
    TSet<FGuid> CommittedOperationIds;
};
