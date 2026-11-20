# Development review — 20 November 2026, morning

**Repository baseline:** [`8fd4aaff`](https://github.com/simon-gros/MarburgumII/commit/8fd4aaff1daad50d3e2bc8dd3f06677ed5e93efe) (20 November 05:21 UTC), including archive migration `659e28f`, graphics roadmap ~05:01 UTC, UI merge ~02:14 UTC and interiors ADR-076. **Method:** GitHub commits/files, roadmap and ADR reconciliation, evidence and Actions run statuses. No new Unreal build, package smoke, GPU benchmark or owner playtest was run in this documentation review; older reports retain their own tested SHAs.

## Recent implementation and planning evidence

| Change | What is verified | Not yet accepted |
| --- | --- | --- |
| Research archive `659e28f` + expansion `8fd4aaf` | **246 historical art records (236 inherited + 10 researched), 240 reference-only index entries, 21 manifest entries without imported binaries, five source-linked rules, six image/cartographic leads (three imported + three researched)** | All six leads unverified for particular 1990s Maribor sheets/rights/geo; no licensed rasters or period-specific in-game scenery |
| Graphics `275136b` and linked documentation | New GFX-00–09 measured roadmap, renderer constraints, graphics and NVIDIA research separation | Proposals only; no packaged improvement, DLSS 5 runtime, World Partition or owner approval |
| UI `e8416bd` / `c992833` | Merged HUD/management/Settings/chat/creator/Escape redesign, 657 localized interface strings, fixed U9/contrast, **37/37 editor automation** | Full after-change 720p–4K 80–200%, gamepad, chat/door input, gameplay translations, cooked UI assets and owner approval |
| Interiors `d7d58b7` / `725510d` | Deterministic floor and doors for **586/932 eligible footprints**, 346 exceptions; **five cut shells** with editor three-client walkthrough; packaged Client/Server built after UFS staging | **581 still sealed**, staged door smoke/streaming/latency/owner walkthrough and furnishings |
| Earlier street package `c967c46` | Shared/capped crowd scenarios and bridge route pass in their recorded scope | **Opposite-bank staged 11/28**, editor 14/28 timeout; no subsequent passing test recorded |

## P0: repository-level CI is failing

The latest reviewed revision's [source validation](https://github.com/simon-gros/MarburgumII/actions/runs/44680469847) is **completed/failure**; [archive validation](https://github.com/simon-gros/MarburgumII/actions/runs/44680469864) is **completed/success**. Earlier source [run 44680141472](https://github.com/simon-gros/MarburgumII/actions/runs/44680141472) located failure at `cargo run --release --manifest-path tools/mb2/Cargo.toml -- validate all`; simulation/account/.NET tests passed. Existing [ADR-018](DECISIONS.md) and `tools/mb2/src/validate/python_free.rs` expressly disallow `.py` sources and Python workflows. The archive introduced both, a direct consistency conflict. Review full logs for any additional failures; do not infer isolated archive success means repository-level acceptance.

**Remediation:** [ADR-077](DECISIONS.md) asks for an explicit owner tooling decision. Prefer porting validation, provenance/rules tests and authorized-source downloader into Rust `mb2` while preserving schema, unique IDs and licensing restrictions. Otherwise formally approve a strictly bounded non-runtime archival research Python exception and update ADR-018, Rust guard, workflows and developer policy in one reviewed change. No silent disabling or wildcard exemption. Require source + archive CI green **on the same SHA**.

## Corrected next priorities and stop conditions

1. **P0 — CI/architecture:** fix and retest `mb2 validate all` and archive CI together; resolve ADR-077.
2. **P0 — Game AI:** fix strict opposite-bank **28-person** eligible-node shortage with passing editor and clean packaged full day/night matrices; do not relax timeout/budget or invalidate shared crowd and 48 cap.
3. **P0 — Interiors/package:** matched UE 5.8.3 Client+Server with source/asset/UFS hashes. Pass `door-smoke-linux.sh staged` on at least the already cut Hotel Lent and vine museum, with three players, denial/reach/blocking, late join, two simultaneous interiors, restart and stream out/back in. Then obtain owner personal test, before 581-shell rebuild.
4. **P0 — Security and integrated gameplay:** close AR1 ticket audience and AR13 secret URL; run first launch/auth, chat, missions/fishing/shop, driving >150 m, combat, condition, swimming, weather, street/incidents and reconnect on **one current** packaged source/asset pair.
5. **P0 — UI/HUD:** prove cooked fonts/ICU/Slovene resources, full 720p–4K/ultrawide 80–200% live test, controller and E-door/chat/F/R/G focus, story receipt privacy, performance and personal owner approval.
6. **P1 — Interior rollout:** after staged/owner gate, rebuild remaining 581 cuttable shells in audited batches with rollback and byte/hash consistency across footprints, measured floors, façade door cuts and cooked content. Keep 346 named exceptions open; add era-appropriate rooms, window details and furnishings.
7. **P1 — History and graphics:** verify rights/dates/georeferencing for 246 references and six leads, prioritizing Lent and 1990s. Add source-linked approved world changes; do not confuse 2006 orthos, 2026 photos or 19th-century cadastral records with 1990s truth. Baseline GFX-00 on packaged active NPC/door/UI/weather/Drava scenes with GPU/CPU p50/p95/p99 and VRAM before new shaders.
8. **P2 — Scale:** World Partition, city model rights, service observability, backup/rollback and 250-player scale remain separate evidence-gated work.

## Governance and omitted safeguards

Maintain **implemented source → editor-verified → clean packaged-staged PASS → personal owner-approved** as four distinct states. Never declare 586 buildings playable because layout algorithms accept them, or claim graphics improvements because a renderer plan was written. Period references are metadata, not licensed textures/TRELLIS inputs; approval requires source-specific rights, depicted-date and actual-location rulings. Compare packaged mirrors of `buildings.json` and `interior_floors.json` with physical shell cuts and reproducible material/asset digests. Keep the existing [overnight review](DEVELOPMENT_REVIEW_2026-11-20.md) and [QA audit](QA_AUDIT_2026-11-19.md) unchanged as dated evidence. Canonical priorities: [ROADMAP](ROADMAP.md); graphical plan [GRAPHICS_ROADMAP](GRAPHICS_ROADMAP.md); interface [UI_HUD_ROADMAP](UI_HUD_ROADMAP.md); historical rights [HISTORICAL_ARCHIVES](HISTORICAL_ARCHIVES.md); interiors [INTERIORS](INTERIORS.md); decisions [DECISIONS](DECISIONS.md). This update changes documentation only.
