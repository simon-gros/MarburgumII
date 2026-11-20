# Fresh Builds and Assets for Every Owner Playtest

**Status (20 November 2026):** The fail-closed build/asset guard, executable hashing and process-group cleanup are implemented. Separately, a **clean UE 5.8.3 Development package of `c967c46`** was created and used for real multi-client crowd tests; the staged night pair settled but daytime opposite-bank population failed (11/28). This does **not** demonstrate end-to-end qualification of the generic `mb2 playtest` example profile, the entire game or owner approval.

**Latest integration note (20 November evening):** Clean Development packages exist for `a0d0b89` staged interior/older regression, `9b88369` staged English/Slovene story/Photography and `44106bc` staged signed-in encrypted account/packet controls. **They are separate proven versions, not one all-feature packaged PASS.** The ADR-079 crowd fix was editor-tested but needs a fresh staged run. The latest playtest seal must include account-service/crypto source, local UI ICU/fonts, schema-6 story, intermediate/cooked interiors and walk graph. Owner acceptance remains open. [24-hour review](DEVELOPMENT_REVIEW_2026-11-20_EVENING.md).



## Principle

A test of an old executable or cooked map is not acceptance of current MarburgumII. Before each personal gameplay test, synchronize `main` with `origin/main`, reject dirty/untracked source changes, inventory/hashes all game asset roots (including locally licensed/ignored and separately supplied generated content), run required import commands, build client and server, fully cook/package a **new, isolated** archive, seal file hashes, verify the seal immediately before launching, and verify it again afterward.

**Current mechanism:** `scripts/mb2.sh playtest` (Rust, `tools/mb2/src/playtest.rs`) implements `build`, `verify`, `launch`; `scripts/playtest-profile.example.json` is an **example configuration that MUST be adapted to real UE 5.8.3 packaging paths**, not an already-qualified launch manifest. The script refuses to claim a valid package without both client and server archives containing actual executables and cooked assets. The archive seal hashes every file in each archive, its `Binaries` included, and leaves out only the game's run-time `Saved` output; until the QA audit of 2026-11-19 it skipped every `Binaries` folder, so `verify` could print FRESH for a swapped executable. `launch` runs each role in its own process group and stops the whole group, because UE's launcher `.sh` starts the binary without `exec`; stopping only the script left the old server holding the playtest port. It does not itself prove multiplayer acceptance, client-server network truth, or owner approval.

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

## Latest Development package evidence — 20 November 2026

The clean `c967c46` Client/Server executable hashes are recorded in [AI evidence](AI_GAMEPLAY.md). The staged nighttime crowd settled at two and the daytime matrix settled most shared/district/cap cases, **but opposite banks timed out at 11/28, script exit 1**. The earlier QA package's 13/14 staged smokes belong to different source/asset snapshots. For every playtest keep the exact commit, sealed cooked data, build stamps, server/client logs and explicit owner outcome. See [current review](DEVELOPMENT_REVIEW_2026-11-20.md).

## Microsoft TRELLIS asset freshness (ADR-071)

**For new generated content, Microsoft TRELLIS is the selected workflow; Hunyuan3D-2.1 is superseded.** Inventory the exact source images, installed TRELLIS code/checkpoint/weights, exporter and bake dependencies/licenses, intermediate vertex-colour meshes, Blender/Cycles materials and retopology/UV exports, UE import settings and all final cooked assets. Include ignored `PrivateAssets/TRELLIS/`, `AssetGeneration/TRELLIS/` and `Saved/AssetGeneration/TRELLIS/` roots in the private profile when used, so every dependency of a generated asset contributes to its freshness seal. See [TRELLIS pipeline](ASSET_GENERATION_TRELLIS.md).

## Asset source and license boundary

Commercial city geometry, proprietary reference images, locally generated 3D models, candidate PBR textures and model weights must remain in approved local private directories unless licensing explicitly permits public publication. The UI and server must accurately reflect current approved assets. Never republish licensed commercial maps to public GitHub merely to simplify test packaging.


## 19 November street/content freshness expansion (ADR-068–070)

The asset seal must now include the exact committed zoned `Content/Marburgum/Street/lent_walk_graph.json` (each node's district and activity, and the 239 pinned place entries), `Data/Gameplay/street.v0.1.json` (hour/zone/population/place behavior and visit chances), related shared C++ contracts, and the original road-graph/OSM/cadastre sources **when used to regenerate them**. Re-run the `tools/lent-ground` `street-zones` and `attractors` steps in the documented dependency order when changing those sources. An older cooked graph silently omits the new district and destination AI even when the executable is current.

Before the next owner build, execute ADR-070's previously unrun CombatRules/StreetRules automation and visually investigate an unreachable hostile and a repeatedly blocked pavement; then require packaged two-client district/places/incident regressions, manifest/hash comparisons and owner acceptance. Review: [19 November](DEVELOPMENT_REVIEW_2026-11-19.md).
