#include "Weather/MarburgumWeatherPresentation.h"

#include "Engine/World.h"
#include "MarburgumGameState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Weather/MarburgumWeatherEffects.h"

namespace MarburgumWeatherPresentation
{
    bool Same(const MarburgumII::WeatherLook::FInput& A, const MarburgumII::WeatherLook::FInput& B)
    {
        return A.CloudCover == B.CloudCover && A.Precipitation == B.Precipitation && A.PrecipitationType == B.PrecipitationType
            && A.Fog == B.Fog && A.Thunder == B.Thunder && A.WindSpeed == B.WindSpeed && A.WindDirectionDegrees == B.WindDirectionDegrees
            && A.Gust == B.Gust && A.Wetness == B.Wetness && A.SnowDepthM == B.SnowDepthM;
    }
}

bool UMarburgumWeatherPresentation::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return World && World->IsGameWorld() && !IsRunningDedicatedServer() && !FParse::Param(FCommandLine::Get(), TEXT("MB2NoWeatherLook"))
        && Super::ShouldCreateSubsystem(Outer);
}

bool UMarburgumWeatherPresentation::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UMarburgumWeatherPresentation::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UMarburgumWeatherPresentation, STATGROUP_Tickables);
}

MarburgumII::WeatherLook::FInput UMarburgumWeatherPresentation::InputOf(const FMarburgumWeather& Weather)
{
    MarburgumII::WeatherLook::FInput Input;
    Input.CloudCover = Weather.CloudCover;
    Input.Precipitation = Weather.Precipitation;
    Input.PrecipitationType = Weather.PrecipitationType.IsNone() ? FString() : Weather.PrecipitationType.ToString().ToLower();
    Input.Fog = Weather.Fog;
    Input.Thunder = Weather.Thunder;
    Input.WindSpeed = Weather.WindSpeed;
    Input.WindDirectionDegrees = Weather.WindDirectionDegrees;
    Input.Gust = Weather.Gust;
    Input.Wetness = Weather.Wetness;
    Input.SnowDepthM = Weather.SnowDepthM;
    return Input;
}

void UMarburgumWeatherPresentation::Tick(float DeltaTime)
{
    UWorld* World = GetWorld();
    // A dedicated server's game world has no viewer; a listen server's host does, and gets the weather as a client.
    if (!World || World->GetNetMode() == NM_DedicatedServer)
    {
        return;
    }
    const FMarburgumWeather* Weather = AMarburgumGameState::WeatherOf(World);
    if (!Weather || !Weather->bKnown)
    {
        return;
    }
    if (!Effects)
    {
        FActorSpawnParameters Params;
        Params.Name = TEXT("MarburgumWeatherEffects");
        Params.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
        Params.ObjectFlags |= RF_Transient;
        Effects = World->SpawnActor<AMarburgumWeatherEffects>(Params);
        bShown = false;
        if (!Effects)
        {
            return;
        }
    }
    const MarburgumII::WeatherLook::FInput Input = InputOf(*Weather);
    if (!bShown || !MarburgumWeatherPresentation::Same(Input, Shown))
    {
        Effects->SetWeather(Input);
        Shown = Input;
        bShown = true;
    }
}

void UMarburgumWeatherPresentation::Deinitialize()
{
    if (Effects)
    {
        Effects->Destroy();
        Effects = nullptr;
    }
    Super::Deinitialize();
}
