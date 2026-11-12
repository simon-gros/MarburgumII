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

    // The generated catalogue header is exactly what the JSON produces (ADR-049).
    crate::generate::check(root, &catalogue, problems);

    // Items: snake_case ids with provenance.
    let json_items: BTreeMap<String, String> = list(&catalogue, "items").iter()
        .map(|item| (text(&item["id"]).to_string(), text(&item["name"]["en"]).to_string())).collect();
    let snake = Regex::new(r"^[a-z0-9_]+$").unwrap();
    for item in list(&catalogue, "items") {
        let id = text(&item["id"]);
        problems.expect(snake.is_match(id), format!("item id not snake_case: {id}"));
        problems.expect(item.get("provenance").is_some(), format!("item without provenance: {id}"));
    }
    let skills: Vec<&str> = list(&catalogue["skills"], "entries").iter().map(|s| text(&s["id"])).collect();
    problems.expect(skills.contains(&"fishing"), "the Fishing skill is not registered");
    for spot in list(&catalogue["fishing"], "spots") {
        let id = text(&spot["id"]);
        problems.expect(list(&catalogue, "interactables").iter().any(|e| e["id"] == spot["id"] && e["kind"] == "fishing_spot"),
            format!("fishing spot {id} has no interactable"));
        for species in list(spot, "species") {
            problems.expect(json_items.contains_key(text(&species["item"])), format!("fishing spot {id}: unknown fish {}", text(&species["item"])));
            problems.expect(species["weight"].as_i64().unwrap_or(0) > 0 && species["xp"].as_i64().unwrap_or(0) > 0,
                format!("fishing spot {id}: weight and XP must be positive"));
        }
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
    let ids: Vec<&str> = list(&catalogue, "interactables").iter().map(|e| text(&e["id"])).collect();
    for entry in list(&catalogue, "interactables") {
        let id = text(&entry["id"]);
        let name = text(&entry["display_name"]["en"]);
        if entry["kind"] == "test_fixture" {
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
            continue;
        }
        problems.expect(entry.get("provenance").is_some(), format!("{id}: interactable without provenance"));
        if entry["placement"] == "map" {
            // The map commandlet places these (the angler); its values must be the catalogue's.
            problems.expect(maps.contains(&format!("InteractableId = FName(TEXT(\"{id}\"))")), format!("{id}: not placed by the map commandlet"));
            problems.expect(maps.contains(&format!("DisplayName = FText::FromString(TEXT(\"{name}\"))")), format!("{id}: display name differs"));
            problems.expect(maps.contains(&format!("InteractionRange = {};", metres(&entry["range_cm"]))), format!("{id}: range differs"));
            problems.expect(maps.contains(text(&entry["dialogue"]["en"])), format!("{id}: dialogue differs"));
        } else {
            problems.expect(list(entry, "godot_position_m").len() == 3, format!("{id}: needs an original position"));
        }
    }
    // Every mission's offer and stage targets exist; every item it names is registered.
    let mission_ids: Vec<&str> = missions.iter().map(|m| text(&m["id"])).collect();
    for mission in missions {
        let id = text(&mission["id"]);
        problems.expect(ids.contains(&text(&mission["offer_target"])), format!("mission {id}: unknown offer target"));
        for required in list(mission, "requires_completed") {
            problems.expect(mission_ids.contains(&text(required)), format!("mission {id}: requires unknown mission {}", text(required)));
        }
        let stages = list(mission, "stages");
        problems.expect(!stages.is_empty(), format!("mission {id}: no stages"));
        for stage in stages {
            let stage_id = text(&stage["id"]);
            problems.expect(ids.contains(&text(&stage["complete_target"])), format!("mission {id}: stage {stage_id} has an unknown target"));
            let mut named: Vec<&str> = list(&stage["objective"], "items").iter().map(text).collect();
            for key in ["required_items", "grant_items"] {
                named.extend(list(stage, key).iter().map(|q| text(&q["id"])));
            }
            for item in named {
                problems.expect(json_items.contains_key(item), format!("mission {id}: stage {stage_id} names unregistered item {item}"));
            }
            let choice = stage["objective"]["type"] == "choice";
            problems.expect(choice == !list(stage, "choices").is_empty(), format!("mission {id}: stage {stage_id}: choices and objective disagree"));
            for option in list(stage, "choices") {
                for skill in option["skill_xp"].as_object().map(|o| o.keys().cloned().collect::<Vec<_>>()).unwrap_or_default() {
                    problems.expect(skills.contains(&skill.as_str()), format!("mission {id}: choice awards unknown skill {skill}"));
                }
            }
        }
        problems.expect(stages.last().map_or(false, |s| s["final"] == true) || stages.iter().all(|s| s["final"] != true),
            format!("mission {id}: only the last stage may be final"));
        for start in list(mission, "start_items") {
            problems.expect(json_items.contains_key(text(&start["id"])), format!("mission {id}: unknown start item {}", text(&start["id"])));
        }
        problems.expect(mission.get("provenance").is_some(), format!("mission {id} without provenance"));
    }
    // Shops (ADR-060): a placed keeper, goods only, buy-back below the price, sane hours, limits and XP.
    let shops = list(&catalogue, "shops");
    let goods: Vec<&str> = list(&catalogue, "items").iter().filter(|i| i["kind"] == "goods").map(|i| text(&i["id"])).collect();
    for shop in shops {
        let id = text(&shop["id"]);
        problems.expect(snake.is_match(id), format!("shop id not snake_case: {id}"));
        problems.expect(shop.get("provenance").is_some(), format!("shop {id} without provenance"));
        let npc = text(&shop["npc"]);
        problems.expect(list(&catalogue, "interactables").iter().any(|e| e["id"] == npc && e["kind"] == "npc"),
            format!("shop {id}: keeper {npc} is not an NPC"));
        problems.expect(shops.iter().filter(|s| s["npc"] == shop["npc"]).count() == 1, format!("shop {id}: {npc} keeps more than one counter"));
        problems.expect(shop["hours"]["timezone"] == "Europe/Ljubljana", format!("shop {id}: hours must be Europe/Ljubljana"));
        problems.expect(shop["max_purchase_quantity"].as_i64().map_or(false, |q| q >= 1), format!("shop {id}: max_purchase_quantity must be positive"));
        problems.expect(shop["intro_trading_xp"].as_i64().map_or(false, |x| x >= 0), format!("shop {id}: intro_trading_xp must not be negative"));
        problems.expect(skills.contains(&"trading"), "shops train Trading, which is not registered");
        let offers = list(shop, "offers");
        problems.expect(!offers.is_empty(), format!("shop {id}: no offers"));
        for offer in offers {
            let item = text(&offer["item"]);
            problems.expect(goods.contains(&item), format!("shop {id}: {item} is not registered goods"));
            let (buy, sell, stock) = (offer["buy_price"].as_i64().unwrap_or(-1), offer["sell_price"].as_i64().unwrap_or(-1),
                offer["stock"].as_i64().unwrap_or(-1));
            problems.expect(buy > 0 && stock >= 0, format!("shop {id}: {item} needs a positive price and a stock"));
            problems.expect(sell < buy, format!("shop {id}: {item} would be bought back at or above its price"));
            problems.expect(offers.iter().filter(|o| o["item"] == offer["item"]).count() == 1, format!("shop {id}: {item} offered twice"));
        }
    }
    for item in &goods {
        problems.expect(shops.iter().any(|s| list(s, "offers").iter().any(|o| text(&o["item"]) == *item)),
            format!("goods {item} are on no counter"));
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
        ("RunResumeStamina", "run_resume_stamina"), ("BreathHoldSeconds", "breath_hold_seconds"), ("RecoverySeconds", "recovery_seconds")] {
        problems.expect(constant(name) == number(key), format!("{name} differs from the catalogue {key}"));
    }
    // Swimming and drowning (ADR-032): the contract keeps centimetres, the catalogue metres.
    for (name, key) in [("SwimSpeedCm", "swim_speed_mps"), ("MouthHeightCm", "mouth_height_m"), ("LethalWaterImpactCm", "lethal_water_impact_m")] {
        problems.expect((constant(name) - number(key) * 100.0).abs() < 1e-3, format!("{name} differs from the catalogue {key}"));
    }
    problems.expect(contract.contains("BreathDrainPerSecond = DefaultStamina / BreathHoldSeconds;"),
        "held breath does not drain the whole bar in BreathHoldSeconds");

    // Authority boundary: the reducers and store are ServerOnly; nothing else includes them.
    let sources = source_files(root);
    for name in ["MarburgumWorldAuthority", "MarburgumWorldStore", "MarburgumMissionRules", "MarburgumFishingRules", "MarburgumRetailRules"] {
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
    for property in ["Inventory", "bInventoryKnown", "bHasCharacter", "Missions", "bMissionsKnown", "AreaId", "bLocationKnown", "Condition",
        "bProgressKnown", "Skills", "Money", "Fishing"] {
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
    for path in ["Data/Gameplay/gameplay.v0.1.json", CONTRACT, MAPS, crate::generate::HEADER] {
        let body = read(root, path, problems).to_lowercase();
        for banned in ["talent", "wanted", "pursuit"] {
            problems.expect(!body.contains(banned), format!("{path} mentions excluded system '{banned}'"));
        }
    }

    vec![format!(
        "Gameplay contract check passed: {} item(s), {} skill(s), {} fishing spot(s), {} area(s), {} interactable(s), {} mission(s), \
         {} shop(s), {reasons} refusal reasons, generated catalogue current, server-only authority and owner-only custody, progress, location, \
         condition, skills, money and fishing.",
        json_items.len(), skills.len(), list(&catalogue["fishing"], "spots").len(), json_areas.len(), list(&catalogue, "interactables").len(),
        missions.len(), shops.len())]
}
