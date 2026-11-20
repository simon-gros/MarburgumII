//! World-store checks the smoke-test scripts run after their first server session. Each prints a
//! one-line summary on stdout for the script to read, and its problems on stderr.

use serde_json::Value;

use crate::repo::Problems;

fn load(path: &str) -> Result<Value, String> {
    let text =
        std::fs::read_to_string(path).map_err(|error| format!("cannot read {path}: {error}"))?;
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
    doc["format"] == "marburgumii.world_store" && doc["schema_version"] == 6
}

/// No character's condition was saved (the Gate 1 and Lent smoke clients never run).
fn no_conditions(doc: &Value) -> bool {
    doc["conditions"]
        .as_object()
        .is_some_and(serde_json::Map::is_empty)
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
    problems.expect(
        no_conditions(doc),
        format!("conditions={}", doc["conditions"]),
    );
    problems.expect(
        doc["revision"] == 3,
        format!("revision={}", doc["revision"]),
    );
    problems.expect(
        doc["stocks"]["dev_supply_crate"] == 0,
        format!("stock={}", doc["stocks"]["dev_supply_crate"]),
    );
    problems.expect(
        doc["stocks"].get("dev_supply_crate_far").is_none(),
        "far_stock_written",
    );
    problems.expect(a + b == 3, format!("granted={}", a + b));
    let receipts = doc["receipts"].as_object().map(|r| r.len()).unwrap_or(0);
    problems.expect(receipts == 3, format!("receipts={receipts}"));
    problems.expect(
        doc["receipts"][first_op]["character"] == key_a,
        "first_op_receipt",
    );
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
    problems.expect(
        no_conditions(doc),
        format!("conditions={}", doc["conditions"]),
    );
    problems.expect(
        doc["revision"] == 1,
        format!("revision={}", doc["revision"]),
    );
    problems.expect(
        stage == "catch_first_fish" && mission["started_revision"] == 1,
        format!("mission={mission}"),
    );
    problems.expect(
        doc["inventories"][key_m]["fishing_rod"] == 1,
        format!("rods={}", doc["inventories"][key_m]),
    );
    problems.expect(
        doc["inventories"].get(key_w).is_none() && doc["missions"].get(key_w).is_none(),
        "walker_written",
    );
    let receipt = &doc["receipts"][first_op];
    let receipts = doc["receipts"].as_object().map(|r| r.len()).unwrap_or(0);
    problems.expect(
        receipts == 1
            && receipt["kind"] == "mission_started"
            && receipt["character"] == key_m
            && receipt["item"] == "fishing_rod"
            && receipt["mission"] == "lent_first_catch",
        format!("receipts={}", doc["receipts"]),
    );
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
    let tired = |key: &str| {
        saved(key).is_some_and(|entry| {
            entry["vitality"] == 100
                && entry["stamina"]
                    .as_i64()
                    .is_some_and(|s| (0..100).contains(&s))
        })
    };
    problems.expect(is_store(doc), "format");
    let empty = |field: &str| {
        doc[field]
            .as_object()
            .is_some_and(serde_json::Map::is_empty)
    };
    problems.expect(
        empty("inventories") && empty("missions") && empty("receipts"),
        "custody_written",
    );
    problems.expect(
        conditions.as_object().map(serde_json::Map::len) == Some(2),
        format!("conditions={conditions}"),
    );
    problems.expect(tired(key_c), format!("cheater={}", conditions[key_c]));
    match phase {
        "1" => {
            problems.expect(
                doc["revision"] == 2,
                format!("revision={}", doc["revision"]),
            );
            // R left the moment it was winded: almost no breath, and winded.
            let runner = &conditions[key_r];
            problems.expect(
                tired(key_r)
                    && runner["winded"] == true
                    && runner["stamina"].as_i64().is_some_and(|s| s <= 5),
                format!("runner={runner}"),
            );
            problems.expect(saved(key_s).is_none(), "stopper_written_early");
        }
        "2" => {
            problems.expect(
                doc["revision"] == 4,
                format!("revision={}", doc["revision"]),
            );
            // A rested body is stored as no entry at all.
            problems.expect(
                saved(key_r).is_none(),
                format!("runner_not_rested={}", conditions[key_r]),
            );
            problems.expect(tired(key_s), format!("stopper={}", conditions[key_s]));
        }
        _ => problems.0.push(format!("unknown phase {phase}")),
    }
    let runner = &conditions[key_r];
    finish(
        problems,
        format!(
            "{} {} {}",
            doc["revision"],
            runner["stamina"]
                .as_i64()
                .map_or("-".to_string(), |s| s.to_string()),
            if runner["winded"] == true { 1 } else { 0 }
        ),
    )
}

