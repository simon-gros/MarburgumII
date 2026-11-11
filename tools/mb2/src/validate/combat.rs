//! Combat (ADR-041): the combat data and the shared C++ contract carry the same rules and the same people
//! (the original's combat.h and npcs.json); a hostile can fight and a civilian cannot; every stagger is shorter
//! than the cooldown of the swing that causes it; the wanted level the Unreal port took from San Andreas stays
//! out; and the player's guard and blows reach the server only in the movement component's move stream.

use regex::Regex;
use serde_json::Value;
use std::path::Path;

use crate::repo::{read, read_json, Problems};

const DATA: &str = "Data/Gameplay/combat.v0.1.json";
const CONTRACT: &str = "Source/MarburgumIIShared/Public/MarburgumCombatContract.h";
const MOVEMENT: &str = "Source/MarburgumII/Private/MarburgumCharacterMovement.cpp";
const UI_ONTOLOGY: &str = "Data/Ontology/ui_hud.v0.1.json";

/// Data rule → contract constant, and the factor from the data's unit to the contract's.
const RULES: [(&str, &str, f64); 17] = [
    ("fist_damage", "FistDamage", 1.0),
    ("heavy_multiplier", "HeavyMultiplier", 1.0),
    ("guard_damage_share", "GuardDamageShare", 1.0),
    ("guard_stamina_cost", "GuardStaminaCost", 1.0),
    ("heavy_guard_stamina_cost", "HeavyGuardStaminaCost", 1.0),
    ("fist_reach", "FistReachCm", 100.0),
    ("fist_arc_degrees", "FistArcDegrees", 1.0),
    ("fist_stamina_cost", "FistStaminaCost", 1.0),
    ("heavy_stamina_cost", "HeavyStaminaCost", 1.0),
    ("guard_drain_per_second", "GuardDrainPerSecond", 1.0),
    ("fist_cooldown_msec", "FistCooldownSeconds", 0.001),
    ("heavy_cooldown_msec", "HeavyCooldownSeconds", 0.001),
    ("stagger_msec", "StaggerSeconds", 0.001),
    ("heavy_stagger_msec", "HeavyStaggerSeconds", 0.001),
    ("guard_expiry_msec", "GuardExpirySeconds", 0.001),
    ("swing_msec", "SwingSeconds", 0.001),
    ("heavy_swing_msec", "HeavySwingSeconds", 0.001),
];

/// NPC behaviour rules, same shape.
const NPC_RULES: [(&str, &str, f64); 9] = [
    ("tick_rate", "TickRate", 1.0),
    ("npc_company_range", "CompanyRangeCm", 100.0),
    ("npc_watch_range", "WatchRangeCm", 100.0),
    ("npc_wander_radius", "WanderRadiusCm", 100.0),
    ("npc_avoid_range", "AvoidRangeCm", 100.0),
    ("npc_avoid_home", "AvoidHomeCm", 100.0),
    ("npc_home_arrive", "HomeArriveCm", 100.0),
    ("npc_wander_ticks", "WanderTicks", 1.0),
    ("npc_pause_ticks", "PauseTicks", 1.0),
];

fn constant(contract: &str, name: &str) -> Option<f64> {
    let pattern = Regex::new(&format!(r"inline constexpr (?:float|int32|double) {name} = ([0-9.]+)f?;")).unwrap();
    pattern.captures(contract).and_then(|c| c[1].parse().ok())
}

fn close(a: f64, b: f64) -> bool {
    (a - b).abs() < 1e-6 * a.abs().max(1.0)
}

/// `{ .Id = TEXT("..."), ... }` rows of the contract's Npcs[] registry, as raw text.
fn npc_rows(contract: &str, problems: &mut Problems) -> Vec<String> {
    let Some(start) = contract.find("Npcs[] = {") else {
        problems.0.push("contract has no Npcs[] registry".into());
        return Vec::new();
    };
    let body = &contract[start..];
    let end = body.find("\n    };").unwrap_or(body.len());
    body[..end].split("\n        {\n").skip(1).map(str::to_string).collect()
}

