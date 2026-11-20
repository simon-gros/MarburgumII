//! The historical research archives (ADR-077): the art reference catalogue, its licensed-media manifest and
//! reference-only index, the archive-derived production rules and the historical imagery/map catalogue. All of it
//! is metadata; no image or raster is in Git, and downloading a reference never makes it a game asset.
//!
//! `check` is `mb2 validate history`; `fetch` is `mb2 fetch-art-reference`, the only downloader, which takes
//! `auto_fetch=yes` manifest rows over HTTPS into a directory outside the checkout.

use std::collections::{HashMap, HashSet};
use std::path::{Component, Path, PathBuf};
use std::process::Command;

use serde_json::Value;
use sha2::{Digest, Sha256};

use crate::repo::{self, Problems};

pub const CATALOG: &str = "art_reference/CATALOG.csv";
pub const MANIFEST: &str = "art_reference/media/licensed/MANIFEST.csv";
pub const INDEX: &str = "art_reference/media/reference-only/INDEX.md";
pub const MEDIA: &str = "art_reference/media";
pub const RULES: &str = "Data/History/historical_reference_rules.json";
pub const IMAGERY: &str = "Data/History/historical_imagery_catalogue.json";
const ARCHIVE_WORKFLOW: &str = ".github/workflows/historical-archives.yml";
const SOURCE_WORKFLOW: &str = ".github/workflows/validation.yml";

const PERIOD_START_YEAR: i64 = 1990;
const PERIOD_END_YEAR: i64 = 1999;
const SCHEMA_VERSION: i64 = 1;
const CONFIDENCES: [&str; 3] = ["high", "medium", "low"];
const RULE_KINDS: [&str; 2] = ["art_direction", "world_state"];
const RULE_FIELDS: [&str; 11] = ["id", "kind", "domain", "location", "years", "confidence", "review_required", "ref_ids",
    "requirements", "prohibitions", "review_targets"];
const PRODUCT_KINDS: [&str; 5] = ["orthophoto", "cadastral_map", "topographic_map", "aerial_photo", "other"];
/// Present on every imagery product; null when not verified.
const NULLABLE_NUMBER_FIELDS: [&str; 2] = ["georeference_accuracy_m", "resolution_m"];
/// Present and non-empty on every imagery product; "unknown" when not verified.
const PRODUCT_TEXT_FIELDS: [&str; 8] = ["id", "title", "publisher", "access", "coverage", "license", "known_distortions",
    "absence_presence_reliability"];
/// User-supplied photographs have no catalogue record and no remote source, so they are never fetched.
const USER_SUPPLIED_PREFIX: &str = "MAR-2026-USER-";
const USER_AGENT: &str = "MarburgumII-Art-Reference/1.0 (+https://github.com/simon-gros/MarburgumII)";

/// One CSV data row with its line number in the file.
#[derive(Debug)]
pub struct Row {
    pub line: u64,
    pub fields: HashMap<String, String>,
}

impl Row {
    pub fn get(&self, field: &str) -> &str {
        self.fields.get(field).map(String::as_str).unwrap_or("")
    }
}

/// A non-empty CSV with every `mandatory` header; a row with too few or too many fields is an error.
pub fn parse_csv(name: &str, text: &str, mandatory: &[&str]) -> Result<Vec<Row>, String> {
    let mut reader = csv::ReaderBuilder::new().from_reader(text.trim_start_matches('\u{feff}').as_bytes());
    let headers: Vec<String> = reader.headers().map_err(|error| format!("{name}: {error}"))?.iter().map(str::to_string).collect();
    let mut missing: Vec<&str> = mandatory.iter().copied().filter(|m| !headers.iter().any(|h| h == m)).collect();
    if !missing.is_empty() {
        missing.sort_unstable();
        return Err(format!("{name}: missing required headers: {missing:?}"));
    }
    let mut rows = Vec::new();
    for record in reader.records() {
        let record = record.map_err(|error| format!("{name}: malformed CSV row: {error}"))?;
        let line = record.position().map_or(0, csv::Position::line);
        rows.push(Row { line, fields: headers.iter().cloned().zip(record.iter().map(str::to_string)).collect() });
    }
    if rows.is_empty() {
        return Err(format!("{name}: empty catalogue"));
    }
    Ok(rows)
}

