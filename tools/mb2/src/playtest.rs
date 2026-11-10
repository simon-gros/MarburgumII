//! Fail-closed freshness guard for native Unreal 5.8.3 client/server playtests (Docs/PLAYTEST_FRESHNESS.md).
//!
//! It never claims a build exists until both real UAT archives are sealed. A local, ignored profile
//! supplies the actual engine path, import commands, build/cook commands and launch commands.

use std::collections::BTreeMap;
use std::fs::{self, File, OpenOptions};
use std::io::{Read, Write};
use std::os::unix::fs::PermissionsExt;
use std::os::unix::io::AsRawFd;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use regex::Regex;
use serde_json::{json, Map, Value};
use sha2::{Digest, Sha256};

use crate::repo;

const SCHEMA: i64 = 1;
const MANDATORY_ROOTS: [&str; 10] = [
    "Content", "Config", "Data", "Plugins", "Assets", "SourceAssets", "ThirdParty/Licensed", "Content/Licensed",
    "Content/ThirdPartyLicensed", "PrivateAssets",
];
const SKIPPED_DIRS: [&str; 5] = ["Binaries", "Intermediate", "Saved", "DerivedDataCache", ".git"];

/// Why a playtest step refused to proceed.
#[derive(Debug)]
pub struct Blocked(pub String);

type Outcome<T> = Result<T, Blocked>;

fn blocked<T>(message: impl Into<String>) -> Outcome<T> {
    Err(Blocked(message.into()))
}

/// The guard's view of one checkout; tests point it at a temporary directory.
pub struct Guard {
    root: PathBuf,
}

impl Guard {
    pub fn new(root: PathBuf) -> Self {
        Self { root }
    }

    fn playtests(&self) -> PathBuf {
        self.root.join("Saved").join("Playtests")
    }

    // ---- commands and git ---------------------------------------------------------------------

    fn shell(&self, args: &[&str]) -> Outcome<String> {
        let output = Command::new(args[0]).args(&args[1..]).current_dir(&self.root)
            .env("GIT_TERMINAL_PROMPT", "0").output()
            .map_err(|error| Blocked(format!("Required command unavailable: {} ({error})", args[0])))?;
        if !output.status.success() {
            let stderr = String::from_utf8_lossy(&output.stderr);
            return blocked(format!("Command failed: {}: {}", args[..args.len().min(3)].join(" "),
                stderr.trim().chars().take(350).collect::<String>()));
        }
        Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
    }

    fn git(&self, args: &[&str]) -> Outcome<String> {
        let mut all = vec!["git"];
        all.extend_from_slice(args);
        self.shell(&all)
    }

    fn git_gate(&self, refresh: bool) -> Outcome<String> {
        if refresh {
            self.git(&["fetch", "--no-tags", "origin", "main"])?;
        }
        if self.git(&["symbolic-ref", "--short", "HEAD"])? != "main" {
            return blocked("Playtest requires a checked-out main branch.");
        }
        let local = self.git(&["rev-parse", "HEAD"])?;
        let remote = self.git(&["rev-parse", "refs/remotes/origin/main"])?;
        if local != remote {
            return blocked(format!("Stale or unpublished main: local={}, origin/main={}. Sync and retry.", &local[..12], &remote[..12]));
        }
        if !self.git(&["status", "--porcelain=v1", "--untracked-files=all"])?.is_empty() {
            return blocked("Working tree contains changes/untracked files. Commit and push source and public assets before playtesting.");
        }
        let submodules = self.git(&["submodule", "status", "--recursive"])?;
        if submodules.lines().any(|line| line.starts_with(['-', '+', 'U'])) {
            return blocked("Submodule not initialized or at the committed revision.");
        }
        if !self.git(&["lfs", "ls-files", "--name-only"])?.is_empty() {
            self.git(&["lfs", "fsck"])?;
        }
        Ok(local)
    }

    // ---- snapshots ----------------------------------------------------------------------------

    fn roots(&self, profile: &Value) -> Outcome<Vec<PathBuf>> {
        let mut paths: Vec<PathBuf> = MANDATORY_ROOTS.iter().map(|r| self.root.join(r)).collect();
        for name in profile["asset_roots"].as_array().into_iter().flatten() {
            match name.as_str() {
                Some(name) if !name.is_empty() => {
                    let path = expand_home(name);
                    paths.push(if path.is_absolute() { path } else { self.root.join(name) });
                }
                _ => return blocked("Invalid additional asset root"),
            }
        }
        let mut unique: Vec<PathBuf> = Vec::new();
        for path in paths {
            if path.is_symlink() {
                return blocked(format!("Linked asset root not permitted: {}", path.display()));
            }
            let resolved = resolve(&path);
            if !unique.contains(&resolved) {
                unique.push(resolved);
            }
        }
        Ok(unique)
    }

