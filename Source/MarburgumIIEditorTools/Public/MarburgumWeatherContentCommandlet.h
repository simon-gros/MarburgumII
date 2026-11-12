#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MarburgumWeatherContentCommandlet.generated.h"

/**
 * Builds the content weather and ground surfaces need (ADR-061).
 *
 *   UnrealEditor-Cmd MarburgumII.uproject -run=MarburgumWeatherContent -Steps=surfaces,audio
 *
 * surfaces  /Game/World/Ground/Surfaces/PM_asphalt, PM_paving, PM_cobblestone and PM_concrete, and each of Lent's
 *           paved materials (MI_LentAsphalt, …) pointed at its surface (MarburgumSurface.h PavedSlotSurface), so a
 *           trace on the paving reports what it is made of. The landscape keeps none: its surface comes from
 *           Content/Marburgum/Ground/lent_surface.bin.
 * audio     /Game/Audio/Footsteps: Kenney Impact Sounds 1.0 footstep_concrete, footstep_grass and footstep_snow (CC0)
 *           from SourceArt/Audio/Footsteps/KenneyImpactSounds, and footstep_concrete_wet from
 *           SourceArt/Audio/Footsteps/Generated (`mb2 generate wet-footsteps`).
 */
UCLASS()
class UMarburgumWeatherContentCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UMarburgumWeatherContentCommandlet();
    virtual int32 Main(const FString& Params) override;
};