/// Every catalogue error, and the stable `ref_id`s that rules and the manifest may cite.
pub fn validate_catalog(rows: &[Row]) -> (HashSet<String>, Vec<String>) {
    let mut ids = HashSet::new();
    let mut errors = Vec::new();
    for row in rows {
        let id = row.get("ref_id").trim();
        if id.is_empty() || !ids.insert(id.to_string()) {
            errors.push(format!("{CATALOG}:{}: missing/duplicate ref_id {id:?}", row.line));
        }
        for field in ["title", "rights_status", "repository_status"] {
            if row.get(field).trim().is_empty() {
                errors.push(format!("{CATALOG}:{}: {field} empty", row.line));
            }
        }
    }
    (ids, errors)
}

/// Every manifest error: unique rows, each citing the catalogue (or a never-fetched user photograph), and only
/// HTTPS sources fetched automatically.
pub fn validate_manifest(rows: &[Row], catalog: &HashSet<String>) -> Vec<String> {
    let mut seen = HashSet::new();
    let mut errors = Vec::new();
    for row in rows {
        let at = format!("{MANIFEST}:{}", row.line);
        let id = row.get("ref_id").trim();
        if id.is_empty() || !seen.insert(id.to_string()) {
            errors.push(format!("{at}: missing/duplicate ref_id {id:?}"));
        }
        let auto_fetch = row.get("auto_fetch");
        if !catalog.contains(id) && !(id.starts_with(USER_SUPPLIED_PREFIX) && auto_fetch == "no") {
            errors.push(format!("{at}: untracked source reference {id:?}"));
        }
        if auto_fetch != "yes" && auto_fetch != "no" {
            errors.push(format!("{at}: invalid auto_fetch setting"));
        }
        if auto_fetch == "yes" && !row.get("remote_url").starts_with("https://") {
            errors.push(format!("{at}: automatic fetch requires https URL"));
        }
        if safe_relative(row.get("filename")).is_none() {
            errors.push(format!("{at}: filename must be a relative path inside the download directory"));
        }
    }
    errors
}

fn non_empty_text(value: &Value) -> bool {
    value.as_str().is_some_and(|s| !s.trim().is_empty())
}

fn non_empty_text_list(value: &Value) -> bool {
    value.as_array().is_some_and(|items| !items.is_empty() && items.iter().all(non_empty_text))
}

fn year(value: &Value) -> Option<i64> {
    value.as_i64().filter(|y| (1001..3000).contains(y))
}

