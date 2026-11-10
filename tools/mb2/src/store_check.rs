//! World-store checks the smoke-test scripts run after their first server session. Each prints a
//! one-line summary on stdout for the script to read, and its problems on stderr.

use serde_json::Value;

use crate::repo::Problems;

fn load(path: &str) -> Result<Value, String> {
    let text = std::fs::read_to_string(path).map_err(|error| format!("cannot read {path}: {error}"))?;
    serde_json::from_str(&text).map_err(|error| format!("invalid JSON in {path}: {error}"))
}

fn finish(problems: Problems, summary: String) -> u8 {
    println!("{summary}");
    if problems.is_empty() {
        0
    } else {
        eprintln!("store check FAILED: {}", problems.0.join(","));
        1
    }
}

fn is_store(doc: &Value) -> bool {
    doc["format"] == "marburgumii.world_store" && doc["schema_version"] == 3
}

/// No character's condition was saved (the Gate 1 and Lent smoke clients never run).
fn no_conditions(doc: &Value) -> bool {
    doc["conditions"].as_object().is_some_and(serde_json::Map::is_empty)
}

/// Gate 1 (scripts/interaction-smoke-linux.sh): three grants in total and nothing else.
/// Prints "<rods of A> <rods of B> <revision>".
pub fn interaction(store: &str, key_a: &str, key_b: &str, first_op: &str) -> u8 {
    let doc = match load(store) {
        Ok(doc) => doc,
        Err(error) => return finish(Problems(vec![error]), "0 0 none".to_string()),
    };
    check_interaction(&doc, key_a, key_b, first_op)
}

fn check_interaction(doc: &Value, key_a: &str, key_b: &str, first_op: &str) -> u8 {
    let mut problems = Problems::default();
    let rods = |key: &str| doc["inventories"][key]["fishing_rod"].as_i64().unwrap_or(0);
    let (a, b) = (rods(key_a), rods(key_b));
    problems.expect(is_store(doc), "format");
    problems.expect(no_conditions(doc), format!("conditions={}", doc["conditions"]));
    problems.expect(doc["revision"] == 3, format!("revision={}", doc["revision"]));
    problems.expect(doc["stocks"]["dev_supply_crate"] == 0, format!("stock={}", doc["stocks"]["dev_supply_crate"]));
    problems.expect(doc["stocks"].get("dev_supply_crate_far").is_none(), "far_stock_written");
    problems.expect(a + b == 3, format!("granted={}", a + b));
    let receipts = doc["receipts"].as_object().map(|r| r.len()).unwrap_or(0);
    problems.expect(receipts == 3, format!("receipts={receipts}"));
    problems.expect(doc["receipts"][first_op]["character"] == key_a, "first_op_receipt");
    finish(problems, format!("{a} {b} {}", doc["revision"]))
}

/// Lent slice (scripts/lent-smoke-linux.sh): exactly one commit, M's mission start with its rod.
/// Prints "<revision> <stage>".
pub fn lent(store: &str, key_m: &str, key_w: &str, first_op: &str) -> u8 {
    let doc = match load(store) {
        Ok(doc) => doc,
        Err(error) => return finish(Problems(vec![error]), "none -".to_string()),
    };
    check_lent(&doc, key_m, key_w, first_op)
}

fn check_lent(doc: &Value, key_m: &str, key_w: &str, first_op: &str) -> u8 {
    let mut problems = Problems::default();
    let mission = &doc["missions"][key_m]["lent_first_catch"];
    let stage = mission["stage"].as_str().unwrap_or("-");
    problems.expect(is_store(doc), "format");
    problems.expect(no_conditions(doc), format!("conditions={}", doc["conditions"]));
    problems.expect(doc["revision"] == 1, format!("revision={}", doc["revision"]));
    problems.expect(stage == "catch_first_fish" && mission["started_revision"] == 1, format!("mission={mission}"));
    problems.expect(doc["inventories"][key_m]["fishing_rod"] == 1, format!("rods={}", doc["inventories"][key_m]));
    problems.expect(doc["inventories"].get(key_w).is_none() && doc["missions"].get(key_w).is_none(), "walker_written");
    let receipt = &doc["receipts"][first_op];
    let receipts = doc["receipts"].as_object().map(|r| r.len()).unwrap_or(0);
    problems.expect(receipts == 1 && receipt["kind"] == "mission_started" && receipt["character"] == key_m
        && receipt["item"] == "fishing_rod" && receipt["mission"] == "lent_first_catch", format!("receipts={}", doc["receipts"]));
    finish(problems, format!("{} {stage}", doc["revision"]))
}

/// Player condition (scripts/condition-smoke-linux.sh). Phase 1, after the runner R left winded and the
/// cheater C left: two condition commits and nothing else. Phase 2, after R rested to full (its entry is
/// removed) and S was saved by the server stop: four commits. Prints "<revision> <R stamina|-> <R winded>".
pub fn condition(store: &str, key_r: &str, key_c: &str, key_s: &str, phase: &str) -> u8 {
    let doc = match load(store) {
        Ok(doc) => doc,
        Err(error) => return finish(Problems(vec![error]), "none - -".to_string()),
    };
    check_condition(&doc, key_r, key_c, key_s, phase)
}