fn field<'a>(row: &'a str, name: &str) -> &'a str {
    let key = format!(".{name} = ");
    row.find(&key)
        .map(|at| {
            let rest = &row[at + key.len()..];
            let end = if rest.starts_with('{') { rest.find('}').map_or(rest.len(), |e| e + 1) } else { rest.find(",\n").unwrap_or(rest.len()) };
            rest[..end].trim()
        })
        .unwrap_or("")
}

fn quoted(text: &str) -> String {
    Regex::new(r#"TEXT\("([^"]*)"\)"#).unwrap().captures(text).map(|c| c[1].to_string()).unwrap_or_default()
}

/// A number, or a named default from the contract.
fn number(contract: &str, text: &str) -> Option<f64> {
    let text = text.trim_end_matches('f');
    text.parse().ok().or_else(|| constant(contract, text))
}

fn numbers(text: &str) -> Vec<f64> {
    text.trim_matches(|c| c == '{' || c == '}' || char::is_whitespace(c)).split(',').filter_map(|n| n.trim().parse().ok()).collect()
}

pub fn check(root: &Path, problems: &mut Problems) -> Vec<String> {
    let data = read_json(root, DATA, problems);
    let contract = read(root, CONTRACT, problems);
    let movement = read(root, MOVEMENT, problems);
    let ontology = read_json(root, UI_ONTOLOGY, problems);
    if data.is_null() || contract.is_empty() {
        return Vec::new();
    }

    for (rules, table) in [(&data["rules"], &RULES[..]), (&data["rules"], &NPC_RULES[..])] {
        for (key, name, factor) in table {
            let want = rules[key].as_f64().map(|v| v * factor);
            let have = constant(&contract, name);
            problems.expect(matches!((want, have), (Some(w), Some(h)) if close(w, h)), format!("rule {key}: data {want:?} vs contract {name} {have:?}"));
        }
    }
    let rule = |key: &str| data["rules"][key].as_f64().unwrap_or(f64::NAN);
    problems.expect(rule("stagger_msec") < rule("fist_cooldown_msec"), "a jab's stagger outlasts its cooldown (stun-lock)");
    problems.expect(rule("heavy_stagger_msec") < rule("heavy_cooldown_msec"), "a heavy swing's stagger outlasts its cooldown (stun-lock)");

    let defaults = &data["npc_defaults"];
    for (key, name, factor) in [("damage", "DefaultNpcDamage", 1.0), ("reach", "DefaultNpcReachCm", 100.0),
        ("attack_interval_msec", "DefaultAttackIntervalSeconds", 0.001), ("pursuit_range", "DefaultPursuitRangeCm", 100.0)]
    {
        let want = defaults[key].as_f64().map(|v| v * factor);
        let have = constant(&contract, name);
        problems.expect(matches!((want, have), (Some(w), Some(h)) if close(w, h)), format!("npc default {key}: data {want:?} vs contract {name} {have:?}"));
    }

    let json: Vec<&Value> = data["npcs"].as_array().map(|a| a.iter().collect()).unwrap_or_default();
    let cpp = npc_rows(&contract, problems);
    let json_ids: Vec<&str> = json.iter().map(|n| n["id"].as_str().unwrap_or("")).collect();
    let cpp_ids: Vec<String> = cpp.iter().map(|r| quoted(field(r, "Id"))).collect();
    problems.expect(json_ids == cpp_ids, format!("NPC order mismatch: data {json_ids:?} vs contract {cpp_ids:?}"));
    let mut hostiles = 0;
    for (npc, row) in json.iter().zip(&cpp) {
        let id = npc["id"].as_str().unwrap_or("?");
        problems.expect(npc["name"].as_str() == Some(quoted(field(row, "Name")).as_str()), format!("{id}: name differs"));
        let role = npc["role"].as_str().unwrap_or("");
        let cpp_role = match field(row, "Role") { "RoleHostile" => "hostile", "RoleCivilian" => "civilian", other => other };
        problems.expect(role == cpp_role, format!("{id}: role data {role} vs contract {cpp_role}"));
        problems.expect(role == "hostile" || role == "civilian", format!("{id}: unknown role {role}"));
        hostiles += usize::from(role == "hostile");
        let position: Vec<f64> = npc["position"].as_array().map(|a| a.iter().filter_map(Value::as_f64).collect()).unwrap_or_default();
        let cpp_position = numbers(field(row, "GodotPosition"));
        problems.expect(position.len() == 3 && position.iter().zip(&cpp_position).all(|(a, b)| close(*a, *b)) && cpp_position.len() == 3,
            format!("{id}: position data {position:?} vs contract {cpp_position:?}"));
        for (key, name, factor, default) in [
            ("facing", "GodotFacing", 1.0, None),
            ("vitality", "Vitality", 1.0, None),
            ("stamina", "Stamina", 1.0, None),
            ("scale", "Scale", 1.0, None),
            ("damage", "Damage", 1.0, Some("damage")),
            ("reach", "ReachCm", 100.0, Some("reach")),
            ("attack_interval_msec", "AttackIntervalSeconds", 0.001, Some("attack_interval_msec")),
            ("pursuit_range", "PursuitRangeCm", 100.0, Some("pursuit_range")),
        ] {
            let want = npc[key].as_f64().or_else(|| default.and_then(|d| defaults[d].as_f64())).map(|v| v * factor);
            let have = number(&contract, field(row, name));
            problems.expect(matches!((want, have), (Some(w), Some(h)) if close(w, h)), format!("{id}: {key} data {want:?} vs contract {name} {have:?}"));
        }
        for (key, name) in [("primary_color", "PrimaryColor"), ("secondary_color", "SecondaryColor")] {
            let want = npc[key].as_str().map(|c| c.trim_start_matches('#').to_lowercase());
            let have = field(row, name).trim_start_matches("0x").to_lowercase();
            problems.expect(want.as_deref() == Some(have.as_str()), format!("{id}: {key} data {want:?} vs contract {have}"));
        }
    }
    problems.expect(hostiles >= 1, "no hostile NPC: nobody can fight back");
    problems.expect(json_ids.contains(&"lent_drunk"), "the Lent drinker is missing");

    // Nothing of San Andreas' wanted system: the ontology excludes it, and the contract never names it.
    let excluded: Vec<&str> = ontology["excluded_features"].as_array().map(|a| a.iter().filter_map(|e| e["mechanic"].as_str()).collect()).unwrap_or_default();
    for mechanic in ["wanted_stars", "wanted_level_or_heat", "police_pursuit_or_chase"] {
        problems.expect(excluded.contains(&mechanic), format!("ui ontology no longer excludes {mechanic}"));
    }
    let lowered = contract.to_lowercase();
    problems.expect(!lowered.contains("wanted") && !lowered.contains("police"), "the combat contract names a wanted level or police");

    // The player's guard and swings travel in the move stream, simulated the same way on both ends: the guard as
    // a held flag, swings as a request count serialized in every move (a one-move flag can be skipped).
    problems.expect(movement.contains("FLAG_Custom_1"), "movement does not carry the guard flag (FLAG_Custom_1)");
    problems.expect(movement.contains("Ar << SwingRequest;"), "movement does not serialize the swing request count in its move data");
    problems.expect(!movement.contains("FLAG_Custom_2") && !movement.contains("FLAG_Custom_3"), "swings must not ride a one-move flag");

    vec![format!(
        "Combat contract OK: {} rules and {} people ({} hostile) match {DATA}; wanted level and police excluded.",
        RULES.len() + NPC_RULES.len(),
        cpp.len(),
        hostiles
    )]
}