    pub fn asset_snapshot(&self, profile: &Value) -> Outcome<Value> {
        let roots = self.roots(profile)?;
        let mut entries = BTreeMap::new();
        for root in &roots {
            for path in walk(root)? {
                let size = fs::metadata(&path).map(|m| m.len()).unwrap_or(0);
                entries.insert(resolve(&path).to_string_lossy().to_string(), file_entry(&path, size)?);
            }
        }
        let files: Map<String, Value> = entries.into_iter().collect();
        let digest = hex(&Sha256::digest(serde_json::to_string(&files).unwrap_or_default().as_bytes()));
        Ok(json!({
            "files": files.clone(), "count": files.len(),
            "roots": roots.iter().map(|r| r.to_string_lossy().to_string()).collect::<Vec<_>>(),
            "digest": digest,
        }))
    }

    pub fn archive_snapshot(&self, profile: &Value, values: &BTreeMap<String, String>, run_dir: &Path) -> Outcome<Value> {
        let mut result = Map::new();
        let artifacts = profile["artifact_roots"].as_object().ok_or_else(|| Blocked("artifact_roots must be an object".into()))?;
        for (kind, template) in artifacts {
            let directory = resolve(&expand_home(&format_template(template.as_str().unwrap_or(""), values)?));
            if !directory.starts_with(run_dir) || directory == run_dir {
                return blocked(format!("Archive must be located within this new run: {}", directory.display()));
            }
            if !directory.is_dir() {
                return blocked(format!("Missing {kind} packaged directory: {}", directory.display()));
            }
            let mut files = Map::new();
            let mut cooked = false;
            let mut executable = false;
            for path in walk(&directory)? {
                let relative = path.strip_prefix(&directory).unwrap_or(&path).to_string_lossy().to_string();
                let meta = fs::metadata(&path).map_err(|e| Blocked(e.to_string()))?;
                cooked |= [".pak", ".utoc", ".ucas", ".uasset"].iter().any(|ext| relative.ends_with(ext));
                executable |= meta.permissions().mode() & 0o111 != 0;
                files.insert(relative, file_entry(&path, meta.len())?);
            }
            if files.is_empty() || !cooked {
                return blocked(format!("Archive has no cooked game assets: {kind}"));
            }
            if !executable {
                return blocked(format!("Archive has no executable: {kind}"));
            }
            result.insert(kind.clone(), json!({ "root": directory.to_string_lossy(), "files": files }));
        }
        Ok(Value::Object(result))
    }

    fn clean_cook(&self) -> Outcome<()> {
        for sub in ["Saved/Cooked", "Saved/StagedBuilds", "Saved/Sandboxes"] {
            let path = self.root.join(sub);
            if path.is_symlink() {
                return blocked(format!("Refusing to clear symlink: {}", path.display()));
            }
            if path.is_dir() {
                fs::remove_dir_all(&path).map_err(|e| Blocked(format!("Cannot clear {}: {e}", path.display())))?;
            }
        }
        Ok(())
    }

    fn variables(&self, engine: &Path, run_dir: &Path, sha: &str, session: &str) -> BTreeMap<String, String> {
        BTreeMap::from([
            ("repo".to_string(), self.root.to_string_lossy().to_string()),
            ("project_file".to_string(), self.root.join("MarburgumII.uproject").to_string_lossy().to_string()),
            ("engine_root".to_string(), engine.to_string_lossy().to_string()),
            ("run_dir".to_string(), run_dir.to_string_lossy().to_string()),
            ("commit".to_string(), sha.to_string()),
            ("session_id".to_string(), session.to_string()),
        ])
    }

