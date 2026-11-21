#pragma once

#include "CoreMinimal.h"

/**
 * The street lamps (ADR-065): where they stand, and when and how brightly they burn.
 *
 * The original game designed street lighting (its climate roadmap §10, "street lights activating from ambient-light/time
 * thresholds"; its 1990s roadmap §50, orange sodium-vapour streets) and never placed a lamp. tools/lent-ground places
 * them from this map's OpenStreetMap extract into Content/Marburgum/Lights/lent_street_lights.json: every mapped lamp,
 * masts along every lit car road and lanterns along pedestrian streets, lit paths and round squares, clear of the carriageway, the
 * buildings, the river and where the street people walk.
 *
 * The file is in plan metres (x east, y south of the middle of Stari most); everything here is in Unreal centimetres
 * (X north, Y east), converted once on load. Heights are the surveyed walkable top, which the game grounds by a trace or,
 * on the World Partition map, from the editor's ground file (MarburgumGroundFile.h).
 *
 * **When they burn** is decided from the world clock and the weather alone, so every client and the server agree without
 * a message: each lamp's photocell switches it on once the sun is low enough for the sky, earlier under thick cloud or
 * fog, and off again after sunrise. Photocells differ, so each lamp's threshold is spread a little by its index. A
 * high-pressure sodium lamp then takes minutes to warm up, starting dim and pinkish-white and reaching its orange in
 * WarmUpSeconds; that too is worked out from how long ago the sun crossed the lamp's threshold, not remembered.
 *
 * Pure and engine-neutral apart from the JSON reader; AMarburgumStreetLamps draws them.
 */
namespace MarburgumII::StreetLights
{
    /** Relative to the project's Content directory. */
    inline constexpr const TCHAR* LampsFile = TEXT("Marburgum/Lights/lent_street_lights.json");

    enum class EKind : uint8
    {
        /** An 8 m mast with an arm reaching over the carriageway. */
        Mast,
        /** A 3.6 m post with a lantern on it, along paths and round squares. */
        Lantern,
        Count,
    };

    enum class ESource : uint8
    {
        Mapped,
        Road,
        Path,
        Square,
        Count,
    };

    struct FLamp
    {
        /** On the ground plane in Unreal centimetres; Z the surveyed top, until grounded. */
        FVector Location = FVector::ZeroVector;
        /** Unreal yaw, degrees: the way the head reaches. */
        float Yaw = 0.0f;
        EKind Kind = EKind::Mast;
        ESource Source = ESource::Road;
        /** False where the world had no ground under it: such a lamp is not put up and casts no light. */
        bool bStanding = true;
    };

    /** Reads the lamps file. False with OutError when it is not one. */
    MARBURGUMIISHARED_API bool Parse(const FString& Json, TArray<FLamp>& OutLamps, FString& OutError);

    /** Plan metres (x east, y south, z up) to Unreal centimetres (X north, Y east). */
    inline FVector FromPlan(double East, double South, double Up)
    {
        return FVector(-South * 100.0, East * 100.0, Up * 100.0);
    }

    /** A plan yaw (degrees, 0 east, 90 south) as an Unreal yaw (0 north, 90 east). */
    inline float YawFromPlan(double PlanYawDeg)
    {
        return static_cast<float>(FRotator::NormalizeAxis(PlanYawDeg + 90.0));
    }

    // ---- The content (scripts/create-weather-content.sh lamps). ----

    inline constexpr const TCHAR* MastMeshPackage = TEXT("/Game/Marburgum/Lights/SM_StreetLampMast");
    inline constexpr const TCHAR* MastMesh = TEXT("/Game/Marburgum/Lights/SM_StreetLampMast.SM_StreetLampMast");
    inline constexpr const TCHAR* LanternMeshPackage = TEXT("/Game/Marburgum/Lights/SM_StreetLampLantern");
    inline constexpr const TCHAR* LanternMesh = TEXT("/Game/Marburgum/Lights/SM_StreetLampLantern.SM_StreetLampLantern");
    inline constexpr const TCHAR* MetalMaterialPackage = TEXT("/Game/Marburgum/Lights/M_StreetLampMetal");
    inline constexpr const TCHAR* GlassMaterialPackage = TEXT("/Game/Marburgum/Lights/M_StreetLampGlass");
    /**
     * The glass reads its lamp's state from per-instance custom data: the output (0..1), then the light's linear colour.
     * It glows at GlassGlowOnScreen times the screen's mid-grey whatever the eye's exposure, so a burning lamp reads as a
     * lamp from across the river and blooms a little.
     */
    inline constexpr int32 CustomDataOutput = 0;
    inline constexpr int32 CustomDataColour = 1;
    inline constexpr int32 CustomDataFloats = 4;
    inline constexpr float GlassGlowOnScreen = 4.0f;

    // ---- The shapes, centimetres from the foot of the post, X the way the head reaches. ----

    inline constexpr float MastHeightCm = 800.0f;
    inline constexpr float MastReachCm = 150.0f;
    /** Where a mast's light comes from: under its head. */
    inline constexpr float MastLightHeightCm = 795.0f;
    inline constexpr float LanternHeightCm = 360.0f;
    inline constexpr float LanternLightHeightCm = 385.0f;
    /** The posts block: a lamp is something to walk round and drive into. */
    inline constexpr float MastPostRadiusCm = 9.0f;
    inline constexpr float LanternPostRadiusCm = 6.0f;

