#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"

class FLoaderAdapterShape;
class UClass;
class UWorld;

/**
 * A level opened by a commandlet with every actor in memory (ADR-091). /Game/World/lent is a World Partition map, and
 * opening one in the editor loads only its always-loaded actors; the commandlets that rebuild, correct and audit the
 * level need all of it, as they had in the one-file level. Keep the object alive until after Save(): the actors unload
 * with it. A map without World Partition (the front end, NetSmoke, the old Lent) opens and saves as before.
 */
class FMarburgumWholeLevel
{
public:
    explicit FMarburgumWholeLevel(const FString& InMapPath);
    ~FMarburgumWholeLevel();

    UWorld* GetWorld() const { return World; }

    /** Sets which actors stream (MarburgumPartition::ShouldStream) and saves the map with every actor's own package. */
    bool Save();

private:
    FString MapPath;
    UWorld* World = nullptr;
    TUniquePtr<FLoaderAdapterShape> Loader;
};

namespace MarburgumPartition
{
    /**
     * What streams with the grid on a client (ADR-091): the buildings, bridges and paving, which are static mesh actors,
     * and the landscape's streaming proxies. Everything else is always loaded: the distant ring and the Drava's sheets,
     * which are seen from everywhere; light, sky, fog and post process; the river's water volumes, which the water
     * subsystem reads once; player starts, the parked car, areas and interior volumes, which gameplay code finds by
     * iterating the world.
     */
    bool ShouldStream(const UClass& Class, const FString& Label, const FString& Folder);

    /** Applies ShouldStream to every actor of the world that may change it; returns how many changed. */
    int32 ApplyStreamingRules(UWorld& World);
}
