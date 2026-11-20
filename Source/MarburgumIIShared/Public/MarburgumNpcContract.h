#pragma once

#include "CoreMinimal.h"

/**
 * What every machine agrees on about how an authored NPC looks and moves (ADR-081): the assets its body, rod and
 * clips come from, and the angler's fishing cycle as a pure function of the server's world clock, so every client
 * draws the same cast at the same moment without anything extra on the wire.
 */
namespace MarburgumII::Npc
{
    inline constexpr const TCHAR* BodyMeshPath = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");
    /** The template's walk, for a person strolling between posts (ADR-081). */
    inline constexpr const TCHAR* WalkClipPath = TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd.MF_Unarmed_Walk_Fwd");
    /** A person strolls between posts at this pace. */
    inline constexpr double StrollCmPerSecond = 120.0;
    /** The template's standing idle; everyone who is not doing anything else breathes in it. */
    inline constexpr const TCHAR* IdleClipPath = TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle");

    /** Built by `scripts/create-npc-content.sh` (MarburgumNpcContent commandlet). */
    inline constexpr const TCHAR* RodMeshPath = TEXT("/Game/Characters/Props/SM_FishingRod.SM_FishingRod");
    /** On the mannequin skeleton, on hand_r: the rod's grip sits here and its blank points along the socket's X. */
    inline constexpr const TCHAR* RodSocket = TEXT("fishing_rod_r");
    /** On the rod mesh: the tip ring the line leaves from. */
    inline constexpr const TCHAR* RodTipSocket = TEXT("tip");

    inline constexpr const TCHAR* FishHoldClipPath = TEXT("/Game/Characters/Animations/A_FishHold.A_FishHold");
    inline constexpr const TCHAR* FishCastClipPath = TEXT("/Game/Characters/Animations/A_FishCast.A_FishCast");
    inline constexpr const TCHAR* FishWaitClipPath = TEXT("/Game/Characters/Animations/A_FishWait.A_FishWait");
    inline constexpr const TCHAR* FishReelClipPath = TEXT("/Game/Characters/Animations/A_FishReel.A_FishReel");
    inline constexpr const TCHAR* TalkClipPath = TEXT("/Game/Characters/Animations/A_NpcTalk.A_NpcTalk");

    inline constexpr double FishCastSeconds = 2.4;
    inline constexpr double FishReelLoopSeconds = 1.2;
    inline constexpr double FishHoldLoopSeconds = 4.0;
    inline constexpr double FishWaitLoopSeconds = 6.0;
    inline constexpr double TalkLoopSeconds = 4.0;

    /** How long an NPC keeps talking (gesture, turned to the speaker) after the server says someone spoke to it. */
    inline constexpr double TalkSeconds = 4.5;
    /** An idle NPC turns its body toward the nearest player this close... */
    inline constexpr double AttentionRangeCm = 500.0;
    /** ...but never further round from where it was placed to face than this. */
    inline constexpr double AttentionMaxTurnDegrees = 110.0;
    inline constexpr double AttentionTurnDegreesPerSecond = 120.0;

    /** Where the angler is in his cycle: the line goes out, sits, comes in, and he stands a while. */
    enum class EAnglerPhase : uint8
    {
        Cast,
        Wait,
        Reel,
        Hold,
    };

    struct FAnglerMoment
    {
        EAnglerPhase Phase = EAnglerPhase::Hold;
        /** Seconds since this phase began. */
        double Elapsed = 0.0;
        /** Which cycle this is (changes once per cycle), for a client that wants to notice a new cast. */
        int64 Cycle = 0;
    };

    /** Seconds of each phase, in cycle order (Cast, Wait, Reel, Hold). */
    inline constexpr double AnglerPhaseSeconds[] = { FishCastSeconds, 18.0, 6.0, 6.0 };

    /**
     * The angler's phase at WorldSeconds of the server's clock. Seed (a hash of the NPC's id) shifts the cycle so two
     * anglers do not cast together. Negative times are treated as zero.
     */
    MARBURGUMIISHARED_API FAnglerMoment AnglerMomentAt(double WorldSeconds, uint32 Seed);

    MARBURGUMIISHARED_API const TCHAR* ToString(EAnglerPhase Phase);
}
