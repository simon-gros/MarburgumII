#pragma once

#include "CoreMinimal.h"
#include "MarburgumStreetLights.h"
#include "MarburgumWorldClock.h"

/**
 * Lit windows (ADR-066): the façades' glass at night, a room lit behind it or not.
 *
 * The original designed this and never built it: its lighting plan (RAPID_VISUAL_IMPROVEMENT_ROADMAP V11) lists
 * "lit-window probability" among what the city-wide time-of-day system controls, its climate roadmap (§10) has
 * "windows ... reacting to darkness", and its 1990s art direction (§7) asks for "warm residential windows" beside "harsh
 * fluorescent kiosk or shop interiors", and above all heterogeneity.
 *
 * **Households.** Each window pane of a Lent façade (MarburgumBuildingMesher, ADR-034) belongs to a household: the panes
 * of one storey of one façade are grouped two or three to a flat by the building. A household has three fixed ranks,
 * hashed from the building's id and where the flat is, so every machine and every rebuild agrees: when it puts its
 * evening light on and off, whether it is up early, and what lights its room. The façade rebuild writes them into the
 * glass's texture coordinates (Tag), where the window glass material reads them.
 *
 * **When.** Per hour of the Marburgum clock (the mirrored 1990s date's own weekday), a share of the upper floors'
 * households is up with a light on (EveningShare, MorningShare): most of them through the evening, falling away after
 * ten and a few all night; on a workday an early wave before seven. A household is lit while its rank is under the
 * share, so as the share falls the same windows go out first every night, and none blinks. The ground floor is shops:
 * lit in shop hours, otherwise only a few with a night light (GroundShare). And all of it only once it is dark enough
 * outside for a room's light to show, from the sun and the weather (Dusk): the street lamps' threshold with a margin.
 *
 * Every client works this out from the replicated clock and weather into MPC_LitWindows, and the glass does the rest on
 * the GPU, so nothing is sent and the server does nothing. Pure and engine-neutral.
 */
namespace MarburgumII::LitWindows
{
    // ---- The content (scripts/create-weather-content.sh windows). ----

    inline constexpr const TCHAR* GlassMaterialPackage = TEXT("/Game/Marburgum/Lights/M_WindowGlass");
    inline constexpr const TCHAR* GlassMaterial = TEXT("/Game/Marburgum/Lights/M_WindowGlass.M_WindowGlass");
    inline constexpr const TCHAR* CollectionPackage = TEXT("/Game/Marburgum/Lights/MPC_LitWindows");
    inline constexpr const TCHAR* Collection = TEXT("/Game/Marburgum/Lights/MPC_LitWindows.MPC_LitWindows");
    /** The façades' glass instance, now a child of M_WindowGlass. */
    inline constexpr const TCHAR* FacadeGlassInstance = TEXT("/Game/Marburgum/Materials/Instances/building_envelope/MI_facade_glass.MI_facade_glass");

    inline constexpr const TCHAR* EveningParameter = TEXT("EveningShare");
    inline constexpr const TCHAR* MorningParameter = TEXT("MorningShare");
    inline constexpr const TCHAR* GroundParameter = TEXT("GroundShare");
    inline constexpr const TCHAR* DuskParameter = TEXT("Dusk");

    /** The glass's texture coordinates: TagChannel holds (evening, morning), KindChannel (kind, 1 on the ground floor). */
    inline constexpr int32 TagChannel = 2;
    inline constexpr int32 KindChannel = 3;
    inline constexpr int32 UVChannels = 4;

    /** A household's ranks, each (2k + 1) / 512 for a byte k: exact in a half float, so the GPU reads what was written. */
    struct FTag
    {
        float Evening = 0.5f;
        float Morning = 0.5f;
        float Kind = 0.5f;
        bool bGround = false;
    };

    /** A rank from a byte. */
    inline float Rank(uint32 Byte)
    {
        return static_cast<float>(2 * (Byte & 0xffu) + 1) / 512.0f;
    }

