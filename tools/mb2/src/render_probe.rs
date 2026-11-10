//! What this host offers the optional NVIDIA render features (ADR-029), as JSON on stdout.
//!
//! Static: it reads plugin descriptors, driver files and Vulkan layer manifests and asks nvidia-smi
//! for the GPU. Whether a feature actually runs is decided by the game at launch and logged on its
//! MB2RENDER line; this answers what to install before that can happen.

use std::path::{Path, PathBuf};
use std::process::Command;

use serde_json::{json, Value};

use crate::repo;

/// Plugins the game binds at runtime, plus the unofficial DLSS 5 experiment the original project probed for.
const PLUGINS: [&str; 5] = ["DLSS", "StreamlineCore", "StreamlineDLSSG", "StreamlineReflex", "DLSS5ForUE5"];

pub fn run(engine_root: Option<&Path>) -> u8 {
    let project = repo::root();
    let mut roots = vec![project.join("Plugins")];
    if let Some(engine) = engine_root {
        roots.push(engine.join("Engine/Plugins/Marketplace"));
        roots.push(engine.join("Engine/Plugins"));
    }
    let plugins: serde_json::Map<String, Value> = PLUGINS
        .iter()
        .map(|name| (name.to_string(), find_plugin(&roots, name).map_or(Value::Null, |path| plugin_record(&path))))
        .collect();
    let gpus = nvidia_gpus();
    let layers = nv_present_manifests(&layer_dirs());
    let ngx = ngx_runtime();
    let dlss_plugin = &plugins["DLSS"];

    let mut findings = Vec::new();
    if gpus.is_empty() {
        findings.push("no NVIDIA GPU reported by nvidia-smi: every NVIDIA feature resolves to TSR".to_string());
    }
    if dlss_plugin.is_null() {
        findings.push("DLSS plugin not installed: the game runs TSR (download NVIDIA's UE 5.8 DLSS package)".to_string());
    } else if cfg!(target_os = "linux") && !supports_linux(dlss_plugin) {
        findings.push("installed DLSS plugin's DLSS module is not built for Linux: the game reports dlss:unsupported here".to_string());
    }
    if cfg!(target_os = "linux") && !layers.is_empty() {
        findings.push("NVIDIA Smooth Motion layer present: scripts/smooth-motion-linux.sh can request it".to_string());
    }

    let report = json!({
        "schema": 1,
        "host_os": std::env::consts::OS,
        "nvidia_gpus": gpus,
        "ngx_driver_runtime": ngx,
        "plugin_roots": roots.iter().map(|root| root.display().to_string()).collect::<Vec<_>>(),
        "plugins": plugins,
        "smooth_motion": {
            "ownership": "NVIDIA driver (NvPresent), outside the engine",
            "nv_present_manifests": layers,
            "requested_now": std::env::var("NVPRESENT_ENABLE_SMOOTH_MOTION").is_ok_and(|value| value.trim() == "1"),
        },
        "neural_rendering": "no supported UE 5.8 package; not a game setting",
        "findings": findings,
    });
    println!("{}", serde_json::to_string_pretty(&report).unwrap_or_default());
    0
}

/// `<root>/<name>/<name>.uplugin`, otherwise the first match up to three directories deep.
fn find_plugin(roots: &[PathBuf], name: &str) -> Option<PathBuf> {
    let file = format!("{name}.uplugin");
    roots.iter().filter(|root| root.is_dir()).find_map(|root| {
        let direct = root.join(name).join(&file);
        if direct.is_file() {
            return Some(direct);
        }
        search(root, &file, 3)
    })
}

fn search(dir: &Path, file: &str, depth: u32) -> Option<PathBuf> {
    let mut entries: Vec<PathBuf> = std::fs::read_dir(dir).ok()?.flatten().map(|entry| entry.path()).collect();
    entries.sort();
    if let Some(found) = entries.iter().find(|path| path.file_name().is_some_and(|n| n == file) && path.is_file()) {
        return Some(found.clone());
    }
    if depth == 0 {
        return None;
    }
    entries.iter().filter(|path| path.is_dir()).find_map(|path| search(path, file, depth - 1))
}

fn plugin_record(path: &Path) -> Value {
    let descriptor: Value = std::fs::read_to_string(path)
        .ok()
        .and_then(|text| serde_json::from_str(text.trim_start_matches('\u{feff}')).ok())
        .unwrap_or(Value::Null);
    json!({
        "path": path.display().to_string(),
        "version_name": descriptor["VersionName"],
        "engine_version": descriptor["EngineVersion"],
        "module_platforms": module_platforms(&descriptor),
        "main_module_platforms": main_module_platforms(&descriptor, &path.file_stem().unwrap_or_default().to_string_lossy()),
    })
}

/// Union of the modules' PlatformAllowList; "any" when some module has no list.
pub fn module_platforms(descriptor: &Value) -> Vec<String> {
    let mut platforms: Vec<String> = Vec::new();
    for module in descriptor["Modules"].as_array().into_iter().flatten() {
        match module["PlatformAllowList"].as_array() {
            None => platforms.push("any".to_string()),
            Some(list) => platforms.extend(list.iter().filter_map(Value::as_str).map(str::to_string)),
        }
    }
    platforms.sort();
    platforms.dedup();
    platforms
}

