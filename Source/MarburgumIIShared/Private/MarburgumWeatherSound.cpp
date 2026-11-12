#include "MarburgumWeatherSound.h"

namespace MarburgumII
{
    FWeatherSound::FWeatherSound(const float InSampleRate, const uint32 Seed)
        : SampleRate(FMath::Max(InSampleRate, 8000.0f))
        , Control(Seed | 1u)
    {
        // Changes of weather ease in over about half a second, the gusts over a second and a half.
        Smoothing = 1.0f - FMath::Exp(-1.0f / (0.4f * SampleRate));
        GustSmoothing = 1.0f - FMath::Exp(-1.0f / (1.5f * SampleRate));
        HissTopPole = Pole(7000.0f);
        HissFloorPole = Pole(500.0f);
        RumblePole = Pole(180.0f);
        SplashDecay = FMath::Exp(-1.0f / (0.003f * SampleRate));
        Channels[0].Noise = (Seed * 747796405u + 2891336453u) | 1u;
        Channels[1].Noise = (Seed * 2654435761u + 40503u) | 1u;
    }

    float FWeatherSound::Pole(const float CutoffHz) const
    {
        return 1.0f - FMath::Exp(-UE_TWO_PI * CutoffHz / SampleRate);
    }

    float FWeatherSound::White(uint32& State)
    {
        // xorshift32: cheap, and plenty random for noise nobody listens to sample by sample.
        State ^= State << 13;
        State ^= State >> 17;
        State ^= State << 5;
        return static_cast<float>(State) / 2147483648.0f - 1.0f;
    }

    float FWeatherSound::Random01()
    {
        return 0.5f * (White(Control) + 1.0f);
    }

