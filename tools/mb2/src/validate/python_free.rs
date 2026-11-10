//! Repository tooling is Rust, C# and C++ only: no Python sources, no Python calls from scripts or CI,
//! and the editor's Python plugins stay disabled (map generation is a C++ commandlet).

use std::path::Path;

use crate::repo::{read, read_json, Problems};

/// Generated, local or third-party trees that are not repository tooling.
const SKIPPED: [&str; 10] = [".git", "Binaries", "Intermediate", "Saved", "DerivedDataCache", "target", "bin", "obj", "Licensed", "node_modules"];

fn walk(dir: &Path, out: &mut Vec<std::path::PathBuf>) {
    let Ok(entries) = std::fs::read_dir(dir) else { return };
    for entry in entries.flatten() {
        let path = entry.path();
        let name = entry.file_name().to_string_lossy().to_string();
        if path.is_dir() {
            if !SKIPPED.contains(&name.as_str()) {
                walk(&path, out);
            }
        } else {
            out.push(path);
        }
    }
}

pub fn check(root: &Path, problems: &mut Problems) -> Vec<String> {
    let mut files = Vec::new();
    walk(root, &mut files);
    let mut scripts = 0;
    for path in &files {
        let relative = path.strip_prefix(root).unwrap_or(path).to_string_lossy().replace('\\', "/");
        problems.expect(path.extension().and_then(|e| e.to_str()) != Some("py"), format!("Python source in the repository: {relative}"));
        let is_script = relative.starts_with("scripts/") && relative.ends_with(".sh");
        let is_workflow = relative.starts_with(".github/workflows/");
        if is_script || is_workflow {
            scripts += 1;
            let body = std::fs::read_to_string(path).unwrap_or_default();
            problems.expect(!body.contains("python"), format!("{relative} calls Python"));
        }
    }
    let project = read_json(root, "MarburgumII.uproject", problems);
    for plugin in ["PythonScriptPlugin", "EditorScriptingUtilities"] {
        let entry = project["Plugins"].as_array().into_iter().flatten().find(|p| p["Name"] == plugin);
        problems.expect(entry.map(|p| p["Enabled"] == false).unwrap_or(false), format!("{plugin} must be explicitly disabled in MarburgumII.uproject"));
    }
    let create_maps = read(root, "scripts/create-maps.sh", problems);
    problems.expect(create_maps.contains("-run=MarburgumCreateMaps"), "scripts/create-maps.sh must use the C++ map commandlet");
    vec![format!("Python-free tooling: no .py sources, {scripts} shell scripts and workflows checked, editor Python plugins disabled.")]
}