/// Every error in the archive-derived production rules. With `catalog`, each cited `ref_id` must exist in it.
pub fn validate_rules(store: &Value, catalog: Option<&HashSet<String>>) -> Vec<String> {
    let Some(top) = store.as_object() else {
        return vec!["historical reference rules: top level is not an object".to_string()];
    };
    let mut errors = Vec::new();
    if store["schema_version"].as_i64() != Some(SCHEMA_VERSION) {
        errors.push(format!("historical reference rules: schema_version {} is not {SCHEMA_VERSION}", store["schema_version"]));
    }
    let period = &store["game_period"];
    if !period.is_object() {
        errors.push("historical reference rules: game_period must be an object".to_string());
    } else if period["start_year"].as_i64() != Some(PERIOD_START_YEAR) || period["end_year"].as_i64() != Some(PERIOD_END_YEAR) {
        errors.push(format!("historical reference rules: game_period must be {PERIOD_START_YEAR}-{PERIOD_END_YEAR}"));
    }
    if !non_empty_text(&store["source_catalog"]) {
        errors.push("historical reference rules: source_catalog must be a non-empty string".to_string());
    }
    let Some(rules) = top.get("rules").and_then(Value::as_array) else {
        errors.push("historical reference rules: rules must be an array".to_string());
        return errors;
    };
    if rules.is_empty() {
        errors.push("historical reference rules: rules must not be empty".to_string());
    }
    let mut seen = HashSet::new();
    for (index, rule) in rules.iter().enumerate() {
        let mut at = format!("rules[{index}]");
        let Some(fields) = rule.as_object() else {
            errors.push(format!("{at}: not an object"));
            continue;
        };
        match rule["id"].as_str().filter(|id| !id.trim().is_empty()) {
            Some(id) => {
                at = format!("rules[{index}] ({id})");
                if !seen.insert(id) {
                    errors.push(format!("historical reference rules: duplicate id {id:?}"));
                }
            }
            None => errors.push(format!("{at}: id must be a non-empty string")),
        }
        for field in RULE_FIELDS {
            if !fields.contains_key(field) {
                errors.push(format!("{at}: missing required field {field:?}"));
            }
        }
        if !rule["kind"].as_str().is_some_and(|k| RULE_KINDS.contains(&k)) {
            errors.push(format!("{at}: kind {} not in {RULE_KINDS:?}", rule["kind"]));
        }
        for field in ["domain", "location"] {
            if !non_empty_text(&rule[field]) {
                errors.push(format!("{at}: {field} must be a non-empty string"));
            }
        }
        if !rule["confidence"].as_str().is_some_and(|c| CONFIDENCES.contains(&c)) {
            errors.push(format!("{at}: confidence {} not in {CONFIDENCES:?}", rule["confidence"]));
        }
        if !rule["review_required"].is_boolean() {
            errors.push(format!("{at}: review_required must be boolean"));
        }
        let years = &rule["years"];
        if !years.is_object() {
            errors.push(format!("{at}: years must be an object"));
        } else {
            match (year(&years["start"]), year(&years["end"])) {
                (Some(start), Some(end)) if start > end => errors.push(format!("{at}: years.start {start} is after years.end {end}")),
                (Some(start), Some(end)) if start < PERIOD_START_YEAR || end > PERIOD_END_YEAR => {
                    errors.push(format!("{at}: years {start}-{end} outside {PERIOD_START_YEAR}-{PERIOD_END_YEAR}"));
                }
                (Some(_), Some(_)) => {}
                _ => errors.push(format!("{at}: years.start/end must be integer years")),
            }
        }
        for field in ["ref_ids", "requirements", "prohibitions", "review_targets"] {
            if !non_empty_text_list(&rule[field]) {
                errors.push(format!("{at}: {field} must be a non-empty string array"));
            }
        }
        if non_empty_text_list(&rule["ref_ids"]) {
            let refs: Vec<&str> = rule["ref_ids"].as_array().into_iter().flatten().filter_map(Value::as_str).collect();
            if refs.iter().collect::<HashSet<_>>().len() != refs.len() {
                errors.push(format!("{at}: ref_ids contains duplicates"));
            }
            for id in refs.iter().filter(|id| catalog.is_some_and(|c| !c.contains(**id))) {
                errors.push(format!("{at}: ref_id {id:?} not found in CATALOG.csv"));
            }
        }
    }
    errors
}

fn validate_product(product: &Value, index: usize) -> Vec<String> {
    let at = format!("products[{index}]");
    let Some(fields) = product.as_object() else {
        return vec![format!("{at}: must be an object")];
    };
    let mut errors = Vec::new();
    for field in PRODUCT_TEXT_FIELDS {
        if !non_empty_text(&product[field]) {
            errors.push(format!("{at}.{field}: required non-empty string (use \"unknown\")"));
        }
    }
    if !product["kind"].as_str().is_some_and(|k| PRODUCT_KINDS.contains(&k)) {
        errors.push(format!("{at}.kind: must be one of {PRODUCT_KINDS:?}"));
    }
    for field in NULLABLE_NUMBER_FIELDS {
        match fields.get(field) {
            None => errors.push(format!("{at}.{field}: required (use null when unknown)")),
            Some(Value::Null) => {}
            Some(value) if value.as_f64().is_some_and(|n| n > 0.0) => {}
            Some(_) => errors.push(format!("{at}.{field}: must be a positive number or null")),
        }
    }
    match product["date_range"].as_object() {
        Some(range) if range.len() == 2 && range.contains_key("start") && range.contains_key("end") => {
            for key in ["start", "end"] {
                if !range[key].is_null() && !range[key].is_string() {
                    errors.push(format!("{at}.date_range.{key}: must be a string or null"));
                }
            }
            if let (Some(start), Some(end)) = (range["start"].as_str(), range["end"].as_str()) {
                if start > end {
                    errors.push(format!("{at}.date_range: start is after end"));
                }
            }
        }
        _ => errors.push(format!("{at}.date_range: must be an object with start and end (null when unknown)")),
    }
    let urls_ok = product["source_urls"].as_array().is_some_and(|urls| urls.iter().all(|u| u.as_str().is_some_and(|s| !s.is_empty())));
    if !urls_ok {
        errors.push(format!("{at}.source_urls: must be a list of non-empty strings"));
    }
    if !product["verified"].is_boolean() {
        errors.push(format!("{at}.verified: must be a boolean"));
    }
    errors
}