    /** Panes of one storey of one façade share a household this many to a flat, by the building: 2 or 3. */
    MARBURGUMIISHARED_API int32 PanesPerFlat(const FString& BuildingId);

    /**
     * The household of a pane: building, façade (in the order the mesher builds them), storey (0 the ground floor) and
     * bay (0 the first along the wall).
     */
    MARBURGUMIISHARED_API FTag TagOf(const FString& BuildingId, int32 Facade, int32 Storey, int32 Bay);

    // ---- What lights a room. ----

    enum class ERoom : uint8
    {
        /** An incandescent bulb behind a curtain: most homes. */
        Tungsten,
        /** A fluorescent tube: a kitchen, a stairwell, an office. */
        Fluorescent,
        /** Only the television on. */
        Television,
        /** A shop's tubes. */
        Shop,
    };

    inline constexpr float TungstenShare = 0.74f;
    inline constexpr float FluorescentShare = 0.16f;
    inline constexpr float TungstenKelvin = 2700.0f;
    inline constexpr float FluorescentKelvin = 4300.0f;
    inline constexpr float TelevisionKelvin = 8500.0f;
    inline constexpr float ShopKelvin = 4000.0f;
    /**
     * A lit window glows at this many times the screen's mid-grey whatever the eye's exposure, well under the lamps' glass
     * (4). At 1.1 a bulb's room read cream-white up close at night; at this it stays amber.
     */
    inline constexpr float GlowOnScreen = 0.75f;
    /** A television lights its room this much of a bulb; a shop's tubes this much more. */
    inline constexpr float TelevisionGlow = 0.4f;
    inline constexpr float ShopGlow = 1.3f;

    MARBURGUMIISHARED_API ERoom RoomOf(const FTag& Tag);
    /** How brightly its curtains let the room through, 0.55..1: the material's frac(7 e + 3 m), the same exactly. */
    MARBURGUMIISHARED_API float CurtainOf(const FTag& Tag);

    // ---- When. ----

    /** The clock and the sky, worked into the four parameters of MPC_LitWindows. */
    struct FState
    {
        /** Share of upper-floor households lit by their evening rank, and by their morning rank. */
        float Evening = 0.0f;
        float Morning = 0.0f;
        /** Share of ground-floor shops lit. */
        float Ground = 0.0f;
        /** 0 by day, 1 once it is dark enough outside for a lit room to show. */
        float Dusk = 0.0f;
    };

    /** Shops in Maribor in the 1990s: weekdays 07:00–19:00, Saturdays to 13:00, shut on Sundays. */
    MARBURGUMIISHARED_API bool ShopsOpen(int32 Weekday, double Hour);

    /** The share of households up with a light on, before the dark: by evening, and by the early-morning wave. */
    MARBURGUMIISHARED_API float EveningShareAt(const Clock::FCivil& Local);
    MARBURGUMIISHARED_API float MorningShareAt(const Clock::FCivil& Local);

    /**
     * How dark it is for the windows: 0 above DuskStartAboveDeg over the street lamps' switch-on threshold (so
     * earlier under a closed sky), smoothly to 1 at DuskFullBelowDeg under it.
     */
    inline constexpr double DuskStartAboveDeg = 4.0;
    inline constexpr double DuskFullBelowDeg = 2.0;
    MARBURGUMIISHARED_API float DuskOf(const StreetLights::FSky& Sky);

    /** Everything at once: Local the Marburgum clock (its weekday), Sky over Maribor at the same instant. */
    MARBURGUMIISHARED_API FState StateAt(const Clock::FCivil& Local, const StreetLights::FSky& Sky);

    /** Whether a household's window is lit under this state: the material's rule, for tests and counts. */
    MARBURGUMIISHARED_API bool IsLit(const FTag& Tag, const FState& State);

    /** The linear colour and on-screen glow (before the eye) a lit window of this household gives. */
    MARBURGUMIISHARED_API FLinearColor GlowOf(const FTag& Tag);
}