    // ---- The light (client only). ----

    /**
     * Only the lamps nearest the eye cast light, up to this many; every burning lamp glows. A street is lit as far as
     * one can see the pavement under its lamps, and further away the glow is what shows.
     */
    inline constexpr int32 LitPoolSize = 48;
    /** A lamp further than this from the eye casts no light. */
    inline constexpr float LitReachCm = 15000.0f;
    /**
     * The lamps' light is not physical. Lent's whole scale is set by an 8-lux sun (ADR-064): a full moon is 0.41 lux and
     * the night eye cannot open far enough to show a real street, where a sodium lamp lights the pavement forty times as
     * bright as the full moon and the wall beside it ten times brighter again. Inverse-square light did exactly that to
     * the old town's narrow streets: walls a metre or two from a lamp came out as bright as a sunny wall.
     *
     * So each light falls off smoothly to nothing at its reach, (1 - (d / reach)^2)^FalloffExponent, from a peak of
     * PeakLux at the lamp. The pavement under a mast gets about 0.6 lux (half again the full moon), under a lantern about
     * 0.45, and a wall beside either at most about twice its pavement.
     */
    inline constexpr float MastPeakLux = 1.45f;
    inline constexpr float LanternPeakLux = 0.75f;
    inline constexpr float FalloffExponent = 3.0f;
    /** How far a lamp's light reaches, centimetres. */
    inline constexpr float MastReachLightCm = 1600.0f;
    inline constexpr float LanternReachLightCm = 1000.0f;
    /**
     * A mast throws its cone down on the road and little sideways, as a cut-off sodium luminaire does; a lantern lights
     * all round. Half-angles from straight down.
     */
    inline constexpr float MastInnerConeDeg = 25.0f;
    inline constexpr float MastOuterConeDeg = 60.0f;

    /** The light a lamp gives at this distance from it, lux before the surface's angle and a mast's cone. */
    MARBURGUMIISHARED_API float LuxAt(EKind Kind, float DistanceCm, float Output = 1.0f);

    /** The linear colour of a light at this colour temperature, for the glass. */
    MARBURGUMIISHARED_API FLinearColor KelvinColour(float Kelvin);

    /** Where a lamp's light comes from, in the world. */
    MARBURGUMIISHARED_API FVector LightPoint(const FLamp& Lamp);

    // ---- When they burn. ----

    /**
     * The sun's elevation, degrees, at which a lamp's photocell switches it on under a clear sky: about ten minutes after
     * sunset in Maribor, around 25 lux of sky.
     */
    inline constexpr double ClearSwitchOnDeg = -2.0;
    /** Thick cloud or fog darkens the street as much as the sun sinking this much further; a storm a little more. */
    inline constexpr double OvercastEarlierDeg = 4.0;
    inline constexpr double FogEarlierDeg = 3.0;
    inline constexpr double StormEarlierDeg = 1.5;
    /** Photocells differ: each lamp's threshold is this much either side of the street's. */
    inline constexpr double PhotocellSpreadDeg = 0.6;
    /** In the morning a photocell lets go this much later (higher), so a lamp does not flicker at its threshold. */
    inline constexpr double MorningLaterDeg = 0.5;
    /** A high-pressure sodium lamp reaches full output this long after it strikes. */
    inline constexpr double WarmUpSeconds = 240.0;
    /** And it strikes at this share of its output. */
    inline constexpr float StrikeOutput = 0.08f;
    /** Its colour, kelvin: a cold pinkish-white as it strikes, the familiar orange once warm. */
    inline constexpr float StrikeKelvin = 3400.0f;
    inline constexpr float WarmKelvin = 2050.0f;

    /** What the sky and the weather are doing, from the clock and the server's weather. */
    struct FSky
    {
        double SunElevationDeg = 45.0;
        /** Degrees per clock second; negative while it sets. */
        double SunRateDegPerSecond = 0.0;
        double CloudCover = 0.0;
        double Fog = 0.0;
        double Thunder = 0.0;
    };

    /** The sky at a UTC instant over Maribor, under this weather. */
    MARBURGUMIISHARED_API FSky SkyAt(const FDateTime& Utc, double CloudCover = 0.0, double Fog = 0.0, double Thunder = 0.0);

    /** The street's threshold under this weather, before a lamp's own spread. */
    MARBURGUMIISHARED_API double SwitchOnDeg(const FSky& Sky);

    /** A lamp's own spread, degrees, from its index in the file: fixed, so every machine agrees. */
    MARBURGUMIISHARED_API double PhotocellOffsetDeg(int32 LampIndex);

    struct FOutput
    {
        /** 0 off; StrikeOutput as it strikes; 1 warm. */
        float Output = 0.0f;
        float Kelvin = WarmKelvin;
        /** Seconds of clock time since it struck; 0 when off. */
        double OnSeconds = 0.0;

        bool IsOn() const { return Output > 0.0f; }
    };

    /** What a lamp is doing under this sky. */
    MARBURGUMIISHARED_API FOutput OutputOf(const FSky& Sky, int32 LampIndex);
}
