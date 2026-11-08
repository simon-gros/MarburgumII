#!/usr/bin/env python3
"""Fail-closed freshness guard for native Unreal 5.8.3 client/server playtests.

This program never claims a build exists until both real UAT archives are sealed.
A local ignored profile supplies actual engine paths, imports and launch commands.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
PLAYTESTS = ROOT / "Saved" / "Playtests"
SCHEMA = 1
MANDATORY_ROOTS = ("Content", "Config", "Data", "Plugins", "Assets", "SourceAssets",
                   "ThirdParty/Licensed", "Content/Licensed",
                   "Content/ThirdPartyLicensed", "PrivateAssets")


class Blocked(Exception):
    pass


def shell(args, cwd=ROOT, *, timeout=180):
    env = {**os.environ, "GIT_TERMINAL_PROMPT": "0"}
    try:
        result = subprocess.run(args, cwd=cwd, env=env, text=True,
                                capture_output=True, timeout=timeout, check=False)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise Blocked(f"Required command unavailable: {args[0]} ({exc})") from exc
    if result.returncode:
        raise Blocked(f"Command failed: {' '.join(args[:3])}: "
                      f"{result.stderr.strip()[:350]}")
    return result.stdout.strip()


def git(*args):
    return shell(["git", *args])


def git_gate(refresh=True):
    if refresh:
        git("fetch", "--no-tags", "origin", "main")
    if git("symbolic-ref", "--short", "HEAD") != "main":
        raise Blocked("Playtest requires a checked-out main branch.")
    local = git("rev-parse", "HEAD")
    remote = git("rev-parse", "refs/remotes/origin/main")
    if local != remote:
        raise Blocked(f"Stale or unpublished main: local={local[:12]}, "
                      f"origin/main={remote[:12]}. Sync and retry.")
    if git("status", "--porcelain=v1", "--untracked-files=all"):
        raise Blocked("Working tree contains changes/untracked files. "
                      "Commit and push source and public assets before playtesting.")
    submods = git("submodule", "status", "--recursive")
    if any(line.startswith(("-", "+", "U")) for line in submods.splitlines()):
        raise Blocked("Submodule not initialized or at the committed revision.")
    if git("lfs", "ls-files", "--name-only"):
        git("lfs", "fsck")
    return local


def engine_gate(engine):
    path = engine / "Engine" / "Build" / "Build.version"
    if not path.is_file():
        raise Blocked(f"Missing engine Build.version: {path}")
    raw = path.read_bytes()
    data = json.loads(raw)
    version = ".".join(str(data[k]) for k in
                       ("MajorVersion", "MinorVersion", "PatchVersion"))
    if version != "5.8.3":
        raise Blocked(f"Unreal {version} is not the required 5.8.3")
    return {"version": version, "path": str(engine),
            "build_version_sha256": hashlib.sha256(raw).hexdigest()}


def file_hash(path):
    h = hashlib.sha256()
    with path.open("rb") as source:
        for part in iter(lambda: source.read(1024 * 1024), b""):
            h.update(part)
    return h.hexdigest()


def walk(root):
    if not root.exists():
        return
    if root.is_symlink():
        raise Blocked(f"Linked asset root is not allowed: {root}")
    if not root.is_dir():
        raise Blocked(f"Asset root is not a directory: {root}")
    for current, dirs, files in os.walk(root, followlinks=False):
        current = Path(current)
        for dirname in dirs:
            if (current / dirname).is_symlink():
                raise Blocked(f"Linked asset directory: {current / dirname}")
        dirs[:] = sorted(d for d in dirs if d not in
                         {"Binaries", "Intermediate", "Saved", "DerivedDataCache", ".git"})
        for filename in sorted(files):
            path = current / filename
            if path.is_symlink():
                raise Blocked(f"Linked asset file: {path}")
            if path.is_file():
                yield path


def profile_read(path):
    if not path.is_file():
        raise Blocked(f"Missing local profile: {path}. Copy "
                      "scripts/playtest-profile.example.json into "
                      "Saved/PlaytestProfile/profile.json and configure it.")
    obj = json.loads(path.read_text(encoding="utf-8"))
    if obj.get("schema") != SCHEMA:
        raise Blocked("Profile schema must be 1")
    if not obj.get("engine_root"):
        raise Blocked("Profile must specify the UE 5.8.3 engine_root")
    for key in ("import_commands", "build_commands", "cook_commands", "launch_commands"):
        if not isinstance(obj.get(key), list):
            raise Blocked(f"Invalid {key}: expected array")
    if len(obj["build_commands"]) != 2 or len(obj["cook_commands"]) != 2:
        raise Blocked("Exactly two native builds and two full client/server cooks required")
    if not isinstance(obj.get("artifact_roots"), dict) or set(obj["artifact_roots"]) != {"client", "server"}:
        raise Blocked("artifact_roots must identify both client and server archives")
    if sorted(x.get("role") for x in obj["launch_commands"] if isinstance(x, dict)) != ["client", "client", "server"]:
        raise Blocked("launch_commands must start one server and two independent clients")
    if not isinstance(obj.get("asset_roots", []), list):
        raise Blocked("asset_roots must be a list of additional directories")
    return obj


def roots(profile):
    paths = [ROOT / x for x in MANDATORY_ROOTS]
    for name in profile.get("asset_roots", []):
        if not isinstance(name, str) or not name:
            raise Blocked("Invalid additional asset root")
        paths.append(Path(name).expanduser() if Path(name).is_absolute()
                     else ROOT / name)
    result = {}
    for path in paths:
        if path.is_symlink():
            raise Blocked(f"Linked asset root not permitted: {path}")
        p = path.resolve()
        result[str(p)] = p
    return list(result.values())


def asset_snapshot(profile):
    entries = {}
    base_roots = roots(profile)
    for root in base_roots:
        for path in walk(root):
            st = path.stat()
            entries[str(path.resolve())] = {"size": st.st_size, "sha256": file_hash(path)}
    packed = json.dumps(entries, sort_keys=True, separators=(",", ":")).encode()
    return {"files": entries, "count": len(entries),
            "roots": [str(x) for x in base_roots],
            "digest": hashlib.sha256(packed).hexdigest()}


def assert_same(before, after, label):
    if before != after:
        old = before.get("files", {})
        new = after.get("files", {})
        differences = [x for x in set(old) | set(new) if old.get(x) != new.get(x)]
        raise Blocked(f"{label} changed: {len(differences)} files differ. "
                      "Rebuild/reimport before testing.")


def format_argv(values, argv):
    if not isinstance(argv, list) or not argv or not all(isinstance(x, str) for x in argv):
        raise Blocked("Each command must be a nonempty argv array (no shell strings)")
    try:
        return [x.format_map(values) for x in argv]
    except KeyError as exc:
        raise Blocked(f"Unknown command variable {exc}") from exc


def run_stage(commands, label, values, run_dir):
    for index, raw in enumerate(commands, 1):
        args = format_argv(values, raw)
        log = run_dir / "logs" / f"{label}-{index:02d}.log"
        log.parent.mkdir(parents=True, exist_ok=True)
        with log.open("w", encoding="utf-8") as out:
            out.write(f"Stage={label} step={index}\nArgs: {json.dumps(args)}\n")
            out.flush()
            try:
                code = subprocess.run(args, cwd=ROOT, stdout=out,
                                      stderr=subprocess.STDOUT,
                                      env={**os.environ, "UE_ROOT": values["engine_root"]},
                                      check=False).returncode
            except OSError as exc:
                raise Blocked(f"Cannot run {args[0]}: {exc}") from exc
        if code:
            raise Blocked(f"{label}/{index} failed (exit {code}); see {log}")


def clean_cook():
    for sub in ("Saved/Cooked", "Saved/StagedBuilds", "Saved/Sandboxes"):
        p = ROOT / sub
        if p.is_symlink():
            raise Blocked(f"Refusing to clear symlink: {p}")
        if p.is_dir():
            shutil.rmtree(p)


def archive_snapshot(profile, values, run_dir):
    result = {}
    for kind, template in profile["artifact_roots"].items():
        directory = Path(template.format_map(values)).expanduser().resolve()
        if not directory.is_relative_to(run_dir) or directory == run_dir:
            raise Blocked(f"Archive must be located within this new run: {directory}")
        if not directory.is_dir():
            raise Blocked(f"Missing {kind} packaged directory: {directory}")
        files = {str(f.relative_to(directory)): {"size": f.stat().st_size,
                                                  "sha256": file_hash(f)}
                 for f in walk(directory)}
        if not files or not any(f.endswith((".pak", ".utoc", ".ucas", ".uasset"))
                                for f in files):
            raise Blocked(f"Archive has no cooked game assets: {kind}")
        if not any((directory / f).is_file() and
                   os.access(directory / f, os.X_OK) for f in files):
            raise Blocked(f"Archive has no executable: {kind}")
        result[kind] = {"root": str(directory), "files": files}
    return result


def save_json(path, document):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + ".tmp")
    temp.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8")
    temp.replace(path)


def variables(engine, run_dir, sha, session_id):
    return {"repo": str(ROOT), "project_file": str(ROOT / "MarburgumII.uproject"),
            "engine_root": str(engine), "run_dir": str(run_dir),
            "commit": sha, "session_id": session_id}


def build(profile):
    sha = git_gate()
    engine = Path(profile["engine_root"]).expanduser().resolve()
    engine_data = engine_gate(engine)
    if (any(any(walk(ROOT / folder)) for folder in
            ("ThirdParty/Licensed", "PrivateAssets")) or
            any(any(walk(Path(x))) for x in profile.get("asset_roots", [])
                if Path(x).is_absolute())) and not profile["import_commands"]:
        raise Blocked("Protected source assets exist but import_commands are empty; "
                      "cannot prove the newest raw sources reached cooked Unreal assets.")
    session_id = (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") +
                  "-" + sha[:8] + "-" + uuid.uuid4().hex[:8])
    run_dir = (PLAYTESTS / session_id).resolve()
    if run_dir.exists():
        raise Blocked("Playtest directory already exists")
    run_dir.mkdir(parents=True)
    values = variables(engine, run_dir, sha, session_id)
    save_json(run_dir / "build_state.json",
              {"status": "building", "commit": sha, "session": session_id})
    try:
        if profile["import_commands"]:
            run_stage(profile["import_commands"], "import", values, run_dir)
        if git_gate(refresh=False) != sha:
            raise Blocked("Revision or tracked assets changed during import")
        before = asset_snapshot(profile)
        run_stage(profile["build_commands"], "build", values, run_dir)
        clean_cook()
        run_stage(profile["cook_commands"], "cook", values, run_dir)
        if git_gate() != sha:
            raise Blocked("New upstream revision appeared while building")
        assert_same(before, asset_snapshot(profile), "Source/asset snapshot")
        if engine_gate(engine) != engine_data:
            raise Blocked("Engine changed while building/cooking")
        artifacts = archive_snapshot(profile, values, run_dir)
        seal = {"schema": SCHEMA, "status": "sealed", "session_id": session_id,
                "commit": sha, "engine": engine_data, "asset_snapshot": before,
                "artifacts": artifacts, "run_dir": str(run_dir),
                "created_utc": datetime.now(timezone.utc).isoformat(),
                "owner_approval": "pending_personal_playtest"}
        save_json(run_dir / "build_state.json", seal)
        save_json(PLAYTESTS / "latest.json",
                  {"session_id": session_id, "run_dir": str(run_dir)})
        print(f"SEALED: {session_id} HEAD={sha} asset_inputs={before['count']}")
        print("Next: python3 scripts/playtest-freshness.py launch "
              "--profile Saved/PlaytestProfile/profile.json")
    except Exception as exc:
        save_json(run_dir / "build_state.json",
                  {"status": "blocked", "session_id": session_id, "error": str(exc)})
        raise


def load_latest():
    pointer = PLAYTESTS / "latest.json"
    if not pointer.is_file():
        raise Blocked("No sealed playtest build; run build first")
    info = json.loads(pointer.read_text(encoding="utf-8"))
    run_dir = Path(info["run_dir"]).resolve()
    if not run_dir.is_relative_to(PLAYTESTS.resolve()):
        raise Blocked("Latest manifest points outside playtest storage")
    state = json.loads((run_dir / "build_state.json").read_text(encoding="utf-8"))
    if state.get("status") != "sealed" or state.get("session_id") != info.get("session_id"):
        raise Blocked("Playtest manifest is not a sealed matching session")
    return state, run_dir


def verify(profile):
    state, run_dir = load_latest()
    engine = Path(profile["engine_root"]).expanduser().resolve()
    if engine_gate(engine) != state["engine"]:
        raise Blocked("Engine version/metadata changed since packaging")
    sha = git_gate()
    if sha != state["commit"]:
        raise Blocked(f"Packaged SHA {state['commit'][:12]} != latest {sha[:12]}")
    assert_same(state["asset_snapshot"], asset_snapshot(profile), "Asset inputs")
    values = variables(engine, run_dir, sha, state["session_id"])
    if archive_snapshot(profile, values, run_dir) != state["artifacts"]:
        raise Blocked("Client/server archive changed since sealing")
    print(f"FRESH: {state['session_id']} commit={sha[:12]} "
          f"assets={state['asset_snapshot']['count']} packaged client+server verified")
    return state, values


def launch(profile):
    state, values = verify(profile)
    run_dir = Path(state["run_dir"])
    commands = profile["launch_commands"]
    children = []
    failure = None
    try:
        for index, spec in enumerate(commands):
            args = format_argv(values, spec["command"])
            executable = Path(args[0]).resolve()
            kind = spec["role"]
            allowed = Path(state["artifacts"][kind]["root"])
            if not executable.is_relative_to(allowed) or not executable.is_file():
                raise Blocked(f"{kind} executable is outside its sealed archive: {executable}")
            log_path = run_dir / "logs" / f"runtime-{index:02d}-{kind}.log"
            log_path.parent.mkdir(exist_ok=True)
            stream = log_path.open("wb")
            try:
                child = subprocess.Popen(args, cwd=executable.parent,
                                         stdout=stream, stderr=subprocess.STDOUT)
            finally:
                stream.close()
            children.append(child)
            if index == 0:
                time.sleep(2)
            if child.poll() is not None:
                raise Blocked(f"{kind} process exited immediately: {child.returncode}")
        print("PLAYTEST STARTED: exact sealed client/server build. "
              "Ctrl+C stops all; owner approval is never automatic.")
        while children[0].poll() is None and any(x.poll() is None for x in children[1:]):
            time.sleep(0.5)
        if children[0].poll() is not None:
            raise Blocked("Dedicated server stopped during playtest")
    except KeyboardInterrupt:
        print("Playtest stopped by user.")
    except Exception as exc:
        failure = str(exc)
    finally:
        for p in reversed(children):
            if p.poll() is None:
                p.terminate()
        for p in children:
            try:
                p.wait(timeout=8)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()
        try:
            verify(profile)
        except Exception as exc:
            failure = f"Playtest INVALIDATED by post-session freshness check: {exc}"
        save_json(run_dir / "owner_playtest_session.json", {
            "session_id": state["session_id"], "commit": state["commit"],
            "status": "needs_owner_review" if failure is None else "invalid",
            "reason": failure, "owner_approval": "not_given",
            "finished_utc": datetime.now(timezone.utc).isoformat()})
    if failure:
        raise Blocked(failure)
    print("Session ended; personal owner playtest/approval still required.")


@contextmanager
def exclusive_lock():
    PLAYTESTS.mkdir(parents=True, exist_ok=True)
    with (PLAYTESTS / ".lock").open("a+") as handle:
        try:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            raise Blocked("Another playtest build/session is running") from exc
        try:
            yield
        finally:
            fcntl.flock(handle, fcntl.LOCK_UN)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("build", "verify", "launch"))
    parser.add_argument("--profile", type=Path, required=True)
    args = parser.parse_args()
    try:
        profile = profile_read(args.profile)
        with exclusive_lock():
            {"build": build, "verify": verify, "launch": launch}[args.action](profile)
        return 0
    except (Blocked, OSError, ValueError, KeyError, TypeError) as exc:
        print(f"PLAYTEST BLOCKED: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
