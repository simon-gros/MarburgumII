#!/usr/bin/env python3
"""Source-only, isolated tests of the playtest freshness guard (no UE required)."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("playtest_freshness",
    Path(__file__).resolve().parents[1] / "playtest-freshness.py")
guard = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(guard)


class GuardTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.oldroot = guard.ROOT
        guard.ROOT = self.root
        self.addCleanup(setattr, guard, "ROOT", self.oldroot)
        self.profile = {"asset_roots": []}

    def put(self, filename, contents=b"v1"):
        path = self.root / filename
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(contents)
        return path

    def test_snapshot_detects_asset_modification_and_deletion(self):
        target = self.put("Content/Licensed/MariborMesh.uasset")
        self.put("Data/World/map.json")
        before = guard.asset_snapshot(self.profile)
        target.write_bytes(b"v2")
        with self.assertRaises(guard.Blocked):
            guard.assert_same(before, guard.asset_snapshot(self.profile), "assets")
        target.unlink()
        with self.assertRaises(guard.Blocked):
            guard.assert_same(before, guard.asset_snapshot(self.profile), "assets")

    def test_snapshot_detects_added_untracked_local_asset(self):
        before = guard.asset_snapshot(self.profile)
        self.put("PrivateAssets/Maribor/model.fbx")
        with self.assertRaises(guard.Blocked):
            guard.assert_same(before, guard.asset_snapshot(self.profile), "assets")

    def test_external_asset_root_hashed(self):
        outside = self.root / "external" / "model.fbx"
        outside.parent.mkdir()
        outside.write_bytes(b"first")
        config = {"asset_roots": [str(outside.parent)]}
        before = guard.asset_snapshot(config)
        outside.write_bytes(b"second")
        with self.assertRaises(guard.Blocked):
            guard.assert_same(before, guard.asset_snapshot(config), "assets")

    def test_symlink_refused(self):
        target = self.put("Assets/real.uasset")
        link = self.root / "Content" / "fake.uasset"
        link.parent.mkdir()
        link.symlink_to(target)
        with self.assertRaises(guard.Blocked):
            guard.asset_snapshot(self.profile)

    def test_missing_cooked_assets_rejected(self):
        run = self.root / "Saved/Playtests/run"
        (run / "client").mkdir(parents=True)
        (run / "server").mkdir(parents=True)
        exe = self.put("Saved/Playtests/run/client/GameClient.sh", b"#!/bin/sh\n")
        exe.chmod(0o755)
        server = self.put("Saved/Playtests/run/server/GameServer.sh", b"#!/bin/sh\n")
        server.chmod(0o755)
        profile = {"artifact_roots": {"client": "{run_dir}/client",
                                      "server": "{run_dir}/server"}}
        with self.assertRaises(guard.Blocked):
            guard.archive_snapshot(profile, {"run_dir": str(run)}, run)

    def test_modified_package_invalidates_seal(self):
        run = self.root / "Saved/Playtests/run"
        for role, exe in (("client", "GameClient.sh"), ("server", "GameServer.sh")):
            sh = self.put(f"Saved/Playtests/run/{role}/{exe}", b"#!/bin/sh\n")
            sh.chmod(0o755)
            self.put(f"Saved/Playtests/run/{role}/Content.pak", b"cooked v1")
        profile = {"artifact_roots": {"client": "{run_dir}/client",
                                      "server": "{run_dir}/server"}}
        before = guard.archive_snapshot(profile, {"run_dir": str(run)}, run)
        self.put("Saved/Playtests/run/client/Content.pak", b"stale tampering")
        after = guard.archive_snapshot(profile, {"run_dir": str(run)}, run)
        self.assertNotEqual(before, after)

    def test_wrong_engine_patch_rejected(self):
        root = self.root / "UE"
        version = root / "Engine/Build/Build.version"
        version.parent.mkdir(parents=True)
        version.write_text('{"MajorVersion":5,"MinorVersion":8,"PatchVersion":2}')
        with self.assertRaises(guard.Blocked):
            guard.engine_gate(root)


if __name__ == "__main__":
    unittest.main()
