#pragma once

#include "CoreMinimal.h"

// Unarmed combat from the original Marburgum (ADR-041): src/shared/combat.h, the fight rules of
// src/server/world_simulation.cpp (attack, set_guard, apply_damage, melee_target, tick_combat) and the
// fighting cast of project/data/npcs.json. Engine-neutral: Data/Gameplay/combat.v0.1.json carries the same
// numbers and people, and `mb2 validate combat` keeps the two in step.
//
// The original proves one unarmed confrontation, so this is a condition model, not a medical simulator:
// enough for the player to read a fight and decide what to do about it. Clients are told which of these
// words applies; only the server ever decides.
namespace MarburgumII::Combat
{
    // ---- Wire names (combat.h) ----

    // How an NPC answers violence. A hostile fights whoever hit it, and only them; a civilian never fights.
    inline constexpr const TCHAR* RoleCivilian = TEXT("civilian");
    inline constexpr const TCHAR* RoleHostile = TEXT("hostile");

    // What a body is doing. Death is deliberately absent: a fight stops at incapacitated, which is what keeps
    // non-lethal outcomes, arrests and bar fights expressible at all.
    inline constexpr const TCHAR* StateIdle = TEXT("idle");
    inline constexpr const TCHAR* StateHostile = TEXT("hostile");
    inline constexpr const TCHAR* StateStaggered = TEXT("staggered");
    inline constexpr const TCHAR* StateIncapacitated = TEXT("incapacitated");

    // What an NPC is doing with its feet, inside those states. A civilian who steps away from a fight is
    // still idle: leaving is not joining in.
    inline constexpr const TCHAR* IntentWait = TEXT("wait");
    inline constexpr const TCHAR* IntentWander = TEXT("wander");
    inline constexpr const TCHAR* IntentWatch = TEXT("watch");
    inline constexpr const TCHAR* IntentAvoid = TEXT("avoid");
    inline constexpr const TCHAR* IntentReturn = TEXT("return");
    inline constexpr const TCHAR* IntentClose = TEXT("close");
    inline constexpr const TCHAR* IntentStrike = TEXT("strike");

    // How the server ruled one swing. The client animates these; it never decides one.
    inline constexpr const TCHAR* OutcomeHit = TEXT("hit");
    inline constexpr const TCHAR* OutcomeBlocked = TEXT("blocked");
    inline constexpr const TCHAR* OutcomeMiss = TEXT("miss");

    // Why a swing was refused. A refusal is not a miss: a miss happened in the world, a refusal never left
    // the client's imagination.
    inline constexpr const TCHAR* ErrorNoCharacter = TEXT("no_character");
    inline constexpr const TCHAR* ErrorTooSoon = TEXT("too_soon");
    inline constexpr const TCHAR* ErrorExhausted = TEXT("exhausted");
    inline constexpr const TCHAR* ErrorIncapacitated = TEXT("incapacitated");
    inline constexpr const TCHAR* ErrorSeated = TEXT("seated");

    // ---- Numbers (combat.h) ----

    // Fists hurt, but slowly: long enough to change your mind and short enough that a fight is never background
    // noise. The original's comment says about eight landed jabs put someone down; against its people's 70 to 85
    // vitality it is ten to thirteen (MarburgumCombatRulesTest counts the drinker's).
    inline constexpr float FistDamage = 7.f;
    // A committed swing is worth roughly two jabs and costs the stamina and recovery to match. It is the
    // answer to a guard, not a better jab.
    inline constexpr float HeavyMultiplier = 2.1f;
    // A guard turns most of a blow into stamina loss rather than injury. It is deliberately not immunity.
    inline constexpr float GuardDamageShare = 0.25f;
    inline constexpr float GuardStaminaCost = 14.f;
    // The original prices a guarded heavy blow too. Nothing can throw one at a guard yet: NPCs only jab and
    // players cannot fight players, so it is carried for the contract only.
    inline constexpr float HeavyGuardStaminaCost = 30.f;

    // How far an unarmed strike reaches, and how far off the nose the target may stand: usable without a
    // lock-on, too tight to punch someone standing behind you.
    inline constexpr float FistReachCm = 200.f;
    inline constexpr float FistArcDegrees = 75.f;

    // Swinging costs stamina; without enough you cannot swing at all. Holding a guard up costs a trickle,
    // so standing behind one indefinitely is not a strategy.
    inline constexpr float FistStaminaCost = 9.f;
    inline constexpr float HeavyStaminaCost = 22.f;
    inline constexpr float GuardDrainPerSecond = 3.5f;

