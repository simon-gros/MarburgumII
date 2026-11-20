#pragma once

#include "CoreMinimal.h"

class UWorld;

namespace MarburgumII::RenderAudit
{
    /**
     * GFX-00 (Docs/GRAPHICS_ROADMAP.md): what the renderer is actually doing in a running client, read back rather than
     * assumed. Writes one JSON file: RHI, adapter and driver; engine version and build; the scalability levels; the
     * console variables that choose GI, reflections, shadows, anti-aliasing, Nanite and post-processing; and an
     * inventory of the loaded level's lights, sky, fog, post-process volumes, meshes and materials.
     * Read-only: it changes no setting. Returns false when the file could not be written.
     */
    bool WriteJson(UWorld& World, const FString& Path);
}
