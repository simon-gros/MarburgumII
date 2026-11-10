//! The installed Unreal Engine matches the pinned version (.unreal-engine-version).

use std::path::Path;

use crate::repo;

/// "5.8.3" from an Engine/Build/Build.version document.
pub fn installed_version(build_version: &serde_json::Value) -> Option<String> {
    let part = |key: &str| build_version[key].as_u64().map(|n| n.to_string());
    Some(format!("{}.{}.{}", part("MajorVersion")?, part("MinorVersion")?, part("PatchVersion")?))
}

pub fn run(engine_root: &Path) -> u8 {
    let build_file = engine_root.join("Engine/Build/Build.version");
    let pin_file = repo::root().join(".unreal-engine-version");
    let required = match std::fs::read_to_string(&pin_file) {
        Ok(text) => text.trim().to_string(),
        Err(error) => {
            eprintln!("ERROR: cannot read {}: {error}", pin_file.display());
            return 1;
        }
    };
    let actual = std::fs::read_to_string(&build_file).ok()
        .and_then(|text| serde_json::from_str(&text).ok())
        .and_then(|doc| installed_version(&doc));
    match actual {
        Some(actual) if actual == required => {
            println!("Verified Unreal Engine {actual} from {}", build_file.display());
            0
        }
        Some(actual) => {
            eprintln!("ERROR: Unreal Engine {actual} installed; Marburgum II requires {required}.");
            1
        }
        None => {
            eprintln!("ERROR: unreadable Unreal Engine build metadata: {}", build_file.display());
            1
        }
    }
}