    fn run_stage(&self, commands: &Value, label: &str, values: &BTreeMap<String, String>, run_dir: &Path) -> Outcome<()> {
        for (index, raw) in commands.as_array().into_iter().flatten().enumerate() {
            let args = format_argv(values, raw)?;
            let log = run_dir.join("logs").join(format!("{label}-{:02}.log", index + 1));
            fs::create_dir_all(log.parent().unwrap()).map_err(|e| Blocked(e.to_string()))?;
            let mut out = File::create(&log).map_err(|e| Blocked(e.to_string()))?;
            writeln!(out, "Stage={label} step={}\nArgs: {}", index + 1, json!(args)).map_err(|e| Blocked(e.to_string()))?;
            let status = Command::new(&args[0]).args(&args[1..]).current_dir(&self.root)
                .env("UE_ROOT", &values["engine_root"])
                .stdout(out.try_clone().map_err(|e| Blocked(e.to_string()))?).stderr(out)
                .status().map_err(|e| Blocked(format!("Cannot run {}: {e}", args[0])))?;
            if !status.success() {
                return blocked(format!("{label}/{} failed (exit {}); see {}", index + 1, status.code().unwrap_or(-1), log.display()));
            }
        }
        Ok(())
    }

    // ---- actions ------------------------------------------------------------------------------

    fn build(&self, profile: &Value) -> Outcome<()> {
        let sha = self.git_gate(true)?;
        let engine = resolve(&expand_home(profile["engine_root"].as_str().unwrap_or("")));
        let engine_data = engine_gate(&engine)?;
        let protected_sources = ["ThirdParty/Licensed", "PrivateAssets"].iter()
            .any(|folder| walk(&self.root.join(folder)).map(|f| !f.is_empty()).unwrap_or(true))
            || profile["asset_roots"].as_array().into_iter().flatten().filter_map(Value::as_str)
                .filter(|p| Path::new(p).is_absolute()).any(|p| walk(Path::new(p)).map(|f| !f.is_empty()).unwrap_or(true));
        if protected_sources && profile["import_commands"].as_array().map(Vec::is_empty).unwrap_or(true) {
            return blocked("Protected source assets exist but import_commands are empty; cannot prove the newest raw sources reached cooked Unreal assets.");
        }
        let session = format!("{}-{}-{}", utc_compact(), &sha[..8], &uuid::Uuid::new_v4().simple().to_string()[..8]);
        let run_dir = self.playtests().join(&session);
        if run_dir.exists() {
            return blocked("Playtest directory already exists");
        }
        fs::create_dir_all(&run_dir).map_err(|e| Blocked(e.to_string()))?;
        let run_dir = resolve(&run_dir);
        let values = self.variables(&engine, &run_dir, &sha, &session);
        save_json(&run_dir.join("build_state.json"), &json!({ "status": "building", "commit": sha, "session": session }))?;
        let attempt = || -> Outcome<()> {
            if profile["import_commands"].as_array().map(|c| !c.is_empty()).unwrap_or(false) {
                self.run_stage(&profile["import_commands"], "import", &values, &run_dir)?;
            }
            if self.git_gate(false)? != sha {
                return blocked("Revision or tracked assets changed during import");
            }
            let before = self.asset_snapshot(profile)?;
            self.run_stage(&profile["build_commands"], "build", &values, &run_dir)?;
            self.clean_cook()?;
            self.run_stage(&profile["cook_commands"], "cook", &values, &run_dir)?;
            if self.git_gate(true)? != sha {
                return blocked("New upstream revision appeared while building");
            }
            assert_same(&before, &self.asset_snapshot(profile)?, "Source/asset snapshot")?;
            if engine_gate(&engine)? != engine_data {
                return blocked("Engine changed while building/cooking");
            }
            let artifacts = self.archive_snapshot(profile, &values, &run_dir)?;
            let seal = json!({
                "schema": SCHEMA, "status": "sealed", "session_id": session, "commit": sha, "engine": engine_data,
                "asset_snapshot": before, "artifacts": artifacts, "run_dir": run_dir.to_string_lossy(),
                "created_utc": utc_iso(), "owner_approval": "pending_personal_playtest",
            });
            save_json(&run_dir.join("build_state.json"), &seal)?;
            save_json(&self.playtests().join("latest.json"), &json!({ "session_id": session, "run_dir": run_dir.to_string_lossy() }))?;
            println!("SEALED: {session} HEAD={sha} asset_inputs={}", before["count"]);
            println!("Next: scripts/mb2.sh playtest launch --profile Saved/PlaytestProfile/profile.json");
            Ok(())
        };
        attempt().inspect_err(|error| {
            let _ = save_json(&run_dir.join("build_state.json"), &json!({ "status": "blocked", "session_id": session, "error": error.0 }));
        })
    }

