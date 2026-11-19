# Asset generation — Microsoft TRELLIS

**Official project decision, 19 November 2026 (ADR-071): Microsoft TRELLIS is the selected image-to-3D generator for all new Marburgum II asset work. Hunyuan3D-2.1 is superseded. Do not select Hunyuan3D for new assets or use it as an automatic fallback.**

**Canonical repository:** [Microsoft TRELLIS](https://github.com/microsoft/TRELLIS), the original `microsoft/TRELLIS` image-to-3D project. `TRELLIS.2` is a distinct successor and is **not** automatically approved by this decision. A later engine/model substitution requires its own explicit review.

**Current qualification:** the existing playable protagonist already uses TRELLIS vertex-colour geometry, manual retopology and an independent Blender/Cycles bake (ADR-021/031); the overall reusable generation pipeline is **not yet proven production-ready**. This is an accepted tooling direction, not a claim of completed graphics or unrestricted licensing.

## Purpose and boundaries

Use TRELLIS to generate **candidates** for period-appropriate street props, decorative architecture, furniture, shop objects, accessories, individual character or ecological presentation variants, and other suitable original visual assets. A candidate is not a shipping asset until its provenance, topology, scale, UV/PBR, collision, performance, period accuracy and visual acceptance are established.

TRELLIS does **not** regenerate Maribor's surveyed road/terrain network, river, bridges, authored city layout or historical/gameplay logic. The original game's fish/plant species IDs, habitats and mission identities remain canonical. The separate candidate licensed city map has independent procurement and owner-adoption gates. The generator is offline and **never required by packaged Unreal clients or dedicated servers**.

## Rights and licensing

Microsoft distributes the original TRELLIS code and primary official model under permissive/MIT terms, but this **does not clear all dependency, bake/export, checkpoint, input-image and derivative rights**. Review the actual pinned versions.

The protagonist's prior textured-bake path involved **nvdiffrast and Inria's differentiable rasterizer**, whose published terms were identified by the project as limited to research/evaluation. Until an applicable license is established, **do not use those restricted rasterizers to produce commercial production textures**. The established safer route for this project is **TRELLIS vertex-colour geometry → reviewed retopology and UVs → independently performed Blender/Cycles texture baking**, verifying that this exact path does not invoke those components. Other routes require specific rights review; this is not a universal legal conclusion about every generated output.

For every asset, retain image provenance and usage rights, model/source revisions, checkpoint/license, third-party dependencies, parameters/seed where applicable, exporter and baking method, hashes, manual changes and the review decision. Do not assume a publicly available image, generated mesh, model weight or software package is automatically commercially redistributable.

## Image-to-game-asset process

1. **Intake:** establish the visual intent, rights-cleared input image, project asset/species identifier, appropriate 1990s references and intended use.
2. **Pin environment:** verify the real installed Microsoft TRELLIS source revision, checkpoint/weights and hash, GPU/software dependencies, export mode and license chain. Do not invent a local executable path or a universal `generate.py` command.
3. **Generate candidate geometry:** use the approved TRELLIS image-conditioned workflow. Prefer the verified vertex-colour export path where the textured export's rasterizers cannot be cleared.
4. **Inspect quality:** evaluate multiple angles and correct wrong backs, collapsed hands, holes, scale errors, silhouette artifacts, nonmanifold edges, inverted surfaces or missing topology.
5. **Optimize:** perform manual retopology, UV unwrap, independently licensed texture bake and PBR/material design; set budgets, pivots, LODs, instancing, collisions and rigs where required.
6. **Import into UE 5.8.3:** retain source/output mapping and verify the Unreal material, lighting, collision, terrain contact, shadows, animation and streaming costs.
7. **Review in context:** compare against the target district's period identity and art direction; test real camera ranges, multiplayer consistency and performance. For fish/plants, independently verify species/habitat correctness.
8. **Accept and package:** only rights-cleared, optimized and approved work is promoted to public `SourceArt/` or distributable `Content/` as permitted. Rebuild/re-cook matching fresh client/server content and obtain a personal owner playtest and explicit approval for major visual/playable features.

## Local directories and freshness

Use ignored roots such as `PrivateAssets/TRELLIS/`, `LocalGenerators/TRELLIS/`, `AssetGeneration/TRELLIS/` and `Saved/AssetGeneration/TRELLIS/` for private inputs, checkpoints and intermediate exports; the project's `.gitignore` covers these roots. Do not commit proprietary weights, restricted references, secrets or uncleared outputs.

The [playtest freshness protocol](PLAYTEST_FRESHNESS.md) must inventory and hash both local source/intermediate inputs and packaged game assets. Include pinned TRELLIS code/checkpoints, input image digests, export and independent bake versions, retopology and Unreal import settings. A newly generated or changed mesh requires a fresh cook, source/cooked hash seal and relevant visual/owner acceptance.

## Rollout milestones

| Gate | Deliverable | Status |
| --- | --- | --- |
| GEN-00 | Verify the local installation and official model/code/export dependency licenses, reproduce the verified vertex-colour path | Pending general-pipeline qualification; protagonist is existing partial precedent |
| GEN-01 | Create one rights-cleared, documented candidate prop with geometry and material inspection | Pending |
| GEN-02 | Import one game-ready optimized/collidable TRELLIS-derived prop in the actual world and obtain personal owner approval | Pending |
| GEN-03 | Introduce separately reviewed fish and vegetation visual variants, preserving existing data IDs | Pending |
| GEN-04 | Reproducible provenance-tracked batch work with asset freshness, performance and rights audits | Pending |

## References

[Official Microsoft TRELLIS project](https://github.com/microsoft/TRELLIS) · [Official license](https://github.com/microsoft/TRELLIS/blob/main/LICENSE) · [Protagonist pipeline](CHARACTER_PROTAGONIST.md) · [ADR-071](DECISIONS.md) · [Roadmap](ROADMAP.md) · [World map strategy](WORLD_MAP_ADOPTION.md)