/// Missions, fishing and skills (scripts/mission-smoke-linux.sh), after phase 1.
/// Prints "<money> <fishing xp> <reputation xp>".
pub fn missions(store: &str, key: &str) -> u8 {
    let doc = match load(store) {
        Ok(doc) => doc,
        Err(error) => return finish(Problems(vec![error]), "0 0 0".to_string()),
    };
    check_missions(&doc, key)
}

fn check_missions(doc: &Value, key: &str) -> u8 {
    let mut problems = Problems::default();
    problems.expect(is_store(doc), "format");
    problems.expect(
        no_conditions(doc),
        format!("conditions={}", doc["conditions"]),
    );
    let item = |id: &str| doc["inventories"][key][id].as_i64().unwrap_or(0);
    for id in [
        "fishing_rod",
        "brass_key",
        "drava_cashbox",
        "water_damaged_accounting_page",
        "torn_gate_photograph",
        "faded_transport_ticket",
        "carbon_copy_receipt",
    ] {
        problems.expect(item(id) == 1, format!("{id}={}", item(id)));
    }
    for id in ["chub", "roach", "perch"] {
        problems.expect(item(id) == 0, format!("fish_left {id}={}", item(id)));
    }
    let first = &doc["missions"][key]["lent_first_catch"];
    problems.expect(
        first["completed"] == true
            && first["stage"] == "meet_vera"
            && first["reward_paid"] == true
            && first["journal_entry"] == "blue_ledger_r90",
        format!("first_catch={first}"),
    );
    let kiosk = &doc["missions"][key]["kiosk_67_customer_stopped_coming"];
    problems.expect(
        kiosk["completed"] == true
            && kiosk["stage"] == "resolution"
            && kiosk["choice"] == "pay_tab"
            && kiosk["outcome"] == "tab_paid"
            && kiosk["journal_entry"] == "kiosk_67_missing_customer"
            && kiosk["flags"]["ledger_initials_recognized"] == true,
        format!("kiosk={kiosk}"),
    );
    let names = &doc["missions"][key]["raeda_names_no_longer_match"];
    problems.expect(
        names["completed"] == true
            && names["stage"] == "decide_maja"
            && names["choice"] == "tell_maja"
            && names["outcome"] == "maja_informed"
            && names["journal_entry"] == "blue_ledger_names_no_longer_match"
            && names["flags"]["maja_informed"] == true,
        format!("names={names}"),
    );
    for id in [
        "raeda_directory_copy",
        "successor_company_notice",
        "returned_raeda_envelope",
        "raeda_newspaper_clipping",
    ] {
        problems.expect(item(id) == 1, format!("{id}={}", item(id)));
    }
    let money = doc["money"][key].as_i64().unwrap_or(0);
    let fishing = doc["skills"][key]["fishing"].as_i64().unwrap_or(0);
    let reputation = doc["skills"][key]["reputation"].as_i64().unwrap_or(0);
    {
        let mut done = |id: &str, stage: &str, choice: &str| {
            let mission = &doc["missions"][key][id];
            // A mission with no decision stores no choice.
            problems.expect(
                mission["completed"] == true
                    && mission["stage"] == stage
                    && mission["choice"].as_str().unwrap_or("") == choice,
                format!("{id}={mission}"),
            );
        };
        done("lent_late_delivery", "resolve_fee", "leave_clerk_share");
        done(
            "hall_three_last_shift",
            "resolve_inventory",
            "negotiate_settlement",
        );
        done(
            "under_the_paint_four_names",
            "decide_sign_window",
            "preserve_layers",
        );
        done("raeda_last_shift", "show_vera", "");
        done("raeda_city_for_sale", "decide_milan", "trust_milan");
    }
    problems.expect(item("brown_paper_parcel") == 0, "parcel_not_consumed");
    problems.expect(
        item("raeda_yard_archive_boxes") == 0,
        "archive_boxes_not_consumed",
    );
    for id in [
        "hall_three_plate_photos",
        "hall_three_keyring",
        "raeda_transport_maintenance_book",
        "storefront_layer_one_photo",
        "storefront_layer_two_photo",
        "storefront_layer_three_photo",
        "storefront_oldest_lettering_photo",
        "storefront_archived_advertisements",
        "tone_vogrin_locker_card",
        "kainach_serial_photos",
        "yard_vehicle_log",
        "corrected_yard_schedule",
        "exchange_counterfoil_copy",
        "kainach_contract_reference",
        "prehod_classified_clippings",
        "pohorska_bank_file_locator",
        "k67_raeda_concession_copy",
    ] {
        problems.expect(item(id) == 1, format!("{id}={}", item(id)));
    }
    problems.expect(money == 90, format!("money={money}"));
    problems.expect(
        [16, 20, 24].contains(&fishing),
        format!("fishing_xp={fishing}"),
    );
    problems.expect(reputation == 95, format!("reputation_xp={reputation}"));
    finish(problems, format!("{money} {fishing} {reputation}"))
}