    fn load_latest(&self) -> Outcome<(Value, PathBuf)> {
        let pointer = self.playtests().join("latest.json");
        let info = read_json(&pointer).map_err(|_| Blocked("No sealed playtest build; run build first".into()))?;
        let run_dir = resolve(Path::new(info["run_dir"].as_str().unwrap_or("")));
        if !run_dir.starts_with(resolve(&self.playtests())) {
            return blocked("Latest manifest points outside playtest storage");
        }
        let state = read_json(&run_dir.join("build_state.json"))?;
        if state["status"] != "sealed" || state["session_id"] != info["session_id"] {
            return blocked("Playtest manifest is not a sealed matching session");
        }
        Ok((state, run_dir))
    }

    fn verify(&self, profile: &Value) -> Outcome<(Value, BTreeMap<String, String>)> {
        let (state, run_dir) = self.load_latest()?;
        let engine = resolve(&expand_home(profile["engine_root"].as_str().unwrap_or("")));
        if engine_gate(&engine)? != state["engine"] {
            return blocked("Engine version/metadata changed since packaging");
        }
        let sha = self.git_gate(true)?;
        let sealed = state["commit"].as_str().unwrap_or("");
        if sha != sealed {
            return blocked(format!("Packaged SHA {} != latest {}", &sealed[..sealed.len().min(12)], &sha[..12]));
        }
        assert_same(&state["asset_snapshot"], &self.asset_snapshot(profile)?, "Asset inputs")?;
        let values = self.variables(&engine, &run_dir, &sha, state["session_id"].as_str().unwrap_or(""));
        if self.archive_snapshot(profile, &values, &run_dir)? != state["artifacts"] {
            return blocked("Client/server archive changed since sealing");
        }
        println!("FRESH: {} commit={} assets={} packaged client+server verified",
            state["session_id"].as_str().unwrap_or(""), &sha[..12], state["asset_snapshot"]["count"]);
        Ok((state, values))
    }

    fn launch(&self, profile: &Value) -> Outcome<()> {
        let (state, values) = self.verify(profile)?;
        let run_dir = PathBuf::from(state["run_dir"].as_str().unwrap_or(""));
        let mut children: Vec<Child> = Vec::new();
        let interrupted = install_interrupt_flag();
        let mut session = || -> Outcome<()> {
            for (index, spec) in profile["launch_commands"].as_array().into_iter().flatten().enumerate() {
                let args = format_argv(&values, &spec["command"])?;
                let executable = resolve(Path::new(&args[0]));
                let kind = spec["role"].as_str().unwrap_or("");
                let allowed = PathBuf::from(state["artifacts"][kind]["root"].as_str().unwrap_or(""));
                if !executable.starts_with(&allowed) || !executable.is_file() {
                    return blocked(format!("{kind} executable is outside its sealed archive: {}", executable.display()));
                }
                let log = run_dir.join("logs").join(format!("runtime-{index:02}-{kind}.log"));
                fs::create_dir_all(log.parent().unwrap()).map_err(|e| Blocked(e.to_string()))?;
                let stream = File::create(&log).map_err(|e| Blocked(e.to_string()))?;
                let child = Command::new(&executable).args(&args[1..]).current_dir(executable.parent().unwrap())
                    .stdout(stream.try_clone().map_err(|e| Blocked(e.to_string()))?).stderr(stream).stdin(Stdio::null())
                    .spawn().map_err(|e| Blocked(format!("Cannot start {kind}: {e}")))?;
                children.push(child);
                if index == 0 {
                    std::thread::sleep(Duration::from_secs(2));
                }
                if let Some(status) = children.last_mut().unwrap().try_wait().map_err(|e| Blocked(e.to_string()))? {
                    return blocked(format!("{kind} process exited immediately: {status}"));
                }
            }
            println!("PLAYTEST STARTED: exact sealed client/server build. Ctrl+C stops all; owner approval is never automatic.");
            loop {
                if interrupted.load(Ordering::SeqCst) {
                    println!("Playtest stopped by user.");
                    return Ok(());
                }
                let server_running = children[0].try_wait().map(|s| s.is_none()).unwrap_or(false);
                let any_client = children[1..].iter_mut().any(|c| c.try_wait().map(|s| s.is_none()).unwrap_or(false));
                if !server_running {
                    return blocked("Dedicated server stopped during playtest");
                }
                if !any_client {
                    return Ok(());
                }
                std::thread::sleep(Duration::from_millis(500));
            }
        };
        let mut failure = session().err().map(|b| b.0);
        stop_all(&mut children);
        if let Err(error) = self.verify(profile) {
            failure = Some(format!("Playtest INVALIDATED by post-session freshness check: {}", error.0));
        }
        save_json(&run_dir.join("owner_playtest_session.json"), &json!({
            "session_id": state["session_id"], "commit": state["commit"],
            "status": if failure.is_none() { "needs_owner_review" } else { "invalid" },
            "reason": failure, "owner_approval": "not_given", "finished_utc": utc_iso(),
        }))?;
        if let Some(failure) = failure {
            return blocked(failure);
        }
        println!("Session ended; personal owner playtest/approval still required.");
        Ok(())
    }