/// PlatformAllowList of the module named after the plugin (the one that does the work, not its
/// cross-platform blueprint stubs); "any" when it has none, empty when there is no such module.
pub fn main_module_platforms(descriptor: &Value, plugin: &str) -> Vec<String> {
    let module = descriptor["Modules"].as_array().into_iter().flatten().find(|module| module["Name"] == plugin);
    match module.map(|module| module["PlatformAllowList"].as_array()) {
        None => Vec::new(),
        Some(None) => vec!["any".to_string()],
        Some(Some(list)) => list.iter().filter_map(Value::as_str).map(str::to_string).collect(),
    }
}

fn supports_linux(record: &Value) -> bool {
    record["main_module_platforms"]
        .as_array()
        .into_iter()
        .flatten()
        .any(|platform| matches!(platform.as_str(), Some("Linux" | "any")))
}

fn nvidia_gpus() -> Vec<Value> {
    let output = Command::new("nvidia-smi")
        .args(["--query-gpu=name,driver_version", "--format=csv,noheader,nounits"])
        .output();
    let Some(output) = output.ok().filter(|output| output.status.success()) else {
        return Vec::new();
    };
    String::from_utf8_lossy(&output.stdout)
        .lines()
        .filter_map(|line| line.split_once(','))
        .map(|(name, driver)| json!({ "name": name.trim(), "driver": driver.trim() }))
        .collect()
}

/// The driver's NGX loader, which DLSS needs at run time on Linux.
fn ngx_runtime() -> Value {
    if !cfg!(target_os = "linux") {
        return Value::Null;
    }
    ["/usr/lib", "/usr/lib64", "/usr/lib/x86_64-linux-gnu"]
        .iter()
        .map(|dir| Path::new(dir).join("libnvidia-ngx.so.1"))
        .find(|path| path.exists())
        .map_or(Value::Null, |path| Value::String(path.display().to_string()))
}

fn layer_dirs() -> Vec<PathBuf> {
    let mut dirs: Vec<PathBuf> = ["/etc/vulkan/implicit_layer.d", "/usr/local/share/vulkan/implicit_layer.d", "/usr/share/vulkan/implicit_layer.d"]
        .iter()
        .map(PathBuf::from)
        .collect();
    if let Some(home) = std::env::var_os("HOME") {
        dirs.push(PathBuf::from(home).join(".local/share/vulkan/implicit_layer.d"));
    }
    dirs
}

/// Implicit-layer manifests that declare VK_LAYER_NV_present (NVIDIA Smooth Motion).
pub fn nv_present_manifests(dirs: &[PathBuf]) -> Vec<String> {
    let mut found = Vec::new();
    for dir in dirs {
        let Ok(entries) = std::fs::read_dir(dir) else { continue };
        let mut paths: Vec<PathBuf> = entries.flatten().map(|entry| entry.path()).collect();
        paths.sort();
        for path in paths.into_iter().filter(|path| path.extension().is_some_and(|ext| ext == "json")) {
            let Some(manifest) = std::fs::read_to_string(&path).ok().and_then(|text| serde_json::from_str::<Value>(&text).ok()) else {
                continue;
            };
            let layers = manifest["layers"].as_array().cloned().unwrap_or_default();
            let names = std::iter::once(&manifest["layer"]).chain(layers.iter());
            if names.filter_map(|layer| layer["name"].as_str()).any(|name| name == "VK_LAYER_NV_present") {
                found.push(path.display().to_string());
            }
        }
    }
    found
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn module_platforms_unions_allow_lists() {
        let descriptor = json!({ "Modules": [
            { "Name": "DLSS", "PlatformAllowList": ["Win64"] },
            { "Name": "DLSSBlueprint", "PlatformAllowList": ["Win64", "Linux"] },
        ]});
        assert_eq!(module_platforms(&descriptor), vec!["Linux", "Win64"]);
        assert_eq!(module_platforms(&json!({ "Modules": [{ "Name": "Any" }] })), vec!["any"]);
        assert_eq!(main_module_platforms(&descriptor, "DLSS"), vec!["Win64"]);
        assert!(main_module_platforms(&descriptor, "Other").is_empty());
        assert!(!supports_linux(&json!({ "main_module_platforms": ["Win64"] })));
    }

    #[test]
    fn finds_nv_present_in_single_and_multi_layer_manifests() {
        let dir = tempfile::tempdir().unwrap();
        std::fs::write(dir.path().join("a.json"), r#"{"layer":{"name":"VK_LAYER_NV_optimus"}}"#).unwrap();
        std::fs::write(dir.path().join("b.json"), r#"{"layers":[{"name":"VK_LAYER_NV_optimus"},{"name":"VK_LAYER_NV_present"}]}"#).unwrap();
        std::fs::write(dir.path().join("c.txt"), r#"{"layer":{"name":"VK_LAYER_NV_present"}}"#).unwrap();
        let found = nv_present_manifests(&[dir.path().to_path_buf()]);
        assert_eq!(found.len(), 1);
        assert!(found[0].ends_with("b.json"));
    }

    #[test]
    fn finds_nested_plugin_descriptor() {
        let dir = tempfile::tempdir().unwrap();
        let nested = dir.path().join("Runtime/Nvidia/DLSS");
        std::fs::create_dir_all(&nested).unwrap();
        std::fs::write(nested.join("DLSS.uplugin"), "{}").unwrap();
        assert_eq!(find_plugin(&[dir.path().to_path_buf()], "DLSS"), Some(nested.join("DLSS.uplugin")));
        assert_eq!(find_plugin(&[dir.path().to_path_buf()], "StreamlineCore"), None);
    }
}
