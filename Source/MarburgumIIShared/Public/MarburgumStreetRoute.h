#pragma once

#include "CoreMinimal.h"
#include "MarburgumStreetContract.h"

/**
 * The street-district smoke's route (ADR-068, scripts/street-district-smoke-linux.sh): one stop in each of six of the
 * original's zones, where every walk-graph node from 22 to 60 m out (where people appear) is on the same zone, so who
 * turns up there is that district's people. The server sets the first player down at each in turn (-MB2StreetRoute,
 * UMarburgumStreetRouteWalker) and the client counts who is about (-MB2StreetSmoke -MB2StreetCount=).
 *
 * Plan metres, x east and y south of the middle of Stari most; Yaw is the Unreal yaw towards most of the nodes round
 * the stop, so the captures look along the street.
 */
namespace MarburgumII::StreetRoute
{
    struct FStop
    {
        const TCHAR* Id;
        double East;
        double South;
        float Yaw;
        MarburgumII::Street::EZone Zone;
    };

    inline constexpr FStop Stops[] = {
        { TEXT("old_town"), -122.3, -628.9, -173.9f, MarburgumII::Street::EZone::OldTown },
        { TEXT("commercial"), 405.5, -505.1, -154.0f, MarburgumII::Street::EZone::Commercial },
        { TEXT("residential_blocks"), -44.2, 216.8, 32.1f, MarburgumII::Street::EZone::ResidentialBlocks },
        { TEXT("residential"), -669.1, 673.9, 145.7f, MarburgumII::Street::EZone::Residential },
        { TEXT("industrial"), 359.8, 530.3, -100.6f, MarburgumII::Street::EZone::Industrial },
        { TEXT("school"), -476.9, -349.1, 63.2f, MarburgumII::Street::EZone::School },
    };

    /** Seconds at each stop: the last stop's people are culled and the new street fills well inside it. */
    inline constexpr double DwellSeconds = 60.0;

    /** Seconds after the player's body appears before the first stop. */
    inline constexpr double LeadSeconds = 6.0;

    inline constexpr const TCHAR* ServerOption = TEXT("MB2StreetRoute");
}
