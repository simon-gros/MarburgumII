#pragma once

#include "CoreMinimal.h"

// Street people from the original Marburgum (ADR-044): the ambient population of its Unreal port
// (UnrealMarburgum/Source/MarburgumServer/.../MarburgumAmbientPopulation.{h,cpp}, docs/AMBIENT_STREETS.md and
// project/data/population_groups.json + npc_visual_profiles.json). Engine-neutral: Data/Gameplay/street.v0.1.json
// carries the same numbers, groups and tables, and `mb2 validate street` keeps the two in step.
//
// People appear on the pavements around every player, out of sight, walk the walk graph built from the
// streets (tools/lent-ground walk.rs), and go again once nobody is near. Each has a personality, and what they
// do about a fight, someone lying in the street, a car about to hit them or a player walking into them is
// rolled from that personality's table.
//
// What the original took from Rockstar's games stays out, as in ADR-041: San Andreas' shipped decision weights
// (its fight, carjack and ram answers), the spawn and cull bands read out of its executable, GTA III's
// 45-degree wander, signal clock and car following, and its crime and pursuit systems. Where the original used
// one of those, the number here is Marburgum II's own and says so. The tables the original's author wrote themselves
// (being hit, being bumped, a body), the groups, their paces and personality mixes and the looks are copied
// unchanged.
namespace MarburgumII::Street
{
    inline constexpr const TCHAR* StreetLogPrefix = TEXT("MB2_STREET");

    // ---- Wire names ----

    // What a street person is doing, as evidence and logs name it.
    inline constexpr const TCHAR* IntentWalk = TEXT("walk");
    inline constexpr const TCHAR* IntentFlee = TEXT("flee");
    inline constexpr const TCHAR* IntentWatch = TEXT("watch");
    inline constexpr const TCHAR* IntentGather = TEXT("gather");
    inline constexpr const TCHAR* IntentDodge = TEXT("dodge");
    inline constexpr const TCHAR* IntentFight = TEXT("fight");
    inline constexpr const TCHAR* IntentDown = TEXT("down");

    // Personalities (the original's norm / tough / weak).
    enum class EPersonality : uint8 { Norm, Tough, Weak, Count };
    inline constexpr const TCHAR* PersonalityNames[] = { TEXT("norm"), TEXT("tough"), TEXT("weak") };

    // What the tables answer. Fight: a fight close by. RunOver: someone hit by a car close by. Body: someone
    // lying in the street. Attacked: they are hit themselves. Bumped: a player on foot walks into them.
    enum class EEvent : uint8 { Fight, RunOver, Body, Attacked, Bumped, Count };
    inline constexpr const TCHAR* EventNames[] = { TEXT("fight"), TEXT("run_over"), TEXT("body"), TEXT("attacked"), TEXT("bumped") };

    // Who it is about, relative to the person reacting: the table's column. The original had friend and enemy
    // columns waiting for factions; Marburgum II has no factions yet.
    enum class ESource : uint8 { Stranger, Player, Count };

    enum class EResponse : uint8 { Flee, Watch, Ignore, Fight, Count };
    inline constexpr const TCHAR* ResponseNames[] = { TEXT("flee"), TEXT("watch"), TEXT("ignore"), TEXT("fight") };

    // ---- How many, and where they come and go (Marburgum II's own numbers) ----

    // The original kept 24 per player across the whole city; Lent is one window of it.
    inline constexpr int32 PeoplePerPlayer = 20;
    inline constexpr int32 MaxPeople = 48;
    // At most this many appear per second, so a teleport does not stall the server.
    inline constexpr float SpawnsPerSecond = 4.f;
    // A player sees what lies within this half-angle of where they look.
    inline constexpr float ViewHalfAngleDegrees = 60.f;
    // People appear this far off inside a player's view, or nearer outside it, never closer to any player.
    inline constexpr float SpawnInViewMinCm = 4000.f;
    inline constexpr float SpawnInViewMaxCm = 5500.f;
    inline constexpr float SpawnOutOfViewMinCm = 2200.f;
    inline constexpr float SpawnOutOfViewMaxCm = 3500.f;
    // Share of new people placed ahead of a player rather than round them.
    inline constexpr float InViewSpawnShare = 0.6f;
    // Always kept within KeepCm of some player; up to CullCm while a player saw them in the last UnseenSeconds.
    inline constexpr float KeepCm = 3500.f;
    inline constexpr float CullCm = 6500.f;
    inline constexpr float UnseenSeconds = 5.f;
    // Someone knocked out stays where they fell until no player is this close (then they are taken away).
    inline constexpr float ClearBodyCm = 3000.f;
    inline constexpr float ClearBodySeconds = 60.f;

