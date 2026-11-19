#pragma once

#include "CoreMinimal.h"
#include "MarburgumLitWindows.h"

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
    inline constexpr const TCHAR* IntentVisit = TEXT("visit");

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
        // The night's people (ADR-067), after the rest so the indices above stay what clients already know.
        { TEXT("lent_passerby"), TEXT("evening_walker"), 0x4e5c68, 0x2a3036, 0.99f },
        { TEXT("hall_three_night_guard"), TEXT("night_guard"), 0x313a42, 0x1f2428, 1.05f },
        { TEXT("lent_drunk"), TEXT("belligerent_drinker"), 0x5a3a3a, 0x2a2222, 1.04f },
        // Workers and people out of doors, for the districts (ADR-068), after the rest for the same reason.
        { TEXT("hall_three_worker"), TEXT("industrial_worker"), 0x4e5a63, 0x2b3035, 1.03f },
        { TEXT("lent_workshop_mechanic"), TEXT("mechanic"), 0x354b58, 0x242b30, 1.04f },
        { TEXT("matej_hall_three"), TEXT("industrial_custodian"), 0x59604c, 0x2d3129, 1.02f },
        { TEXT("brane_sign_painter"), TEXT("sign_painter"), 0x4e5962, 0x2b3035, 1.02f },
        { TEXT("drago_potocnik"), TEXT("mechanic_dispatcher"), 0x3a4a56, 0x22282c, 1.05f },
        { TEXT("lent_angler"), TEXT("riverside_angler"), 0x596455, 0x353a34, 1.f },
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

    // The original's street groups, all but the ones ADR-006 keeps out. The old town's come first (ADR-067); workers
    // and people out of doors came with the other districts (ADR-068), after them so the indices stay.
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
        { TEXT("night_people"), TEXT("Night owl"), { TEXT("night_bus_teen"), TEXT("evening_walker"), TEXT("night_guard"), TEXT("belligerent_drinker"), nullptr },
            1.0f, 1.5f, { 40, 50, 10 } },
        { TEXT("workers"), TEXT("Worker"), { TEXT("industrial_worker"), TEXT("mechanic"), TEXT("industrial_custodian"), TEXT("sign_painter"), TEXT("mechanic_dispatcher") },
            1.2f, 1.5f, { 55, 40, 5 } },
        { TEXT("outdoor"), TEXT("Out walking"), { TEXT("evening_walker"), TEXT("riverside_angler"), TEXT("courtyard_walker"), nullptr, nullptr },
            1.0f, 1.4f, { 70, 15, 15 } },
    };
    inline constexpr int32 GroupCount = UE_ARRAY_COUNT(Groups);
    enum EGroupIndex : int32 { Commuters, Shoppers, Pensioners, Students, Residents, NightPeople, Workers, Outdoor };

    // ---- Who is out when (ADR-067), and where (ADR-068) ----
    //
    // The original's time-of-day tables (population_groups.json `periods` and `zones`), read on the world clock
    // (ADR-064): its Ljubljana hour and the mirrored 1990s date's own weekday. ADR-044 left them out for want of a
    // clock; ADR-067 brought in the old town's, ADR-068 every zone's, with the zone of every walk-graph node from the
    // original's road graph (tools/lent-ground street-zones). The weekday tables are copied unchanged, all but the
    // share of every zone's street that ADR-006 keeps out. The original's weekend (Friday 20:00 to Sunday 20:00,
    // with its own tables per zone) and its rain rule came in from a reverse-engineering report of San Andreas'
    // population cycle, so they stay out; Marburgum II's own weekend and rain are below. Cars wait for traffic.

    enum class EPeriod : uint8 { Night, Morning, Day, Evening, Late, Count };
    inline constexpr const TCHAR* PeriodNames[] = { TEXT("night"), TEXT("morning"), TEXT("day"), TEXT("evening"), TEXT("late") };
    inline constexpr int32 PeriodCount = static_cast<int32>(EPeriod::Count);
    /** The hour each period begins on a weekday: night 0-6, morning 6-9, day 9-17, evening 17-21, late 21-24. */
    inline constexpr int32 PeriodStartHours[] = { 0, 6, 9, 17, 21 };

    /**
     * The original's zones (tools/build_road_graph.py ZONES), in its order, which is the walk graph's: what kind of
     * street a node is on, from the cadastre's homes and business premises, the Center quarter's heritage listings,
     * OpenStreetMap's industrial and school buildings and the green land-use round it.
     */
    enum class EZone : uint8 { OldTown, Commercial, ResidentialBlocks, Residential, Industrial, School, Park, Rural, Count };
    inline constexpr const TCHAR* ZoneNames[] = { TEXT("old_town"), TEXT("commercial"), TEXT("residential_blocks"), TEXT("residential"),
        TEXT("industrial"), TEXT("school"), TEXT("park"), TEXT("rural") };
    inline constexpr int32 ZoneCount = static_cast<int32>(EZone::Count);

    /** How full the street is in each zone and period, of PeoplePerPlayer (the zones' `pedestrians`). */
    inline constexpr float ZoneDensity[][PeriodCount] = {
        // night, morning, day, evening, late
        { 0.15f, 0.6f, 1.0f, 0.9f, 0.45f }, // old_town
        { 0.1f, 0.6f, 0.9f, 0.7f, 0.25f }, // commercial
        { 0.1f, 0.7f, 0.6f, 0.8f, 0.3f }, // residential_blocks
        { 0.05f, 0.45f, 0.4f, 0.55f, 0.15f }, // residential
        { 0.05f, 0.5f, 0.35f, 0.25f, 0.05f }, // industrial
        { 0.03f, 0.9f, 0.8f, 0.35f, 0.1f }, // school
        { 0.02f, 0.25f, 0.45f, 0.5f, 0.08f }, // park
        { 0.0f, 0.08f, 0.1f, 0.08f, 0.0f }, // rural
    };

    /**
     * Who is out in each zone and period: relative weights over Groups (the zones' `pedestrian_mix`). The old town's
     * day row is ADR-044's, which added its residents to the original's.
     */
    inline constexpr int32 ZoneMix[][PeriodCount][GroupCount] = {
        // commuters, shoppers, pensioners, students, residents, night people, workers, out walking
        { // old_town
            { 0, 0, 0, 0, 20, 80, 0, 0 },
            { 50, 20, 15, 15, 0, 0, 0, 0 },
            { 20, 40, 20, 20, 10, 0, 0, 0 },
            { 0, 30, 0, 25, 25, 20, 0, 0 },
            { 0, 0, 0, 30, 10, 60, 0, 0 },
        },
        { // commercial
            { 0, 0, 0, 0, 0, 70, 30, 0 },
            { 60, 20, 0, 0, 0, 0, 20, 0 },
            { 25, 50, 15, 10, 0, 0, 0, 0 },
            { 25, 45, 0, 15, 0, 15, 0, 0 },
            { 30, 0, 0, 0, 0, 70, 0, 0 },
        },
        { // residential_blocks
            { 0, 0, 0, 0, 30, 70, 0, 0 },
            { 45, 0, 10, 25, 20, 0, 0, 0 },
            { 0, 20, 30, 10, 40, 0, 0, 0 },
            { 25, 0, 0, 25, 50, 0, 0, 0 },
            { 0, 0, 0, 0, 50, 50, 0, 0 },
        },
        { // residential
            { 0, 0, 0, 0, 40, 60, 0, 0 },
            { 45, 0, 10, 20, 25, 0, 0, 0 },
            { 0, 20, 35, 0, 45, 0, 0, 0 },
            { 20, 0, 0, 0, 60, 0, 0, 20 },
            { 0, 0, 0, 0, 60, 40, 0, 0 },
        },
        { // industrial
            { 0, 0, 0, 0, 0, 30, 70, 0 },
            { 20, 0, 0, 0, 0, 0, 80, 0 },
            { 15, 0, 0, 0, 0, 0, 85, 0 },
            { 0, 0, 0, 0, 30, 0, 70, 0 },
            { 0, 0, 0, 0, 0, 50, 50, 0 },
        },
        { // school
            { 0, 0, 0, 0, 0, 100, 0, 0 },
            { 25, 0, 0, 75, 0, 0, 0, 0 },
            { 15, 0, 0, 70, 15, 0, 0, 0 },
            { 0, 0, 0, 50, 50, 0, 0, 0 },
            { 0, 0, 0, 50, 0, 50, 0, 0 },
        },
        { // park
            { 0, 0, 0, 0, 0, 100, 0, 0 },
            { 20, 0, 30, 0, 0, 0, 0, 50 },
            { 0, 0, 30, 25, 0, 0, 0, 45 },
            { 0, 0, 0, 25, 20, 0, 0, 55 },
            { 0, 0, 0, 0, 0, 70, 0, 30 },
        },
        { // rural
            { 0, 0, 0, 0, 0, 0, 0, 100 },
            { 0, 0, 0, 0, 0, 0, 60, 40 },
            { 0, 0, 0, 0, 0, 0, 40, 60 },
            { 0, 0, 0, 0, 30, 0, 0, 70 },
            { 0, 0, 0, 0, 0, 0, 0, 100 },
        },
    };
    static_assert(UE_ARRAY_COUNT(PeriodNames) == PeriodCount);
    static_assert(UE_ARRAY_COUNT(PeriodStartHours) == PeriodCount);
    static_assert(UE_ARRAY_COUNT(ZoneNames) == ZoneCount);
    static_assert(UE_ARRAY_COUNT(ZoneDensity) == ZoneCount);
    static_assert(UE_ARRAY_COUNT(ZoneMix) == ZoneCount);

    /** The zone of a walk-graph name, or the old town for one the game does not know (ADR-067's street). */
    inline EZone ZoneNamed(const FString& Name)
    {
        for (int32 Zone = 0; Zone < ZoneCount; ++Zone)
        {
            if (Name == ZoneNames[Zone])
            {
                return static_cast<EZone>(Zone);
            }
        }
        return EZone::OldTown;
    }

    // Marburgum II's own weekend, after the lit windows' households (ADR-066): Friday and Saturday evenings run this
    // many hours later, into the small hours of Saturday and Sunday, and Saturday and Sunday mornings start as late.
    // Nobody commutes on a Saturday or a Sunday, the shops are shut on a Sunday (MarburgumLitWindows.h ShopsOpen) and
    // there is no school at the weekend (ADR-068): their share of the street walks as residents instead. Workers keep
    // their shifts.
    inline constexpr int32 WeekendLaterHours = 1;

    // Marburgum II's own rain: whatever falls (rain, sleet or snow, MarburgumWeather.h's 0..1) keeps people in.
    // A downpour leaves a quarter of them.
    inline constexpr float RainThinning = 0.75f;

    // With more people out than the hour wants, one nobody has seen for UnseenSeconds goes home this often, the
    // furthest first, and never nearer a player than SpawnOutOfViewMinCm. Anyone in sight stays.
    inline constexpr float HomeEverySeconds = 2.f;

    /** The street at an hour of the Marburgum clock, in one zone. */
    struct FStreetHour
    {
        EPeriod Period = EPeriod::Day;
        /** The day the period belongs to (0 Monday): Saturday's small hours are Friday's late evening. */
        int32 Weekday = 1;
        EZone Zone = EZone::OldTown;
        /** Of PeoplePerPlayer, before the weather. */
        float Density = 1.f;
        int32 Mix[GroupCount] = {};
    };

    /**
     * The street at Hour (0..24, with minutes as a fraction) on Weekday (0 Monday .. 6 Sunday) of the Marburgum clock,
     * in Zone (the old town unless said).
     */
    inline FStreetHour StreetHourAt(int32 Weekday, double Hour, EZone Zone = EZone::OldTown)
    {
        FStreetHour Out;
        Weekday = ((Weekday % 7) + 7) % 7;
        const bool bLateNight = Weekday == 4 || Weekday == 5;
        const bool bLateMorning = Weekday == 5 || Weekday == 6;
        Out.Weekday = Weekday;
        Out.Zone = static_cast<int32>(Zone) < ZoneCount ? Zone : EZone::OldTown;
        if (bLateMorning && Hour < WeekendLaterHours)
        {
            Out.Period = EPeriod::Late;
            Out.Weekday = Weekday - 1;
        }
        else if (Hour < PeriodStartHours[1] + (bLateMorning ? WeekendLaterHours : 0))
        {
            Out.Period = EPeriod::Night;
        }
        else if (Hour < PeriodStartHours[2])
        {
            Out.Period = EPeriod::Morning;
        }
        else if (Hour < PeriodStartHours[3])
        {
            Out.Period = EPeriod::Day;
        }
        else if (Hour < PeriodStartHours[4] + (bLateNight ? WeekendLaterHours : 0))
        {
            Out.Period = EPeriod::Evening;
        }
        else
        {
            Out.Period = EPeriod::Late;
        }
        const int32 Period = static_cast<int32>(Out.Period);
        const int32 ZoneIndex = static_cast<int32>(Out.Zone);
        Out.Density = ZoneDensity[ZoneIndex][Period];
        for (int32 Group = 0; Group < GroupCount; ++Group)
        {
            Out.Mix[Group] = ZoneMix[ZoneIndex][Period][Group];
        }
        const bool bWeekend = Out.Weekday == 5 || Out.Weekday == 6;
        const bool bDaylight = Out.Period == EPeriod::Morning || Out.Period == EPeriod::Day;
        if (bWeekend)
        {
            Out.Mix[Residents] += Out.Mix[Commuters];
            Out.Mix[Commuters] = 0;
        }
        if (Out.Weekday == 6 && bDaylight)
        {
            Out.Mix[Residents] += Out.Mix[Shoppers];
            Out.Mix[Shoppers] = 0;
        }
        if (bWeekend && bDaylight && Out.Zone == EZone::School)
        {
            Out.Mix[Residents] += Out.Mix[Students];
            Out.Mix[Students] = 0;
        }
        return Out;
    }

    /** The share of people the weather leaves out of doors: Precipitation is MarburgumWeather.h's 0..1. */
    inline float WeatherShare(float Precipitation)
    {
        return 1.f - RainThinning * FMath::Clamp(Precipitation, 0.f, 1.f);
    }

    /** How many people the street keeps for this many players at this share (density times weather). */
    inline int32 PeopleFor(int32 Players, float Share)
    {
        return Players <= 0 ? 0 : FMath::Clamp(FMath::RoundToInt(PeoplePerPlayer * Players * FMath::Max(0.f, Share)), 0, MaxPeople);
    }

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

    // ---- Places that draw people in (ADR-069) ----
    //
    // The original's attractors (project/data/road_graph.json `attractors`, population_groups.json `attractors` and each
    // group's `visits`; FPopulation::TryVisit, StepVisit and TrySpawnAtDoor): bus stops, cafés, restaurants, bars, fast
    // food, shops and sights, each pinned to the walk node people reach it from (tools/lent-ground --mode attractors).
    // Somebody walking about who reaches that node, or one next to it within AttractorReachCm, goes to a place there that
    // is open, with a chance of their group's visit weight for its kind times how open it is: they wait at a bus stop,
    // look at a sight for a while and walk on, or go into a venue and leave the street. People also come out of open
    // doors, and some are already waiting at a stop when they appear. The kinds, their actions, dwell times, hours and
    // the groups' weights are the original author's, copied unchanged. Its people already waiting at a stop were spawned
    // only 09:00-19:59 "as San Andreas spawns attractor users", which stays out: here a stop has people waiting whenever
    // it is open. Shops keep the 1990s shop hours the lit windows and Kiosk 67 keep (ShopsOpen), so none is open on a
    // Sunday. And nobody goes straight back to the place they have just left (the original's people could).

    /** The original's ATTRACTOR_KINDS, in its order, which is the walk graph's. */
    enum class EAttractor : uint8 { BusStop, Cafe, Restaurant, Bar, FastFood, Shop, Sight, Count };
    inline constexpr const TCHAR* AttractorNames[] = { TEXT("bus_stop"), TEXT("cafe"), TEXT("restaurant"), TEXT("bar"), TEXT("fast_food"),
        TEXT("shop"), TEXT("sight") };
    inline constexpr int32 AttractorCount = static_cast<int32>(EAttractor::Count);

    /** What somebody does there: wait a while (a bus stop), go in and leave the street (a venue), look a while (a sight). */
    enum class EVisit : uint8 { Wait, Enter, Look };
    inline constexpr const TCHAR* VisitNames[] = { TEXT("wait"), TEXT("enter"), TEXT("look") };

    struct FAttractorKind
    {
        EVisit Visit;
        /** Seconds there, for waiting and looking. */
        float DwellMin;
        float DwellMax;
        /** Per period, 0..1: how open it is (night, morning, day, evening, late). */
        float Open[PeriodCount];
    };

    inline constexpr FAttractorKind AttractorKinds[] = {
        { EVisit::Wait, 20.f, 90.f, { 0.2f, 1.0f, 0.8f, 0.8f, 0.4f } }, // bus_stop
        { EVisit::Enter, 0.f, 0.f, { 0.0f, 0.8f, 1.0f, 0.8f, 0.2f } }, // cafe
        { EVisit::Enter, 0.f, 0.f, { 0.0f, 0.1f, 0.8f, 1.0f, 0.3f } }, // restaurant
        { EVisit::Enter, 0.f, 0.f, { 0.5f, 0.0f, 0.2f, 0.8f, 1.0f } }, // bar
        { EVisit::Enter, 0.f, 0.f, { 0.3f, 0.3f, 0.8f, 0.8f, 0.8f } }, // fast_food
        { EVisit::Enter, 0.f, 0.f, { 0.0f, 0.5f, 1.0f, 0.5f, 0.0f } }, // shop
        { EVisit::Look, 8.f, 25.f, { 0.1f, 0.5f, 1.0f, 0.8f, 0.3f } }, // sight
    };

    /** Each group's chance (0..1) of going to an open place of each kind it passes (the groups' `visits`). */
    inline constexpr float Visits[][AttractorCount] = {
        // bus stop, cafe, restaurant, bar, fast food, shop, sight
        { 0.5f, 0.15f, 0.0f, 0.0f, 0.1f, 0.1f, 0.0f }, // commuters
        { 0.0f, 0.3f, 0.1f, 0.0f, 0.0f, 0.5f, 0.05f }, // shoppers
        { 0.4f, 0.2f, 0.0f, 0.0f, 0.0f, 0.3f, 0.1f }, // pensioners
        { 0.4f, 0.2f, 0.0f, 0.15f, 0.3f, 0.0f, 0.0f }, // students
        { 0.2f, 0.1f, 0.0f, 0.05f, 0.0f, 0.25f, 0.0f }, // residents
        { 0.0f, 0.0f, 0.0f, 0.5f, 0.3f, 0.0f, 0.0f }, // night_people
        { 0.3f, 0.0f, 0.0f, 0.1f, 0.2f, 0.1f, 0.0f }, // workers
        { 0.1f, 0.15f, 0.0f, 0.0f, 0.0f, 0.0f, 0.4f }, // outdoor
    };
    static_assert(UE_ARRAY_COUNT(AttractorNames) == AttractorCount);
    static_assert(UE_ARRAY_COUNT(AttractorKinds) == AttractorCount);
    static_assert(UE_ARRAY_COUNT(Visits) == GroupCount);

    // The original's: a place draws people from its own walk node and the nodes joined to it within this; at most this
    // many wait at a stop or look at a sight, each this far along the kerb from the last; a sight is looked at from
    // this far off (the ground tool places the spot); and this share of newcomers come out of an open door when there
    // is one where they would appear.
    inline constexpr float AttractorReachCm = 4000.f;
    inline constexpr int32 AttractorCapacity = 3;
    inline constexpr float AttractorSlotSpacingCm = 80.f;
    inline constexpr float SightStandOffCm = 300.f;
    inline constexpr float DoorSpawnShare = 0.3f;
    // Marburgum II's own: somebody who cannot reach their spot in this long gives up and walks on.
    inline constexpr float AttractorReachSeconds = 30.f;

    /** The kind of a walk-graph name, or Count for one the game does not know. */
    inline EAttractor AttractorNamed(const FString& Name)
    {
        for (int32 Kind = 0; Kind < AttractorCount; ++Kind)
        {
            if (Name == AttractorNames[Kind])
            {
                return static_cast<EAttractor>(Kind);
            }
        }
        return EAttractor::Count;
    }

    /**
     * How open a place of this kind is, 0..1, in the street's hour (StreetHourAt, whose period has the weekend's later
     * evenings and mornings) at Hour on Weekday of the Marburgum clock: a shop only in the 1990s shop hours.
     */
    inline float AttractorOpen(EAttractor Kind, const FStreetHour& Street, int32 Weekday, double Hour)
    {
        const int32 Index = static_cast<int32>(Kind);
        if (Index < 0 || Index >= AttractorCount)
        {
            return 0.f;
        }
        if (Kind == EAttractor::Shop && !MarburgumII::LitWindows::ShopsOpen(((Weekday % 7) + 7) % 7, Hour))
        {
            return 0.f;
        }
        return AttractorKinds[Index].Open[static_cast<int32>(Street.Period)];
    }

    /** The chance somebody of Group goes to a place of this kind that is Open (0..1) as they pass it. */
    inline float VisitChance(int32 Group, EAttractor Kind, float Open)
    {
        const int32 Index = static_cast<int32>(Kind);
        if (Group < 0 || Group >= GroupCount || Index < 0 || Index >= AttractorCount)
        {
            return 0.f;
        }
        return Visits[Group][Index] * FMath::Clamp(Open, 0.f, 1.f);
    }

    /** Where the Slot'th visitor stands, along the kerb either side of the first spot: 0, then +1, -1, +2, ... places along. */
    inline FVector2D AttractorSlotOffset(const FVector2D& FromNodeToSpot, int32 Slot)
    {
        if (Slot <= 0)
        {
            return FVector2D::ZeroVector;
        }
        FVector2D Along = FromNodeToSpot.GetSafeNormal();
        Along = Along.IsNearlyZero() ? FVector2D(1.0, 0.0) : FVector2D(-Along.Y, Along.X);
        const double Side = Slot % 2 == 1 ? 1.0 : -1.0;
        return Along * Side * ((Slot + 1) / 2) * AttractorSlotSpacingCm;
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