/// Every error in the historical imagery/map catalogue. Unknown facts must be explicit null or "unknown", so an
/// unverified value can be neither silently absent nor invented.
pub fn validate_imagery(catalogue: &Value) -> Vec<String> {
    if !catalogue.is_object() {
        return vec!["catalogue must be an object".to_string()];
    }
    let mut errors = Vec::new();
    if catalogue["schema_version"].as_i64() != Some(SCHEMA_VERSION) {
        errors.push(format!("schema_version must be {SCHEMA_VERSION}"));
    }
    let Some(products) = catalogue["products"].as_array() else {
        errors.push("products must be a list".to_string());
        return errors;
    };
    let mut seen = HashSet::new();
    for (index, product) in products.iter().enumerate() {
        errors.extend(validate_product(product, index));
        if let Some(id) = product["id"].as_str() {
            if !seen.insert(id) {
                errors.push(format!("products[{index}].id: duplicate id {id:?}"));
            }
        }
    }
    errors
}

/// Files under the media tree other than CSV manifests and Markdown notes; the archive is research metadata only.
fn media_binaries(root: &Path) -> Vec<String> {
    fn walk(dir: &Path, out: &mut Vec<PathBuf>) {
        for entry in std::fs::read_dir(dir).into_iter().flatten().flatten() {
            let path = entry.path();
            if path.is_dir() { walk(&path, out) } else { out.push(path) }
        }
    }
    let mut files = Vec::new();
    walk(&root.join(MEDIA), &mut files);
    let mut binaries: Vec<String> = files.iter()
        .filter(|f| !matches!(f.extension().and_then(|e| e.to_str()), Some("csv" | "md")))
        .map(|f| f.strip_prefix(root).unwrap_or(f).to_string_lossy().replace('\\', "/"))
        .collect();
    binaries.sort();
    binaries
}

fn load_csv(root: &Path, relative: &str, mandatory: &[&str], problems: &mut Problems) -> Vec<Row> {
    let text = repo::read(root, relative, problems);
    if text.is_empty() {
        return Vec::new();
    }
    parse_csv(relative, &text, mandatory).unwrap_or_else(|error| {
        problems.0.push(error);
        Vec::new()
    })
}

pub fn check(root: &Path, problems: &mut Problems) -> Vec<String> {
    let art = load_csv(root, CATALOG, &["ref_id", "title", "source_url", "rights_status", "repository_status"], problems);
    let (catalog, errors) = validate_catalog(&art);
    problems.0.extend(errors);
    let manifest = load_csv(root, MANIFEST, &["ref_id", "filename", "remote_url", "license", "creator", "auto_fetch"], problems);
    problems.0.extend(validate_manifest(&manifest, &catalog));
    let index = repo::read(root, INDEX, problems);
    problems.expect(index.contains(&format!("Current coverage: **{} catalogue records**", catalog.len())),
        format!("{INDEX} coverage does not match {CATALOG} ({} records)", catalog.len()));
    let rules = repo::read_json(root, RULES, problems);
    problems.0.extend(validate_rules(&rules, Some(&catalog)).into_iter().map(|e| format!("{RULES}: {e}")));
    let imagery = repo::read_json(root, IMAGERY, problems);
    problems.0.extend(validate_imagery(&imagery).into_iter().map(|e| format!("{IMAGERY}: {e}")));
    for file in media_binaries(root) {
        problems.0.push(format!("unexpected binary in research-only archive: {file}"));
    }
    // Integration needs both workflows green on the same revision, so neither may skip a commit by path.
    let archive_ci = repo::read(root, ARCHIVE_WORKFLOW, problems);
    problems.expect(archive_ci.contains("-- validate history") && archive_ci.contains("history::"),
        format!("{ARCHIVE_WORKFLOW} must run `mb2 validate history` and the history unit tests"));
    for (workflow, text) in [(ARCHIVE_WORKFLOW, &archive_ci), (SOURCE_WORKFLOW, &repo::read(root, SOURCE_WORKFLOW, problems))] {
        problems.expect(!text.contains("paths:") && !text.contains("paths-ignore:") && text.contains("push:") && text.contains("pull_request:"),
            format!("{workflow} must run on every push and pull request, without path filters"));
    }
    vec![format!(
        "Historical archives OK: {} art records, {} manifest entries, {} evidence rules, {} historical imagery/map products; no media binaries; archive and source CI run on every revision.",
        catalog.len(), manifest.len(), rules["rules"].as_array().map_or(0, Vec::len), imagery["products"].as_array().map_or(0, Vec::len)
    )]
}

