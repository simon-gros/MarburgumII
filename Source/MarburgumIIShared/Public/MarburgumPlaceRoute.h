#pragma once

#include "CoreMinimal.h"

/**
 * The place-name smoke's route (ADR-063, scripts/place-smoke-linux.sh): ten stops through Lent and the old town, each
 * where the banner should name a known place, street or square and landmark with a margin of metres either side of
 * every threshold (the margins come from the places file; see the ADR). The server walks the first player along it
 * (-MB2PlaceRoute, UMarburgumPlaceRouteWalker); the client checks the banner at each stop (-MB2PlaceSmoke).
 *
 * Plan metres, x east and y south of the middle of Stari most; Yaw is the Unreal yaw to face from the stop, towards
 * its landmark or along its square, so the captures show what is named.
 */
namespace MarburgumII::PlaceRoute
{
    struct FStop
    {
        const TCHAR* Id;
        double East;
        double South;
        float Yaw;
        /** The banner's place, street and landmark lines; empty for a line that should be hidden. */
        const TCHAR* Place;
        const TCHAR* Street;
        const TCHAR* Landmark;
    };

    inline constexpr FStop Stops[] = {
        { TEXT("stari_most"), 0.0, -5.0, 0.f, TEXT("Center"), TEXT("Stari most"), TEXT("") },
        { TEXT("vodni_stolp"), 186.0, -67.0, 151.5f, TEXT("Lent"), TEXT("Usnjarska ulica"), TEXT("Vodni stolp") },
        { TEXT("zidovski_stolp"), 185.0, -97.0, -65.4f, TEXT("Lent"), TEXT("Židovski trg"), TEXT("Židovski stolp") },
        { TEXT("sinagoga"), 143.0, -109.0, -155.9f, TEXT("Center"), TEXT("Židovski trg"), TEXT("Sinagoga") },
        { TEXT("glavni_trg"), -10.0, -184.0, -43.6f, TEXT("Center"), TEXT("Glavni trg"), TEXT("") },
        { TEXT("rotovz"), -17.0, -206.0, -9.0f, TEXT("Center"), TEXT("Glavni trg"), TEXT("Rotovž") },
        { TEXT("stolnica"), -50.0, -349.0, -61.1f, TEXT("Center"), TEXT("Slomškov trg"), TEXT("Cerkev sv. Janeza Krstnika") },
        { TEXT("grad"), 178.0, -527.0, 121.2f, TEXT("Center"), TEXT("Grajska ulica"), TEXT("Grad") },
        { TEXT("magdalena"), 9.0, 160.0, -3.2f, TEXT("Magdalena"), TEXT("Trg revolucije"), TEXT("") },
        { TEXT("stari_most_back"), 0.0, -5.0, 0.f, TEXT("Center"), TEXT("Stari most"), TEXT("") },
    };

    /** Seconds the server keeps the player at each stop: the 4 Hz place tick, replication and a capture fit inside. */
    inline constexpr double DwellSeconds = 6.0;

    /** Seconds after the player's body appears before the first stop, so the client has mounted its HUD. */
    inline constexpr double LeadSeconds = 4.0;

    inline constexpr const TCHAR* ServerOption = TEXT("MB2PlaceRoute");
    inline constexpr const TCHAR* ClientOption = TEXT("MB2PlaceSmoke");
}