    // ---- Walking (Marburgum II's own: the original walked GTA III's 45-degree headings) ----

    // A walker keeps to the right of the line it follows, so two coming the other way pass.
    inline constexpr float KeepRightCm = 30.f;
    // Close enough to a node to take the next edge.
    inline constexpr float NodeArriveCm = 70.f;
    // At a branch, how strongly they keep their way: weight exp(Straightness * cos(turn)).
    inline constexpr float Straightness = 3.f;
    // Their way drifts by up to this much per second, so nobody walks one street for ever.
    inline constexpr float HeadingDriftDegreesPerSecond = 6.f;
    // Under this share of a tick's stride for this long, a walker turns back.
    inline constexpr float StuckSeconds = 2.f;

    // ---- Noticing (the original's own distances unless marked) ----

    // People notice a fight or a run-over this close. Marburgum II's own (the original's 40 m was San Andreas').
    inline constexpr float FightNoticeCm = 3000.f;
    // Nobody walks into the last FightClearanceCm of a fight; a watcher closer than WatchStandoffCm backs away.
    inline constexpr float FightClearanceCm = 350.f;
    inline constexpr float WatchStandoffCm = 450.f;
    // Someone running from or watching a fight alarms people this close, who answer the same fight, one hop
    // per tick. A flee from a car, a body or a bump does not spread.
    inline constexpr float CrowdSpreadCm = 1200.f;
    // Someone on the ground is noticed this close, and within this height.
    inline constexpr float BodyNoticeCm = 2000.f;
    inline constexpr float BodyNoticeHeightCm = 400.f;
    // Onlookers stand this far off the body, facing it, for a while, then walk on.
    inline constexpr float GatherOffsetCm = 150.f;
    inline constexpr float GatherSecondsMin = 6.f;
    inline constexpr float GatherSecondsMax = 14.f;
    // A player on foot this close and walking into them, between these speeds, is a bump.
    inline constexpr float BumpCm = 90.f;
    inline constexpr float BumpSpeedMinCm = 60.f;
    inline constexpr float BumpSpeedMaxCm = 700.f;

    // ---- Answers ----

    // How long a scare lasts (Marburgum II's own).
    inline constexpr float FleeSeconds = 8.f;
    inline constexpr float WatchSeconds = 8.f;
    // Fleeing runs at the game's run speed times the person's own pace.
    inline constexpr float FleePaceMin = 0.85f;
    inline constexpr float FleePaceMax = 1.05f;
    // A car that will pass within DodgeClearanceCm of them within DodgeLookaheadSeconds makes them jump aside.
    inline constexpr float DodgeLookaheadSeconds = 1.5f;
    inline constexpr float DodgeClearanceCm = 250.f;
    inline constexpr float DodgeMinSpeedCm = 200.f;
    inline constexpr float DodgeSeconds = 1.2f;
    inline constexpr float DodgeStepCm = 350.f;

    // ---- Run over (the original's MarburgumRunOver rules, without San Andreas' crime points) ----

    // A car at least this fast strikes whoever stands inside its body (plus the margin); slower, it only nudges.
    inline constexpr float RunOverMinSpeedCm = 250.f;
    inline constexpr float RunOverMarginCm = 30.f;
    inline constexpr float RunOverHeightCm = 200.f;
    // Vitality lost per metre per second.
    inline constexpr float RunOverDamagePerMetrePerSecond = 10.f;
    // Struck but not out: down for this long, then up and running from where the car was.
    inline constexpr float RunOverDownSeconds = 4.f;
    // A run-over counts as an incident there for this long.
    inline constexpr float RunOverIncidentSeconds = 3.f;
    // A fight counts as going on this long after its last swing.
    inline constexpr float FightIncidentSeconds = 2.f;

    // Street people's bodies (the original's street person: 70 vitality).
    inline constexpr float StreetVitality = 70.f;

    // ---- Decision tables ----

    /** Relative weights of each response, [stranger, player]. Zero rows never happen. */
    struct FAnswer
    {
        int32 Flee[2];
        int32 Watch[2];
        int32 Ignore[2];
        int32 Fight[2];
    };