    /// One build or session at a time, across processes.
    fn with_lock<T>(&self, action: impl FnOnce() -> Outcome<T>) -> Outcome<T> {
        fs::create_dir_all(self.playtests()).map_err(|e| Blocked(e.to_string()))?;
        let handle = OpenOptions::new().create(true).append(true).open(self.playtests().join(".lock"))
            .map_err(|e| Blocked(e.to_string()))?;
        // SAFETY: flock on a file descriptor this function owns for the whole locked section.
        if unsafe { libc::flock(handle.as_raw_fd(), libc::LOCK_EX | libc::LOCK_NB) } != 0 {
            return blocked("Another playtest build/session is running");
        }
        let result = action();
        // SAFETY: as above; the descriptor is still open.
        unsafe { libc::flock(handle.as_raw_fd(), libc::LOCK_UN) };
        result
    }
}

// ---- free helpers ---------------------------------------------------------------------------------

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

fn expand_home(path: &str) -> PathBuf {
    match (path.strip_prefix("~/"), std::env::var_os("HOME")) {
        (Some(rest), Some(home)) => PathBuf::from(home).join(rest),
        _ => PathBuf::from(path),
    }
}

/// Canonical path when it exists, otherwise the path made absolute.
fn resolve(path: &Path) -> PathBuf {
    fs::canonicalize(path).unwrap_or_else(|_| std::path::absolute(path).unwrap_or_else(|_| path.to_path_buf()))
}

fn read_json(path: &Path) -> Outcome<Value> {
    let text = fs::read_to_string(path).map_err(|e| Blocked(format!("Cannot read {}: {e}", path.display())))?;
    serde_json::from_str(&text).map_err(|e| Blocked(format!("Invalid JSON in {}: {e}", path.display())))
}

fn save_json(path: &Path, document: &Value) -> Outcome<()> {
    fs::create_dir_all(path.parent().unwrap()).map_err(|e| Blocked(e.to_string()))?;
    let temp = path.with_file_name(format!("{}.tmp", path.file_name().unwrap().to_string_lossy()));
    let text = serde_json::to_string_pretty(&sort_keys(document)).unwrap_or_default() + "\n";
    fs::write(&temp, text).and_then(|_| fs::rename(&temp, path)).map_err(|e| Blocked(e.to_string()))
}

/// Saved documents list keys alphabetically, so successive seals diff cleanly.
fn sort_keys(value: &Value) -> Value {
    match value {
        Value::Object(map) => {
            let sorted: BTreeMap<_, _> = map.iter().map(|(k, v)| (k.clone(), sort_keys(v))).collect();
            Value::Object(sorted.into_iter().collect())
        }
        Value::Array(items) => Value::Array(items.iter().map(sort_keys).collect()),
        other => other.clone(),
    }
}

fn file_entry(path: &Path, size: u64) -> Outcome<Value> {
    let mut hasher = Sha256::new();
    let mut source = File::open(path).map_err(|e| Blocked(format!("Cannot read {}: {e}", path.display())))?;
    let mut buffer = vec![0u8; 1024 * 1024];
    loop {
        let read = source.read(&mut buffer).map_err(|e| Blocked(e.to_string()))?;
        if read == 0 {
            break;
        }
        hasher.update(&buffer[..read]);
    }
    Ok(json!({ "sha256": hex(&hasher.finalize()), "size": size }))
}

/// Files under an asset root, sorted; links anywhere are refused, generated trees skipped.
pub fn walk(root: &Path) -> Outcome<Vec<PathBuf>> {
    if root.is_symlink() {
        return blocked(format!("Linked asset root is not allowed: {}", root.display()));
    }
    if !root.exists() {
        return Ok(Vec::new());
    }
    if !root.is_dir() {
        return blocked(format!("Asset root is not a directory: {}", root.display()));
    }
    let mut files = Vec::new();
    let mut pending = vec![root.to_path_buf()];
    while let Some(dir) = pending.pop() {
        let mut entries: Vec<_> = fs::read_dir(&dir).map_err(|e| Blocked(e.to_string()))?.flatten().map(|e| e.path()).collect();
        entries.sort();
        for path in entries {
            let name = path.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default();
            if path.is_symlink() {
                return blocked(format!("Linked asset {}: {}", if path.is_dir() { "directory" } else { "file" }, path.display()));
            }
            if path.is_dir() {
                if !SKIPPED_DIRS.contains(&name.as_str()) {
                    pending.push(path);
                }
            } else if path.is_file() {
                files.push(path);
            }
        }
    }
    files.sort();
    Ok(files)
}

