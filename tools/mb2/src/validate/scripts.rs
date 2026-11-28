//! Every `scripts/*.sh` is executable in the checkout. Packaging and the smokes call each other directly
//! (`package-linux.sh` runs `build-stamp.sh`), so a script committed without its mode bit fails with
//! "Permission denied" only at the end of a twenty-minute package. And every script that can run the staged
//! (packaged) builds records their build stamp, so its evidence names the build it ran. Each has a PowerShell twin for
//! Windows (ADR-093) that does the same.

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
    let twins = check_windows_twins(root, &names, problems);
    vec![format!(
        "Scripts: {} shell scripts under scripts/, all executable; {staged} staged runners record the build stamp; {} Windows twins (ADR-093), {twins} staged.",
        names.len(),
        names.len()
    )]
}

/// The Windows twin of a shell script (ADR-093): `x-linux.sh` -> `x-windows.ps1`, any other `x.sh` -> `x.ps1`.
fn windows_twin(name: &str) -> String {
    match name.strip_suffix("-linux.sh") {
        Some(stem) => format!("{stem}-windows.ps1"),
        None => format!("{}.ps1", name.trim_end_matches(".sh")),
    }
}

/// Every shell script has its PowerShell twin, and every twin that runs staged builds records their stamp too.
fn check_windows_twins(root: &Path, names: &[String], problems: &mut Problems) -> usize {
    let mut staged = 0;
    for name in names {
        let twin = windows_twin(name);
        let path = root.join("scripts").join(&twin);
        let Ok(body) = std::fs::read_to_string(&path) else {
            problems.0.push(format!("scripts/{name} has no Windows twin scripts/{twin}"));
            continue;
        };
        // Wrappers that call another twin (daylight runs weather's) inherit its stamp.
        if body.contains("Resolve-SmokeTargets") && !body.contains("& (Join-Path $PSScriptRoot '") {
            staged += 1;
            problems.expect(
                body.contains("Add-BuildStampCheck"),
                format!("scripts/{twin} runs staged builds but never records build-stamp.ps1 check (Add-BuildStampCheck)"),
            );
        }
    }
    staged
}

#[cfg(test)]
mod tests {
    use super::windows_twin;

    #[test]
    fn twins_are_named_after_the_shell_script() {
        assert_eq!(windows_twin("net-smoke-linux.sh"), "net-smoke-windows.ps1");
        assert_eq!(windows_twin("build-stamp.sh"), "build-stamp.ps1");
        assert_eq!(windows_twin("create-maps.sh"), "create-maps.ps1");
    }
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
