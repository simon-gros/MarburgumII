#include "MarburgumWeatherLook.h"
#include "MarburgumWeatherSound.h"
#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Ports of the original's MarburgumWeatherLookTest.cpp, MarburgumWeatherShelterTest.cpp and MarburgumWeatherSoundTest.cpp
// (feat/dynamic-weather), with the trees' sway left out (Lent has no swaying vegetation) and the ground's wetness and
// snow cover added (ADR-062).

namespace
{
    using namespace MarburgumII::WeatherLook;

    constexpr EAutomationTestFlags LookTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
        | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter;

    FInput Weather(double Cover, double Precipitation, const TCHAR* Type, double Wind = 0.0, double From = 0.0)
    {
        FInput Input;
        Input.CloudCover = Cover;
        Input.Precipitation = Precipitation;
        Input.PrecipitationType = Type;
        Input.WindSpeed = Wind;
        Input.WindDirectionDegrees = From;
        return Input;
    }

    constexpr float Rate = 48000.0f;

    struct FMeasure
    {
        float Peak = 0.0f;
        float Rms = 0.0f;
        /** Sign changes per second on the left channel: a rough brightness. */
        float Crossings = 0.0f;
        /** How alike the two channels are, -1..1. */
        float Correlation = 0.0f;
    };

    FMeasure Measure(const TArray<float>& Interleaved, int32 Channels)
    {
        FMeasure Out;
        const int32 Frames = Interleaved.Num() / Channels;
        double Sum = 0.0;
        double LeftRight = 0.0;
        double LeftSquared = 0.0;
        double RightSquared = 0.0;
        int32 Signs = 0;
        for (int32 Frame = 0; Frame < Frames; ++Frame)
        {
            const float Left = Interleaved[Frame * Channels];
            const float Right = Interleaved[Frame * Channels + (Channels > 1 ? 1 : 0)];
            Out.Peak = FMath::Max(Out.Peak, FMath::Max(FMath::Abs(Left), FMath::Abs(Right)));
            Sum += Left * Left;
            LeftRight += Left * Right;
            LeftSquared += Left * Left;
            RightSquared += Right * Right;
            if (Frame > 0 && (Left >= 0.0f) != (Interleaved[(Frame - 1) * Channels] >= 0.0f))
            {
                ++Signs;
            }
        }
        Out.Rms = Frames > 0 ? static_cast<float>(FMath::Sqrt(Sum / Frames)) : 0.0f;
        Out.Crossings = Frames > 0 ? Signs * Rate / Frames : 0.0f;
        Out.Correlation = LeftSquared > 0.0 && RightSquared > 0.0 ? static_cast<float>(LeftRight / FMath::Sqrt(LeftSquared * RightSquared)) : 0.0f;
        return Out;
    }