/// `path` as a plain relative path (no root, no `..`), so a manifest row cannot write outside the download directory.
fn safe_relative(path: &str) -> Option<PathBuf> {
    let candidate = Path::new(path);
    let plain = !path.is_empty() && candidate.components().all(|c| matches!(c, Component::Normal(_)));
    plain.then(|| candidate.to_path_buf())
}

fn sha256_file(path: &Path) -> Result<String, String> {
    let bytes = std::fs::read(path).map_err(|error| format!("cannot read {}: {error}", path.display()))?;
    Ok(Sha256::digest(&bytes).iter().map(|b| format!("{b:02x}")).collect())
}

/// HTTPS only, also across redirects; curl writes to a temporary name so a failed transfer leaves no file behind.
fn download(url: &str, target: &Path) -> Result<(), String> {
    if let Some(parent) = target.parent() {
        std::fs::create_dir_all(parent).map_err(|error| format!("cannot create {}: {error}", parent.display()))?;
    }
    let partial = target.with_extension("part");
    let status = Command::new("curl")
        .args(["--fail", "--silent", "--show-error", "--location", "--proto", "=https", "--proto-redir", "=https", "--max-time", "60",
            "--user-agent", USER_AGENT, "--output"])
        .arg(&partial)
        .arg(url)
        .status()
        .map_err(|error| format!("cannot run curl: {error}"))?;
    if !status.success() {
        let _ = std::fs::remove_file(&partial);
        return Err(format!("curl failed ({status})"));
    }
    std::fs::rename(&partial, target).map_err(|error| format!("cannot move {} into place: {error}", partial.display()))
}

/// `mb2 fetch-art-reference [--output <dir>] [--force]`: download the `auto_fetch=yes` rows of the licensed manifest
/// (default `~/marburgum-data/art-reference/licensed`, outside Git) and write `CHECKSUMS.sha256`. Every other row
/// needs manual rights review and is skipped.
pub fn fetch(args: &[&str]) -> u8 {
    let mut output = None;
    let mut force = false;
    let mut rest = args.iter();
    while let Some(arg) = rest.next() {
        match *arg {
            "--force" => force = true,
            "--output" => match rest.next() {
                Some(dir) => output = Some(PathBuf::from(dir)),
                None => return usage(),
            },
            _ => return usage(),
        }
    }
    let root = repo::root();
    let output = match output {
        Some(dir) if dir.is_absolute() => dir,
        Some(dir) => root.join(dir),
        None => match std::env::var_os("HOME") {
            Some(home) => PathBuf::from(home).join("marburgum-data/art-reference/licensed"),
            None => {
                eprintln!("HOME is not set; pass --output <dir>");
                return 2;
            }
        },
    };
    let mut problems = Problems::default();
    let manifest = load_csv(&root, MANIFEST, &["ref_id", "filename", "remote_url", "auto_fetch"], &mut problems);
    if !problems.is_empty() {
        eprintln!("{}", problems.0.join("\n"));
        return 1;
    }
    let mut checksums = Vec::new();
    for row in &manifest {
        let id = row.get("ref_id");
        if !row.get("auto_fetch").trim().eq_ignore_ascii_case("yes") {
            println!("skip {id}: manual review required");
            continue;
        }
        let (Some(relative), true) = (safe_relative(row.get("filename")), row.get("remote_url").starts_with("https://")) else {
            eprintln!("ERROR {id}: filename must stay inside the download directory and the URL must be https");
            return 1;
        };
        let target = output.join(&relative);
        if target.exists() && !force {
            println!("keep {id}: {}", relative.display());
        } else {
            println!("fetch {id}: {}", row.get("remote_url"));
            if let Err(error) = download(row.get("remote_url"), &target) {
                eprintln!("ERROR {id}: {error}");
                return 1;
            }
        }
        match sha256_file(&target) {
            Ok(digest) => checksums.push(format!("{digest}  {}  # {id}\n", relative.to_string_lossy().replace('\\', "/"))),
            Err(error) => {
                eprintln!("ERROR {id}: {error}");
                return 1;
            }
        }
    }
    let checksum_path = output.join("CHECKSUMS.sha256");
    if let Err(error) = std::fs::create_dir_all(&output).and_then(|()| std::fs::write(&checksum_path, checksums.concat())) {
        eprintln!("cannot write {}: {error}", checksum_path.display());
        return 1;
    }
    println!("wrote {}", checksum_path.display());
    0
}

