//! Street people (ADR-044): the street data and the shared C++ contract carry the same rules, decision tables,
//! groups and looks (the original's ambient population); every table answers something; every group can be
//! dressed; San Andreas' crime and pursuit vocabulary stays out; and the walk graph the server reads is there,
//! staged, and mostly one network.

use regex::Regex;
use serde_json::Value;
use std::path::Path;

use crate::repo::{read, read_json, Problems};

const DATA: &str = "Data/Gameplay/street.v0.1.json";
const CONTRACT: &str = "Source/MarburgumIIShared/Public/MarburgumStreetContract.h";
const GRAPH: &str = "Content/Marburgum/Street/lent_walk_graph.json";
const GAME_INI: &str = "Config/DefaultGame.ini";
const PEDESTRIAN: &str = "Source/MarburgumII/Private/MarburgumPedestrian.cpp";
const SUBSYSTEM: &str = "Source/MarburgumII/Private/MarburgumStreetSubsystem.cpp";

/// Data rule → contract constant, and the factor from the data's unit to the contract's.
const RULES: [(&str, &str, f64); 48] = [
    ("people_per_player", "PeoplePerPlayer", 1.0),
    ("max_people", "MaxPeople", 1.0),
    ("spawns_per_second", "SpawnsPerSecond", 1.0),
    ("view_half_angle_degrees", "ViewHalfAngleDegrees", 1.0),
    ("spawn_in_view_min", "SpawnInViewMinCm", 100.0),
    ("spawn_in_view_max", "SpawnInViewMaxCm", 100.0),
    ("spawn_out_of_view_min", "SpawnOutOfViewMinCm", 100.0),
    ("spawn_out_of_view_max", "SpawnOutOfViewMaxCm", 100.0),
    ("in_view_spawn_share", "InViewSpawnShare", 1.0),
    ("keep", "KeepCm", 100.0),
    ("cull", "CullCm", 100.0),
    ("unseen_seconds", "UnseenSeconds", 1.0),
    ("clear_body", "ClearBodyCm", 100.0),
    ("clear_body_seconds", "ClearBodySeconds", 1.0),
    ("keep_right", "KeepRightCm", 100.0),
    ("node_arrive", "NodeArriveCm", 100.0),
    ("straightness", "Straightness", 1.0),
    ("heading_drift_degrees_per_second", "HeadingDriftDegreesPerSecond", 1.0),
    ("stuck_seconds", "StuckSeconds", 1.0),
    ("fight_notice", "FightNoticeCm", 100.0),
    ("fight_clearance", "FightClearanceCm", 100.0),
    ("watch_standoff", "WatchStandoffCm", 100.0),
    ("crowd_spread", "CrowdSpreadCm", 100.0),
    ("body_notice", "BodyNoticeCm", 100.0),
    ("body_notice_height", "BodyNoticeHeightCm", 100.0),
    ("gather_offset", "GatherOffsetCm", 100.0),
    ("gather_seconds_min", "GatherSecondsMin", 1.0),
    ("gather_seconds_max", "GatherSecondsMax", 1.0),
    ("bump", "BumpCm", 100.0),
    ("bump_speed_min", "BumpSpeedMinCm", 100.0),
    ("bump_speed_max", "BumpSpeedMaxCm", 100.0),
    ("flee_seconds", "FleeSeconds", 1.0),
    ("watch_seconds", "WatchSeconds", 1.0),
    ("flee_pace_min", "FleePaceMin", 1.0),
    ("flee_pace_max", "FleePaceMax", 1.0),
    ("dodge_lookahead_seconds", "DodgeLookaheadSeconds", 1.0),
    ("dodge_clearance", "DodgeClearanceCm", 100.0),
    ("dodge_min_speed", "DodgeMinSpeedCm", 100.0),
    ("dodge_seconds", "DodgeSeconds", 1.0),
    ("dodge_step", "DodgeStepCm", 100.0),
    ("run_over_min_speed", "RunOverMinSpeedCm", 100.0),
    ("run_over_margin", "RunOverMarginCm", 100.0),
    ("run_over_height", "RunOverHeightCm", 100.0),
    ("run_over_damage_per_metre_per_second", "RunOverDamagePerMetrePerSecond", 1.0),
    ("run_over_down_seconds", "RunOverDownSeconds", 1.0),
    ("run_over_incident_seconds", "RunOverIncidentSeconds", 1.0),
    ("fight_incident_seconds", "FightIncidentSeconds", 1.0),
    ("street_vitality", "StreetVitality", 1.0),
];

const PERSONALITIES: [&str; 3] = ["norm", "tough", "weak"];
const EVENTS: [&str; 5] = ["fight", "run_over", "body", "attacked", "bumped"];
const RESPONSES: [&str; 4] = ["flee", "watch", "ignore", "fight"];

