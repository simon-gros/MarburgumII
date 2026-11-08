# Fresh Builds and Assets for Every Owner Playtest

**Status:** A source-level fail-closed freshness guard and unit tests are present; real Unreal Engine **5.8.3** build/cook/package and launch qualification **has not yet been demonstrated**. A local playtest may be approved only when the exact build and source/asset snapshot have been verified.

## Principle

A test of an old executable or cooked map is not acceptance of current MarburgumII. Before each personal gameplay test, synchronize `main` with `origin/main`, reject dirty/untracked source changes, inventory/hashes all game asset roots (including locally licensed/ignored and separately supplied generated content), run required import commands, build client and server, fully cook/package a **new, isolated** archive, seal file hashes, verify the seal immediately before launching, and verify it again afterward.

**Current mechanism:** `scripts/playtest-freshness.py` implements `build`, `verify`, `launch`; `scripts/playtest-profile.example.json` is an **example configuration that MUST be adapted to real UE 5.8.3 packaging paths**, not an already-qualified launch manifest. The script refuses to claim a valid package without both client and server archives containing actual executables and cooked assets. It does not itself prove multiplayer acceptance, client-server network truth, or owner approval.

## Local configuration

Copy `scripts/playtest-profile.example.json` to `Saved/PlaytestProfile/profile.json` (ignored by Git). Configure `engine_root`, actual Unreal BuildCookRun commands, import steps for local licensed or generated sources, the resultant real `artifact_roots`, and one server + two real client process commands. Record *all* external asset directories in `asset_roots`, including generator output roots, and ensure no directories or source dependencies fall outside the fingerprint.

```bash
python3 -m unittest discover -s scripts/tests -p 'test_*.py'
python3 scripts/playtest-freshness.py build --profile Saved/PlaytestProfile/profile.json
python3 scripts/playtest-freshness.py verify --profile Saved/PlaytestProfile/profile.json
python3 scripts/playtest-freshness.py launch --profile Saved/PlaytestProfile/profile.json
```

Only use the launcher after real package paths, cook output and smoke test have been verified. Git state and upstream checks require an online `origin/main` fetch; this intentionally blocks offline acceptance tests. If an asset changes after packaging or a newer Git commit lands, the previous seal is invalidated. Do not use old launcher shortcuts, Editor-cached PIE sessions or an old `Saved/Cooked` tree as proof of freshness. An installed image-to-3D generator is only an input-production tool; generated content must have been imported and included in the cook.

**Important open qualification:** the generic scripts are a source scaffold. The example does not know the real local packaged executable layout, map path, asset importer or engine installation, and tests currently verify source logic only. Before the first acceptance build, check the UAT switches and archive locations on the actual machine, prove that altered mesh/texture inputs trigger invalidation, ensure every locally generated asset root is included, and verify actual client/server packaged content and logs.

A passing launcher guard supports an owner playtest; it **never authorizes itself to grant owner approval**. Record the exact SHA, engine version, asset digest, session ID, scenarios, observed issues and an explicit owner decision.

## Asset source and license boundary

Commercial city geometry, proprietary reference images, locally generated 3D models, candidate PBR textures and model weights must remain in approved local private directories unless licensing explicitly permits public publication. The UI and server must accurately reflect current approved assets. Never republish licensed commercial maps to public GitHub merely to simplify test packaging.
