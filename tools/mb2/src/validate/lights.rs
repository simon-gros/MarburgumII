//! Street lamps (ADR-065): the lamps file tools/lent-ground writes and the shared C++ reader agree on its format,
//! version, kinds and sources, in order; every lamp is a well-formed row in the square; and the file is staged into
//! packaged builds, since server and client both put the lamps up from it.

use regex::Regex;

use crate::repo::{read, read_json, Problems};

const LAMPS: &str = "Content/Marburgum/Lights/lent_street_lights.json";
const CONTRACT: &str = "Source/MarburgumIIShared/Public/MarburgumStreetLights.h";
const READER: &str = "Source/MarburgumIIShared/Private/MarburgumStreetLights.cpp";
const GAME_INI: &str = "Config/DefaultGame.ini";
/// Half the side of the ground's square, metres (lent-ground's default extent).
const HALF_M: f64 = 1280.0;

/// The enumerators of `enum class <name>` in the contract, in order, without `Count`.
fn enumerators(contract: &str, name: &str) -> Vec<String> {
    let pattern = Regex::new(&format!(r"enum class {name} : uint8\s*\{{([^}}]*)\}}")).unwrap();
    let Some(body) = pattern.captures(contract) else {
        return Vec::new();
    };
    let without_comments = Regex::new(r"/\*\*?[^*]*\*+(?:[^/*][^*]*\*+)*/|//[^\n]*")
        .unwrap()
        .replace_all(&body[1], "");
    without_comments
        .split(',')
        .map(|e| e.trim().to_string())
        .filter(|e| !e.is_empty() && e != "Count")
        .collect()
}

pub fn check(root: &std::path::Path, problems: &mut Problems) -> Vec<String> {
    let lamps = read_json(root, LAMPS, problems);
    let contract = read(root, CONTRACT, problems);
    let reader = read(root, READER, problems);
    let ini = read(root, GAME_INI, problems);

    problems.expect(
        lamps["format"].as_str() == Some("marburgumii.street_lights"),
        format!("{LAMPS} is not a street lights file"),
    );
    problems.expect(
        reader.contains("\"marburgumii.street_lights\"") && reader.contains("Version != 1"),
        "the C++ reader does not read format marburgumii.street_lights version 1",
    );
    problems.expect(
        lamps["version"].as_i64() == Some(1),
        format!("{LAMPS} is not version 1"),
    );

    // The file's kinds and sources are the contract's enumerations, in order: a row stores their index.
    let names = |v: &serde_json::Value| -> Vec<String> {
        v.as_array()
            .map(|a| {
                a.iter()
                    .filter_map(|s| s.as_str())
                    .map(str::to_lowercase)
                    .collect()
            })
            .unwrap_or_default()
    };
    let kinds = names(&lamps["kinds"]);
    let sources = names(&lamps["sources"]);
    let lower =
        |v: Vec<String>| -> Vec<String> { v.into_iter().map(|s| s.to_lowercase()).collect() };
    let contract_kinds = lower(enumerators(&contract, "EKind"));
    let contract_sources = lower(enumerators(&contract, "ESource"));
    problems.expect(
        !kinds.is_empty() && kinds == contract_kinds,
        format!("lamp kinds: file {kinds:?} vs contract {contract_kinds:?}"),
    );
    problems.expect(
        !sources.is_empty() && sources == contract_sources,
        format!("lamp sources: file {sources:?} vs contract {contract_sources:?}"),
    );
    problems.expect(
        contract.contains("Marburgum/Lights/lent_street_lights.json"),
        "the contract does not name the lamps file",
    );

    let rows = lamps["lamps"].as_array().cloned().unwrap_or_default();
    let mut bad = 0usize;
    let mut outside = 0usize;
    let mut by_kind = vec![0usize; kinds.len()];
    for row in &rows {
        let values: Vec<f64> = row
            .as_array()
            .map(|r| r.iter().filter_map(|v| v.as_f64()).collect())
            .unwrap_or_default();
        if values.len() != 6 || values.iter().any(|v| !v.is_finite()) {
            bad += 1;
            continue;
        }
        let (kind, source) = (values[3], values[5]);
        if kind.fract() != 0.0
            || source.fract() != 0.0
            || kind < 0.0
            || source < 0.0
            || kind as usize >= kinds.len()
            || source as usize >= sources.len()
        {
            bad += 1;
            continue;
        }
        by_kind[kind as usize] += 1;
        if values[0].abs() > HALF_M || values[1].abs() > HALF_M {
            outside += 1;
        }
    }
    problems.expect(
        bad == 0,
        format!("{bad} lamps are not [east, south, top, kind, yaw, source] rows"),
    );
    problems.expect(
        outside == 0,
        format!("{outside} lamps stand outside the ±{HALF_M} m square"),
    );
    problems.expect(rows.len() >= 1000, format!("only {} lamps", rows.len()));
    problems.expect(
        lamps["summary"]["lamps"].as_u64() == Some(rows.len() as u64),
        "the lamps summary does not match its rows",
    );
    problems.expect(
        ini.contains("+DirectoriesToAlwaysStageAsUFS=(Path=\"Marburgum/Lights\")"),
        "the lamps file is not staged into packaged builds",
    );

    vec![format!(
        "Street lamps OK: {} lamps ({}) in {LAMPS}, kinds and sources match {CONTRACT}, staged.",
        rows.len(),
        kinds
            .iter()
            .zip(&by_kind)
            .map(|(k, n)| format!("{n} {k}"))
            .collect::<Vec<_>>()
            .join(", ")
    )]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_enumerators_past_their_comments() {
        let c = "enum class EKind : uint8\n{\n    /** A mast, with, commas. */\n    Mast,\n    // a lantern\n    Lantern,\n    Count,\n};";
        assert_eq!(enumerators(c, "EKind"), vec!["Mast", "Lantern"]);
        assert!(enumerators(c, "ESource").is_empty());
    }
}
