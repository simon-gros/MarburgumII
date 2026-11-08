# Default World Map — Licensed Maribor 3D City Model

**Decision status:** User-selected **planned default source**; **purchase pending, no files received/imported, not yet qualified**.  
**Engine:** Unreal Engine **5.8.3**.  
**Asset:** [TurboSquid product 1639711 — Maribor Slovenia](https://www.turbosquid.com/3d-models/3d-city-area-building-model/1639711).  
**Development intent:** Adopt this third-party Maribor city model as the **primary geometric foundation** of MarburgumII, **replacing the original Marburgum generated-map workflow as the default map source** after the purchased deliverable passes the intake and owner's visual/playable acceptance gates. Original Marburgum remains the source of authored gameplay, interactions, storyline, fictionalized identities, period corrections, fish and vegetation ecology; it is not the default base-mesh importer.

## 1. Product listing facts and limits

The seller currently describes this product as a **Maribor, Slovenia** 3D city model, with GIS data **year 2022** and "**30km**" size. Listing formats include **FBX**, **OBJ**, 3DS, STL, DXF, Rhino and Cinema 4D. It claims building/road/rail/water/land-use and other thematic layers, included base satellite/map textures, and a native Cinema 4D scene. The listing shows approximately **100,000,000 polygons/vertices** in specifications yet describes the model as "**Low Poly**" and says "5m+" polygons in large cities. Treat all such performance figures as **unverified seller metadata** pending a per-file geometry inspection. "30km" does not unambiguously specify playable radius, diameter, side length or exact city coverage.

**Most important export caveat:** The seller states that **building sketch/toon strokes, MoGraph-cloned trees, water effects and stroked roads are only in the native Cinema 4D file**. FBX/OBJ must **not** be assumed to contain finished vegetation, animated river water, correct road appearance or native C4D material effects. Unreal material/river/foliage work remains real work.

A satellite/map texture is mentioned but its independent source, dates, distribution rights, actual CRS/axis units and 1990s suitability are **not verified** until files and purchase/license evidence are available. The model is **not** assumed to be a navigable, textured, collidable, performant game level or 1990s reconstruction out of the box.

## 2. License and repository protection — blocking

The product is paid/royalty-free **for permitted uses**, not public-domain or freely redistributable. TurboSquid's published license generally allows games and **closed MMO** experiences but restricts redistribution of the raw model and sufficiently extractable forms. An **open-source project repository is not a license for proprietary map contents**. Do **not** add purchased FBX/OBJ/C4D, textures, unprotected derived mesh/geometry, converted source payloads, renders with excluded external imagery, or equivalent data to public GitHub, Releases, Git LFS or automatic public CI artifacts. Keep product receipts, licensing terms, files and derived build products **outside version control** in protected local storage. Only source-neutral **scripts, schemas, non-sensitive manifest hashes, transform rules, audit summaries and independently authored game code** may be committed.

The final game distribution pipeline must package purchased content in a manner consistent with the actual license. Verify with TurboSquid support if the game's planned online world, modding/export facilities, asset-extraction exposure or developer collaboration creates licensing ambiguity. Do not use an open-world asset export/marketplace model without appropriate rights.

[Model license](https://blog.turbosquid.com/turbosquid-3d-model-license/) is the legal authority, not a generic "royalty-free" claim; revisit the license attached to the actual purchase. If we want game source to be public, provide a reproducible optional local ingest step that **requires the developer to own a license**. Public checkout builds must have an independently authored placeholder/test district and may omit the proprietary city.

## 3. Data source precedence and spatial reconciliation

The intended priority is:

1. **City structure base:** accepted purchased city geometry and its internal building/road organization.
2. **Historical accuracy:** reviewed archival 1990s corrections override the 2022 built environment wherever the fiction's period matters. Missing/modern buildings, transport, signs, road plans and details need conscious transformations rather than silent acceptance.
3. **Canonical interaction geography:** stable legacy Marburgum identities and authored district/place/mission coordinates must be mapped by a reproducible transform, **not copied as arbitrary 3D coordinates**.
4. **Ecology:** preserve [fish species/habitat migration](../Data/Ecology/fish.v0.1.json) and [vegetation/placement migration](../Data/Ecology/vegetation.v0.1.json); the old 2014 LiDAR distribution is evidence and a candidate placement input, **not another competing base-map**. Do not double-place plant meshes into trees already included in the purchased deliverable.
5. **Rendering/gameplay:** Unreal World Partition, scenery collision, navmesh, instancing/HLOD, landmarks, roads and water are **derived from the adopted asset and tested independently**.
6. **MMORPG authority:** dedicated server owns interactions, position/movement decisions, resource state, NPCs and persistence. A geometry asset is not a network simulation or AOI solution.

A first geospatial benchmark should record exact source file, import units, coordinate frame, material/image source, scene origin and at least 5–8 named fixed landmarks covering different areas (e.g., Old Bridge, Lent, Tabor, Melje, Drava bridges, prominent city crossings), fit one documented transform and report spatial residuals. Any unknown coordinate system is an open problem, **not** a basis to invent an EPSG code. Unreal's optional Georeferencing plugin can help once the supplied coordinates are actually known. Keep source geometry IDs and any crosswalk to canonical district/landmark IDs stable.

**Crucial distinction:** replacing the base map does **not** replace the period social geography or gameplay ontology. We are remapping Marburgum's existing stories, jobs, services, contacts, skills, fish, vegetation and other city semantics into a new geometric realization.

## 4. Acquisition and intake gate (WORLD-001)

Before the purchase is treated as a usable production asset:

- Record purchase confirmation and exact commercial license locally (never credentials/payment documents in GitHub), final file inventory, archive checksums, format versions and permitted distribution/use.
- Compare FBX and OBJ with C4D contents; confirm which of geometry/materials/foliage/strokes/water/texture/roads/rail/terrain survive export.
- Open representative slices in appropriate DCC software; record bounding boxes, mesh counts, vertex/triangle counts, coordinate units, pivot/origin, whether scene is truly 3D elevation or flat, UVs, texture paths, duplicates, mesh grouping, naming and topology.
- Check GIS datum/source attribution and ownership of satellite imagery; do not republish third-party texture/source images.
- Record actual performance and machine resource costs. The listing's large nominal polygon count must not become a monolithic unstreamable Unreal asset.
- Prepare a licensable **local** import without committing any proprietary mesh data.

If this gate fails, keep the asset as a **conditional** candidate and retain the separate test world. A purchase is not a completed import or approved world design.

## 5. UE 5.8.3 import pipeline and quality

**WORLD-001A: local proof.** Import a *small, recognizable* Maribor region (prefer Lent and an adjacent Drava riverbank) into a local Unreal 5.8.3 **World Partition** map. Separate meshes by meaningful road/building/ground/water/land-use category and spatial chunk. Establish real collision, walkable surfaces, correct scale, doorway/bridge clearance, lighting, material and camera navigation. Do not rely on simply opening a seller preview render.

**WORLD-001B: geographic alignment.** Create a transform and stable crosswalk from old Marburgum landmarks, NPC interactions, mission points and the Drava fishing context. Preserve the old `drava_fishing_spot` semantic ID, but only position it after legitimate road/bank geometry validation. Test realistic foot and vehicle paths.

**WORLD-001C: ecology and scene finish.** Reconstruct missing water visuals, wind/foliage, period-correct vegetation and riverbank vegetation. Inspect placement duplications, road overlap, relevant canopy and visibility. Fish habitats remain server-authored logic; water rendering is presentation.

**WORLD-001D: performance/streaming.** Profile native packaged client/Editor for traversal, streaming boundaries, draw calls, VRAM, frame time tails, collision/nav and world-origin behavior. Treat World Partition as **rendering/content streaming** independent of AOI and server replication. Tune instance batching, HLOD, mesh/material budgets and precision based on results; do not prematurely mandate experimental foliage rendering.

**WORLD-001E: historical and public presentation.** Replace or correct visually significant modern street furniture, construction, satellite imagery, signage and vehicles for the selected 1990s date. Apply a consistent high-quality HUD so the first screenshots represent the actual playable district, not a raw untextured import.

## 6. Mandatory owner playtest and approval

**The owner must personally test every major playable feature**, especially this default-map adoption. The acceptance build is not complete merely because imports or automated tests pass.

**Minimum personal map route:** spawn and orient at Lent; recognize the Drava and named landmarks; walk street/sidewalk paths, bridge approaches and fishing-bank access; navigate interactions around correctly aligned mission contacts; drive a short street loop if driving exists; inspect terrain/foliage/water/LOD at pedestrian and elevated camera distances; check day/night and modest motion/stream transitions when implemented. Review appearance as a screenshot/video, legibility of UI, performance/hitches and whether the 1990s Marburgum identity is retained.

Record exact SHA/build, UE 5.8.3 version, map source revision, tested scenarios, observed failures and a **personal explicit outcome**: `approved`, `changes_requested` or `rejected`. Without an approval, the asset remains **candidate / awaiting-owner-test**; do not mark the map or corresponding feature "finished." Subsequent materially changed imports require regression playtest and reapproval.

## 7. Scheduling

- **Stage 0B network smoke:** continue using a free/minimal empty test map. No paid asset dependency.
- **Stage 0C UI design:** can use internally labeled prototype screenshots; do not require paid map purchase to build widgets.
- **Stage 1 early district:** once purchased/qualified, use licensed Maribor **Lent/Drava** slice as the default showcase district instead of building the old 121-window mesh pipeline. If purchase is pending, a clean fallback prevents blocking other milestones.
- **Stage 2:** first real gameplay interactions/fishing/missions reuse original semantic data mapped to the selected geometry.
- **Stage 3:** progressively extend licensed geometry across city, applying 1990s corrections and streaming, collision and ecological review. Full world-scale import only after representative slice earns personal approval.

No original generated base-city import is a default prerequisite. The original's authored source data remains a strategic asset, and its original world mesh/placement pipeline is preserved as **reference/fallback**, not silently deleted.

## 8. Optional supporting image-to-3D production

The owner's locally installed Hunyuan3D-2.1 can be evaluated as a source of **distinctive props, period details, vegetation/fish geometry and PBR material candidates**, not a wholesale replacement for the planned licensed Maribor city source. The [asset-production workflow](ASSET_GENERATION_HUNYUAN3D.md) and rights check apply independently to both the generator's license and each input photograph. **The published generator community license excludes EU territory**, so generated assets cannot enter the production map until applicable use rights are resolved. Never infer accurate dimensions, GIS coordinates, period identity, botanical species or Unreal-ready collision from an image-generated shape. Review native mesh quality, lighting, PBR materials, LODs, scale, sources, streaming, and owner playtests.

## 9. References

- [TurboSquid model listing](https://www.turbosquid.com/3d-models/3d-city-area-building-model/1639711)
- [TurboSquid 3D Model License](https://blog.turbosquid.com/turbosquid-3d-model-license/)
- [Epic: FBX static mesh pipeline](https://dev.epicgames.com/documentation/en-us/unreal-engine/fbx-static-mesh-pipeline-in-unreal-engine)
- [Epic: Georeferencing](https://dev.epicgames.com/documentation/en-us/unreal-engine/georeferencing-a-level-in-unreal-engine)
- [Epic: Level streaming](https://dev.epicgames.com/documentation/en-us/unreal-engine/level-streaming-in-unreal-engine)
- [Gameplay/world ontology](ONTOLOGY.md), [ecology catalogues](../Data/Ecology/vegetation.v0.1.json), [fishing catalogue](../Data/Ecology/fish.v0.1.json), [top-level roadmap](ROADMAP.md).
