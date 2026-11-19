//! Every `scripts/*.sh` is executable in the checkout. Packaging and the smokes call each other directly
//! (`package-linux.sh` runs `build-stamp.sh`), so a script committed without its mode bit fails with
//! "Permission denied" only at the end of a twenty-minute package.

use std::path::Path;

use crate::repo::Problems;

pub fn check(root: &Path, problems: &mut Problems) -> Vec<String> {
    let Ok(entries) = std::fs::read_dir(root.join("scripts")) else {
        problems.0.push("missing: scripts/".to_string());
        return Vec::new();
    };
    let mut names: Vec<String> = entries
        .flatten()
        .map(|entry| entry.file_name().to_string_lossy().to_string())
        .filter(|name| name.ends_with(".sh"))
        .collect();
    names.sort();
    for name in &names {
        problems.expect(executable(&root.join("scripts").join(name)), format!("scripts/{name} is not executable (git update-index --chmod=+x)"));
    }
    vec![format!("Scripts: {} shell scripts under scripts/, all executable.", names.len())]
}

#[cfg(unix)]
fn executable(path: &Path) -> bool {
    use std::os::unix::fs::PermissionsExt;
    std::fs::metadata(path).map(|meta| meta.permissions().mode() & 0o111 != 0).unwrap_or(false)
}

/// Windows checkouts carry no mode bits; the Linux CI job checks them.
#[cfg(not(unix))]
fn executable(_path: &Path) -> bool {
    true
}
