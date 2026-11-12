#pragma once

#include "CoreMinimal.h"

/**
 * The weather's ambience, synthesised: rain, wind and thunder from filtered noise.
 *
 * Synthesised rather than recorded because the project has no weather recordings, and because each
 * of these three is a sound noise makes well: rain is a broadband hiss with a patter of drops in it,
 * wind is low noise swelling with the gusts, and thunder is a rumble of brown noise rolling in after
 * its flash. It also follows the weather continuously, where loops would need crossfading between
 * intensities.
 *
 * Plain C++ with no engine audio types, so tests can render it and measure what comes out. The
 * client plays it through a synth component (MarburgumWeatherSynth.h). Ported unchanged from the original's
 * MarburgumWeatherSound.h/.cpp on its unmerged feat/dynamic-weather branch (ADR-062).
 */
namespace MarburgumII
{
    class MARBURGUMIISHARED_API FWeatherSound
    {
    public:
        explicit FWeatherSound(float SampleRate, uint32 Seed = 0x5EED1996u);

        /** How hard it rains and blows, 0..1 each, and how gusty the wind is. Eased in over a moment. */
        void SetTargets(float Rain, float Wind, float Gust);

        /** Thunder from a flash Delay seconds ago, as loud as Loudness 0..1 says. Up to four roll at once. */
        void Thunder(float DelaySeconds, float Loudness);

        /** Fills Frames frames of Channels interleaved channels; the first two carry the stereo bed. */
        void Render(float* Interleaved, int32 Frames, int32 Channels);

        /** True while thunder is waiting or rolling. */
        bool IsThundering() const;

    private:
        /** One side of the stereo bed: its own noise, so the two sides are not the same sound. */
        struct FChannel
        {
            uint32 Noise = 1;
            /** The rain's hiss is white noise between two one-pole low-passes, about 500 Hz to 7 kHz. */
            float HissTop = 0.0f;
            float HissFloor = 0.0f;
            /** The drop now splashing, decaying in a few milliseconds. */
            float Splash = 0.0f;
            /** Two one-pole low-passes in a row: the wind's rush. */
            float WindA = 0.0f;
            float WindB = 0.0f;
        };

        /** One thunderclap, from its wait for the sound to arrive to the end of its rumble. */
        struct FRoll
        {
            bool bActive = false;
            int64 Wait = 0;
            int64 Age = 0;
            float Loudness = 0.0f;
            float Brown = 0.0f;
            float LowA = 0.0f;
            float LowB = 0.0f;
            float CrackPrevious = 0.0f;
            /** Swells of the rumble: when each starts, how long it takes to die, how strong it is. */
            float SwellStart[4] = {};
            float SwellDecay[4] = {};
            float SwellPeak[4] = {};
            float Length = 0.0f;
        };

        float White(uint32& State);
        float Random01();
        float Pole(float CutoffHz) const;

        float SampleRate;
        uint32 Control;
        float Smoothing;
        float GustSmoothing;
        float HissTopPole;
        float HissFloorPole;
        float RumblePole;
        float SplashDecay;
        float RainTarget = 0.0f;
        float WindTarget = 0.0f;
        float GustTarget = 0.0f;
        float Rain = 0.0f;
        float Wind = 0.0f;
        float Gust = 0.0f;
        /** A slow random walk the wind's loudness and pitch follow. */
        float GustLevel = 0.5f;
        float GustAim = 0.5f;
        int64 GustCountdown = 0;
        FChannel Channels[2];
        FRoll Rolls[4];
    };
}
