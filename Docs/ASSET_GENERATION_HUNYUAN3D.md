# Local Image-to-3D Generation — Hunyuan3D-2.1

**Status:** Locally installed, as reported by the project owner. Installation path, checkpoints, local invocation and performance **not verified by this repository**. **Production use blocked pending license authorization applicable to Slovenia/EU.**  
**Product:** [Tencent Hunyuan3D-2.1](https://github.com/Tencent-Hunyuan/Hunyuan3D-2.1).  
**Target game:** MarburgumII, Unreal Engine **5.8.3**, with a separate asset-production pipeline rather than a runtime dependency.

## Purpose and scope

Hunyuan3D-2.1 is a locally available **candidate** for converting supplied or rights-cleared reference images into 3D mesh candidates and physically based surface textures. It can accelerate original asset development for 1990s Maribor: street furniture, signage, benches, market objects, period equipment, interiors, building details, vegetation shapes and selected fish models. The output is a **starting point for professional 3D production**; it is never automatically a finished, historically accurate, optimized, licensed or playable asset. The image-to-geometry and image-to-PBR stages should be tracked separately.

The planned licensed Maribor city model remains the **primary base-map geometry**, not a candidate for wholesale replacement by generative reconstruction. The original game's vegetation and fish catalogues retain their species IDs, habitats and distribution requirements; image generation can help produce **visual variants**, but cannot supply botanical identification, local population counts or game rules.

## Important licensing gate — the EU

The [published Tencent Hunyuan 3D 2.1 Community License](https://github.com/Tencent-Hunyuan/Hunyuan3D-2.1/blob/main/LICENSE) states explicitly that the agreement **does not apply in the European Union, United Kingdom or South Korea**. Slovenia is in the EU. A locally installed copy or publicly downloadable weights do **not** establish that the game has rights to run this model there or incorporate its outputs. The question must be resolved against the precise distribution, checkpoint and applicable licensing terms.

**Policy for this repository:** until the owner provides and records an applicable authorization or alternative license for local use, commercial/game-related production, generated outputs and distribution, **do not integrate Hunyuan-generated assets into the production game, release them, or mark them approved**. Obtain specific guidance or written permission from the licensor; consult qualified legal advice if needed. Do not assume a wrapper, fork or offline installation removes underlying restrictions. This is a project compliance gate, not a legal determination about what uses are permissible.

Reference-image rights must be independently verified. Images obtained from archives, photographers, online sources, stock libraries or the commercial city map are **not** automatically authorized as inputs for model generation or redistribution. Retain source/license records privately where appropriate.

## Proposed image-to-model workflow (only after license clearance)

1. **Intake:** record image identifiers, image copyright/license, intended asset ID, original Marburgum species/object ID where relevant, historical period, expected visual role, known references and uncertainties.
2. **Reproducible local model generation:** pin the exact installed model/checkpoint and code revision, model parameters, random seed (where supported), GPU/memory profile and source-image SHA-256. Never invent an invocation without checking the actual local installation.
3. **Shape review:** check whether reconstructed geometry is plausible from multiple views. Fix silhouettes, holes, topology, unnecessary internal polygons, symmetry errors and inaccurate scale. A single image cannot establish hidden surfaces or exact dimensions.
4. **PBR review:** inspect base color, roughness, metallic and normal channels and reconcile them with Unreal's material conventions. Correct baked lighting, text artifacts, seams, texture bleeding and illegible historical signs; make LOD-friendly UVs.
5. **Manual retopology and optimization:** prepare game-ready topology, watertightness where necessary, triangle budgets derived from screen size, clean pivots, real-world scale, collision shapes, UVs and suitable LODs or instancing. For vegetation, check alpha-mask performance, wind response, vertex density and seasonal variants. For fish, check silhouette, material, rigging and swimming animation separately.
6. **Engine import:** convert via an approved interchange format compatible with the locally installed generator and UE **5.8.3**, set materials and collision, create deterministic paths and source-to-output manifests. Never hard-code a purported output format or CLI path without inspecting the installed package.
7. **Review:** test under real in-game lighting and representative camera distances. Check cultural/1990s authenticity, mesh/material quality, streaming, shadowing, mobile/low-end fallback and source licensing.
8. **Owner signoff:** any major playable/visual feature needs the owner's **personal in-game test** on the exact fresh client/server build and explicit `approved` / `changes_requested` / `rejected` decision. A generated preview image and successful Unreal import are insufficient.

## Directory and asset-tracking policy

- **Sensitive/unapproved generation inputs and results:** local ignored `PrivateAssets/Hunyuan3D/` and `Saved/AssetGeneration/Hunyuan3D/`. These must not enter public commits, Git LFS, CI logs or build artefacts by accident.
- **Approved, redistributable outputs only:** after licensing and rights review, promote game-ready processed assets into controlled Unreal `Content/` paths, with a small metadata manifest documenting source references and output versions. Do not commit proprietary model weights, secrets, private installation paths, or restricted reference images.
- **Stable asset IDs:** keep `fish.v0.1.json` and `vegetation.v0.1.json` identifiers independent from generator asset names and random output files.
- **Freshness:** the [playtest freshness process](PLAYTEST_FRESHNESS.md) must hash local inputs, processed Unreal assets and cooked client/server archives. A new accepted mesh, texture or material requires a fresh cook/package and a new owner playtest—not just a newer source commit.
- **Server separation:** rendering assets are presentation-only. Geometry collision/navigation and gameplay rules remain authored and checked independently; the headless server does not load unnecessary client material/texture dependencies.

## Validation milestones

**GEN-00 — rights and environment:** demonstrate the appropriate license, inventory the installed generator/version/checkpoints without leaking local secrets, record GPU resource feasibility. Until then status is `blocked_license`.

**GEN-01 — controlled prototype:** one rights-cleared reference object, source/provenance metadata, before/after screenshots, manually reviewed mesh, PBR channel review and basic Unreal 5.8.3 import. No general rollout.

**GEN-02 — one game-ready prop:** optimized, collidable, scaled, UV-correct, correctly lit and streamed object in an actual MarburgumII playable street/interior, personally approved by the owner.

**GEN-03 — ecological variants:** selected legacy vegetation species/fish models added only after verified species identity, period plausibility and approved live gameplay/visual testing. Expand species coverage incrementally.

**GEN-04 — reproducible batch pipeline:** manifest-driven production with model/checkpoint hashes, versioned input/output tracking, repeatability, rights provenance, stale-build prevention, performance and owner reapproval for substantive changes.

These are **planned** milestones only: no Hunyuan-generated mesh or texture was produced/imported during this documentation change.

## References

- [Official Hunyuan3D-2.1 repository](https://github.com/Tencent-Hunyuan/Hunyuan3D-2.1)
- [Official model license](https://github.com/Tencent-Hunyuan/Hunyuan3D-2.1/blob/main/LICENSE)
- [Map adoption](WORLD_MAP_ADOPTION.md)
- [Original fish catalogue](../Data/Ecology/fish.v0.1.json)
- [Original vegetation catalogue](../Data/Ecology/vegetation.v0.1.json)
- [Top-level roadmap](ROADMAP.md)