pub fn assert_same(before: &Value, after: &Value, label: &str) -> Outcome<()> {
    if before == after {
        return Ok(());
    }
    let empty = Map::new();
    let old = before["files"].as_object().unwrap_or(&empty);
    let new = after["files"].as_object().unwrap_or(&empty);
    let differences = old.keys().chain(new.keys()).collect::<std::collections::BTreeSet<_>>().into_iter()
        .filter(|key| old.get(*key) != new.get(*key)).count();
    blocked(format!("{label} changed: {differences} files differ. Rebuild/reimport before testing."))
}

pub fn engine_gate(engine: &Path) -> Outcome<Value> {
    let path = engine.join("Engine/Build/Build.version");
    let raw = fs::read(&path).map_err(|_| Blocked(format!("Missing engine Build.version: {}", path.display())))?;
    let data: Value = serde_json::from_slice(&raw).map_err(|e| Blocked(format!("Invalid {}: {e}", path.display())))?;
    let version = crate::engine::installed_version(&data).unwrap_or_default();
    if version != "5.8.3" {
        return blocked(format!("Unreal {version} is not the required 5.8.3"));
    }
    Ok(json!({ "version": version, "path": engine.to_string_lossy(), "build_version_sha256": hex(&Sha256::digest(&raw)) }))
}

/// `{name}` placeholders in a template; unknown names are refused.
fn format_template(template: &str, values: &BTreeMap<String, String>) -> Outcome<String> {
    let placeholder = Regex::new(r"\{([A-Za-z_][A-Za-z0-9_]*)\}").unwrap();
    let mut unknown = None;
    let text = placeholder.replace_all(template, |c: &regex::Captures| match values.get(&c[1]) {
        Some(value) => value.clone(),
        None => {
            unknown = Some(c[1].to_string());
            String::new()
        }
    }).to_string();
    match unknown {
        Some(name) => blocked(format!("Unknown command variable '{name}'")),
        None => Ok(text),
    }
}

fn format_argv(values: &BTreeMap<String, String>, argv: &Value) -> Outcome<Vec<String>> {
    let items = argv.as_array().filter(|a| !a.is_empty() && a.iter().all(Value::is_string))
        .ok_or_else(|| Blocked("Each command must be a nonempty argv array (no shell strings)".into()))?;
    items.iter().map(|x| format_template(x.as_str().unwrap(), values)).collect()
}

pub fn profile_read(path: &Path) -> Outcome<Value> {
    if !path.is_file() {
        return blocked(format!("Missing local profile: {}. Copy scripts/playtest-profile.example.json into \
            Saved/PlaytestProfile/profile.json and configure it.", path.display()));
    }
    let profile = read_json(path)?;
    if profile["schema"].as_i64() != Some(SCHEMA) {
        return blocked("Profile schema must be 1");
    }
    if profile["engine_root"].as_str().map(str::is_empty).unwrap_or(true) {
        return blocked("Profile must specify the UE 5.8.3 engine_root");
    }
    for key in ["import_commands", "build_commands", "cook_commands", "launch_commands"] {
        if !profile[key].is_array() {
            return blocked(format!("Invalid {key}: expected array"));
        }
    }
    if profile["build_commands"].as_array().unwrap().len() != 2 || profile["cook_commands"].as_array().unwrap().len() != 2 {
        return blocked("Exactly two native builds and two full client/server cooks required");
    }
    let artifact_kinds: Vec<&String> = profile["artifact_roots"].as_object().map(|m| m.keys().collect()).unwrap_or_default();
    if artifact_kinds.len() != 2 || !artifact_kinds.iter().any(|k| *k == "client") || !artifact_kinds.iter().any(|k| *k == "server") {
        return blocked("artifact_roots must identify both client and server archives");
    }
    let mut roles: Vec<&str> = profile["launch_commands"].as_array().unwrap().iter().filter_map(|x| x["role"].as_str()).collect();
    roles.sort_unstable();
    if roles != ["client", "client", "server"] {
        return blocked("launch_commands must start one server and two independent clients");
    }
    if !profile["asset_roots"].is_null() && !profile["asset_roots"].is_array() {
        return blocked("asset_roots must be a list of additional directories");
    }
    Ok(profile)
}

