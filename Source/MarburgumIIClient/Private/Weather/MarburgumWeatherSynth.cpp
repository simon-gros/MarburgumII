#include "Weather/MarburgumWeatherSynth.h"

#include "MarburgumWeatherSound.h"
#include "Sound/SoundGenerator.h"

namespace
{
    class FWeatherSoundGenerator final : public ISoundGenerator
    {
    public:
        FWeatherSoundGenerator(const FSoundGeneratorInitParams& Params,
            TSharedPtr<FMarburgumWeatherSoundControls, ESPMode::ThreadSafe> InControls)
            : Sound(Params.SampleRate)
            , Channels(FMath::Max(Params.NumChannels, 1))
            , Controls(MoveTemp(InControls))
        {
        }

        virtual int32 OnGenerateAudio(float* OutAudio, const int32 NumSamples) override
        {
            Sound.SetTargets(Controls->Rain.load(std::memory_order_relaxed),
                Controls->Wind.load(std::memory_order_relaxed),
                Controls->Gust.load(std::memory_order_relaxed));
            TPair<float, float> Clap;
            while (Controls->Thunder.Dequeue(Clap))
            {
                Sound.Thunder(Clap.Key, Clap.Value);
            }
            Sound.Render(OutAudio, NumSamples / Channels, Channels);
            return NumSamples;
        }

        virtual int32 GetNumChannels() const override
        {
            return Channels;
        }

    private:
        MarburgumII::FWeatherSound Sound;
        int32 Channels;
        TSharedPtr<FMarburgumWeatherSoundControls, ESPMode::ThreadSafe> Controls;
    };
}

UMarburgumWeatherSynth::UMarburgumWeatherSynth(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , Controls(MakeShared<FMarburgumWeatherSoundControls, ESPMode::ThreadSafe>())
{
    NumChannels = 2;
    // A bed heard the same wherever the camera turns: the weather is all round, not at a point.
    bAllowSpatialization = false;
    bAutoActivate = false;
}

void UMarburgumWeatherSynth::SetWeather(const float Rain, const float Wind, const float Gust)
{
    Controls->Rain.store(Rain, std::memory_order_relaxed);
    Controls->Wind.store(Wind, std::memory_order_relaxed);
    Controls->Gust.store(Gust, std::memory_order_relaxed);
}

void UMarburgumWeatherSynth::Thunder(const float DelaySeconds, const float Loudness)
{
    Controls->Thunder.Enqueue(TPair<float, float>(DelaySeconds, Loudness));
}

ISoundGeneratorPtr UMarburgumWeatherSynth::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
    return MakeShared<FWeatherSoundGenerator, ESPMode::ThreadSafe>(InParams, Controls);
}