fn usage() -> u8 {
    eprintln!("Usage: mb2 fetch-art-reference [--output <dir>] [--force]");
    2
}

#[cfg(test)]
mod tests {
    //! Ported from the archive's former Python tests: the rule and imagery contracts, and the 20 November 2026
    //! research expansion, which must not invent rights, georeferencing or 1990s evidence.
    use super::*;
    use serde_json::json;

    fn root() -> PathBuf {
        Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
    }

    fn committed(relative: &str) -> Value {
        serde_json::from_str(&std::fs::read_to_string(root().join(relative)).unwrap()).unwrap()
    }

    fn committed_csv(relative: &str) -> Vec<Row> {
        parse_csv(relative, &std::fs::read_to_string(root().join(relative)).unwrap(), &["ref_id"]).unwrap()
    }

    fn catalog_ids() -> HashSet<String> {
        let (ids, errors) = validate_catalog(&committed_csv(CATALOG));
        assert_eq!(errors, Vec::<String>::new());
        ids
    }

    fn only(id: &str) -> HashSet<String> {
        HashSet::from([id.to_string()])
    }

    fn valid_store() -> Value {
        json!({
            "schema_version": 1,
            "game_period": { "start_year": 1990, "end_year": 1999 },
            "source_catalog": "art_reference/CATALOG.csv",
            "rules": [{
                "id": "test-rule", "kind": "art_direction", "domain": "signage", "location": "Maribor",
                "years": { "start": 1990, "end": 1990 }, "confidence": "high", "review_required": false,
                "ref_ids": ["MAR-TEST-001"], "requirements": ["use the evidenced format"],
                "prohibitions": ["do not over-read the source"], "review_targets": ["signage"]
            }]
        })
    }

    fn mentions(errors: &[String], needle: &str) -> bool {
        errors.iter().any(|e| e.contains(needle))
    }

    const NEW_ART_IDS: [&str; 10] = [
        "MAR-1992-UNION-OTROK-OPENING-001", "MAR-1992-OTROK-FESTIVAL-PROGRAM-001", "MAR-1992-OTROK-KRAMBERGER-001",
        "MAR-1995-96-OTROK-FESTIVAL-PROGRAM-001", "MAR-1993-JEKLOTEHNA-MERKUR-PRESS-001", "MAR-1997-ODPRTA-PLESNA-SCENA-006",
        "MAR-1992-OREHOVA-VAS-DIRKA-001", "MAR-1998-FRIEDRIGER-SCHOOL-STUDY-001", "MAR-1999-FRIEDRIGER-UGM-EXHIBITION-001",
        "MAR-1994-CERNIGOJ-MUSEUM-EXHIBITION-001",
    ];
    const NEW_MAP_IDS: [&str; 3] = [
        "gurs-cas-1975-2005-historical-air-photos", "gurs-dof5-initial-1994-1995", "si-as-177-styrian-franciscan-cadastral-1823-1869",
    ];

    #[test]
    fn the_committed_archives_pass_the_whole_check() {
        let mut problems = Problems::default();
        check(&root(), &mut problems);
        assert_eq!(problems.0, Vec::<String>::new());
    }