    // Swings are rate-limited by the server, not by the client's good manners: a jab about three times a
    // second at most, a committed swing far less.
    inline constexpr float FistCooldownSeconds = 0.35f;
    inline constexpr float HeavyCooldownSeconds = 0.9f;

    // A landed blow interrupts what the victim was doing. Each stagger MUST stay shorter than the cooldown
    // of the swing that causes it, or one attacker could hold somebody down for ever; the gap is the
    // victim's opening. A guarded blow staggers for half as long.
    inline constexpr float StaggerSeconds = 0.2f;
    inline constexpr float HeavyStaggerSeconds = 0.65f;
    static_assert(StaggerSeconds < FistCooldownSeconds && HeavyStaggerSeconds < HeavyCooldownSeconds,
        "a stagger outlasting its cooldown is a stun-lock");

    // A guard is held, and forgotten when the client goes quiet: no word for this long drops the hands.
    inline constexpr float GuardExpirySeconds = 0.4f;

    // How long a swing is the thing a body is doing, so the picture starts the clip; the clip plays out on
    // the client (world_simulation.cpp attack and tick_combat: 420 ms, 750 ms committed).
    inline constexpr float SwingSeconds = 0.42f;
    inline constexpr float HeavySwingSeconds = 0.75f;

    // ---- NPC behaviour (world_simulation.cpp) ----

    // The original thinks ten times a second; its NPC timers and the wander schedule are counted in those ticks.
    inline constexpr int32 TickRate = 10;
    inline constexpr float TickSeconds = 1.f / TickRate;

    // Somebody this close is company: an NPC stops wandering and, a little closer, turns to look.
    inline constexpr float CompanyRangeCm = 1600.f;
    inline constexpr float WatchRangeCm = 1100.f;
    // A short stroll around home, not a patrol of the quarter.
    inline constexpr float WanderRadiusCm = 120.f;
    // A fight this close is close enough to step away from; and nobody is led further than AvoidHomeCm from
    // home by somebody else's fight.
    inline constexpr float AvoidRangeCm = 1200.f;
    inline constexpr float AvoidHomeCm = 1100.f;
    inline constexpr float HomeArriveCm = 45.f;
    inline constexpr int32 WanderTicks = 28;
    inline constexpr int32 PauseTicks = 40;
    // Inside this share of its reach a hostile is standing in the other person and steps back first.
    inline constexpr float BackOffReachShare = 0.42f;
    // A hostile that cannot get closer tries the side, swapping sides every few refused steps.
    inline constexpr int32 SideStepSwapSteps = 3;

    // An NPC definition's defaults (world_simulation.h Npc): what a hostile hits for, how far it reaches,
    // how often it swings and how far from home it follows somebody.
    inline constexpr float DefaultNpcDamage = 6.f;
    inline constexpr float DefaultNpcReachCm = 190.f;
    inline constexpr float DefaultAttackIntervalSeconds = 1.1f;
    inline constexpr float DefaultPursuitRangeCm = 2600.f;

    // Marburgum II's addition: a downed NPC gets up at home after the same wait a downed player has
    // (RecoverySeconds). The original's NPCs stay down until its server restarts, which a persistent
    // world never does.
    inline constexpr float NpcRecoverySeconds = 60.f;

    /** One person who can be in a fight (npcs.json, npc_visual_profiles.json). Godot frame: metres, y up. */
    struct FNpcDefinition
    {
        const TCHAR* Id;
        const TCHAR* Name;
        const TCHAR* Role;
        /** The original's Lent position: x east and z south of the middle of Stari most, y up. */
        double GodotPosition[3];
        /** The original's facing: radians, 0 looks along -z, positive turns toward -x. */
        double GodotFacing;
        float Vitality;
        float Stamina;
        float Damage;
        float ReachCm;
        float AttackIntervalSeconds;
        float PursuitRangeCm;
        /** npc_visual_profiles.json colours, sRGB 0xRRGGBB as authored. */
        uint32 PrimaryColor;
        uint32 SecondaryColor;
        float Scale;
    };