    // Rows: personality; columns: event in EEvent order.
    // fight, run_over: Marburgum II's own (the original's fight row was San Andreas'; it had no run-over row).
    // body, attacked, bumped: the original's own, unchanged.
    inline constexpr FAnswer Decisions[3][5] = {
        // norm
        {
            { .Flee = { 50, 55 }, .Watch = { 40, 35 }, .Ignore = { 10, 10 }, .Fight = { 0, 0 } },
            { .Flee = { 45, 50 }, .Watch = { 50, 45 }, .Ignore = { 5, 5 }, .Fight = { 0, 0 } },
            { .Flee = { 40, 55 }, .Watch = { 50, 40 }, .Ignore = { 10, 5 }, .Fight = { 0, 0 } },
            { .Flee = { 80, 80 }, .Watch = { 0, 0 }, .Ignore = { 0, 0 }, .Fight = { 20, 20 } },
            { .Flee = { 60, 60 }, .Watch = { 0, 0 }, .Ignore = { 40, 40 }, .Fight = { 0, 0 } },
        },
        // tough
        {
            { .Flee = { 15, 20 }, .Watch = { 55, 50 }, .Ignore = { 30, 30 }, .Fight = { 0, 0 } },
            { .Flee = { 10, 15 }, .Watch = { 75, 70 }, .Ignore = { 15, 15 }, .Fight = { 0, 0 } },
            { .Flee = { 10, 15 }, .Watch = { 60, 65 }, .Ignore = { 30, 20 }, .Fight = { 0, 0 } },
            { .Flee = { 30, 30 }, .Watch = { 0, 0 }, .Ignore = { 0, 0 }, .Fight = { 70, 70 } },
            { .Flee = { 0, 0 }, .Watch = { 25, 25 }, .Ignore = { 60, 60 }, .Fight = { 15, 15 } },
        },
        // weak
        {
            { .Flee = { 85, 90 }, .Watch = { 15, 10 }, .Ignore = { 0, 0 }, .Fight = { 0, 0 } },
            { .Flee = { 80, 85 }, .Watch = { 20, 15 }, .Ignore = { 0, 0 }, .Fight = { 0, 0 } },
            { .Flee = { 80, 90 }, .Watch = { 20, 10 }, .Ignore = { 0, 0 }, .Fight = { 0, 0 } },
            { .Flee = { 100, 100 }, .Watch = { 0, 0 }, .Ignore = { 0, 0 }, .Fight = { 0, 0 } },
            { .Flee = { 100, 100 }, .Watch = { 0, 0 }, .Ignore = { 0, 0 }, .Fight = { 0, 0 } },
        },
    };

    /** The answer for a roll in [0, 1): the weights in Flee, Watch, Ignore, Fight order. */
    inline EResponse Decide(EPersonality Personality, EEvent Event, ESource Source, double Roll)
    {
        const FAnswer& Row = Decisions[static_cast<int32>(Personality)][static_cast<int32>(Event)];
        const int32 Column = static_cast<int32>(Source);
        const int32 Weights[] = { Row.Flee[Column], Row.Watch[Column], Row.Ignore[Column], Row.Fight[Column] };
        const int32 Total = Weights[0] + Weights[1] + Weights[2] + Weights[3];
        if (Total <= 0)
        {
            return EResponse::Ignore;
        }
        double Left = FMath::Clamp(Roll, 0.0, 0.999999) * Total;
        for (int32 Index = 0; Index < 4; ++Index)
        {
            Left -= Weights[Index];
            if (Left < 0.0 && Weights[Index] > 0)
            {
                return static_cast<EResponse>(Index);
            }
        }
        return EResponse::Ignore;
    }

    /** What a louder incident is: one being answered gives way to a louder one, never to a quieter one. */
    inline int32 Loudness(EEvent Event)
    {
        switch (Event)
        {
        case EEvent::Attacked: return 5;
        case EEvent::RunOver: return 4;
        case EEvent::Fight: return 3;
        case EEvent::Body: return 2;
        default: return 1;
        }
    }

    // ---- Who walks (population_groups.json and npc_visual_profiles.json, unchanged) ----

