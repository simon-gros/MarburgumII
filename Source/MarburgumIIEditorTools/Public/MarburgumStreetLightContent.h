#pragma once

#include "CoreMinimal.h"

/**
 * The street lamps' content (ADR-065; MarburgumStreetLights.h names it), generated rather than authored: two lamps for
 * the whole map, drawn as instances.
 *
 * - **SM_StreetLampMast**: a galvanised 8 m mast, its arm reaching 1.5 m over the carriageway to a flat sodium
 *   luminaire, the road lamp of a 1990s Yugoslav town. Its base is a little thicker, where the access door is.
 * - **SM_StreetLampLantern**: a dark-green cast post of 3.6 m with a lantern on it, for paths and squares.
 *
 * Each has two sections, the metal (M_StreetLampMetal, coloured and made metallic by the mesh's vertex colours) and the
 * glass (M_StreetLampGlass, which glows by its instance's custom data). Their simple collision is a capsule round the
 * post, so people walk round a lamp and a car stops at one; the arm and the head are out of reach.
 *
 * Neither is a hero asset: octagons and boxes that carry the silhouette at street distance, as the original's street kit
 * built its furniture (Tier C).
 */
namespace MarburgumStreetLightContent
{
    /** Writes both meshes and both materials and saves them. */
    bool Write(FString& OutError);
}