    void FWeatherSound::SetTargets(const float InRain, const float InWind, const float InGust)
    {
        auto Unit = [](const float Value) { return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.0f, 1.0f) : 0.0f; };
        RainTarget = Unit(InRain);
        WindTarget = Unit(InWind);
        GustTarget = Unit(InGust);
    }

    void FWeatherSound::Thunder(float DelaySeconds, float Loudness)
    {
        Loudness = FMath::IsFinite(Loudness) ? FMath::Clamp(Loudness, 0.0f, 1.0f) : 0.0f;
        if (Loudness <= 0.0f || !FMath::IsFinite(DelaySeconds))
        {
            return;
        }
        // Thunder from further than about 30 km is not heard at all; a minute and a half is its longest wait.
        DelaySeconds = FMath::Min(DelaySeconds, 90.0f);
        // A free slot, or else the quietest roll, which is the one least missed.
        FRoll* Slot = &Rolls[0];
        for (FRoll& Roll : Rolls)
        {
            if (!Roll.bActive)
            {
                Slot = &Roll;
                break;
            }
            if (Roll.Loudness < Slot->Loudness)
            {
                Slot = &Roll;
            }
        }
        FRoll& Roll = *Slot;
        Roll = FRoll();
        Roll.bActive = true;
        Roll.Wait = static_cast<int64>(FMath::Max(DelaySeconds, 0.0f) * SampleRate);
        Roll.Loudness = Loudness;
        // Thunder rolls because the channel is kilometres long: its parts are heard one after another.
        // The first swell is the nearest part; a distant flash rolls longer and arrives softer.
        for (int32 Index = 0; Index < 4; ++Index)
        {
            Roll.SwellStart[Index] = Index == 0 ? 0.0f : 0.2f + 2.6f * Random01();
            Roll.SwellDecay[Index] = (Index == 0 ? 0.8f : 0.6f) + (1.8f - Loudness) * Random01();
            Roll.SwellPeak[Index] = Index == 0 ? 1.0f : 0.3f + 0.6f * Random01();
            Roll.Length = FMath::Max(Roll.Length, Roll.SwellStart[Index] + 6.0f * Roll.SwellDecay[Index]);
        }
    }

    bool FWeatherSound::IsThundering() const
    {
        for (const FRoll& Roll : Rolls)
        {
            if (Roll.bActive)
            {
                return true;
            }
        }
        return false;
    }

    void FWeatherSound::Render(float* Interleaved, const int32 Frames, const int32 OutChannels)
    {
        if (Interleaved == nullptr || Frames <= 0 || OutChannels <= 0)
        {
            return;
        }
        for (int32 Frame = 0; Frame < Frames; ++Frame)
        {
            Rain += Smoothing * (RainTarget - Rain);
            Wind += Smoothing * (WindTarget - Wind);
            Gust += Smoothing * (GustTarget - Gust);

            // The gusts: a new aim every few seconds, wider the gustier the wind, eased towards.
            if (--GustCountdown <= 0)
            {
                GustAim = FMath::Clamp(0.5f + (Random01() - 0.5f) * (0.4f + 1.6f * Gust), 0.0f, 1.0f);
                GustCountdown = static_cast<int64>((1.5f + 3.0f * Random01()) * SampleRate);
            }
            GustLevel += GustSmoothing * (GustAim - GustLevel);

            // Drops per second on each side: a light shower patters, a downpour roars.
            const float DropChance = (30.0f + 900.0f * Rain) / SampleRate;
            const float RainGain = FMath::Sqrt(Rain);
            const float WindPole = Pole(110.0f + 520.0f * GustLevel * (0.4f + 0.6f * Wind));
            const float WindGain = 7.0f * FMath::Pow(Wind, 1.3f) * (0.35f + 0.65f * GustLevel);

            float Bed[2] = {0.0f, 0.0f};
            for (int32 Side = 0; Side < 2; ++Side)
            {
                FChannel& Channel = Channels[Side];
                const float Noise = White(Channel.Noise);
                if (RainGain > 0.0f)
                {
                    Channel.HissTop += HissTopPole * (Noise - Channel.HissTop);
                    Channel.HissFloor += HissFloorPole * (Channel.HissTop - Channel.HissFloor);
                    const float Hiss = Channel.HissTop - Channel.HissFloor;
                    if (0.5f * (White(Channel.Noise) + 1.0f) < DropChance)
                    {
                        Channel.Splash = FMath::Max(Channel.Splash, 0.3f + 0.7f * Random01());
                    }
                    Channel.Splash *= SplashDecay;
                    Bed[Side] += RainGain * (0.55f * Hiss + 0.3f * Channel.Splash * White(Channel.Noise));
                }
                if (WindGain > 0.0f)
                {
                    Channel.WindA += WindPole * (Noise - Channel.WindA);
                    Channel.WindB += WindPole * (Channel.WindA - Channel.WindB);
                    Bed[Side] += WindGain * Channel.WindB;
                }
            }

            float Rumble = 0.0f;
            for (FRoll& Roll : Rolls)
            {
                if (!Roll.bActive)
                {
                    continue;
                }
                if (Roll.Wait > 0)
                {
                    --Roll.Wait;
                    continue;
                }
                const float Time = static_cast<float>(Roll.Age++) / SampleRate;
                if (Time > Roll.Length)
                {
                    Roll.bActive = false;
                    continue;
                }
                const float Noise = White(Control);
                // Brown noise, darkened further: thunder heard from a distance is mostly below 200 Hz.
                Roll.Brown = 0.997f * Roll.Brown + 0.03f * Noise;
                Roll.LowA += RumblePole * (Roll.Brown - Roll.LowA);
                Roll.LowB += RumblePole * (Roll.LowA - Roll.LowB);
                float Envelope = 0.0f;
                for (int32 Index = 0; Index < 4; ++Index)
                {
                    const float Since = Time - Roll.SwellStart[Index];
                    if (Since >= 0.0f)
                    {
                        Envelope += Roll.SwellPeak[Index] * FMath::Min(1.0f, Since / 0.08f)
                            * FMath::Exp(-Since / Roll.SwellDecay[Index]);
                    }
                }
                // A near strike cracks before it rumbles.
                float Crack = 0.0f;
                if (Roll.Loudness > 0.6f && Time < 0.5f)
                {
                    Crack = (Noise - Roll.CrackPrevious) * 0.5f * (Roll.Loudness - 0.6f) / 0.4f
                        * FMath::Exp(-Time / 0.07f);
                }
                Roll.CrackPrevious = Noise;
                Rumble += Roll.Loudness * (5.0f * Roll.LowB * Envelope + Crack);
            }

            for (int32 Out = 0; Out < OutChannels; ++Out)
            {
                float Value = 0.0f;
                if (OutChannels == 1)
                {
                    Value = 0.5f * (Bed[0] + Bed[1]) + Rumble;
                }
                else if (Out < 2)
                {
                    Value = Bed[Out] + Rumble;
                }
                // Soft-limited rather than clipped: a downpour under a close strike should still sound
                // like weather, not like distortion.
                Value = Value * (27.0f + Value * Value) / (27.0f + 9.0f * Value * Value);
                Interleaved[Frame * OutChannels + Out] = FMath::Clamp(Value, -1.0f, 1.0f);
            }
        }
    }
}