    /** One look: an npc_visual_profiles.json profile. sRGB 0xRRGGBB as authored. */
    struct FLook
    {
        const TCHAR* Profile;
        const TCHAR* Archetype;
        uint32 PrimaryColor;
        uint32 SecondaryColor;
        float Scale;
    };

    inline constexpr FLook Looks[] = {
        { TEXT("tabor_commuter"), TEXT("commuter"), 0x3f5160, 0x242c32, 1.01f },
        { TEXT("petra_kralj"), TEXT("office_casual"), 0x6d5362, 0x303845, 0.96f },
        { TEXT("irena_vogrin"), TEXT("office_bookkeeper"), 0x5d5862, 0x303038, 0.98f },
        { TEXT("marija_sever"), TEXT("postal_worker"), 0x3e5c58, 0x242e2c, 0.98f },
        { TEXT("lent_dispatcher"), TEXT("delivery_clerk"), 0x425e6b, 0x27333a, 1.01f },
        { TEXT("lent_square_reader"), TEXT("newspaper_reader"), 0x5a5348, 0x2e2c28, 1.f },
        { TEXT("kiosk_67_cafe_regular"), TEXT("cafe_regular"), 0x7a5344, 0x3a322c, 0.97f },
        { TEXT("kiosk_67_neighbour"), TEXT("neighbour"), 0x6a6848, 0x333228, 1.02f },
        { TEXT("under_paint_new_owner"), TEXT("storefront_owner"), 0x625a4f, 0x34302b, 1.f },
        { TEXT("sara_vidic"), TEXT("cultural_organizer"), 0x2c2e33, 0x1c1e22, 1.f },
        { TEXT("vera_horvat"), TEXT("archivist"), 0x665d4e, 0x35322e, 0.98f },
        { TEXT("kiosk_67_bus_regular"), TEXT("bus_stop_regular"), 0x5c6570, 0x2c3136, 1.f },
        { TEXT("tine_kolar"), TEXT("student_lodger"), 0x4d6274, 0x2a3138, 1.03f },
        { TEXT("lara_mlinar"), TEXT("night_bus_teen"), 0x5a3d4e, 0x2a2228, 0.94f },
        { TEXT("nika_vidmar"), TEXT("photographer"), 0x4a403c, 0x2a2624, 0.97f },
        { TEXT("maja_koren"), TEXT("journalist"), 0x3c4658, 0x242830, 1.f },
        { TEXT("ivana_zupan"), TEXT("block_resident"), 0x7a6a5c, 0x3a342e, 0.94f },
        { TEXT("tabor_block_walker"), TEXT("block_walker"), 0x4a5348, 0x262a26, 1.04f },
        { TEXT("tabor_courtyard_walker"), TEXT("courtyard_walker"), 0x6a5e68, 0x322c32, 0.96f },
    };

    /** A population group: its looks (by archetype), walking pace and how many are norm, tough or weak. */
    struct FGroup
    {
        const TCHAR* Id;
        const TCHAR* Name;
        const TCHAR* Archetypes[5];
        /** Metres per second. */
        float WalkSpeedMin;
        float WalkSpeedMax;
        /** Norm, tough, weak. */
        int32 Personality[3];
    };

    // The groups that walk Lent by day (the original's old_town day mix); the rest wait for other districts and
    // a world clock.
    inline constexpr FGroup Groups[] = {
        { TEXT("commuters"), TEXT("Commuter"), { TEXT("commuter"), TEXT("office_casual"), TEXT("office_bookkeeper"), TEXT("postal_worker"), TEXT("delivery_clerk") },
            1.35f, 1.65f, { 75, 10, 15 } },
        { TEXT("shoppers"), TEXT("Shopper"), { TEXT("newspaper_reader"), TEXT("cafe_regular"), TEXT("neighbour"), TEXT("storefront_owner"), TEXT("cultural_organizer") },
            1.0f, 1.35f, { 70, 5, 25 } },
        { TEXT("pensioners"), TEXT("Pensioner"), { TEXT("archivist"), TEXT("newspaper_reader"), TEXT("bus_stop_regular"), nullptr, nullptr },
            0.8f, 1.1f, { 35, 0, 65 } },
        { TEXT("students"), TEXT("Student"), { TEXT("student_lodger"), TEXT("night_bus_teen"), TEXT("photographer"), TEXT("journalist"), nullptr },
            1.3f, 1.7f, { 55, 35, 10 } },
        { TEXT("residents"), TEXT("Resident"), { TEXT("block_resident"), TEXT("block_walker"), TEXT("courtyard_walker"), TEXT("neighbour"), TEXT("bus_stop_regular") },
            1.0f, 1.4f, { 70, 15, 15 } },
    };