    #[test]
    fn a_minimal_rule_is_valid() {
        assert_eq!(validate_rules(&valid_store(), Some(&only("MAR-TEST-001"))), Vec::<String>::new());
    }

    #[test]
    fn a_rule_citing_a_missing_catalogue_record_fails() {
        assert!(mentions(&validate_rules(&valid_store(), Some(&only("MAR-OTHER-001"))), "not found in CATALOG.csv"));
    }

    #[test]
    fn rule_years_stay_inside_the_playable_decade() {
        let mut store = valid_store();
        store["rules"][0]["years"] = json!({ "start": 1989, "end": 1990 });
        assert!(mentions(&validate_rules(&store, Some(&only("MAR-TEST-001"))), "outside 1990-1999"));
    }

    #[test]
    fn requirements_and_prohibitions_are_not_optional() {
        let mut store = valid_store();
        store["rules"][0]["requirements"] = json!([]);
        store["rules"][0]["prohibitions"] = json!([]);
        let errors = validate_rules(&store, Some(&only("MAR-TEST-001")));
        assert!(mentions(&errors, "requirements") && mentions(&errors, "prohibitions"), "{errors:?}");
    }

    #[test]
    fn review_required_is_a_boolean() {
        let mut store = valid_store();
        store["rules"][0]["review_required"] = json!("yes");
        assert!(mentions(&validate_rules(&store, Some(&only("MAR-TEST-001"))), "review_required"));
    }

    #[test]
    fn duplicate_rule_ids_fail() {
        let mut store = valid_store();
        let copy = store["rules"][0].clone();
        store["rules"].as_array_mut().unwrap().push(copy);
        assert!(mentions(&validate_rules(&store, Some(&only("MAR-TEST-001"))), "duplicate id"));
    }

    #[test]
    fn the_committed_rules_resolve_against_the_archive() {
        let store = committed(RULES);
        assert_eq!(validate_rules(&store, Some(&catalog_ids())), Vec::<String>::new());
        assert_eq!(store["rules"].as_array().unwrap().len(), 5);
    }

    #[test]
    fn the_committed_imagery_catalogue_is_valid() {
        let catalogue = committed(IMAGERY);
        assert_eq!(validate_imagery(&catalogue), Vec::<String>::new());
        assert!(!catalogue["products"].as_array().unwrap().is_empty());
    }

    #[test]
    fn an_imagery_product_missing_a_field_fails() {
        let mut catalogue = committed(IMAGERY);
        catalogue["products"][0].as_object_mut().unwrap().remove("resolution_m");
        assert!(mentions(&validate_imagery(&catalogue), "resolution_m"));
    }

    #[test]
    fn an_invented_non_positive_accuracy_fails() {
        let mut catalogue = committed(IMAGERY);
        catalogue["products"][0]["georeference_accuracy_m"] = json!(-1);
        assert!(!validate_imagery(&catalogue).is_empty());
    }

    #[test]
    fn duplicate_imagery_ids_fail() {
        let mut catalogue = committed(IMAGERY);
        let copy = catalogue["products"][0].clone();
        catalogue["products"].as_array_mut().unwrap().push(copy);
        assert!(mentions(&validate_imagery(&catalogue), "duplicate"));
    }

    #[test]
    fn new_art_records_have_unique_ids_and_review_only_rights() {
        let art = committed_csv(CATALOG);
        let by_id: HashMap<&str, &Row> = art.iter().map(|r| (r.get("ref_id"), r)).collect();
        assert_eq!(by_id.len(), art.len(), "duplicate art reference IDs");
        assert!(art.len() >= 246);
        for id in NEW_ART_IDS {
            let record = by_id.get(id).unwrap_or_else(|| panic!("{id} missing"));
            assert!(record.get("source_url").starts_with("https://"), "{id}");
            assert!(["CC-BY-NC", "copyrighted-reference-only"].contains(&record.get("rights_status")), "{id}");
            assert!(["catalogued-link", "bibliographic-lead", "catalogued-collection"].contains(&record.get("repository_status")), "{id}");
            assert!(!record.get("date_start").is_empty() && !record.get("date_end").is_empty(), "{id}");
            assert!(!record.get("notes").trim().is_empty(), "{id}");
        }
    }