    // The original's Lent cast. The Tabor walkers are another district, which Marburgum II has no map for yet.
    inline constexpr FNpcDefinition Npcs[] = {
        {
            .Id = TEXT("lent_drunk"),
            .Name = TEXT("Belligerent drinker"),
            .Role = RoleHostile,
            .GodotPosition = { -118.0, 1.8, -62.0 },
            .GodotFacing = 1.5708,
            .Vitality = 85.f,
            .Stamina = 90.f,
            .Damage = 6.f,
            .ReachCm = 190.f,
            .AttackIntervalSeconds = 1.1f,
            .PursuitRangeCm = 2600.f,
            .PrimaryColor = 0x5a3a3a,
            .SecondaryColor = 0x2a2222,
            .Scale = 1.04f,
        },
        {
            .Id = TEXT("lent_passerby"),
            .Name = TEXT("Evening walker"),
            .Role = RoleCivilian,
            .GodotPosition = { -112.0, 1.8, -64.5 },
            .GodotFacing = -1.5708,
            .Vitality = 70.f,
            .Stamina = 60.f,
            .Damage = DefaultNpcDamage,
            .ReachCm = DefaultNpcReachCm,
            .AttackIntervalSeconds = DefaultAttackIntervalSeconds,
            .PursuitRangeCm = DefaultPursuitRangeCm,
            .PrimaryColor = 0x4e5c68,
            .SecondaryColor = 0x2a3036,
            .Scale = 0.99f,
        },
        {
            .Id = TEXT("lent_square_reader"),
            .Name = TEXT("Newspaper reader"),
            .Role = RoleCivilian,
            .GodotPosition = { -12.0, 11.36, -188.0 },
            .GodotFacing = 0.4,
            .Vitality = 72.f,
            .Stamina = 55.f,
            .Damage = DefaultNpcDamage,
            .ReachCm = DefaultNpcReachCm,
            .AttackIntervalSeconds = DefaultAttackIntervalSeconds,
            .PursuitRangeCm = DefaultPursuitRangeCm,
            .PrimaryColor = 0x5a5348,
            .SecondaryColor = 0x2e2c28,
            .Scale = 1.f,
        },
    };

    inline const FNpcDefinition* FindNpc(const FString& Id)
    {
        for (const FNpcDefinition& Npc : Npcs)
        {
            if (Id == Npc.Id)
            {
                return &Npc;
            }
        }
        return nullptr;
    }

    /** The original's frame onto Unreal's (MarburgumSpace.h of the Unreal port): Unreal = 100 (-z, x, y). */
    inline FVector GodotPositionToUnreal(const double (&Godot)[3])
    {
        return FVector(-Godot[2] * 100.0, Godot[0] * 100.0, Godot[1] * 100.0);
    }

    /** A Godot facing looks along (-sin f, 0, -cos f); in Unreal axes that is yaw -f. */
    inline double GodotFacingToUnrealYaw(double GodotFacing)
    {
        return FMath::RadiansToDegrees(-GodotFacing);
    }

    inline FLinearColor ProfileColor(uint32 Rgb)
    {
        return FLinearColor(FColor((Rgb >> 16) & 0xff, (Rgb >> 8) & 0xff, Rgb & 0xff));
    }

    // ---- Rules (world_simulation.cpp), as pure functions ----

    /** Stamina a swing costs; enough of it is required to swing at all. */
    inline float SwingStaminaCost(bool bHeavy)
    {
        return bHeavy ? HeavyStaminaCost : FistStaminaCost;
    }

    inline float SwingCooldownSeconds(bool bHeavy)
    {
        return bHeavy ? HeavyCooldownSeconds : FistCooldownSeconds;
    }

    inline float SwingDamage(bool bHeavy)
    {
        return bHeavy ? FistDamage * HeavyMultiplier : FistDamage;
    }

    inline float SwingStaggerSeconds(bool bHeavy)
    {
        return bHeavy ? HeavyStaggerSeconds : StaggerSeconds;
    }

    /** What the attacker is, for attack()'s refusals; Now and the two "not yet" times share one clock. */
    struct FSwingAsk
    {
        bool bHasCharacter = true;
        bool bSeated = false;
        bool bDown = false;
        double Now = 0.0;
        double NextSwingAt = 0.0;
        double StaggeredUntil = 0.0;
        float Stamina = 0.f;
        bool bHeavy = false;
    };

    /**
     * attack()'s refusals, in its order; nullptr when the swing goes ahead. Your own recovery and somebody
     * else's blow still going through you are both "not yet".
     */
    inline const TCHAR* SwingRefusal(const FSwingAsk& Ask)
    {
        if (!Ask.bHasCharacter)
        {
            return ErrorNoCharacter;
        }
        if (Ask.bSeated)
        {
            return ErrorSeated;
        }
        if (Ask.bDown)
        {
            return ErrorIncapacitated;
        }
        if (Ask.Now < Ask.NextSwingAt || Ask.Now < Ask.StaggeredUntil)
        {
            return ErrorTooSoon;
        }
        if (Ask.Stamina < SwingStaminaCost(Ask.bHeavy))
        {
            return ErrorExhausted;
        }
        return nullptr;
    }