/// Chapter IV, Maja's folder and the photography missions (scripts/story-smoke-linux.sh, ADR-081), after both phases on a
/// seeded schema 5 store. Prints "<money> <reputation> <photography> <discoveries>".
pub fn story(store: &str, key: &str) -> u8 {
    let doc = match load(store) {
        Ok(doc) => doc,
        Err(error) => return finish(Problems(vec![error]), "0 0 0 0".to_string()),
    };
    check_story(&doc, key)
}

fn check_story(doc: &Value, key: &str) -> u8 {
    let mut problems = Problems::default();
    problems.expect(is_store(doc), "not_a_current_store");
    let item = |id: &str| doc["inventories"][key][id].as_i64().unwrap_or(0);
    let mission = |id: &str| &doc["missions"][key][id];
    let voices = mission("raeda_voices_on_tape");
    problems.expect(
        voices["completed"] == true
            && voices["choice"] == "ask_tone"
            && voices["outcome"] == "tone_questioned"
            && voices["journal_entry"] == "blue_ledger_voices_on_tape"
            && voices["flags"]["canteen_tape_place_melje"] == true,
        format!("voices={voices}"),
    );
    let folder = mission("raeda_mentor_folder");
    problems.expect(
        folder["completed"] == true && folder["flags"]["mentor_question_found"] == true,
        format!("folder={folder}"),
    );
    let nika = mission("nika_then_and_now");
    problems.expect(
        nika["completed"] == true && nika["outcome"] == "darkroom_lesson",
        format!("nika={nika}"),
    );
    let brane = mission("brane_old_letters");
    problems.expect(
        brane["completed"] == true
            && brane["outcome"] == "prints_given"
            && brane["flags"]["prehod_spacing_error_documented"] == true,
        format!("brane={brane}"),
    );
    for id in [
        "camera",
        "canteen_cassette_c60",
        "canteen_tape_notes",
        "raeda_recorder_counter_note",
        "mentor_folder_note",
        "nika_old_prints",
        "photo_vodni_stolp",
        "photo_sodni_stolp",
        "photo_stari_most",
        "photo_kvik_lettering",
        "photo_velika_kavarna_lettering",
        "photo_prehod_sign_melje",
        "photo_sinagoga",
    ] {
        problems.expect(item(id) == 1, format!("{id}={}", item(id)));
    }
    let documented: Vec<&str> = doc["discoveries"][key]
        .as_array()
        .map(|a| a.iter().filter_map(Value::as_str).collect())
        .unwrap_or_default();
    problems.expect(documented.len() == 7, format!("discoveries={documented:?}"));
    let money = doc["money"][key].as_i64().unwrap_or(0);
    let reputation = doc["skills"][key]["reputation"].as_i64().unwrap_or(0);
    let photography = doc["skills"][key]["photography"].as_i64().unwrap_or(0);
    problems.expect(money == 180, format!("money={money}"));
    problems.expect(reputation == 70, format!("reputation_xp={reputation}"));
    problems.expect(photography == 215, format!("photography_xp={photography}"));
    let photographs = doc["receipts"]
        .as_object()
        .map(|r| {
            r.values()
                .filter(|r| {
                    r["character"] == key
                        && (r["kind"] == "photographed"
                            || r["interactable"]
                                .as_str()
                                .is_some_and(|i| documented.contains(&i)))
                })
                .count()
        })
        .unwrap_or(0);
    problems.expect(
        photographs == 7,
        format!("photograph_receipts={photographs}"),
    );
    finish(
        problems,
        format!("{money} {reputation} {photography} {}", documented.len()),
    )
}