fn stop_all(children: &mut [Child]) {
    for child in children.iter_mut().rev() {
        if matches!(child.try_wait(), Ok(None)) {
            // SAFETY: plain kill(2) on a child this process started and has not yet reaped.
            unsafe { libc::kill(child.id() as libc::pid_t, libc::SIGTERM) };
        }
    }
    for child in children.iter_mut() {
        let deadline = Instant::now() + Duration::from_secs(8);
        while matches!(child.try_wait(), Ok(None)) && Instant::now() < deadline {
            std::thread::sleep(Duration::from_millis(100));
        }
        if matches!(child.try_wait(), Ok(None)) {
            let _ = child.kill();
            let _ = child.wait();
        }
    }
}

static INTERRUPTED: AtomicBool = AtomicBool::new(false);

extern "C" fn on_interrupt(_: libc::c_int) {
    INTERRUPTED.store(true, Ordering::SeqCst);
}

/// Ctrl+C ends the session cleanly (children stopped, post-session freshness check) instead of killing the guard.
fn install_interrupt_flag() -> &'static AtomicBool {
    // SAFETY: the handler only stores to an atomic, which is async-signal-safe.
    unsafe { libc::signal(libc::SIGINT, on_interrupt as *const () as libc::sighandler_t) };
    &INTERRUPTED
}

/// Civil UTC date-time from Unix seconds (proleptic Gregorian; Howard Hinnant's algorithm).
fn civil(seconds: i64) -> (i64, i64, i64, i64, i64, i64) {
    let days = seconds.div_euclid(86_400);
    let rem = seconds.rem_euclid(86_400);
    let z = days + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z - era * 146_097;
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let day = doy - (153 * mp + 2) / 5 + 1;
    let month = if mp < 10 { mp + 3 } else { mp - 9 };
    let year = yoe + era * 400 + i64::from(month <= 2);
    (year, month, day, rem / 3600, rem % 3600 / 60, rem % 60)
}

fn now() -> Duration {
    SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default()
}

fn utc_compact() -> String {
    let (y, mo, d, h, mi, s) = civil(now().as_secs() as i64);
    format!("{y:04}{mo:02}{d:02}T{h:02}{mi:02}{s:02}Z")
}

fn utc_iso() -> String {
    let elapsed = now();
    let (y, mo, d, h, mi, s) = civil(elapsed.as_secs() as i64);
    format!("{y:04}-{mo:02}-{d:02}T{h:02}:{mi:02}:{s:02}.{:06}+00:00", elapsed.subsec_micros())
}

pub fn run(action: &str, profile_path: &Path) -> u8 {
    let guard = Guard::new(repo::root());
    let outcome = profile_read(profile_path).and_then(|profile| guard.with_lock(|| match action {
        "build" => guard.build(&profile),
        "verify" => guard.verify(&profile).map(|_| ()),
        "launch" => guard.launch(&profile),
        other => blocked(format!("Unknown action '{other}'; use build, verify or launch")),
    }));
    match outcome {
        Ok(()) => 0,
        Err(Blocked(message)) => {
            eprintln!("PLAYTEST BLOCKED: {message}");
            2
        }
    }
}

#[cfg(test)]
mod tests {
    //! Source-only, isolated tests of the freshness guard (no Unreal required).
    use super::*;
    use std::os::unix::fs::symlink;

    struct Fixture {
        _dir: tempfile::TempDir,
        root: PathBuf,
        guard: Guard,
    }

    impl Fixture {
        fn new() -> Self {
            let dir = tempfile::tempdir().unwrap();
            let root = fs::canonicalize(dir.path()).unwrap();
            Self { guard: Guard::new(root.clone()), root, _dir: dir }
        }

        fn put(&self, relative: &str, contents: &[u8]) -> PathBuf {
            let path = self.root.join(relative);
            fs::create_dir_all(path.parent().unwrap()).unwrap();
            fs::write(&path, contents).unwrap();
            path
        }

        fn executable(&self, relative: &str) -> PathBuf {
            let path = self.put(relative, b"#!/bin/sh\n");
            fs::set_permissions(&path, fs::Permissions::from_mode(0o755)).unwrap();
            path
        }
    }

    fn no_extra_roots() -> Value {
        json!({ "asset_roots": [] })
    }

    fn run_profile() -> Value {
        json!({ "artifact_roots": { "client": "{run_dir}/client", "server": "{run_dir}/server" } })
    }

