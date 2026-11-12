#pragma once

#include "CoreMinimal.h"

class UMaterialParameterCollection;
class UPackage;

/**
 * The content the weather's look needs (ADR-062; MarburgumWeatherLook.h names it), generated rather than authored:
 * the rain and snow curtain (a procedural material on an open cylinder, the original's MarburgumWeatherAssets.cpp),
 * an instance of the engine's simple volumetric cloud so a cooked build carries it, and MPC_Weather, the ground's
 * wetness and snow cover that Lent's paving and landscape materials read (MarburgumRebuildGroundCommandlet.cpp).
 */
namespace MarburgumWeatherLookContent
{
    /** MPC_Weather, created when missing; its parameter ids are fixed so materials compiled against it stay valid. */
    UMaterialParameterCollection* EnsureCollection(TArray<UPackage*>& Dirty);

    /** The curtain's HLSL, for tests and evidence. */
    const TCHAR* CurtainCode();

    /** Writes the curtain material and mesh, the cloud instance and the collection, and saves them. */
    bool Write(FString& OutError);
}
