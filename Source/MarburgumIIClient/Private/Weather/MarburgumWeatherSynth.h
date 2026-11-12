#pragma once

#include "Components/SynthComponent.h"
#include "Containers/Queue.h"
#include "CoreMinimal.h"
#include <atomic>

#include "MarburgumWeatherSynth.generated.h"

/** What the game thread tells the weather sound; read by the audio render thread. */
struct FMarburgumWeatherSoundControls
{
    std::atomic<float> Rain{0.0f};
    std::atomic<float> Wind{0.0f};
    std::atomic<float> Gust{0.0f};
    /** Thunderclaps to start: seconds until the sound arrives, and how loud. Only the game thread adds. */
    TQueue<TPair<float, float>, EQueueMode::Spsc> Thunder;
};

/**
 * Plays MarburgumII::FWeatherSound (MarburgumWeatherSound.h) as a 2D stereo bed for the weather effects actor (ADR-062).
 */
UCLASS()
class UMarburgumWeatherSynth : public USynthComponent
{
    GENERATED_BODY()

public:
    UMarburgumWeatherSynth(const FObjectInitializer& ObjectInitializer);

    void SetWeather(float Rain, float Wind, float Gust);
    void Thunder(float DelaySeconds, float Loudness);

protected:
    virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;

private:
    TSharedPtr<FMarburgumWeatherSoundControls, ESPMode::ThreadSafe> Controls;
};
