#pragma once

#include "CoreMinimal.h"

/**
 * What the ground is made of (ADR-061): the surface ids the original's surface response knows (asphalt, paving,
 * cobblestone, concrete, grass, soil, gravel, …), where they come from on Lent, and which footstep sounds go with
 * them. Engine-neutral; the hit-result side lives in the game module (MarburgumGround.h).
 *
 * Paving carries physical materials named PM_<surface>; the landscape has none, so its surface comes from the
 * landscape surface map that `lent-ground surface-kinds` reduces from the ground's own surface mask.
 */
namespace MarburgumII::Surface
{
    /** "unknown" when the ground is unclassified: tyre grip stays neutral there and steps sound hard. */
    inline constexpr const TCHAR* Unknown = TEXT("unknown");

    /** The physical material assets, one per surface the paving uses (/Game/World/Ground/Surfaces). */
    inline constexpr const TCHAR* PhysicalMaterialFolder = TEXT("/Game/World/Ground/Surfaces");
    inline constexpr const TCHAR* PhysicalMaterialPrefix = TEXT("PM_");

    /** The surface of a physical material called PM_<surface>, or Unknown. */
    inline FString FromPhysicalMaterialName(FString Name)
    {
        return Name.RemoveFromStart(PhysicalMaterialPrefix) && !Name.IsEmpty() ? Name.ToLower() : FString(Unknown);
    }

    /**
     * The surface of each paved material slot (tools/lent-ground mesh.rs Surface): roads and yards are asphalt,
     * pavements pavers, the old town's setts cobblestone, kerbs concrete, and road paint lies on asphalt.
     */
    inline const TCHAR* PavedSlotSurface(const FString& Slot)
    {
        if (Slot == TEXT("asphalt") || Slot == TEXT("yard_asphalt") || Slot == TEXT("paint")) return TEXT("asphalt");
        if (Slot == TEXT("pavers")) return TEXT("paving");
        if (Slot == TEXT("setts")) return TEXT("cobblestone");
        if (Slot == TEXT("kerb")) return TEXT("concrete");
        return Unknown;
    }

    /** The landscape surface map, Content/Marburgum/Ground/lent_surface.bin (tools/lent-ground kinds.rs). */
    inline constexpr const TCHAR* LandscapeMapFile = TEXT("Marburgum/Ground/lent_surface.bin");

    /**
     * One surface kind per cell over the landscape's square. Kinds: 0 hard standing, 1 grass, 2 earth, 3 gravel,
     * 4 river stones. Coordinates are Unreal centimetres; plan x runs east (Unreal +Y), plan y south (Unreal -X).
     */
    class MARBURGUMIISHARED_API FLandscapeMap
    {
    public:
        /** False, leaving the map empty, when the bytes are not a version 1 map. */
        bool Parse(TConstArrayView<uint8> Bytes);
        bool IsLoaded() const { return Cells > 0; }

        /** The surface at an Unreal location, or Unknown outside the square or before a map is loaded. */
        FString SurfaceAt(double UnrealX, double UnrealY) const;

        /** The kind byte at a cell, row 0 north and column 0 west; for tests. */
        uint8 KindAt(int32 Row, int32 Column) const { return Kinds[Row * Cells + Column]; }
        int32 GetCells() const { return Cells; }

    private:
        int32 Cells = 0;
        double HalfExtentM = 0.0;
        TArray<uint8> Kinds;
    };

    /** The surface id of a landscape kind: hard standing reads as concrete, earth as soil, river stones as gravel. */
    MARBURGUMIISHARED_API const TCHAR* LandscapeKindSurface(uint8 Kind);

    /**
     * Footstep sound sets in /Game/Audio/Footsteps: hard ground (Kenney Impact Sounds 1.0 footstep_concrete, CC0), hard
     * ground in the wet (those steps with a generated splash, `mb2 generate wet-footsteps`), soft ground
     * (footstep_grass) and the crunch of gravel or lying snow (footstep_snow).
     */
    inline constexpr const TCHAR* FootstepFolder = TEXT("/Game/Audio/Footsteps");
    inline constexpr int32 FootstepVariants = 5;
    inline const FName FootstepHard(TEXT("hard"));
    inline const FName FootstepHardWet(TEXT("hard_wet"));
    inline const FName FootstepSoft(TEXT("soft"));
    inline const FName FootstepCrunch(TEXT("crunch"));

    /** The file and asset stem of a set's sounds, which keep the pack's own names; empty for an unknown set. */
    inline const TCHAR* FootstepStem(FName Set)
    {
        if (Set == FootstepHard) return TEXT("footstep_concrete");
        if (Set == FootstepHardWet) return TEXT("footstep_concrete_wet");
        if (Set == FootstepSoft) return TEXT("footstep_grass");
        if (Set == FootstepCrunch) return TEXT("footstep_snow");
        return TEXT("");
    }

    /** The wave asset of one footstep: <stem>_00<Variant>. */
    inline FString FootstepAsset(FName Set, int32 Variant)
    {
        const FString Name = FString::Printf(TEXT("%s_%03d"), FootstepStem(Set), Variant);
        return FString::Printf(TEXT("%s/%s.%s"), FootstepFolder, *Name, *Name);
    }

    /** Lying snow this deep turns every step into a crunch. */
    inline constexpr double SnowCrunchDepthM = 0.01;

    /**
     * Share of steps on hard ground that splash, from the server's wetness: none on a damp street, all of them once
     * it is properly wet. Smoothstep from 0.15 to 0.6.
     */
    MARBURGUMIISHARED_API double WetStepShare(double Wetness);

    /** The footstep set for a step on Surface; Roll (0..1) decides whether a hard step on a wet street splashes. */
    MARBURGUMIISHARED_API FName FootstepSetFor(const FString& Surface, double Wetness, double SnowDepthM, double Roll);

    /** How loud a step is at a ground speed (cm/s): walking quieter than running, as the original's GaitVolume. */
    MARBURGUMIISHARED_API float StepVolume(double SpeedCmPerSecond);

    /**
     * When a foot comes down, read from the drawn pose rather than assumed: the foot bone's height above the bottom of
     * the body, tracked against the lowest it has been. A foot that lifts by LiftCm and comes back within ContactCm of
     * its low is a footfall. The low creeps up slowly so a body that settles a little higher does not stop stepping.
     */
    struct FFootfallDetector
    {
        static constexpr double LiftCm = 6.0;
        static constexpr double ContactCm = 2.5;
        static constexpr double LowRiseCmPerSecond = 3.0;

        /** Feeds this frame's height; true on the frame the foot lands. */
        MARBURGUMIISHARED_API bool Update(double HeightCm, double DeltaSeconds);
        void Reset() { bHasLow = false; bLifted = false; }

    private:
        double Low = 0.0;
        bool bHasLow = false;
        bool bLifted = false;
    };
}