fn constant(contract: &str, name: &str) -> Option<f64> {
    let pattern = Regex::new(&format!(r"inline constexpr (?:float|int32|double) {name} = ([0-9.]+)f?;")).unwrap();
    pattern.captures(contract).and_then(|c| c[1].parse().ok())
}

fn close(a: f64, b: f64) -> bool {
    (a - b).abs() < 1e-6 * a.abs().max(1.0)
}

/// The contract's Decisions[3][5] rows in order: [flee, watch, ignore, fight] x [stranger, player].
fn decision_rows(contract: &str) -> Vec<[[i64; 2]; 4]> {
    let Some(start) = contract.find("Decisions[3][5]") else { return Vec::new() };
    let row = Regex::new(r"\{ \.Flee = \{ (\d+), (\d+) \}, \.Watch = \{ (\d+), (\d+) \}, \.Ignore = \{ (\d+), (\d+) \}, \.Fight = \{ (\d+), (\d+) \} \}").unwrap();
    row.captures_iter(&contract[start..])
        .take(15)
        .map(|c| {
            let n = |i: usize| c[i].parse::<i64>().unwrap_or(-1);
            [[n(1), n(2)], [n(3), n(4)], [n(5), n(6)], [n(7), n(8)]]
        })
        .collect()
}

pub fn check(root: &Path, problems: &mut Problems) -> Vec<String> {
    let data = read_json(root, DATA, problems);
    let contract = read(root, CONTRACT, problems);
    let graph = read_json(root, GRAPH, problems);
    let ini = read(root, GAME_INI, problems);
    let pedestrian = read(root, PEDESTRIAN, problems);
    let subsystem = read(root, SUBSYSTEM, problems);
    if data.is_null() || contract.is_empty() {
        return Vec::new();
    }

    for (key, name, factor) in RULES {
        let want = data["rules"][key].as_f64().map(|v| v * factor);
        let have = constant(&contract, name);
        problems.expect(matches!((want, have), (Some(w), Some(h)) if close(w, h)), format!("rule {key}: data {want:?} vs contract {name} {have:?}"));
    }
    let rule = |key: &str| data["rules"][key].as_f64().unwrap_or(f64::NAN);
    problems.expect(rule("spawn_in_view_min") < rule("spawn_in_view_max") && rule("spawn_out_of_view_min") < rule("spawn_out_of_view_max"), "a spawn band is empty");
    problems.expect(rule("spawn_out_of_view_max") <= rule("keep") && rule("spawn_in_view_max") <= rule("cull"), "people would appear where they are culled at once");
    problems.expect(rule("watch_standoff") > rule("fight_clearance"), "a watcher stands inside the fight's clear zone");
    problems.expect(rule("gather_seconds_min") <= rule("gather_seconds_max"), "gather times reversed");

    // Decision tables: same weights, and every column answers something.
    let rows = decision_rows(&contract);
    problems.expect(rows.len() == 15, format!("contract has {} decision rows, expected 15", rows.len()));
    for (i, row) in rows.iter().enumerate() {
        let (personality, event) = (PERSONALITIES[i / 5], EVENTS[i % 5]);
        let table = &data["decisions"][personality][event];
        for column in 0..2 {
            let total: i64 = row.iter().map(|r| r[column]).sum();
            problems.expect(total > 0, format!("{personality}/{event} column {column} has no answer"));
        }
        for (k, response) in RESPONSES.iter().enumerate() {
            let want: Vec<i64> = table[response].as_array().map(|a| a.iter().filter_map(Value::as_i64).collect()).unwrap_or_else(|| vec![0, 0]);
            problems.expect(want == row[k].to_vec(), format!("{personality}/{event}/{response}: data {want:?} vs contract {:?}", row[k]));
        }
        // Seeing a fight or a run-over never starts one: only being hit or walked into does.
        if matches!(event, "fight" | "run_over" | "body") {
            problems.expect(row[3] == [0, 0], format!("{personality}/{event} would join in"));
        }
    }

    // Looks and groups.
    let look_row = Regex::new(r#"\{ TEXT\("(\w+)"\), TEXT\("(\w+)"\), 0x([0-9a-f]{6}), 0x([0-9a-f]{6}), ([0-9.]+)f \}"#).unwrap();
    let looks: Vec<(String, String, String, String, f64)> =
        look_row.captures_iter(&contract).map(|c| (c[1].into(), c[2].into(), c[3].into(), c[4].into(), c[5].parse().unwrap_or(0.0))).collect();
    let data_looks = data["looks"].as_array().cloned().unwrap_or_default();
    problems.expect(looks.len() == data_looks.len(), format!("looks: data {} vs contract {}", data_looks.len(), looks.len()));
    for (have, want) in looks.iter().zip(&data_looks) {
        let same = want["profile"].as_str() == Some(&have.0)
            && want["archetype"].as_str() == Some(&have.1)
            && want["primary_color"].as_str().map(|c| c.trim_start_matches('#').to_lowercase()) == Some(have.2.clone())
            && want["secondary_color"].as_str().map(|c| c.trim_start_matches('#').to_lowercase()) == Some(have.3.clone())
            && want["scale"].as_f64().is_some_and(|s| close(s, have.4));
        problems.expect(same, format!("look {}: data and contract differ", have.0));
    }
    let group_row = Regex::new(r#"\{ TEXT\("(\w+)"\), TEXT\("([^"]+)"\), \{ ([^}]*) \},\s*([0-9.]+)f, ([0-9.]+)f, \{ (\d+), (\d+), (\d+) \} \}"#).unwrap();
    let quoted = Regex::new(r#"TEXT\("(\w+)"\)"#).unwrap();
    let groups: Vec<_> = group_row.captures_iter(&contract).collect();
    let data_groups = data["groups"].as_array().cloned().unwrap_or_default();
    problems.expect(groups.len() == data_groups.len() && !groups.is_empty(), format!("groups: data {} vs contract {}", data_groups.len(), groups.len()));
    for (have, want) in groups.iter().zip(&data_groups) {
        let id = &have[1];
        let archetypes: Vec<String> = quoted.captures_iter(&have[3]).map(|c| c[1].to_string()).collect();
        let want_archetypes: Vec<String> = want["archetypes"].as_array().map(|a| a.iter().filter_map(|v| v.as_str().map(str::to_string)).collect()).unwrap_or_default();
        problems.expect(want["id"].as_str() == Some(id) && want["name"].as_str() == Some(&have[2]), format!("group {id}: id or name differs"));
        problems.expect(archetypes == want_archetypes, format!("group {id}: archetypes data {want_archetypes:?} vs contract {archetypes:?}"));
        let speed: Vec<f64> = want["walk_speed"].as_array().map(|a| a.iter().filter_map(Value::as_f64).collect()).unwrap_or_default();
        let have_speed = [have[4].parse::<f64>().unwrap_or(0.0), have[5].parse::<f64>().unwrap_or(0.0)];
        problems.expect(speed.len() == 2 && close(speed[0], have_speed[0]) && close(speed[1], have_speed[1]) && speed[0] <= speed[1],
            format!("group {id}: walk speed data {speed:?} vs contract {have_speed:?}"));
        for (k, p) in PERSONALITIES.iter().enumerate() {
            problems.expect(want["personality"][p].as_i64() == have[6 + k].parse().ok(), format!("group {id}: personality {p} differs"));
        }
        for archetype in &archetypes {
            problems.expect(looks.iter().any(|l| &l.1 == archetype), format!("group {id}: no look for archetype {archetype}"));
        }
    }
    let mix: Vec<i64> = Regex::new(r"LentMix\[\] = \{ ([^}]*) \}").unwrap().captures(&contract)
        .map(|c| c[1].split(',').filter_map(|n| n.trim().parse().ok()).collect()).unwrap_or_default();
    let data_mix: Vec<i64> = data_groups.iter().map(|g| data["lent_mix"][g["id"].as_str().unwrap_or("")].as_i64().unwrap_or(-1)).collect();
    problems.expect(mix == data_mix && mix.iter().sum::<i64>() > 0, format!("Lent mix: data {data_mix:?} vs contract {mix:?}"));

    // Out of scope by decision (ADR-041, ADR-044).
    let lowered = format!("{contract}\n{pedestrian}\n{subsystem}").to_lowercase();
    for word in ["wanted", "police", "officer"] {
        problems.expect(!lowered.contains(word), format!("street code names {word}"));
    }

    // The graph the server reads, staged into packaged builds, and mostly one network.
    problems.expect(graph["format"].as_str() == Some("marburgumii.walk_graph"), format!("{GRAPH} is not a walk graph"));
    let nodes = graph["summary"]["nodes"].as_f64().unwrap_or(0.0);
    let largest = graph["summary"]["largest_component_nodes"].as_f64().unwrap_or(0.0);
    problems.expect(nodes >= 100.0 && largest >= 0.75 * nodes, format!("walk graph: {nodes} nodes, largest network {largest}"));
    problems.expect(graph["nodes"].as_array().map_or(0, Vec::len) as f64 == nodes, "walk graph summary does not match its nodes");
    problems.expect(ini.contains("+DirectoriesToAlwaysStageAsUFS=(Path=\"Marburgum/Street\")"), "the walk graph is not staged into packaged builds");

    vec![format!(
        "Street contract OK: {} rules, {} decision tables, {} groups and {} looks match {DATA}; walk graph {} nodes ({} in the largest network), staged.",
        RULES.len(),
        rows.len(),
        groups.len(),
        looks.len(),
        nodes,
        largest
    )]
}
