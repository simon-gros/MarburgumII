//! Gate 1 and Lent gameplay slices: the gameplay catalogue, the shared C++ contract and the map
//! generator commandlet agree (items, areas, interactables, NPCs, missions, condition rules); the
//! authoritative domain logic and store live only in the ServerOnly module; custody, missions, location
//! and condition replicate to their owner only; every refusal reason and effect has a wire name.

use std::collections::BTreeMap;
use std::path::Path;

use regex::Regex;
use serde_json::Value;

use crate::repo::{read, read_json, Problems};

const CONTRACT: &str = "Source/MarburgumIIShared/Public/MarburgumGameplayContract.h";
const MAPS: &str = "Source/MarburgumIIEditorTools/Private/MarburgumCreateMapsCommandlet.cpp";

fn list<'a>(value: &'a Value, key: &str) -> &'a [Value] {
    value[key].as_array().map(Vec::as_slice).unwrap_or(&[])
}

fn text(value: &Value) -> &str {
    value.as_str().unwrap_or("")
}

/// Metres as the commandlet writes them (`-78.0`).
fn metres(value: &Value) -> String {
    format!("{:.1}", value.as_f64().unwrap_or(f64::NAN))
}

/// The initializer of `inline constexpr <type> <name>[] = { ... };` in the contract.
fn block<'a>(contract: &'a str, name: &str, problems: &mut Problems) -> &'a str {
    let pattern = Regex::new(&format!(r"(?s){}\[\] = \{{(.*?)\n    \}};", regex::escape(name))).unwrap();
    match pattern.captures(contract).and_then(|c| c.get(1)) {
        Some(found) => found.as_str(),
        None => {
            problems.0.push(format!("contract has no {name}[] registry"));
            ""
        }
    }
}

/// `{ TEXT("id"), TEXT("Name") }` pairs.
fn pairs(text: &str) -> BTreeMap<String, String> {
    let pattern = Regex::new(r#"\{\s*TEXT\("([a-z0-9_]+)"\),\s*TEXT\("([^"]+)"\)\s*\}"#).unwrap();
    pattern.captures_iter(text).map(|c| (c[1].to_string(), c[2].to_string())).collect()
}

fn source_files(root: &Path) -> Vec<(String, String)> {
    let mut files = Vec::new();
    let mut pending = vec![root.join("Source")];
    while let Some(dir) = pending.pop() {
        let Ok(entries) = std::fs::read_dir(&dir) else { continue };
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_dir() {
                pending.push(path);
            } else if matches!(path.extension().and_then(|e| e.to_str()), Some("h" | "cpp")) {
                let relative = path.strip_prefix(root).unwrap_or(&path).to_string_lossy().replace('\\', "/");
                files.push((relative, std::fs::read_to_string(&path).unwrap_or_default()));
            }
        }
    }
    files.sort();
    files
}