    /** Lent's mix by day: relative weights over Groups (the original's old_town day row, plus its residents). */
    inline constexpr int32 LentMix[] = { 20, 40, 20, 20, 10 };
    static_assert(UE_ARRAY_COUNT(LentMix) == UE_ARRAY_COUNT(Groups));

    inline int32 PickWeighted(const int32* Weights, int32 Count, double Roll)
    {
        int32 Total = 0;
        for (int32 Index = 0; Index < Count; ++Index)
        {
            Total += FMath::Max(0, Weights[Index]);
        }
        double Left = FMath::Clamp(Roll, 0.0, 0.999999) * Total;
        for (int32 Index = 0; Index < Count; ++Index)
        {
            Left -= FMath::Max(0, Weights[Index]);
            if (Left < 0.0 && Weights[Index] > 0)
            {
                return Index;
            }
        }
        return Count - 1;
    }

    inline EPersonality PickPersonality(const FGroup& Group, double Roll)
    {
        return static_cast<EPersonality>(PickWeighted(Group.Personality, 3, Roll));
    }

    /** The Looks[] index of an archetype, or INDEX_NONE. */
    inline int32 FindLook(const TCHAR* Archetype)
    {
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Looks); ++Index)
        {
            if (FCString::Strcmp(Looks[Index].Archetype, Archetype) == 0)
            {
                return Index;
            }
        }
        return INDEX_NONE;
    }

    /** The Looks[] index for a group's archetype picked by a roll. */
    inline int32 PickLook(const FGroup& Group, double Roll)
    {
        int32 Count = 0;
        while (Count < 5 && Group.Archetypes[Count])
        {
            ++Count;
        }
        const int32 Pick = FMath::Clamp(static_cast<int32>(FMath::Clamp(Roll, 0.0, 0.999999) * Count), 0, FMath::Max(0, Count - 1));
        return Count > 0 ? FindLook(Group.Archetypes[Pick]) : INDEX_NONE;
    }

    // ---- Run over, as pure rules ----

    /** Vitality a car at Speed (cm/s) takes off whoever it strikes; 0 under the strike speed. */
    inline float RunOverDamage(float SpeedCm)
    {
        return SpeedCm < RunOverMinSpeedCm ? 0.f : SpeedCm / 100.f * RunOverDamagePerMetrePerSecond;
    }

    /**
     * Whether a point lies inside a car's body swept back over the distance it covered: the body is a box of
     * HalfLength x HalfWidth (plus the margin) round Centre, facing Forward (unit, horizontal), and it moved
     * Travelled cm along Forward since the last check (negative when reversing).
     */
    inline bool InSweptBody(const FVector2D& Point, const FVector2D& Centre, const FVector2D& Forward, double HalfLength, double HalfWidth,
        double Travelled)
    {
        const FVector2D Offset = Point - Centre;
        const double Along = FVector2D::DotProduct(Offset, Forward);
        const double Across = FMath::Abs(Offset.X * Forward.Y - Offset.Y * Forward.X);
        const double Margin = RunOverMarginCm;
        const double Back = FMath::Max(0.0, Travelled);
        const double Front = FMath::Max(0.0, -Travelled);
        return Across <= HalfWidth + Margin && Along <= HalfLength + Margin + Front && Along >= -HalfLength - Margin - Back;
    }

    /**
     * Whether a mover at Position with Velocity (cm, cm/s; horizontal) passes within Clearance of Point inside
     * Lookahead seconds: the dodge test.
     */
    inline bool WillPassClose(const FVector2D& Point, const FVector2D& Position, const FVector2D& Velocity, double Lookahead, double Clearance,
        double* OutSeconds = nullptr)
    {
        const double Speed2 = Velocity.SizeSquared();
        if (Speed2 < 1.0)
        {
            return false;
        }
        const double T = FVector2D::DotProduct(Point - Position, Velocity) / Speed2;
        if (T < 0.0 || T > Lookahead)
        {
            return false;
        }
        if (OutSeconds)
        {
            *OutSeconds = T;
        }
        return FVector2D::Distance(Position + Velocity * T, Point) <= Clearance;
    }
}