    fn run_values(run: &Path) -> BTreeMap<String, String> {
        BTreeMap::from([("run_dir".to_string(), run.to_string_lossy().to_string())])
    }

    #[test]
    fn snapshot_detects_asset_modification_and_deletion() {
        let f = Fixture::new();
        let target = f.put("Content/Licensed/MariborMesh.uasset", b"v1");
        f.put("Data/World/map.json", b"v1");
        let before = f.guard.asset_snapshot(&no_extra_roots()).unwrap();
        fs::write(&target, b"v2").unwrap();
        assert!(assert_same(&before, &f.guard.asset_snapshot(&no_extra_roots()).unwrap(), "assets").is_err());
        fs::remove_file(&target).unwrap();
        assert!(assert_same(&before, &f.guard.asset_snapshot(&no_extra_roots()).unwrap(), "assets").is_err());
    }

    #[test]
    fn snapshot_detects_added_untracked_local_asset() {
        let f = Fixture::new();
        let before = f.guard.asset_snapshot(&no_extra_roots()).unwrap();
        f.put("PrivateAssets/Maribor/model.fbx", b"v1");
        assert!(assert_same(&before, &f.guard.asset_snapshot(&no_extra_roots()).unwrap(), "assets").is_err());
    }

    #[test]
    fn external_asset_root_hashed() {
        let f = Fixture::new();
        let outside = f.put("external/model.fbx", b"first");
        let config = json!({ "asset_roots": [outside.parent().unwrap().to_string_lossy()] });
        let before = f.guard.asset_snapshot(&config).unwrap();
        fs::write(&outside, b"second").unwrap();
        assert!(assert_same(&before, &f.guard.asset_snapshot(&config).unwrap(), "assets").is_err());
    }

    #[test]
    fn symlink_refused() {
        let f = Fixture::new();
        let target = f.put("Assets/real.uasset", b"v1");
        fs::create_dir_all(f.root.join("Content")).unwrap();
        symlink(&target, f.root.join("Content/fake.uasset")).unwrap();
        assert!(f.guard.asset_snapshot(&no_extra_roots()).is_err());
    }

    #[test]
    fn missing_cooked_assets_rejected() {
        let f = Fixture::new();
        let run = f.root.join("Saved/Playtests/run");
        f.executable("Saved/Playtests/run/client/GameClient.sh");
        f.executable("Saved/Playtests/run/server/GameServer.sh");
        assert!(f.guard.archive_snapshot(&run_profile(), &run_values(&run), &run).is_err());
    }

    #[test]
    fn modified_package_invalidates_seal() {
        let f = Fixture::new();
        let run = f.root.join("Saved/Playtests/run");
        for (role, exe) in [("client", "GameClient.sh"), ("server", "GameServer.sh")] {
            f.executable(&format!("Saved/Playtests/run/{role}/{exe}"));
            f.put(&format!("Saved/Playtests/run/{role}/Content.pak"), b"cooked v1");
        }
        let before = f.guard.archive_snapshot(&run_profile(), &run_values(&run), &run).unwrap();
        f.put("Saved/Playtests/run/client/Content.pak", b"stale tampering");
        let after = f.guard.archive_snapshot(&run_profile(), &run_values(&run), &run).unwrap();
        assert_ne!(before, after);
    }

    #[test]
    fn wrong_engine_patch_rejected() {
        let f = Fixture::new();
        f.put("UE/Engine/Build/Build.version", br#"{"MajorVersion":5,"MinorVersion":8,"PatchVersion":2}"#);
        assert!(engine_gate(&f.root.join("UE")).is_err());
        f.put("UE/Engine/Build/Build.version", br#"{"MajorVersion":5,"MinorVersion":8,"PatchVersion":3}"#);
        assert!(engine_gate(&f.root.join("UE")).is_ok());
    }

    #[test]
    fn unknown_command_variable_refused() {
        let values = BTreeMap::from([("repo".to_string(), "/r".to_string())]);
        assert_eq!(format_argv(&values, &json!(["{repo}/x", "-a"])).unwrap(), vec!["/r/x", "-a"]);
        assert!(format_argv(&values, &json!(["{nope}"])).is_err());
        assert!(format_argv(&values, &json!("bash -c x")).is_err());
    }

    #[test]
    fn civil_dates() {
        assert_eq!(civil(0), (1970, 1, 1, 0, 0, 0));
        assert_eq!(civil(1_794_317_741), (2026, 11, 10, 13, 35, 41));
    }
}
