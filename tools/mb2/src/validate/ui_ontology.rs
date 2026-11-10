//! The design-time UI/HUD registry (Data/Ontology/ui_hud.v0.1.json) is a consistent graph.

use std::collections::HashSet;
use std::path::Path;

use regex::Regex;
use serde_json::Value;

use crate::repo::{read, read_json, Problems};

const REQUIRED_KEYS: [&str; 9] = [
    "type_definitions", "relation_definitions", "semantic_signals", "module_registry", "presentation_modes",
    "implementation_stages", "invariants", "excluded_features", "verification",
];
const EXCLUSIONS: [&str; 4] = ["wanted_stars", "wanted_level_or_heat", "police_pursuit_or_chase", "legacy_talents"];

fn items<'a>(doc: &'a Value, key: &str) -> &'a [Value] {
    doc[key].as_array().map(Vec::as_slice).unwrap_or(&[])
}

fn str_of<'a>(value: &'a Value, key: &str) -> &'a str {
    value[key].as_str().unwrap_or("")
}

/// Stable ids of a list, recording duplicates and malformed ids.
fn index(list: &[Value], label: &str, problems: &mut Problems) -> HashSet<String> {
    let pattern = Regex::new(r"^[a-z][a-z0-9_]*$").unwrap();
    let mut seen = HashSet::new();
    for item in list {
        match item["id"].as_str() {
            Some(id) if pattern.is_match(id) => {
                problems.expect(seen.insert(id.to_string()), format!("{label} has duplicate ID {id}"));
            }
            other => problems.0.push(format!("{label} has invalid stable ID: {other:?}")),
        }
    }
    seen
}

pub fn check(root: &Path, problems: &mut Problems) -> Vec<String> {
    let doc = read_json(root, "Data/Ontology/ui_hud.v0.1.json", problems);
    let missing: Vec<_> = REQUIRED_KEYS.iter().filter(|key| doc.get(**key).is_none()).collect();
    if doc.is_null() || !missing.is_empty() {
        problems.0.push(format!("missing catalogue keys: {missing:?}"));
        return Vec::new();
    }
    problems.expect(doc["engine"] == "Unreal Engine 5.8.3", "Engine pin must remain exactly Unreal Engine 5.8.3");
    problems.expect(doc["version"] == "0.1.0" && doc["status"] == "design_only_not_implemented",
        "Unexpected ontology version or implementation status");

    let types = index(items(&doc, "type_definitions"), "types", problems);
    let relations = index(items(&doc, "relation_definitions"), "relations", problems);
    let signals = index(items(&doc, "semantic_signals"), "signals", problems);
    let modules = index(items(&doc, "module_registry"), "modules", problems);
    let modes = index(items(&doc, "presentation_modes"), "modes", problems);
    index(items(&doc, "invariants"), "invariants", problems);

    for relation in items(&doc, "relation_definitions") {
        let id = str_of(relation, "id");
        problems.expect(types.contains(str_of(relation, "source")) && types.contains(str_of(relation, "target")),
            format!("broken relation: {id}"));
        problems.expect(["0..1", "1", "0..*", "1..*"].contains(&str_of(relation, "cardinality")),
            format!("unsupported cardinality: {id}"));
    }
    let stages: Vec<String> = items(&doc, "implementation_stages").iter()
        .map(|stage| stage.as_str().unwrap_or("").to_string()).collect();
    let expected: Vec<String> = (0..8).map(|n| format!("UI-{n:02}")).collect();
    problems.expect(stages == expected, "UI-00 through UI-07 stages must be in order");
    for module in items(&doc, "module_registry") {
        let id = str_of(module, "id");
        problems.expect(stages.iter().any(|s| s == str_of(module, "stage")) && modes.contains(str_of(module, "primary_mode")),
            format!("invalid module stage/mode: {id}"));
        let required = items(module, "required_signals");
        problems.expect(!required.is_empty(), format!("module lacks explicit signal contract: {id}"));
        for signal in required {
            let signal = signal.as_str().unwrap_or("");
            problems.expect(signals.contains(signal), format!("unknown signal {signal} in module {id}"));
        }
    }
    for mode in items(&doc, "presentation_modes") {
        let id = str_of(mode, "id");
        problems.expect(stages.iter().any(|s| s == str_of(mode, "stage")), format!("invalid mode stage: {id}"));
        for module in items(mode, "default_modules") {
            let module = module.as_str().unwrap_or("");
            problems.expect(modules.contains(module), format!("unknown module {module} in mode {id}"));
        }
    }
    let recorded: HashSet<&str> = items(&doc, "excluded_features").iter().map(|x| str_of(x, "mechanic")).collect();
    problems.expect(EXCLUSIONS.iter().all(|x| recorded.contains(x)), "Owner-approved exclusion list has been weakened");
    for module in &modules {
        problems.expect(!["wanted", "pursuit", "chase", "talent"].iter().any(|word| module.contains(word)),
            format!("excluded mechanic reintroduced in module: {module}"));
    }
    let roadmap = read(root, "Docs/UI_HUD_ROADMAP.md", problems);
    for stage in &stages {
        problems.expect(roadmap.contains(stage.as_str()), format!("Roadmap missing stage {stage}"));
    }
    let ontology = read(root, "Docs/UI_HUD_ONTOLOGY.md", problems);
    problems.expect(ontology.contains("Data/Ontology/ui_hud.v0.1.json"), "Ontology doc missing catalogue link");

    vec![
        format!("UI/HUD ontology v{} valid: {} types, {} relations, {} signals, {} modules, {} modes, {} phases",
            str_of(&doc, "version"), types.len(), relations.len(), signals.len(), modules.len(), modes.len(), stages.len()),
        "NOTE: Source consistency only; not an Unreal build or running UI.".to_string(),
    ]
}
