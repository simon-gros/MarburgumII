#pragma once

#include "CoreMinimal.h"

/**
 * The trees' and bushes' materials (GFX-01; MarburgumVegetation.h names the meshes).
 *
 * The original game's species meshes (/Game/Vegetation/Species/<id>/SM_<id>, Content/Vegetation/README.md) came through
 * Unreal's glTF import with the engine's glTF material instances (/InterchangeAssets/gltf/MaterialInstances/
 * MI_Default_Opaque_DS), one bark and one foliage instance per species carrying the original's colour as
 * BaseColorFactor. In Lent's light they draw near-black. They are replaced by two materials of ours:
 *
 * - **M_PlantFoliage**: two-sided, the two-sided foliage shading model, so a sunlit crown lets light through its leaves
 *   and a crown seen against the sun is not a hole in the sky; colour from the LeafColour parameter.
 * - **M_PlantBark**: plain lit wood; colour from the BarkColour parameter.
 *
 * Each species gets MI_<id>_Foliage and MI_<id>_Bark beside its mesh, coloured with the original's own BaseColorFactor
 * (read from its glTF instance), and its mesh's two slots are pointed at them. The step also logs each mesh's sections,
 * triangles, old material and how many of its triangles' normals face away from their winding.
 */
namespace MarburgumPlantContent
{
    inline constexpr const TCHAR* FoliageMaterialPackage = TEXT("/Game/Vegetation/Materials/M_PlantFoliage");
    inline constexpr const TCHAR* BarkMaterialPackage = TEXT("/Game/Vegetation/Materials/M_PlantBark");

    /** Writes both materials and every used species' instances, assigns them and saves. */
    bool Write(FString& OutError);
}

/**
 * The street kit's content (GFX-01; MarburgumStreetFurniture.h names the meshes): the original game's six
 * SM_Street_*_01 meshes and their vertex-colour M_MarburgumPaint, copied from the original.
 *
 * - The paint is drawn on instanced components, so it is compiled for them.
 * - The original's meshes collide against every triangle, so a shelter's roof stops whoever stands under it. Each is
 *   given simple boxes instead: the shelter's back and side walls (the original's own collision, its open side +X), and
 *   each other piece's bounds.
 *
 * Each piece's bounds are logged, so the kit's front and footprint can be checked against the placement file.
 */
namespace MarburgumStreetKitContent
{
    inline constexpr const TCHAR* PaintMaterial = TEXT("/Game/World/Materials/M_MarburgumPaint.M_MarburgumPaint");
    /** The original's shelter: walls 2.10 m high and 8 cm thick, the roof 10 cm over them on every side. */
    inline constexpr double ShelterWallTopCm = 210.0;
    inline constexpr double ShelterPanelHalfCm = 4.0;
    inline constexpr double ShelterRoofOverhangCm = 10.0;

    bool Write(FString& OutError);
}