    TArray<float> Render(MarburgumII::FWeatherSound& Sound, float Seconds, int32 Channels = 2)
    {
        TArray<float> Buffer;
        Buffer.SetNumZeroed(static_cast<int32>(Seconds * Rate) * Channels);
        // In blocks the size the mixer asks for, as it would.
        const int32 Block = 1024;
        for (int32 Frame = 0; Frame < Buffer.Num() / Channels; Frame += Block)
        {
            const int32 Frames = FMath::Min(Block, Buffer.Num() / Channels - Frame);
            Sound.Render(Buffer.GetData() + Frame * Channels, Frames, Channels);
        }
        return Buffer;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumWeatherLookTest, "Marburgum.Shared.WeatherLook.Look", LookTestFlags)

bool FMarburgumWeatherLookTest::RunTest(const FString& Parameters)
{
    // --- A clear day is the level as built: the authored daylight, untouched.
    const FLook Clear = Compute(Weather(0.1, 0.0, TEXT("none"), 1.5, 200.0));
    TestTrue(TEXT("a clear sky leaves the sun nearly whole"), Clear.SunScale > 0.97f && Clear.SunScaleUnclouded > 0.97f);
    TestTrue(TEXT("and its shadows sharp"), Clear.SunSoftness < 1.5f);
    TestEqual(TEXT("and the haze as built"), Clear.FogDensityScale, 1.0f);
    TestEqual(TEXT("nothing falls"), Clear.Rain + Clear.Snow, 0.0f);
    TestTrue(TEXT("a few clouds are drawn"), Clear.CloudCover > 0.02f && Clear.CloudCoverage < -0.6f);
    TestEqual(TEXT("no lightning"), Clear.FlashesPerMinute, 0.0f);
    TestEqual(TEXT("a dry street"), Clear.Wetness, 0.0f);

    // --- Steady rain from the west under a closed sky.
    FInput RainInput = Weather(1.0, 0.6, TEXT("rain"), 5.0, 270.0);
    RainInput.Wetness = 0.8;
    const FLook Rain = Compute(RainInput);
    TestTrue(TEXT("our clouds shade the street themselves, so the sun itself only hazes"), Rain.SunScale > 0.75f);
    TestTrue(TEXT("without them, overcast takes most of the sun"), Rain.SunScaleUnclouded > 0.1f && Rain.SunScaleUnclouded < 0.15f);
    TestEqual(TEXT("and shadows go soft under a closed sky"), Rain.SunSoftness, 40.0f);
    const float ClosedSky = Compute(Weather(1.0, 0.0, TEXT("none"))).ExposureBias;
    TestTrue(*FString::Printf(TEXT("a closed sky is darker than a clear one (%.2f EV)"), ClosedSky), ClosedSky < -0.3f && Clear.ExposureBias > ClosedSky);
    TestTrue(*FString::Printf(TEXT("and rain darker still (%.2f EV)"), Rain.ExposureBias), Rain.ExposureBias < ClosedSky - 0.5f);
    TestTrue(TEXT("and the colour drains"), Rain.SaturationScale < 0.75f);
    TestEqual(TEXT("it rains at the server's rate"), Rain.Rain, 0.6f);
    TestEqual(TEXT("and does not snow"), Rain.Snow, 0.0f);
    TestTrue(TEXT("rain shortens the view"), Rain.FogDensityScale > 2.0f);
    TestEqual(TEXT("a westerly carries the rain east"), Rain.LeanBearingDegrees, 90.0f);
    TestTrue(*FString::Printf(TEXT("5 m/s leans 8.5 m/s drops about 30 degrees (%.2f)"), Rain.LeanDegrees),
        FMath::IsNearlyEqual(Rain.LeanDegrees, FMath::RadiansToDegrees(FMath::Atan2(5.0f, 8.5f)), 0.01f));
    TestEqual(TEXT("and is heard"), Rain.RainSound, 0.6f);
    TestTrue(TEXT("rain clouds are darker"), Rain.Storm > 0.3f && Rain.CloudDensity > 0.008f);
    TestEqual(TEXT("the street looks as wet as the server says it is"), Rain.Wetness, 0.8f);

    // --- Snow is slow, so the wind leans it far, and it falls silently.
    FInput SnowInput = Weather(1.0, 0.5, TEXT("snow"), 2.0, 0.0);
    SnowInput.SnowDepthM = 0.05;
    const FLook Snow = Compute(SnowInput);
    TestEqual(TEXT("snow is snow"), Snow.Snow, 0.5f);
    TestEqual(TEXT("and not rain"), Snow.Rain, 0.0f);
    TestTrue(TEXT("a light wind leans snow further than rain"), Snow.LeanDegrees > 50.0f);
    TestEqual(TEXT("a northerly carries it south"), Snow.LeanBearingDegrees, 180.0f);
    TestTrue(TEXT("snow is nearly silent"), Snow.RainSound < 0.1f);
    TestEqual(TEXT("five centimetres covers the ground"), Snow.SnowCover, 1.0f);
    const FLook Gale = Compute(Weather(1.0, 0.5, TEXT("snow"), 40.0, 0.0));
    TestEqual(TEXT("the lean stops short of horizontal"), Gale.LeanDegrees, 65.0f);
    TestEqual(TEXT("no snow lies, none is drawn"), SnowCoverFor(0.0), 0.0f);
    TestTrue(*FString::Printf(TEXT("a centimetre is a dusting (%.2f)"), SnowCoverFor(0.01)), SnowCoverFor(0.01) > 0.3f && SnowCoverFor(0.01) < 0.6f);
    TestEqual(TEXT("NaN snow lies nowhere"), SnowCoverFor(std::numeric_limits<double>::quiet_NaN()), 0.0f);

    const FLook Sleet = Compute(Weather(1.0, 0.8, TEXT("sleet")));
    TestEqual(TEXT("sleet is half rain"), Sleet.Rain, 0.4f);
    TestEqual(TEXT("and half snow"), Sleet.Snow, 0.4f);

    const FLook Trace = Compute(Weather(1.0, 0.01, TEXT("none")));
    TestEqual(TEXT("a trace the server calls none draws nothing"), Trace.Rain + Trace.Snow, 0.0f);
    TestEqual(TEXT("and an empty type is none too"), Compute(Weather(1.0, 0.5, TEXT(""))).Rain, 0.0f);

    // --- Fog and thunder.
    FInput Foggy = Weather(0.8, 0.0, TEXT("none"), 0.5);
    Foggy.Fog = 1.0;
    const FLook Fog = Compute(Foggy);
    TestEqual(TEXT("full fog is fifty times the haze"), Fog.FogDensityScale, 50.0f);
    TestEqual(TEXT("and fully its own colour"), Fog.FogColourAmount, 1.0f);
    FInput Storm = Weather(1.0, 0.85, TEXT("rain"), 8.0, 250.0);
    Storm.Thunder = 1.0;
    TestEqual(TEXT("a thunderstorm flashes three times a minute"), Compute(Storm).FlashesPerMinute, 3.0f);
    TestEqual(TEXT("with the storm clouds"), Compute(Storm).Storm, 1.0f);

    // --- The wire is not trusted.
    FInput Broken;
    Broken.CloudCover = std::numeric_limits<double>::quiet_NaN();
    Broken.Precipitation = std::numeric_limits<double>::infinity();
    Broken.PrecipitationType = TEXT("rain");
    Broken.WindSpeed = -7.0;
    Broken.WindDirectionDegrees = std::numeric_limits<double>::quiet_NaN();
    Broken.Fog = 9.0;
    Broken.Wetness = -3.0;
    const FLook Odd = Compute(Broken);
    TestTrue(TEXT("NaN cover reads as clear"), Odd.SunScaleUnclouded == 1.0f && Odd.SunSoftness == 1.0f);
    TestEqual(TEXT("infinite rain reads as none"), Odd.Rain, 0.0f);
    TestEqual(TEXT("negative wind does not lean"), Odd.LeanDegrees, 0.0f);
    TestTrue(TEXT("a NaN bearing stays a bearing"), FMath::IsFinite(Odd.LeanBearingDegrees));
    TestEqual(TEXT("fog above 1 is full fog"), Odd.FogDensityScale, 50.0f);
    TestEqual(TEXT("negative wetness is dry"), Odd.Wetness, 0.0f);

    // --- Easing.
    const FLook Half = Blend(Clear, Rain, 0.5f);
    TestEqual(TEXT("halfway is halfway"), Half.Rain, 0.3f);
    TestEqual(TEXT("the wetness eases with the rest"), Half.Wetness, 0.4f);
    TestEqual(TEXT("alpha 0 is where it was"), Blend(Clear, Rain, 0.0f).SunScaleUnclouded, Clear.SunScaleUnclouded);
    TestEqual(TEXT("alpha 1 is where it is going"), Blend(Clear, Rain, 1.0f).SunScaleUnclouded, Rain.SunScaleUnclouded);
    FLook North = Clear;
    North.LeanBearingDegrees = 350.0f;
    FLook NorthEast = Clear;
    NorthEast.LeanBearingDegrees = 10.0f;
    const float Between = Blend(North, NorthEast, 0.5f).LeanBearingDegrees;
    TestTrue(TEXT("a wind backing through north turns the short way"),
        FMath::IsNearlyEqual(Between, 0.0f, 0.01f) || FMath::IsNearlyEqual(Between, 360.0f, 0.01f));

    // --- Lightning and thunder.
    TestEqual(TEXT("a flash begins at full brightness"), FlashEnvelope(0.0f, 7u), 1.0f);
    TestEqual(TEXT("nothing before it"), FlashEnvelope(-0.1f, 7u), 0.0f);
    TestEqual(TEXT("and nothing after it"), FlashEnvelope(FlashSeconds, 7u), 0.0f);
    bool bBounded = true;
    bool bSame = true;
    for (float Age = 0.0f; Age < FlashSeconds; Age += 0.005f)
    {
        const float Light = FlashEnvelope(Age, 1234u);
        bBounded &= Light >= 0.0f && Light <= 1.0f;
        bSame &= Light == FlashEnvelope(Age, 1234u);
    }
    TestTrue(TEXT("a flash stays within 0..1"), bBounded);
    TestTrue(TEXT("and the same seed flickers the same way"), bSame);
    TestEqual(TEXT("thunder takes a second per 343 m"), ThunderDelay(343.0f), 1.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumWeatherShelterTest, "Marburgum.Shared.WeatherLook.Shelter", LookTestFlags)

bool FMarburgumWeatherShelterTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("the trace reaches the top of the near curtain"), RoofReachMetres(), 18.0);
    TestEqual(TEXT("open air keeps the rain"), ShelteredAmount(0.6f, 0.0f), 0.6f);
    TestEqual(TEXT("a roof takes the near rain"), ShelteredAmount(0.6f, 1.0f), 0.0f);
    TestEqual(TEXT("a doorway is halfway"), ShelteredAmount(0.8f, 0.5f), 0.4f);
    TestEqual(TEXT("a bad shelter sample does not stop the rain"), ShelteredAmount(0.6f, std::numeric_limits<float>::quiet_NaN()), 0.6f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarburgumWeatherSoundTest, "Marburgum.Shared.WeatherLook.Sound", LookTestFlags)

bool FMarburgumWeatherSoundTest::RunTest(const FString& Parameters)
{
    using MarburgumII::FWeatherSound;

    {
        FWeatherSound Sound(Rate);
        const FMeasure Still = Measure(Render(Sound, 1.0f), 2);
        TestEqual(TEXT("still, dry air is silent"), Still.Peak, 0.0f);
    }

    FMeasure Rain;
    {
        FWeatherSound Sound(Rate);
        Sound.SetTargets(1.0f, 0.0f, 0.0f);
        Render(Sound, 1.5f);
        Rain = Measure(Render(Sound, 2.0f), 2);
        AddInfo(FString::Printf(TEXT("downpour: rms %.3f, peak %.3f, %.0f crossings/s, L/R correlation %.2f"), Rain.Rms, Rain.Peak, Rain.Crossings, Rain.Correlation));
        TestTrue(TEXT("a downpour is heard"), Rain.Rms > 0.05f);
        TestTrue(TEXT("and stays within full scale"), Rain.Peak <= 1.0f);
        TestTrue(TEXT("and is wide, not one sound in both ears"), FMath::Abs(Rain.Correlation) < 0.3f);
    }
    {
        FWeatherSound Sound(Rate);
        Sound.SetTargets(0.2f, 0.0f, 0.0f);
        Render(Sound, 1.5f);
        const FMeasure Shower = Measure(Render(Sound, 2.0f), 2);
        TestTrue(TEXT("a shower is quieter than a downpour"), Shower.Rms > 0.0f && Shower.Rms < Rain.Rms);
    }

    {
        FWeatherSound Sound(Rate);
        Sound.SetTargets(0.0f, 0.8f, 0.6f);
        Render(Sound, 2.0f);
        const FMeasure Wind = Measure(Render(Sound, 4.0f), 2);
        AddInfo(FString::Printf(TEXT("strong wind: rms %.3f, peak %.3f, %.0f crossings/s"), Wind.Rms, Wind.Peak, Wind.Crossings));
        TestTrue(TEXT("strong wind is heard"), Wind.Rms > 0.02f);
        TestTrue(TEXT("and stays within full scale"), Wind.Peak <= 1.0f);
        TestTrue(TEXT("wind is a low rush and rain a hiss"), Wind.Crossings * 3.0f < Rain.Crossings);
    }

    {
        FWeatherSound Sound(Rate);
        Sound.Thunder(0.5f, 1.0f);
        TestTrue(TEXT("thunder is pending as soon as it is asked for"), Sound.IsThundering());
        const FMeasure Before = Measure(Render(Sound, 0.49f), 2);
        TestEqual(TEXT("thunder waits for its travel time"), Before.Peak, 0.0f);
        const FMeasure Clap = Measure(Render(Sound, 3.0f), 2);
        AddInfo(FString::Printf(TEXT("close thunder: rms %.3f, peak %.3f, %.0f crossings/s"), Clap.Rms, Clap.Peak, Clap.Crossings));
        TestTrue(TEXT("then it arrives"), Clap.Rms > 0.02f);
        TestTrue(TEXT("and stays within full scale"), Clap.Peak <= 1.0f);
        Render(Sound, 30.0f);
        TestFalse(TEXT("and dies away"), Sound.IsThundering());
        TestEqual(TEXT("leaving silence"), Measure(Render(Sound, 0.5f), 2).Peak, 0.0f);
    }

    {
        FWeatherSound Sound(Rate);
        Sound.SetTargets(0.7f, 0.5f, 0.3f);
        Render(Sound, 1.0f, 1);
        const FMeasure Mono = Measure(Render(Sound, 1.0f, 1), 1);
        TestTrue(TEXT("a mono device still hears it"), Mono.Rms > 0.02f && Mono.Peak <= 1.0f);
    }

    {
        FWeatherSound Sound(Rate);
        Sound.SetTargets(std::numeric_limits<float>::quiet_NaN(), -3.0f, 5.0f);
        Sound.Thunder(std::numeric_limits<float>::infinity(), 1.0f);
        Sound.Thunder(1.0f, std::numeric_limits<float>::quiet_NaN());
        TestFalse(TEXT("nonsense thunder is not queued"), Sound.IsThundering());
        const FMeasure Nonsense = Measure(Render(Sound, 1.0f), 2);
        TestEqual(TEXT("nonsense targets are silence"), Nonsense.Peak, 0.0f);
    }
    return true;
}

#endif