/// Chapter VI and Frame 37 (scripts/negative-smoke-linux.sh, ADR-084), after phase 1 on a store seeded with Chapters
/// I-III, Then and Now and Kiosk 67 ended in `outcome` for `key`; `other` only started the chapter and dated the print.
/// Prints "<money> <photography> <trading> <discoveries>".
pub fn negative(store: &str, key: &str, other: &str, outcome: &str) -> u8 {
    let doc = match load(store) {
        Ok(doc) => doc,
        Err(error) => return finish(Problems(vec![error]), "0 0 0 0".to_string()),
    };
    check_negative(&doc, key, other, outcome)
}

fn check_negative(doc: &Value, key: &str, other: &str, outcome: &str) -> u8 {
    let mut problems = Problems::default();
    problems.expect(is_store(doc), "not_a_current_store");
    let item = |who: &str, id: &str| doc["inventories"][who][id].as_i64().unwrap_or(0);
    let mission = |who: &str, id: &str| &doc["missions"][who][id];
    let negative = mission(key, "raeda_the_negative");
    problems.expect(
        negative["completed"] == true
            && negative["reward_paid"] == true
            && negative["journal_entry"] == "blue_ledger_the_negative"
            && negative["flags"]["kainach_after_december"] == true
            && negative["flags"]["albin_testified"] == true,
        format!("negative={negative}"),
    );
    let frame = mission(key, "nika_frame_37");
    problems.expect(
        frame["completed"] == true && frame["outcome"] == "scrap_in_lab" && frame["journal_entry"] == "foto_vidmar_frame_37",
        format!("frame_37={frame}"),
    );
    // Every unique piece of evidence once; the original went to the lab safe and its receipt came back.
    for id in [
        "frame_37_print",
        "photo_melje_date_board",
        "photo_foto_vidmar_order_book",
        "photo_zupan_contact_sheet",
        "drago_wallet_print_account",
        "chronology_frame_37",
        "torn_strip_print",
        "albin_testimony",
        "contact_sheet_custody_receipt",
        "torn_gate_photograph",
        "faded_transport_ticket",
    ] {
        problems.expect(item(key, id) == 1, format!("{id}={}", item(key, id)));
    }
    problems.expect(item(key, "zupan_contact_sheet") == 0, "contact_sheet_not_lodged");
    // The route's own traces: the stamp went back to Albin, the scarf is kept.
    match outcome {
        "object_sold" => problems.expect(item(key, "machine_hall_council_stamp") == 0, "stamp_not_returned"),
        "left_alone" => problems.expect(item(key, "terrace_scarf") == 1, "no_scarf"),
        _ => problems.expect(item(key, "machine_hall_council_stamp") == 0 && item(key, "terrace_scarf") == 0, "another_route_left_items"),
    }
    let documented: Vec<&str> = doc["discoveries"][key]
        .as_array()
        .map(|a| a.iter().filter_map(Value::as_str).collect())
        .unwrap_or_default();
    for subject in ["foto_vidmar_order_book_page", "zupan_contact_sheet_copy", "melje_date_board_view"] {
        problems.expect(documented.contains(&subject), format!("not_documented={subject}"));
    }
    let money = doc["money"][key].as_i64().unwrap_or(0);
    let photography = doc["skills"][key]["photography"].as_i64().unwrap_or(0);
    let trading = doc["skills"][key]["trading"].as_i64().unwrap_or(0);
    // Seeded 120 money and no XP: +60 for the chapter, -40 if the stamp was bought; Photography 10 + 20 + 25 for the three
    // documents and 10 for the deduction.
    let bought = outcome == "object_sold";
    problems.expect(money == 120 + 60 - if bought { 40 } else { 0 }, format!("money={money}"));
    problems.expect(photography == 65, format!("photography_xp={photography}"));
    problems.expect(trading == if bought { 15 } else { 0 }, format!("trading_xp={trading}"));
    // The other character's chapter is their own: started and one step on, nothing of the first character's.
    let theirs = mission(other, "raeda_the_negative");
    problems.expect(
        theirs["completed"] != true && theirs["stage"] == "read_order_book",
        format!("other={theirs}"),
    );
    problems.expect(item(other, "frame_37_print") == 0 && item(other, "albin_testimony") == 0, "other_holds_first_characters_evidence");
    // No mission was paid twice: one completion receipt each.
    let completions = |id: &str| {
        doc["receipts"]
            .as_object()
            .map(|r| r.values().filter(|r| r["character"] == key && r["mission"] == id && r["kind"] == "mission_completed").count())
            .unwrap_or(0)
    };
    problems.expect(completions("raeda_the_negative") == 1, format!("negative_completions={}", completions("raeda_the_negative")));
    problems.expect(completions("nika_frame_37") == 1, format!("frame_completions={}", completions("nika_frame_37")));
    finish(problems, format!("{money} {photography} {trading} {}", documented.len()))
}