pub fn check(root: &Path, problems: &mut Problems) -> Vec<String> {
    let catalogue = read_json(root, "Data/Gameplay/gameplay.v0.1.json", problems);
    let contract = read(root, CONTRACT, problems);
    let maps = read(root, MAPS, problems);
    if catalogue.is_null() {
        return Vec::new();
    }

    // Items: the C++ registry and the catalogue list the same ids and English names.
    let cpp_items = pairs(block(&contract, "Items", problems));
    let json_items: BTreeMap<String, String> = list(&catalogue, "items").iter()
        .map(|item| (text(&item["id"]).to_string(), text(&item["name"]["en"]).to_string())).collect();
    problems.expect(cpp_items == json_items, format!("item registry mismatch: C++ {cpp_items:?} vs catalogue {json_items:?}"));
    let snake = Regex::new(r"^[a-z0-9_]+$").unwrap();
    for item in list(&catalogue, "items") {
        let id = text(&item["id"]);
        problems.expect(snake.is_match(id), format!("item id not snake_case: {id}"));
        problems.expect(item.get("provenance").is_some(), format!("item without provenance: {id}"));
    }

    // Areas: the C++ registry, the catalogue and the Lent map agree.
    let cpp_areas = pairs(block(&contract, "Areas", problems));
    let json_areas: BTreeMap<String, String> = list(&catalogue["areas"], "entries").iter()
        .map(|area| (text(&area["id"]).to_string(), text(&area["name"]["en"]).to_string())).collect();
    problems.expect(cpp_areas == json_areas, format!("area registry mismatch: C++ {cpp_areas:?} vs catalogue {json_areas:?}"));
    problems.expect(contract.contains(&format!("OutsideAreaName = TEXT(\"{}\")", text(&catalogue["areas"]["outside"]["name"]["en"]))),
        "outside area name differs");
    for area in list(&catalogue["areas"], "entries") {
        let id = text(&area["id"]);
        problems.expect(area.get("provenance").is_some(), format!("area without provenance: {id}"));
        let (c, s) = (list(area, "center_m"), list(area, "size_m"));
        if c.len() != 3 || s.len() != 3 {
            problems.0.push(format!("area {id}: centre/size must have three components"));
            continue;
        }
        problems.expect(maps.contains(&format!("Ue({}, {}, {}), FRotator::ZeroRotator, TEXT(\"Area_{id}\")", metres(&c[0]), metres(&c[1]), metres(&c[2]))),
            format!("area {id}: centre differs from the map commandlet"));
        problems.expect(maps.contains(&format!("SetBoxExtent(FVector({} * 50.0, {} * 50.0, {} * 50.0))", metres(&s[0]), metres(&s[2]), metres(&s[1]))),
            format!("area {id}: size differs from the map commandlet"));
    }

    // Interactables: ids are shared constants, placed by the map commandlet with the catalogue's values.
    let constants = Regex::new(r#"inline constexpr const TCHAR\* (\w+(?:CrateId|AnglerId)) = TEXT\("([a-z0-9_]+)"\);"#).unwrap();
    for found in constants.captures_iter(&contract) {
        problems.expect(list(&catalogue, "interactables").iter().any(|e| e["id"] == found[2]),
            format!("{}={} missing from catalogue", &found[1], &found[2]));
    }
    let missions = list(&catalogue, "missions");
    for entry in list(&catalogue, "interactables") {
        let id = text(&entry["id"]);
        let name = text(&entry["display_name"]["en"]);
        if entry["kind"] == "npc" {
            let p = list(entry, "position_m");
            let placed = p.len() == 3 && maps.contains(&format!("Ue({}, {}, {}), FRotator", metres(&p[0]), metres(&p[1]), metres(&p[2])));
            problems.expect(placed && maps.contains(&format!("InteractableId = FName(TEXT(\"{id}\"))")),
                format!("{id}: not placed at its catalogue position by the map commandlet"));
            problems.expect(maps.contains(&format!("DisplayName = FText::FromString(TEXT(\"{name}\"))")), format!("{id}: display name differs"));
            problems.expect(maps.contains(&format!("InteractionRange = {};", metres(&entry["range_cm"]))), format!("{id}: range differs"));
            problems.expect(maps.contains(text(&entry["dialogue"]["en"])), format!("{id}: dialogue differs"));
            problems.expect(missions.iter().any(|m| m["id"] == entry["offers_mission"]), format!("{id}: offers an unknown mission"));
            problems.expect(entry.get("provenance").is_some(), format!("{id}: npc without provenance"));
            continue;
        }
        problems.expect(entry["kind"] == "test_fixture", format!("{id}: item sources are labeled test fixtures so far"));
        let granted = text(&entry["grants"]["item"]);
        problems.expect(json_items.contains_key(granted), format!("{id}: grants unknown item {granted}"));
        let crate_call = Regex::new(&format!(r#"Crate\(World, TEXT\("\w+"\), TEXT\("{}"\), TEXT\("([^"]+)"\), (\d+),"#, regex::escape(id))).unwrap();
        match crate_call.captures(&maps) {
            Some(placed) => {
                problems.expect(&placed[1] == name, format!("{id}: display name differs from the map commandlet"));
                problems.expect(placed[2].parse::<i64>().ok() == entry["initial_stock"].as_i64(),
                    format!("{id}: initial stock differs from the map commandlet"));
            }
            None => problems.0.push(format!("{id}: not placed by the map commandlet")),
        }
    }
    problems.expect(maps.contains("GrantedItemId = FName(TEXT(\"fishing_rod\"))"), "map commandlet grants an unexpected item");
    problems.expect(maps.contains("bTestFixture = true;"), "map fixtures are not marked as test fixtures");
    let schema = catalogue["world_store"]["schema_version"].to_string();
    problems.expect(contract.contains(&format!("WorldStoreSchemaVersion = {schema};")),
        "world store schema version differs between catalogue and contract");

    // Condition: the contract's speeds and breath rules are the catalogue's (original combat.h/protocol.h).
    let condition = &catalogue["condition"];
    problems.expect(condition.get("provenance").is_some(), "condition without provenance");
    let number = |key: &str| condition[key].as_f64().unwrap_or(f64::NAN);
    let constant = |name: &str| -> f64 {
        let pattern = Regex::new(&format!(r"inline constexpr float {name} = ([0-9.]+)f;")).unwrap();
        pattern.captures(&contract).and_then(|c| c[1].parse().ok()).unwrap_or(f64::NAN)
    };
    problems.expect((constant("WalkSpeedCm") - number("walk_speed_mps") * 100.0).abs() < 1e-6, "walk speed differs from the catalogue");
    problems.expect(contract.contains("RunSpeedCm = 2.f * WalkSpeedCm;") && (number("run_speed_mps") - 2.0 * number("walk_speed_mps")).abs() < 1e-6,
        "run speed is not twice the walk speed");
    for (name, key) in [("DefaultVitality", "default_vitality"), ("DefaultStamina", "default_stamina"),
        ("RunStaminaDrainPerSecond", "run_stamina_drain_per_second"), ("StaminaRecoveryPerSecond", "stamina_recovery_per_second"),
        ("RunResumeStamina", "run_resume_stamina")] {
        problems.expect(constant(name) == number(key), format!("{name} differs from the catalogue {key}"));
    }

    // Missions: registry entries match the catalogue's names, offer targets, start items and registered stages.
    for mission in missions {
        let id = text(&mission["id"]);
        problems.expect(contract.contains(&format!("{{ TEXT(\"{id}\"), TEXT(\"{}\"), TEXT(\"{}\"), TEXT(\"{}\"),",
            text(&mission["name"]["en"]), text(&mission["category"]), text(&mission["offer_target"]))),
            format!("mission {id}: name/category/offer target differ from the contract"));
        problems.expect(contract.contains(text(&mission["summary"]["en"])), format!("mission {id}: summary differs"));
        for start in list(mission, "start_items") {
            let item = text(&start["id"]);
            problems.expect(json_items.contains_key(item), format!("mission {id}: unknown start item {item}"));
            problems.expect(contract.contains(&format!("TEXT(\"{item}\"), {}, ", start["quantity"])), format!("mission {id}: start item differs"));
        }
        for stage in list(mission, "registered_stages") {
            let stage_id = text(&stage["id"]);
            problems.expect(contract.contains(&format!("TEXT(\"{stage_id}\"),")) && contract.contains(text(&stage["objective_text"]["en"])),
                format!("mission {id}: stage {stage_id} differs"));
            problems.expect(contract.contains(&format!("TEXT(\"{}\"), TEXT(\"{}\")", text(&stage["complete_target"]), text(&stage["action"]["en"]))),
                format!("mission {id}: stage {stage_id} target/action differ"));
            for item in list(&stage["objective"], "items") {
                problems.expect(json_items.contains_key(text(item)), format!("mission {id}: stage {stage_id} needs unregistered item {}", text(item)));
            }
        }
        problems.expect(mission.get("provenance").is_some(), format!("mission {id} without provenance"));
    }

    // Authority boundary: the reducers and store are ServerOnly; nothing else includes them.
    let sources = source_files(root);
    for name in ["MarburgumWorldAuthority", "MarburgumWorldStore", "MarburgumMissionRules"] {
        let owners: Vec<&String> = sources.iter().map(|(path, _)| path)
            .filter(|path| Path::new(path).file_stem().and_then(|s| s.to_str()) == Some(name)).collect();
        problems.expect(!owners.is_empty() && owners.iter().all(|p| p.starts_with("Source/MarburgumIIServer/")),
            format!("{name} outside the server module: {owners:?}"));
        for (path, body) in &sources {
            problems.expect(path.starts_with("Source/MarburgumIIServer/") || !body.contains(&format!("\"{name}.h\"")),
                format!("{path} includes {name}.h"));
        }
    }
    let service = read(root, "Source/MarburgumII/Public/MarburgumAuthorityService.h", problems);
    problems.expect(service.contains("UCLASS(Abstract)"), "core authority service must stay abstract");

    // Privacy: custody, mission progress and location replicate to their owner only.
    let player_state = read(root, "Source/MarburgumII/Private/MarburgumPlayerState.cpp", problems);
    for property in ["Inventory", "bInventoryKnown", "bHasCharacter", "Missions", "bMissionsKnown", "AreaId", "bLocationKnown", "Condition"] {
        problems.expect(player_state.contains(&format!("DOREPLIFETIME_CONDITION(AMarburgumPlayerState, {property}, COND_OwnerOnly)")),
            format!("{property} is not replicated COND_OwnerOnly"));
    }

    // Every refusal reason and effect has a stable wire/log name.
    let types = read(root, "Source/MarburgumII/Public/MarburgumInteractionTypes.h", problems);
    let names = read(root, "Source/MarburgumII/Private/MarburgumAuthorityService.cpp", problems);
    let mut reasons = 0;
    for enumeration in ["EMarburgumInteractionReason", "EMarburgumInteractionEffect"] {
        let body = Regex::new(&format!(r"(?s)enum class {enumeration} : uint8\s*\{{(.*?)\}};", )).unwrap()
            .captures(&types).map(|c| c[1].to_string()).unwrap_or_default();
        problems.expect(!body.is_empty(), format!("{enumeration} not found"));
        for value in Regex::new(r"(?m)^\s*(\w+),?").unwrap().captures_iter(&body) {
            problems.expect(names.contains(&format!("{enumeration}::{}:", &value[1])), format!("{enumeration}::{} has no ToString case", &value[1]));
            if enumeration == "EMarburgumInteractionReason" {
                reasons += 1;
            }
        }
    }

    // Excluded systems never enter the gameplay slice.
    for path in ["Data/Gameplay/gameplay.v0.1.json", CONTRACT, MAPS] {
        let body = read(root, path, problems).to_lowercase();
        for banned in ["talent", "wanted", "pursuit"] {
            problems.expect(!body.contains(banned), format!("{path} mentions excluded system '{banned}'"));
        }
    }

    vec![format!(
        "Gameplay contract check passed: {} item(s), {} area(s), {} interactable(s), {} mission(s), {reasons} refusal reasons, \
         server-only authority and owner-only custody, progress, location and condition.",
        json_items.len(), json_areas.len(), list(&catalogue, "interactables").len(), missions.len())]
}