fn check_condition(doc: &Value, key_r: &str, key_c: &str, key_s: &str, phase: &str) -> u8 {
    let mut problems = Problems::default();
    let conditions = &doc["conditions"];
    let saved = |key: &str| conditions.get(key).filter(|entry| entry.is_object());
    let tired = |key: &str| saved(key).is_some_and(|entry| entry["vitality"] == 100 && entry["stamina"].as_i64().is_some_and(|s| (0..100).contains(&s)));
    problems.expect(is_store(doc), "format");
    let empty = |field: &str| doc[field].as_object().is_some_and(serde_json::Map::is_empty);
    problems.expect(empty("inventories") && empty("missions") && empty("receipts"), "custody_written");
    problems.expect(conditions.as_object().map(serde_json::Map::len) == Some(2), format!("conditions={conditions}"));
    problems.expect(tired(key_c), format!("cheater={}", conditions[key_c]));
    match phase {
        "1" => {
            problems.expect(doc["revision"] == 2, format!("revision={}", doc["revision"]));
            // R left the moment it was winded: almost no breath, and winded.
            let runner = &conditions[key_r];
            problems.expect(tired(key_r) && runner["winded"] == true && runner["stamina"].as_i64().is_some_and(|s| s <= 5),
                format!("runner={runner}"));
            problems.expect(saved(key_s).is_none(), "stopper_written_early");
        }
        "2" => {
            problems.expect(doc["revision"] == 4, format!("revision={}", doc["revision"]));
            // A rested body is stored as no entry at all.
            problems.expect(saved(key_r).is_none(), format!("runner_not_rested={}", conditions[key_r]));
            problems.expect(tired(key_s), format!("stopper={}", conditions[key_s]));
        }
        _ => problems.0.push(format!("unknown phase {phase}")),
    }
    let runner = &conditions[key_r];
    finish(problems, format!("{} {} {}", doc["revision"],
        runner["stamina"].as_i64().map_or("-".to_string(), |s| s.to_string()),
        if runner["winded"] == true { 1 } else { 0 }))
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn lent_store() -> Value {
        json!({
            "format": "marburgumii.world_store", "schema_version": 3, "revision": 1, "stocks": {}, "conditions": {},
            "inventories": { "m": { "fishing_rod": 1 } },
            "missions": { "m": { "lent_first_catch": { "stage": "catch_first_fish", "started_revision": 1 } } },
            "receipts": { "OP": { "character": "m", "kind": "mission_started", "interactable": "lent_angler",
                "item": "fishing_rod", "quantity": 1, "mission": "lent_first_catch", "stage": "catch_first_fish", "revision": 1 } }
        })
    }

    #[test]
    fn lent_store_after_phase_one_passes() {
        assert_eq!(check_lent(&lent_store(), "m", "w", "OP"), 0);
    }

    #[test]
    fn lent_store_with_walker_progress_fails() {
        let mut doc = lent_store();
        doc["inventories"]["w"] = json!({ "fishing_rod": 1 });
        assert_eq!(check_lent(&doc, "m", "w", "OP"), 1);
    }

    #[test]
    fn lent_store_with_a_second_commit_fails() {
        let mut doc = lent_store();
        doc["revision"] = json!(2);
        assert_eq!(check_lent(&doc, "m", "w", "OP"), 1);
    }

    fn condition_store(revision: i64, conditions: Value) -> Value {
        json!({
            "format": "marburgumii.world_store", "schema_version": 3, "revision": revision, "stocks": {},
            "inventories": {}, "missions": {}, "conditions": conditions, "receipts": {}
        })
    }

    #[test]
    fn condition_phases_pass() {
        let one = condition_store(2, json!({
            "r": { "stamina": 1, "vitality": 100, "winded": true },
            "c": { "stamina": 9, "vitality": 100, "winded": true } }));
        assert_eq!(check_condition(&one, "r", "c", "s", "1"), 0);
        let two = condition_store(4, json!({
            "c": { "stamina": 9, "vitality": 100, "winded": true },
            "s": { "stamina": 71, "vitality": 100, "winded": false } }));
        assert_eq!(check_condition(&two, "r", "c", "s", "2"), 0);
    }

    #[test]
    fn condition_rested_runner_or_breathless_save_fails() {
        let rested = condition_store(2, json!({
            "r": { "stamina": 30, "vitality": 100, "winded": false },
            "c": { "stamina": 9, "vitality": 100, "winded": true } }));
        assert_eq!(check_condition(&rested, "r", "c", "s", "1"), 1);
        let kept = condition_store(4, json!({
            "r": { "stamina": 100, "vitality": 100, "winded": false },
            "c": { "stamina": 9, "vitality": 100, "winded": true },
            "s": { "stamina": 71, "vitality": 100, "winded": false } }));
        assert_eq!(check_condition(&kept, "r", "c", "s", "2"), 1);
    }

    #[test]
    fn interaction_store_counts_three_grants() {
        let doc = json!({
            "format": "marburgumii.world_store", "schema_version": 3, "revision": 3, "conditions": {},
            "stocks": { "dev_supply_crate": 0 },
            "inventories": { "a": { "fishing_rod": 2 }, "b": { "fishing_rod": 1 } },
            "receipts": { "OP": { "character": "a" }, "X": { "character": "a" }, "Y": { "character": "b" } }
        });
        assert_eq!(check_interaction(&doc, "a", "b", "OP"), 0);
        let mut tampered = doc.clone();
        tampered["stocks"]["dev_supply_crate_far"] = json!(1);
        assert_eq!(check_interaction(&tampered, "a", "b", "OP"), 1);
    }
}