/// Kiosk 67 (scripts/shop-smoke-linux.sh), after phase 1 on a seeded schema 4 store (revision 1, 50 money, a brass
/// key): three counter commits, first a newspaper bought with its Trading XP, then sold back, then a pen.
/// Prints "<revision> <money> <shelves paper,postcard,pen>".
pub fn shop(store: &str, key: &str, first_op: &str) -> u8 {
    let doc = match load(store) {
        Ok(doc) => doc,
        Err(error) => return finish(Problems(vec![error]), "none 0 -".to_string()),
    };
    check_shop(&doc, key, first_op)
}

fn check_shop(doc: &Value, key: &str, first_op: &str) -> u8 {
    let mut problems = Problems::default();
    problems.expect(is_store(doc), "format");
    problems.expect(
        doc["revision"] == 4,
        format!("revision={}", doc["revision"]),
    );
    let money = doc["money"][key].as_i64().unwrap_or(0);
    problems.expect(money == 23, format!("money={money}"));
    let held = &doc["inventories"][key];
    problems.expect(
        held["ballpoint_pen"] == 1
            && held["brass_key"] == 1
            && held.get("mariborski_glas").is_none(),
        format!("inventory={held}"),
    );
    problems.expect(
        doc["skills"][key]["trading"] == 10,
        format!("skills={}", doc["skills"][key]),
    );
    problems.expect(
        doc["first_purchases"][key] == serde_json::json!(["kiosk_67"]),
        format!("first_purchases={}", doc["first_purchases"]),
    );
    // Only the lines that ever moved are written; the postcard shelf stays at its starting stock.
    let shelves = &doc["shelves"]["kiosk_67"];
    problems.expect(
        shelves["mariborski_glas"] == 12
            && shelves["ballpoint_pen"] == 5
            && shelves.get("maribor_postcard").is_none(),
        format!("shelves={shelves}"),
    );
    let receipts = doc["receipts"].as_object().cloned().unwrap_or_default();
    let mut kinds: Vec<(i64, String, String, i64)> = receipts
        .values()
        .map(|r| {
            (
                r["revision"].as_i64().unwrap_or(0),
                r["kind"].as_str().unwrap_or("").to_string(),
                r["item"].as_str().unwrap_or("").to_string(),
                r["money_delta"].as_i64().unwrap_or(0),
            )
        })
        .collect();
    kinds.sort();
    let want = vec![
        (
            2,
            "item_bought".to_string(),
            "mariborski_glas".to_string(),
            -20,
        ),
        (3, "item_sold".to_string(), "mariborski_glas".to_string(), 8),
        (
            4,
            "item_bought".to_string(),
            "ballpoint_pen".to_string(),
            -15,
        ),
    ];
    problems.expect(kinds == want, format!("receipts={kinds:?}"));
    let first = &doc["receipts"][first_op];
    problems.expect(
        first["kind"] == "item_bought"
            && first["shop"] == "kiosk_67"
            && first["interactable"] == "petra_kralj"
            && first["character"] == key
            && first["skill_xp"]["trading"] == 10,
        format!("first_receipt={first}"),
    );
    problems.expect(
        receipts
            .values()
            .all(|r| r["shop"] == "kiosk_67" && r["character"] == key),
        "receipt_without_shop",
    );
    let shelf = |item: &str, start: i64| shelves[item].as_i64().unwrap_or(start);
    finish(
        problems,
        format!(
            "{} {money} {},{},{}",
            doc["revision"],
            shelf("mariborski_glas", 12),
            shelf("maribor_postcard", 8),
            shelf("ballpoint_pen", 6)
        ),
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn shop_store() -> Value {
        json!({
            "format": "marburgumii.world_store", "schema_version": 6, "revision": 4,
            "money": { "s": 23 }, "inventories": { "s": { "ballpoint_pen": 1, "brass_key": 1 } },
            "skills": { "s": { "trading": 10 } }, "first_purchases": { "s": ["kiosk_67"] },
            "shelves": { "kiosk_67": { "mariborski_glas": 12, "ballpoint_pen": 5 } },
            "receipts": {
                "A": { "character": "s", "kind": "item_bought", "interactable": "petra_kralj", "shop": "kiosk_67", "item": "mariborski_glas",
                    "quantity": 1, "money_delta": -20, "skill_xp": { "trading": 10 }, "revision": 2 },
                "B": { "character": "s", "kind": "item_sold", "interactable": "petra_kralj", "shop": "kiosk_67", "item": "mariborski_glas",
                    "quantity": 1, "money_delta": 8, "revision": 3 },
                "C": { "character": "s", "kind": "item_bought", "interactable": "petra_kralj", "shop": "kiosk_67", "item": "ballpoint_pen",
                    "quantity": 1, "money_delta": -15, "revision": 4 }
            }
        })
    }

    #[test]
    fn shop_store_passes_and_a_double_charge_fails() {
        assert_eq!(check_shop(&shop_store(), "s", "A"), 0);
        let mut twice = shop_store();
        twice["money"]["s"] = json!(3);
        assert_eq!(check_shop(&twice, "s", "A"), 1);
        let mut overfilled = shop_store();
        overfilled["shelves"]["kiosk_67"]["mariborski_glas"] = json!(13);
        assert_eq!(check_shop(&overfilled, "s", "A"), 1);
        let mut extra = shop_store();
        extra["receipts"]["D"] = extra["receipts"]["A"].clone();
        assert_eq!(check_shop(&extra, "s", "A"), 1);
    }

    fn lent_store() -> Value {
        json!({
            "format": "marburgumii.world_store", "schema_version": 6, "revision": 1, "stocks": {}, "conditions": {},
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
            "format": "marburgumii.world_store", "schema_version": 6, "revision": revision, "stocks": {},
            "inventories": {}, "missions": {}, "conditions": conditions, "receipts": {}
        })
    }

    #[test]
    fn condition_phases_pass() {
        let one = condition_store(
            2,
            json!({
            "r": { "stamina": 1, "vitality": 100, "winded": true },
            "c": { "stamina": 9, "vitality": 100, "winded": true } }),
        );
        assert_eq!(check_condition(&one, "r", "c", "s", "1"), 0);
        let two = condition_store(
            4,
            json!({
            "c": { "stamina": 9, "vitality": 100, "winded": true },
            "s": { "stamina": 71, "vitality": 100, "winded": false } }),
        );
        assert_eq!(check_condition(&two, "r", "c", "s", "2"), 0);
    }

    #[test]
    fn condition_rested_runner_or_breathless_save_fails() {
        let rested = condition_store(
            2,
            json!({
            "r": { "stamina": 30, "vitality": 100, "winded": false },
            "c": { "stamina": 9, "vitality": 100, "winded": true } }),
        );
        assert_eq!(check_condition(&rested, "r", "c", "s", "1"), 1);
        let kept = condition_store(
            4,
            json!({
            "r": { "stamina": 100, "vitality": 100, "winded": false },
            "c": { "stamina": 9, "vitality": 100, "winded": true },
            "s": { "stamina": 71, "vitality": 100, "winded": false } }),
        );
        assert_eq!(check_condition(&kept, "r", "c", "s", "2"), 1);
    }

    #[test]
    fn interaction_store_counts_three_grants() {
        let doc = json!({
            "format": "marburgumii.world_store", "schema_version": 6, "revision": 3, "conditions": {},
            "stocks": { "dev_supply_crate": 0 },
            "inventories": { "a": { "fishing_rod": 2 }, "b": { "fishing_rod": 1 } },
            "receipts": { "OP": { "character": "a" }, "X": { "character": "a" }, "Y": { "character": "b" } }
        });
        assert_eq!(check_interaction(&doc, "a", "b", "OP"), 0);
        let mut tampered = doc.clone();
        tampered["stocks"]["dev_supply_crate_far"] = json!(1);
        assert_eq!(check_interaction(&tampered, "a", "b", "OP"), 1);
    }

    #[test]
    fn mission_store_after_the_opening_and_the_kiosk_passes() {
        let doc = json!({
            "format": "marburgumii.world_store", "schema_version": 6, "revision": 12,
            "stocks": {}, "conditions": {},
            "inventories": { "p": {
                "fishing_rod": 1, "brass_key": 1, "drava_cashbox": 1,
                "water_damaged_accounting_page": 1, "torn_gate_photograph": 1,
                "faded_transport_ticket": 1, "carbon_copy_receipt": 1,
                "raeda_directory_copy": 1, "successor_company_notice": 1,
                "returned_raeda_envelope": 1, "raeda_newspaper_clipping": 1,
                "hall_three_plate_photos": 1, "hall_three_keyring": 1, "raeda_transport_maintenance_book": 1,
                "storefront_layer_one_photo": 1, "storefront_layer_two_photo": 1,
                "storefront_layer_three_photo": 1, "storefront_oldest_lettering_photo": 1,
                "storefront_archived_advertisements": 1,
                "tone_vogrin_locker_card": 1, "kainach_serial_photos": 1, "yard_vehicle_log": 1,
                "corrected_yard_schedule": 1, "exchange_counterfoil_copy": 1, "kainach_contract_reference": 1,
                "prehod_classified_clippings": 1, "pohorska_bank_file_locator": 1, "k67_raeda_concession_copy": 1
            }},
            "missions": { "p": {
                "lent_first_catch": {
                    "stage": "meet_vera", "started_revision": 1, "completed": true,
                    "reward_paid": true, "journal_entry": "blue_ledger_r90"
                },
                "kiosk_67_customer_stopped_coming": {
                    "stage": "resolution", "started_revision": 7, "completed": true,
                    "choice": "pay_tab", "outcome": "tab_paid",
                    "journal_entry": "kiosk_67_missing_customer",
                    "flags": { "ledger_initials_recognized": true }
                },
                "raeda_names_no_longer_match": {
                    "stage": "decide_maja", "started_revision": 6, "completed": true,
                    "choice": "tell_maja", "outcome": "maja_informed",
                    "journal_entry": "blue_ledger_names_no_longer_match",
                    "flags": { "maja_informed": true, "investigation_private": false }
                },
                "lent_late_delivery": {
                    "stage": "resolve_fee", "started_revision": 13, "completed": true,
                    "choice": "leave_clerk_share"
                },
                "hall_three_last_shift": {
                    "stage": "resolve_inventory", "started_revision": 16, "completed": true,
                    "choice": "negotiate_settlement"
                },
                "under_the_paint_four_names": {
                    "stage": "decide_sign_window", "started_revision": 24, "completed": true,
                    "choice": "preserve_layers"
                },
                "raeda_last_shift": {
                    "stage": "show_vera", "started_revision": 30, "completed": true
                },
                "raeda_city_for_sale": {
                    "stage": "decide_milan", "started_revision": 38, "completed": true,
                    "choice": "trust_milan"
                }
            }},
            "skills": { "p": { "fishing": 20, "reputation": 95 } },
            "money": { "p": 90 },
            "receipts": {}
        });
        assert_eq!(check_missions(&doc, "p"), 0);
        let mut short = doc.clone();
        short["money"]["p"] = json!(50);
        assert_eq!(check_missions(&short, "p"), 1);
    }
}
