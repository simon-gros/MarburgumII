#pragma once

#include "CoreMinimal.h"

/**
 * The trees and bushes of the map (GFX-01, VQ-022): the original game's surveyed plants, kept only where they grow on
 * grass.
 *
 * tools/lent-ground (`--mode plants`) reads the original's per-window `<window>_plants.json` (ARSO 2014 survey, species
 * by ecological region) and keeps a plant only where the landscape is drawn as grass all round its trunk, no paving lies
 * within reach of it, and it is clear of the buildings, the river, the bridges and where the street people walk. It
 * writes Content/Marburgum/Vegetation/lent_plants.json.
 *
 * The file is in plan metres (x east, y south of the middle of Stari most); everything here is in Unreal centimetres
 * (X north, Y east), converted once on load. Heights are the landscape's, which the game grounds by a trace.
 *
 * Each species is drawn with the original game's mesh at its natural size (/Game/Vegetation/Species/<id>/SM_<id>), its
 * crown scaled across by Spread and its height by Grow. A tree's trunk blocks; leaves and bushes do not.
 *
 * Pure and engine-neutral apart from the JSON reader; AMarburgumVegetation draws them.
 */
namespace MarburgumII::Vegetation
{
    /** Relative to the project's Content directory. */
    inline constexpr const TCHAR* PlantsFile = TEXT("Marburgum/Vegetation/lent_plants.json");

    struct FSpecies
    {
        FString Id;
        /** A tree has a trunk that blocks; a bush or reeds do not. */
        bool bTree = false;
        float NaturalHeightCm = 0.0f;
        float NaturalRadiusCm = 0.0f;
        float TrunkRadiusCm = 0.0f;
        /** From the ground to where the crown begins. */
        float TrunkHeightCm = 0.0f;
    };

    struct FPlant
    {
        /** On the ground plane in Unreal centimetres; Z the landscape, until grounded. */
        FVector Location = FVector::ZeroVector;
        /** Unreal yaw, degrees. */
        float Yaw = 0.0f;
        /** Crown across and height, over the species mesh at its natural size. */
        float Spread = 1.0f;
        float Grow = 1.0f;
        int32 Species = 0;
        /** False where the world had no ground under it: such a plant is not put up. */
        bool bStanding = true;
    };

    /** Reads the plants file. False with OutError when it is not one. */
    MARBURGUMIISHARED_API bool Parse(const FString& Json, TArray<FSpecies>& OutSpecies, TArray<FPlant>& OutPlants, FString& OutError);

    /** The species mesh, as an object path. */
    MARBURGUMIISHARED_API FString MeshPath(const FString& SpeciesId);

    /**
     * How far a plant is drawn, centimetres: the original's budget of 30 m per metre of its natural height, between 70 and
     * 700 m. It fades out over the last FadeShare of that.
     */
    MARBURGUMIISHARED_API float CullDistanceCm(const FSpecies& Species);
    inline constexpr float FadeShare = 0.15f;

    /** Plants shorter than this cast no shadow (the original's rule: undergrowth does not pay for one). */
    inline constexpr float ShadowHeightCm = 600.0f;

    /**
     * A trunk's collision: an upright cylinder from the ground, the species' trunk radius scaled with the crown but never
     * thinner than MinTrunkRadiusCm, and at least MinTrunkHeightCm tall so nobody steps over it.
     */
    inline constexpr float MinTrunkRadiusCm = 10.0f;
    inline constexpr float MinTrunkHeightCm = 250.0f;
    MARBURGUMIISHARED_API FTransform TrunkTransform(const FSpecies& Species, const FPlant& Plant);
}