    /**
     * melee_target()'s test for one candidate: within Reach of From on the ground plane and no more than
     * ArcDegrees off the facing. Distance out, for picking the nearest.
     */
    inline bool InSwingReach(const FVector& From, double FacingYawDegrees, const FVector& To, double& OutDistanceCm,
        double ReachCm = FistReachCm, double ArcDegrees = FistArcDegrees)
    {
        const FVector2D Toward(To.X - From.X, To.Y - From.Y);
        OutDistanceCm = Toward.Size();
        if (OutDistanceCm > ReachCm)
        {
            return false;
        }
        if (Toward.IsNearlyZero())
        {
            return true;
        }
        const double Radians = FMath::DegreesToRadians(FacingYawDegrees);
        const FVector2D Facing(FMath::Cos(Radians), FMath::Sin(Radians));
        const double Cosine = FMath::Clamp(FVector2D::DotProduct(Facing, Toward / OutDistanceCm), -1.0, 1.0);
        return FMath::RadiansToDegrees(FMath::Acos(Cosine)) <= ArcDegrees;
    }

    /** A guard meeting a blow: it holds if the stamina it costs can be paid; otherwise it breaks. */
    struct FGuardPayment
    {
        bool bGuarded = false;
        float StaminaAfter = 0.f;
    };

    inline FGuardPayment PayForGuard(bool bGuarding, float Stamina, float Cost = GuardStaminaCost)
    {
        if (!bGuarding)
        {
            return { false, Stamina };
        }
        if (Stamina < Cost)
        {
            // A guard that cannot be paid for breaks, and the blow it was meant to stop arrives in full.
            return { false, Stamina };
        }
        return { true, Stamina - Cost };
    }

    /** apply_damage(): what one blow does to a body. */
    struct FBlowResult
    {
        float Applied = 0.f;
        float VitalityAfter = 0.f;
        float StaggerSeconds = 0.f;
        bool bIncapacitated = false;
        const TCHAR* Outcome = OutcomeHit;
    };

    inline FBlowResult ResolveBlow(float Vitality, float Damage, bool bGuarded, float StaggerFor)
    {
        FBlowResult Result;
        Result.Applied = bGuarded ? Damage * GuardDamageShare : Damage;
        Result.VitalityAfter = FMath::Max(0.f, Vitality - Result.Applied);
        Result.bIncapacitated = Result.VitalityAfter <= 0.f;
        // Somebody on the floor is not staggered any more; they are down.
        Result.StaggerSeconds = Result.bIncapacitated ? 0.f : (bGuarded ? StaggerFor * 0.5f : StaggerFor);
        Result.Outcome = bGuarded ? OutcomeBlocked : OutcomeHit;
        return Result;
    }

    /** What a hostile with a target does this tick (tick_combat), before it moves. */
    enum class EHostileStep : uint8
    {
        /** The target is gone, down, or has put the width of a few streets between them and home. */
        GiveUp,
        Staggered,
        /** Out of reach, or a wall in between: walk toward the target. */
        Close,
        /** Standing inside the other person is not a stance: one step back, then the swing. */
        BackOff,
        /** The blow is still being thrown; do not walk it into a stroll. */
        Swinging,
        /** Hands up between blows. */
        Recover,
        Strike,
    };

    struct FHostileView
    {
        bool bTargetPresent = false;
        bool bTargetDown = false;
        double TargetFromHomeCm = 0.0;
        double PursuitRangeCm = DefaultPursuitRangeCm;
        bool bStaggered = false;
        double DistanceCm = 0.0;
        bool bWalled = false;
        double ReachCm = DefaultNpcReachCm;
        double Now = 0.0;
        double SwingUntil = 0.0;
        double NextAttackAt = 0.0;
    };

    inline EHostileStep DecideHostile(const FHostileView& View)
    {
        if (!View.bTargetPresent || View.bTargetDown || View.TargetFromHomeCm > View.PursuitRangeCm)
        {
            return EHostileStep::GiveUp;
        }
        if (View.bStaggered)
        {
            return EHostileStep::Staggered;
        }
        if (View.DistanceCm > View.ReachCm || View.bWalled)
        {
            return EHostileStep::Close;
        }
        if (View.DistanceCm < View.ReachCm * BackOffReachShare)
        {
            return EHostileStep::BackOff;
        }
        if (View.Now < View.SwingUntil)
        {
            return EHostileStep::Swinging;
        }
        if (View.Now < View.NextAttackAt)
        {
            return EHostileStep::Recover;
        }
        return EHostileStep::Strike;
    }

