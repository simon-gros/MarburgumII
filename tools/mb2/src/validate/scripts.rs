//! Every `scripts/*.sh` is executable in the checkout. Packaging and the smokes call each other directly
//! (`package-linux.sh` runs `build-stamp.sh`), so a script committed without its mode bit fails with
//! "Permission denied" only at the end of a twenty-minute package. And every script that can run the staged
//! (packaged) builds records their build stamp, so its evidence names the build it ran.

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
    let mut staged = 0;
    for name in &names {
        let path = root.join("scripts").join(name);
        problems.expect(
            executable(&path),
            format!("scripts/{name} is not executable (git update-index --chmod=+x)"),
        );
        let body = std::fs::read_to_string(&path).unwrap_or_default();
        // Wrappers that exec another smoke (daylight runs weather's) inherit its stamp.
        if body.contains("    staged)") && !body.contains("exec \"$repo_root/scripts/") {
            staged += 1;
            problems.expect(body.contains("scripts/build-stamp.sh\" check"), format!("scripts/{name} runs staged builds but never records scripts/build-stamp.sh check"));
        }
    }
    vec![format!("Scripts: {} shell scripts under scripts/, all executable; {staged} staged runners record the build stamp.", names.len())]
}

#[cfg(unix)]
fn executable(path: &Path) -> bool {
    use std::os::unix::fs::PermissionsExt;
    std::fs::metadata(path)
        .map(|meta| meta.permissions().mode() & 0o111 != 0)
        .unwrap_or(false)
}

/// Windows checkouts carry no mode bits; the Linux CI job checks them.
#[cfg(not(unix))]
fn executable(_path: &Path) -> bool {
    true
}
