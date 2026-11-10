//! Locating the repository and reading its files.

use std::path::{Path, PathBuf};

/// The checkout root: `MB2_ROOT` if set, otherwise the nearest ancestor holding `MarburgumII.uproject`,
/// searched from the current directory and then from this executable's location.
pub fn root() -> PathBuf {
    if let Some(explicit) = std::env::var_os("MB2_ROOT") {
        return PathBuf::from(explicit);
    }
    let starts = [std::env::current_dir().ok(), std::env::current_exe().ok()];
    for start in starts.into_iter().flatten() {
        if let Some(found) = start.ancestors().find(|dir| dir.join("MarburgumII.uproject").is_file()) {
            return found.to_path_buf();
        }
    }
    PathBuf::from(".")
}

/// Accumulates check failures so one run reports all of them.
#[derive(Default)]
pub struct Problems(pub Vec<String>);

impl Problems {
    pub fn expect(&mut self, condition: bool, message: impl Into<String>) {
        if !condition {
            self.0.push(message.into());
        }
    }

    pub fn is_empty(&self) -> bool {
        self.0.is_empty()
    }
}

/// A repository file as text; a missing file is recorded as a problem and reads as empty.
pub fn read(root: &Path, relative: &str, problems: &mut Problems) -> String {
    match std::fs::read_to_string(root.join(relative)) {
        Ok(text) => text,
        Err(_) => {
            problems.0.push(format!("missing: {relative}"));
            String::new()
        }
    }
}

/// A repository JSON document; unreadable or invalid documents are recorded and read as null.
pub fn read_json(root: &Path, relative: &str, problems: &mut Problems) -> serde_json::Value {
    let text = read(root, relative, problems);
    if text.is_empty() {
        return serde_json::Value::Null;
    }
    serde_json::from_str(&text).unwrap_or_else(|error| {
        problems.0.push(format!("invalid JSON in {relative}: {error}"));
        serde_json::Value::Null
    })
}
