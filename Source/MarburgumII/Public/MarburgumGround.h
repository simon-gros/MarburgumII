#pragma once

#include "CoreMinimal.h"

struct FHitResult;

/**
 * What a trace hit is standing on (ADR-061), for tyres on the server and feet on clients. Paving answers with its
 * physical material (PM_<surface>); the landscape, which carries none, with the landscape surface map
 * (MarburgumSurface.h), read once from Content/Marburgum/Ground. Anything else is MarburgumII::Surface::Unknown.
 * Traces must ask for bReturnPhysicalMaterial.
 */
namespace MarburgumII::Ground
{
    MARBURGUMII_API FString SurfaceOf(const FHitResult& Hit);

    /** Whether the landscape surface map was found and read; it is loaded on first use. */
    MARBURGUMII_API bool HasLandscapeMap();
}
