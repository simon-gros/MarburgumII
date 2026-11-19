//! Street people (ADR-044): the street data and the shared C++ contract carry the same rules, decision tables,
//! groups and looks (the original's ambient population) and the same hours (ADR-067) in every zone (ADR-068); every table answers
//! something; every group can be dressed and walks at some hour; San Andreas' crime and pursuit vocabulary stays out; and the walk graph the server reads is there,
//! staged, mostly one network, and every node of it on a known zone.

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

const PERIODS: [&str; 5] = ["night", "morning", "day", "evening", "late"];

fn int_list(text: &str) -> Vec<f64> {
    text.split(',').filter_map(|n| n.trim().trim_end_matches('f').parse().ok()).collect()
}

/// The rows of one contract table: every `{ n, n, ... }` from `start` up to the table's closing `};`.
fn table_rows(contract: &str, start: &str) -> Vec<Vec<f64>> {
    let Some(from) = contract.find(start) else { return Vec::new() };
    let body = &contract[from + start.len()..];
    let body = &body[..body.find("\n    };").unwrap_or(body.len())];
    Regex::new(r"\{ (-?[0-9.]+f?(?:, -?[0-9.]+f?)+) \}").unwrap().captures_iter(body).map(|c| int_list(&c[1])).collect()
}

/// Who is out when (ADR-067) and where (ADR-068): the periods cover the day in order, and every zone's density and
/// mix match the contract's, in the walk graph's order of zones; every row has somebody in it, the old town is never
/// empty and every group walks somewhere at some hour. Returns the number of zones.
fn check_hours(data: &Value, contract: &str, subsystem: &str, graph: &Value, data_groups: &[Value], problems: &mut Problems) -> usize {
    let hours = &data["hours"];
    let starts = Regex::new(r"PeriodStartHours\[\] = \{ ([^}]*) \}").unwrap().captures(contract).map(|c| int_list(&c[1])).unwrap_or_default();
    let names: Vec<String> = contract
        .find("ZoneNames[] = {")
        .map(|start| {
            let body = &contract[start..];
            let body = &body[..body.find("};").unwrap_or(body.len())];
            Regex::new(r#"TEXT\("(\w+)"\)"#).unwrap().captures_iter(body).map(|c| c[1].to_string()).collect()
        })
        .unwrap_or_default();
    let density = table_rows(contract, "ZoneDensity[][PeriodCount] = {");
    let mix = table_rows(contract, "ZoneMix[][PeriodCount][GroupCount] = {");
    let zones = hours["zones"].as_object().cloned().unwrap_or_default();
    let data_names: Vec<&String> = zones.keys().collect();
    problems.expect(starts.len() == PERIODS.len(), format!("hours: contract has {} period starts", starts.len()));
    problems.expect(!names.is_empty() && density.len() == names.len() && mix.len() == names.len() * PERIODS.len(),
        format!("hours: contract has {} zones, {} density rows, {} mix rows", names.len(), density.len(), mix.len()));
    problems.expect(data_names.iter().map(|n| n.as_str()).eq(names.iter().map(String::as_str)), format!("hours: data zones {data_names:?} vs contract {names:?}"));
    let graph_zones: Vec<&str> = graph["zones"].as_array().map(|a| a.iter().filter_map(Value::as_str).collect()).unwrap_or_default();
    problems.expect(graph_zones.iter().copied().eq(names.iter().map(String::as_str)), format!("hours: walk graph zones {graph_zones:?} vs contract {names:?}"));

    let mut end = 0.0;
    for (i, period) in PERIODS.iter().enumerate() {
        let span: Vec<f64> = hours["periods"][period].as_array().map(|a| a.iter().filter_map(Value::as_f64).collect()).unwrap_or_default();
        problems.expect(span.len() == 2 && close(span[0], end) && span[1] > span[0], format!("hours: period {period} {span:?} does not follow {end}"));
        problems.expect(starts.get(i).is_some_and(|&s| span.first().is_some_and(|&d| close(s, d))), format!("hours: {period} starts at {span:?} in data, {:?} in the contract", starts.get(i)));
        end = span.get(1).copied().unwrap_or(f64::NAN);
    }
    problems.expect(close(end, 24.0), format!("hours: the periods end at {end}, not 24"));

    let mut walks = vec![0.0; data_groups.len()];
    for (z, name) in names.iter().enumerate() {
        let zone = &hours["zones"][name];
        for (i, period) in PERIODS.iter().enumerate() {
            let want = zone["density"][period].as_f64();
            let have = density.get(z).and_then(|r| r.get(i)).copied();
            let floor = if z == 0 { f64::MIN_POSITIVE } else { 0.0 };
            problems.expect(matches!((want, have), (Some(w), Some(h)) if close(w, h) && w >= floor && w <= 1.0),
                format!("hours: {name} {period} density data {want:?} vs contract {have:?}"));
            let want_mix: Vec<f64> = data_groups.iter().map(|g| zone["mix"][period][g["id"].as_str().unwrap_or("")].as_f64().unwrap_or(0.0)).collect();
            let unknown: Vec<&String> = zone["mix"][period].as_object().map(|o| o.keys().filter(|k| !data_groups.iter().any(|g| g["id"].as_str() == Some(k.as_str()))).collect()).unwrap_or_default();
            problems.expect(unknown.is_empty(), format!("hours: {name} {period} mixes groups that do not exist: {unknown:?}"));
            let row = mix.get(z * PERIODS.len() + i);
            problems.expect(row == Some(&want_mix) && want_mix.iter().sum::<f64>() > 0.0, format!("hours: {name} {period} mix data {want_mix:?} vs contract {row:?}"));
            for (k, w) in want_mix.iter().enumerate() {
                walks[k] += w;
            }
        }
    }
    for (g, total) in data_groups.iter().zip(&walks) {
        problems.expect(*total > 0.0, format!("hours: group {} never walks", g["id"]));
    }
    for (key, name) in [("weekend_later_hours", "WeekendLaterHours"), ("rain_thinning", "RainThinning"), ("home_every_seconds", "HomeEverySeconds")] {
        let want = hours[key].as_f64();
        let have = constant(contract, name);
        problems.expect(matches!((want, have), (Some(w), Some(h)) if close(w, h)), format!("hours {key}: data {want:?} vs contract {name} {have:?}"));
    }
    // The server's own two numbers (ADR-068), in its private namespace.
    for (key, name, factor) in [("min_spawn_activity", "MinSpawnActivity", 1.0), ("viewer_zone_search_m", "ViewerZoneSearchCm", 100.0)] {
        let want = hours[key].as_f64().map(|v| v * factor);
        let have = Regex::new(&format!(r"constexpr (?:float|double) {name} = ([0-9.]+)f?;")).unwrap().captures(subsystem).and_then(|c| c[1].parse::<f64>().ok());
        problems.expect(matches!((want, have), (Some(w), Some(h)) if close(w, h)), format!("hours {key}: data {want:?} vs subsystem {name} {have:?}"));
    }
    // Rain thins the street; it never empties it.
    problems.expect(hours["rain_thinning"].as_f64().is_some_and(|r| (0.0..1.0).contains(&r)), "hours: rain would empty the street");
    names.len()
}

/// Every walk node carries a zone the contract knows and an activity in 0..1 (ADR-068). Returns the nodes per zone.
fn check_graph_zones(graph: &Value, zone_count: usize, problems: &mut Problems) -> Vec<usize> {
    let mut by_zone = vec![0usize; zone_count];
    let mut bad = 0usize;
    for node in graph["nodes"].as_array().map(Vec::as_slice).unwrap_or_default() {
        let zone = node.get(4).and_then(Value::as_u64).map(|z| z as usize);
        let activity = node.get(5).and_then(Value::as_f64);
        match (zone, activity) {
            (Some(z), Some(a)) if z < zone_count && (0.0..=1.0).contains(&a) => by_zone[z] += 1,
            _ => bad += 1,
        }
    }
    problems.expect(bad == 0, format!("walk graph: {bad} nodes without a zone and activity (lent-ground street-zones)"));
    problems.expect(by_zone.iter().filter(|&&n| n > 0).count() >= 4, format!("walk graph: nodes by zone {by_zone:?}, fewer than four districts"));
    by_zone
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
    let zone_count = check_hours(&data, &contract, &subsystem, &graph, &data_groups, problems);
    let by_zone = check_graph_zones(&graph, zone_count, problems);

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
        "Street contract OK: {} rules, {} decision tables, {} groups, {} looks and {zone_count} zones' hours match {DATA}; walk graph {} nodes ({} in the largest network, by zone {by_zone:?}), staged.",
        RULES.len(),
        rows.len(),
        groups.len(),
        looks.len(),
        nodes,
        largest
    )]
}
