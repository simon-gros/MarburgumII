#pragma once

#include "CoreMinimal.h"

/**
 * Where each item of a placement file stands, worked out once by the editor on the whole level (ADR-091). A client on
 * the World Partition map holds only the cells round its player, so a trace there finds no ground under most of the
 * street lamps, plants and street furniture. scripts/partition-world.sh ground (MarburgumPartitionLevel -Ground) writes
 * <file>_ground.json beside each placement file whenever it or the ground changes. A ground file names the placement
 * file it was made for by a fingerprint of its text, and the game traces instead where the two do not match.
 */
namespace MarburgumII::GroundFile
{
    /** The ground file beside a placement file, both relative to the Content directory: a/b.json is a/b_ground.json. */
    MARBURGUMIISHARED_API FString PathFor(const FString& PlacementFile);

    /** A placement file's fingerprint (SHA-1 of its text as UTF-8), as its ground file records it. */
    MARBURGUMIISHARED_API FString Fingerprint(const FString& PlacementJson);

    /** The ground file: for each item in the placement file's order, the height of its foot in Unreal centimetres, unset where none. */
    MARBURGUMIISHARED_API FString Write(const FString& PlacementFingerprint, const TArray<TOptional<double>>& GroundCm);

    /**
     * Reads the ground file for the placement file with PlacementFingerprint and Count items. False with OutError when it
     * is not one, or is for another placement file.
     */
    MARBURGUMIISHARED_API bool Parse(const FString& Json, const FString& PlacementFingerprint, int32 Count,
        TArray<TOptional<double>>& OutGroundCm, FString& OutError);
}