    #[test]
    fn new_restricted_items_are_indexed() {
        let index = std::fs::read_to_string(root().join(INDEX)).unwrap();
        assert!(index.contains("246 catalogue records") && index.contains("240 remain reference-only"));
        for id in NEW_ART_IDS {
            assert!(index.contains(id), "{id} not in {INDEX}");
        }
    }

    #[test]
    fn new_map_leads_do_not_invent_georeferencing() {
        let catalogue = committed(IMAGERY);
        let products = catalogue["products"].as_array().unwrap();
        let by_id: HashMap<&str, &Value> = products.iter().map(|p| (p["id"].as_str().unwrap(), p)).collect();
        assert_eq!(by_id.len(), products.len());
        assert!(products.len() >= 6);
        for id in NEW_MAP_IDS {
            let item = by_id.get(id).unwrap_or_else(|| panic!("{id} missing"));
            assert_eq!(item["verified"], false, "{id}");
            assert!(item["georeference_accuracy_m"].is_null() && item["resolution_m"].is_null(), "{id}");
            assert_eq!(item["license"], "unknown", "{id}");
            assert!(item["source_urls"].as_array().unwrap().iter().all(|u| u.as_str().unwrap().starts_with("https://")), "{id}");
        }
    }

    #[test]
    fn the_older_cadastre_is_not_treated_as_nineties_proof() {
        let catalogue = committed(IMAGERY);
        let item = catalogue["products"].as_array().unwrap().iter()
            .find(|p| p["id"] == "si-as-177-styrian-franciscan-cadastral-1823-1869").unwrap();
        assert_eq!(item["date_range"], json!({ "start": "1823", "end": "1869" }));
        assert!(item["access"].as_str().unwrap().to_lowercase().contains("not evidence"));
    }

    #[test]
    fn the_manifest_cites_the_catalogue_and_fetches_only_over_https() {
        let header = "ref_id,filename,remote_url,license,creator,auto_fetch\n";
        let rows = |body: &str| parse_csv("m", &format!("{header}{body}"), &[]).unwrap();
        let ids = only("MAR-A");
        assert!(validate_manifest(&rows("MAR-A,a/x.jpg,https://e/x,CC0,c,yes\n"), &ids).is_empty());
        assert!(validate_manifest(&rows("MAR-2026-USER-1,u/x.jpg,,CC-BY-4.0,c,no\n"), &ids).is_empty());
        assert!(mentions(&validate_manifest(&rows("MAR-2026-USER-1,u/x.jpg,https://e/x,CC0,c,yes\n"), &ids), "untracked"));
        assert!(mentions(&validate_manifest(&rows("MAR-A,a/x.jpg,http://e/x,CC0,c,yes\n"), &ids), "https"));
        assert!(mentions(&validate_manifest(&rows("MAR-A,a/x.jpg,https://e/x,CC0,c,maybe\n"), &ids), "auto_fetch"));
        assert!(mentions(&validate_manifest(&rows("MAR-A,../x.jpg,https://e/x,CC0,c,yes\n"), &ids), "filename"));
        assert!(mentions(&validate_manifest(&rows("MAR-A,a/x.jpg,https://e/x,CC0,c,yes\nMAR-A,a/y.jpg,https://e/y,CC0,c,yes\n"), &ids), "duplicate"));
    }

    #[test]
    fn a_short_or_long_csv_row_is_malformed() {
        assert!(parse_csv("c", "ref_id,title\nA\n", &[]).unwrap_err().contains("malformed"));
        assert!(parse_csv("c", "ref_id,title\nA,b,c\n", &[]).unwrap_err().contains("malformed"));
        assert!(parse_csv("c", "ref_id\n", &[]).unwrap_err().contains("empty"));
        assert!(parse_csv("c", "\u{feff}title\nA\n", &["ref_id"]).unwrap_err().contains("missing required headers"));
        assert_eq!(parse_csv("c", "\u{feff}ref_id\nA\n", &["ref_id"]).unwrap()[0].get("ref_id"), "A");
    }

    #[test]
    fn download_paths_stay_inside_the_output_directory() {
        assert!(safe_relative("1990s/a.jpg").is_some());
        for bad in ["", "/etc/passwd", "../a.jpg", "1990s/../../a.jpg"] {
            assert!(safe_relative(bad).is_none(), "{bad}");
        }
    }
}
