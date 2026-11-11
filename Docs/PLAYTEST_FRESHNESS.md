# Fresh Builds and Assets for Every Owner Playtest

**Status:** A source-level fail-closed freshness guard and unit tests are present; real Unreal Engine **5.8.3** build/cook/package and launch qualification **has not yet been demonstrated**. A local playtest may be approved only when the exact build and source/asset snapshot have been verified.

## Principle

A test of an old executable or cooked map is not acceptance of current MarburgumII. Before each personal gameplay test, synchronize `main` with `origin/main`, reject dirty/untracked source changes, inventory/hashes all game asset roots (including locally licensed/ignored and separately supplied generated content), run required import commands, build client and server, fully cook/package a **new, isolated** archive, seal file hashes, verify the seal immediately before launching, and verify it again afterward.

**Current mechanism:** `scripts/mb2.sh playtest` (Rust, `tools/mb2/src/playtest.rs`) implements `build`, `verify`, `launch`; `scripts/playtest-profile.example.json` is an **example configuration that MUST be adapted to real UE 5.8.3 packaging paths**, not an already-qualified launch manifest. The script refuses to claim a valid package without both client and server archives containing actual executables and cooked assets. It does not itself prove multiplayer acceptance, client-server network truth, or owner approval.

## Local configuration

Copy `scripts/playtest-profile.example.json` to `Saved/PlaytestProfile/profile.json` (ignored by Git). Configure `engine_root`, actual Unreal BuildCookRun commands, import steps for local licensed or generated sources, the resultant real `artifact_roots`, and one server + two real client process commands. Record *all* external asset directories in `asset_roots`, including generator output roots, and ensure no directories or source dependencies fall outside the fingerprint.

```bash
cargo test --manifest-path tools/mb2/Cargo.toml            # guard unit tests (no Unreal needed)
scripts/mb2.sh playtest build --profile Saved/PlaytestProfile/profile.json
scripts/mb2.sh playtest verify --profile Saved/PlaytestProfile/profile.json
scripts/mb2.sh playtest launch --profile Saved/PlaytestProfile/profile.json
```

Only use the launcher after real package paths, cook output and smoke test have been verified. Git state and upstream checks require an online `origin/main` fetch; this intentionally blocks offline acceptance tests. If an asset changes after packaging or a newer Git commit lands, the previous seal is invalidated. Do not use old launcher shortcuts, Editor-cached PIE sessions or an old `Saved/Cooked` tree as proof of freshness. An installed image-to-3D generator is only an input-production tool; generated content must have been imported and included in the cook.

**Important open qualification:** the generic scripts are a source scaffold. The example does not know the real local packaged executable layout, map path, asset importer or engine installation, and tests currently verify source logic only. Before the first acceptance build, check the UAT switches and archive locations on the actual machine, prove that altered mesh/texture inputs trigger invalidation, ensure every locally generated asset root is included, and verify actual client/server packaged content and logs.

A passing launcher guard supports an owner playtest; it **never authorizes itself to grant owner approval**. Record the exact SHA, engine version, asset digest, session ID, scenarios, observed issues and an explicit owner decision.

## Required latest-world asset closure (ADR-040–045; 11 November 2026)

The normal clean-SHA/client-server seal must now include **the complete nine-window input and derived-asset chain**, not just older Lent's visible Unreal assets. Inventory/hash approved original-window building source and placed-building manifests, all nine LiDAR tiles and orthophoto classification inputs, the committed OSM extract, Rust `tools/lent-ground` output (ground/surface mask/paving and 3×3 footprint merges), generated UE `/Game/World/lent` landscape/building/paving assets, neighboring Drava sheets and swim physics volumes, vehicle placement, and the loose staged `Content/Marburgum/Street/lent_walk_graph.json`. Regenerate any dependent assets in documented order; a source SHA alone is insufficient to ensure a current cook.

Include generated **post-IK swim/tread, car enter/exit and fight animation assets** (ADR-042/043), combat impact audio, original-game geometry/rights manifests and any locally ignored sources. A content change to one window, bridge, walk graph or source height/surface input invalidates the previous seal. Distinguish a fixture game map `/Game/Maps/Lent` from the production-candidate default `/Game/World/lent`, and never reuse the older map's screenshots as nine-window acceptance.

**Current evidence distinction:** the nine-window ray/capsule verification and street/combat/vehicle/swim regression are recorded in **editor mode** at ADR-045; the ambient street smoke is one-client editor-only; the combat packaged two-client smoke predates later animation changes. A complete new packaged build with full nine-window two/three-client scenarios, load/memory and owner approval is **not yet documented as qualified**. Attach run-mode, source/cook/asset checksums, exact map, server/client logs and personal owner outcome to the acceptance package.

## Asset source and license boundary

Commercial city geometry, proprietary reference images, locally generated 3D models, candidate PBR textures and model weights must remain in approved local private directories unless licensing explicitly permits public publication. The UI and server must accurately reflect current approved assets. Never republish licensed commercial maps to public GitHub merely to simplify test packaging.