    /** Godot's String::hash() (32-bit DJB2 over code points); NPC ids are ASCII. Seeds the wander schedule. */
    inline uint32 GodotIdHash(const FString& Id)
    {
        uint32 Hash = 5381;
        for (const TCHAR Char : Id)
        {
            Hash = ((Hash << 5) + Hash) + static_cast<uint32>(Char);
        }
        return Hash;
    }

    /** The first pause before an NPC's first stroll: PauseTicks plus up to 24 more, by id. */
    inline int64 FirstPauseTicks(uint32 IdHash)
    {
        return PauseTicks + static_cast<int64>(IdHash % 25u);
    }

    /** Where a stroll leaves for: an offset from home in Unreal centimetres, from the id and the tick. */
    inline FVector WanderOffsetCm(uint32 IdHash, int64 Tick)
    {
        const uint32 Mix = IdHash ^ static_cast<uint32>(static_cast<uint64>(Tick) * 1315423911ull);
        const double Angle = static_cast<double>(Mix % 628u) / 100.0;
        const double RadiusMetres = 0.35 + static_cast<double>(Mix % 85u) / 100.0;
        const double Godot[3] = { FMath::Sin(Angle) * RadiusMetres, 0.0, FMath::Cos(Angle) * RadiusMetres };
        return GodotPositionToUnreal(Godot);
    }

    /** While waiting at home an NPC glances about its home facing (Unreal yaw degrees). */
    inline double GlanceYaw(double HomeYawDegrees, uint32 IdHash, int64 Tick)
    {
        // The original adds the glance to a Godot facing; its yaw is the negative of that.
        const double Glance = FMath::Sin(static_cast<double>(Tick) * 0.2 + static_cast<double>(IdHash % 7u)) * 0.35;
        return HomeYawDegrees - FMath::RadiansToDegrees(Glance);
    }

    /** Whole ticks for a span the original publishes in milliseconds (ticks_for_msec: rounded down, at least one). */
    inline int64 TicksForSeconds(double Seconds)
    {
        const int64 Msec = static_cast<int64>(FMath::RoundToDouble(Seconds * 1000.0));
        return FMath::Max<int64>(1, Msec * TickRate / 1000);
    }

    // ---- Presentation (ADR-041) ----

    // The original's own fight clips (tools/build_character.gd _guard_animation, _fight_animation), carried onto
    // the mannequin skeleton by the combat content commandlet. Nothing of the original shows a body on the floor
    // that is not a Rockstar placeholder, so the template mannequin's own fall stands in for it.
    inline constexpr const TCHAR* GuardClipPath = TEXT("/Game/Characters/Animations/A_FightGuard.A_FightGuard");
    inline constexpr const TCHAR* JabClipPath = TEXT("/Game/Characters/Animations/A_FightJab.A_FightJab");
    inline constexpr const TCHAR* HeavyClipPath = TEXT("/Game/Characters/Animations/A_FightHeavy.A_FightHeavy");
    inline constexpr const TCHAR* DownClipPath = TEXT("/Game/Characters/Mannequins/Anims/Death/MM_Death_Back_01.MM_Death_Back_01");
    inline constexpr double GuardClipSeconds = 1.1;
    inline constexpr double JabClipSeconds = 0.42;
    inline constexpr double HeavyClipSeconds = 0.78;

    // Impact sounds: Kenney Impact Sounds 1.0 (CC0, SourceArt/Audio/Combat), imported under SoundFolder with
    // their file names. One of SoundVariants is picked per blow. A miss makes no sound: the pack has no swish.
    inline constexpr const TCHAR* SoundFolder = TEXT("/Game/Audio/Combat");
    inline constexpr const TCHAR* JabHitSound = TEXT("impactPunch_medium");
    inline constexpr const TCHAR* HeavyHitSound = TEXT("impactPunch_heavy");
    inline constexpr const TCHAR* BlockedSound = TEXT("impactSoft_medium");
    inline constexpr const TCHAR* FloorSound = TEXT("impactSoft_heavy");
    inline constexpr int32 SoundVariants = 5;

    /** /Game/Audio/Combat/impactPunch_medium_003.impactPunch_medium_003 */
    inline FString SoundPath(const TCHAR* Stem, int32 Variant)
    {
        const FString Name = FString::Printf(TEXT("%s_%03d"), Stem, Variant);
        return FString::Printf(TEXT("%s/%s.%s"), SoundFolder, *Name, *Name);
    }

    // Prefix for machine-parsed combat evidence lines (scripts/combat-smoke-linux.sh).
    inline constexpr const TCHAR* CombatLogPrefix = TEXT("MB2COMBAT");
}
