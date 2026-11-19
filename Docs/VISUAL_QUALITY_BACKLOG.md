# Visual and Environmental Quality Backlog

**Started:** 19 November 2026 on branch `qa/visual-audit-2026-11-19`. Surveyed at `main` `02c68cf` (ADR-069); the branch was then brought up to `417bcf3` (ADR-071), whose changes do not touch the level or its assets. **Decision:** [ADR-072](DECISIONS.md).
**Scope:** the playable level `/Game/World/lent` (ADR-045: Lent and its eight neighbouring 512 m windows, with the distant ring of ADR-054) and every static mesh, texture and material instance under `/Game`.
**Status meanings:** **FIXED AND VERIFIED** (changed and checked by the stated test), **FIX NOW** (safe and understood, not yet done), **TO BE DONE** (needs new assets, redesign or substantial work), **NEEDS VERIFICATION** (plausible, not yet proven), **BLOCKED** (waits on a dependency, purchase, reference material or an owner decision).

This file is the single register for graphical, environmental and 3D-asset problems. Functional QA stays in the [QA audit](https://github.com/simon-gros/MarburgumII/blob/qa/fixes-2026-11-19/Docs/QA_AUDIT_2026-11-19.md) of the same day. Keep an entry's ID when its status changes; add new entries at the end of their group.

## What was inspected, and what was not

| Inspected | How | Evidence |
| --- | --- | --- |
| The whole level, statically | `-run=MarburgumWorldAudit`: every placed primitive (missing meshes, empty or engine-default material slots, duplicate placements, props floating or sunk, actors outside the square), per-folder triangles/LOD/Nanite/collision/draw-distance counts, the height of every hand-placed actor above the surface under it | `Saved/VisualAudit/audit-baseline/`, `audit-after/` (not committed; regenerate with the command below) |
| Every asset under `/Game` | The same commandlet: 1,571 static meshes (triangles, LODs, Nanite, collision, UV channels, sections, bounds), 88 textures (size, mips, compression, sRGB, streaming), 72 material instances (parents) | as above, `meshes.tsv`, `textures.tsv` |
| All nine windows, rendered | `scripts/visual-survey-linux.sh`: 42 fixed views at Maribor noon in clear weather, editor-mode server and offscreen 1920×1080 client: an oblique aerial of each window, three eye-height views per window down the street people's walk graph, the four map edges, the Stari most deck. Run twice with different street samples: before the corrections (first camera set) and after them (`mb2 survey-views`, seed 1990), 54 different street views in all | `Saved/VisualSurvey/baseline-noon/`, `after-noon/` |
| The packaged `main` build | The earlier QA survey's ten views of the morning's package (`marburgum-qa/Saved/QA/views-baseline-noon2`) | outside this repository |
| Defects found | 27 close-ups before and after each correction | `Saved/VisualSurvey/closeups-*`; selected images in [Captures/visual-audit](Captures/visual-audit/) |

**Not inspected:** night, dusk and the weather variants (ADR-062/064/065 have their own captures), any packaged build that contains this branch's changes, the ring at street level, close-ups of vehicles and characters, the Melje/Tabor temporary floors (ADR-052), and most of the 870 buildings one by one. A view per window at street level is a sample, not coverage: **the world has not been visually verified as a whole.** World Partition, streaming seams and HLOD transitions cannot be inspected because the level is one always-loaded level with no streaming (ADR-045); the nine windows share one landscape and one set of paved chunks, and no seam between windows was seen in the nine aerials.

**Performance numbers are a first reading, not a qualification.** The view tool now logs frame, game-thread, render-thread and GPU time per view (`stats.tsv`). In the first survey another session's smoke client shared the GPU (92 % utilisation) and those numbers are discarded. In the final survey (`Saved/VisualSurvey/after-noon`) the GPU was free: every one of the 42 views took 2.55–3.22 ms of GPU time at 1080p in editor `-game` mode on the RTX 5080, and 1.6–2.5 ms of render-thread time, except three Pobrežje views at 7–8 ms (not investigated). These are a high-end GPU and an editor process, not a packaged client on target hardware. The RHI draw-call and primitive counters it logs count only the immediate command context and undercount parallel rendering. No optimisation was made on these numbers (VQ-028).

## How to repeat it

```bash
# Static audit (read-only): findings.tsv, meshes.tsv, textures.tsv, folders.tsv, actors.tsv, summary.txt
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/MarburgumII.uproject" -run=MarburgumWorldAudit -Out="$PWD/Saved/VisualAudit/<name>" -unattended -nosplash -nullrhi
# Every surface a vertical ray meets at X,Y (Unreal cm), top down: what stands on what
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/MarburgumII.uproject" -run=MarburgumWorldAudit "-Probe=9931,26;7042,1813" -unattended -nosplash -nullrhi
# Rendered survey (42 views; MB2_SURVEY_VIEWS for close-ups, MB2_SURVEY_CLOCK / MB2_SURVEY_WEATHER for other light)
MB2_SURVEY_PORT=27891 ./scripts/visual-survey-linux.sh editor <name>
# The corrections of this audit, idempotent; -DryRun reports, -Explain says why a sign spot was refused
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/MarburgumII.uproject" -run=MarburgumVisualFixes -unattended -nosplash -nullrhi
```

`MarburgumVisualFixes` must run again after `MarburgumCreateMaps -FictionLent` (which re-places the opening content) and after a façade or ground rebuild (DEVELOPMENT.md).

## Summary

| Group | Fixed and verified | To be done | Needs verification | Blocked |
| --- | ---: | ---: | ---: | ---: |
| A. Placement and rendering defects | 4 | 3 | 1 | 1 |
| B. Geography and world composition | 0 | 4 | 3 | 1 |
| C. Existing assets | 0 | 4 | 2 | 0 |
| D. Missing content | 0 | 5 | 0 | 0 |
| E. Performance and continuity | 0 | 2 | 0 | 0 |

The static audit found **no** missing meshes, empty or engine-default material slots on placed meshes, orphaned material instances, duplicate placements or failed asset loads in the level. The level's most visible problems are not defects in what exists but **what does not exist yet**: no trees, almost no street furniture, mission people and props drawn as engine primitives, and landmarks drawn as ordinary shells.

## A. Placement and rendering defects

| ID | Title | Where | Observation and evidence | Sev. / prio. | Status | Correction | Effort | Verification |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| VQ-001 | The Lent angler floated 1.31 m above the paving | `MB2Npc_lent_angler`, Marburgum/Opening, (70.4, 18.1) m | Origin 265.8 cm over paving at 134.8 cm; its body and rod hovered with the shadow far below (`angler_side` before). The ground rebuilds (ADR-040/045/053) keep each placed actor's height above what it stood on, so an early offset survived every rebuild | High / P1 (the first person a new player meets) | **FIXED AND VERIFIED** | `MarburgumVisualFixes -Steps=people` stands every `MB2Npc_*` on the first surface under it | S | Audit: origin 0.00 m above `MB2Ground_LentPaved_12_10`; render `angler_side` after; mission smoke on `/Game/World/lent` passes, its fishing step at the angler (`Saved/MissionSmoke/20261119T205845Z`) |
| VQ-002 | The bridge player start was 9.55 m above the Stari most deck | `MB2Start_bridge_0`, (99.3, 0.3) m | 2,289.8 cm over the deck at 1,334.8 cm; a character spawned there drops 8.5 m. Other starts stand 0.96–1.19 m up | Medium / P1 | **FIXED AND VERIFIED** (static) | `-Steps=starts`: a start more than 1.5 m above its surface goes 1 m above it (1,434.8 cm) | S | Audit trace; the Lent smoke that uses the bridge start runs the blockout `/Game/Maps/Lent`, so no runtime spawn on the real deck was exercised (VQ-008) |
| VQ-003 | Slovene letters drew as a box on placed signs | `MB2Sign_street` "Obreška pot" and every `TextRenderActor` | `TextRenderComponent`'s default font (`RobotoDistanceField`) has ASCII only; "š" rendered as a missing-glyph box (`sign_street` before) | Medium / P1 (a Slovene city) | **FIXED AND VERIFIED** | `-Steps=font` builds `/Game/Marburgum/Fonts/F_SignRoboto`, an offline distance-field font from the engine's own `Roboto-Bold.ttf` with ASCII, Latin-1, Latin Extended-A and typographic quotes (393 glyphs, one 1024² page), and sets it on every opening sign | S | Render `sign_street` after reads "Obreška pot"; the commandlet refuses a font whose š glyph is empty |
| VQ-004 | Shop signs inside, behind or across façades | `MB2Sign_shop_0..2` | Placed from the pre-ADR-034 Lent shells: "Kavarna Pri Toku" stood inside a building, "Ribarnica Na Bregu" ran under a neighbour's corner, "Pekarna Pri Mostu" sat in the eaves (`sign_shop_*` before) | Medium / P1 | **FIXED AND VERIFIED** | `-Steps=signs`: on the façade in front, 3 cm proud, facing out, 3.4 m above the street (or the nearest height between window rows, clear of eaves), only where every point of the lettering is plain render on one flat stretch of one wall and a passer-by 8 m out sees it. Building shells collide on both faces, so spots found from inside another building are refused | M | Renders after; a second run moves nothing (`fixes-idempotent-*.log`) |
| VQ-005 | Dark wedges where paving meets lower ground | grass verges and islands in every window; e.g. `street_magdalena_0`, `street_center_0`, `street_ljudski_vrt_0`, `street_titov_most_0` | The paved meshes' outer edging (ADR-040, kerb material, 35 cm deep) shows as a near-black band where the landscape is a few cm to dm lower. Either the landscape cell between a paved corner (15 cm under) and an unpaved one exposes the edging, or ambient occlusion darkens the crevice | Medium / P2 | **NEEDS VERIFICATION** | Measure landscape vs paving height along 100 edge samples (`-Probe`) and view the edging with AO off. If exposure: carry ADR-053's verge rule to the cell corners next to the edge; if AO: a lighter edging material | M | Same street views show no band thicker than the kerb face |
| VQ-006 | Mission people and places drawn as engine primitives | Server-spawned `AMarburgumNpc`/`AMarburgumInteractable` (all mission content, ADR-047–052, ADR-060) and the level's angler | Engine `BasicShapes` cylinder bodies, sphere heads and cubes in the engine's checker material, next to street people drawn with the mannequin and authored looks (`angler_side`, `lent_promenade`) | High / P1 | **TO BE DONE** | Draw mission people with the street-people character and look catalogue (the `lent_angler` look already exists, ADR-068); give each place kind a prop mesh (VA-04) | M | Close-ups at each mission start (`?MB2Start=mission_<id>`) show no primitive |
| VQ-007 | Placed text is bare lettering on walls | the four opening signs | No sign board, plate or shop front; the street name is painted on a façade instead of a street-name plate | Low / P3 | **TO BE DONE** | Sign boards and Maribor street-name plates (VA-06) carrying the same `F_SignRoboto` text | S–M | Close-ups |
| VQ-008 | The Lent smoke runs on the blockout map | `scripts/lent-smoke-linux.sh` (`/Game/Maps/Lent`) | Starts, the angler and signs of the playable `/Game/World/lent` are not exercised by that smoke | Medium / P2 | **TO BE DONE** | Run the Lent smoke on `/Game/World/lent` (its W/F/T clients would then spawn on the real deck) or retire the blockout | S | Smoke passes on `/Game/World/lent` |
| VQ-009 | Street lamps and weather visuals missing from packaged `main` | packaged client | Found by the QA audit: assets loaded by path at run time are not cooked | High / P0 | **BLOCKED** (fixed on `qa/fixes-2026-11-19` at `00b76cc`, awaiting the owner's merge decision) | `DirectoriesToAlwaysCook` | — | Staged views of that branch show the lamps |

## B. Geography and world composition

Positions, footprints and heights come from the original game's OpenStreetMap footprints, the ARSO 2014 LiDAR and the 2026 OpenStreetMap streets (ADR-040/045). "Confirmed" below means the project's own data or a well-documented landmark contradicts what is drawn; the rest needs reference material.

| ID | Title | Where | Observation and evidence | Sev. / prio. | Status | Correction | Effort | Verification |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| VQ-010 | Landmarks are ordinary shells | Stolna cerkev (`w103104221`), Sodni stolp (`w158684490`), Vodni stolp (`w121903446`), Židovski stolp (`w171854182`), Rotovž / Mestna občina (`w106425845`), Grad (`r2233620`), Narodni dom, Stari most details | **Confirmed for the cathedral:** its footprint carries one eaves and ridge (9.2 m and 15.5 m above its ground), so its bell tower, the old town's tallest landmark at about 57 m, is not drawn. **Confirmed for Sodni stolp:** a 24-vertex round footprint under a gabled roof (`buildings.json`), where the tower has a conical roof. The others are plausible boxes that need photographs to judge | High / P2 | **TO BE DONE** | Bespoke meshes from period photographs and measured drawings for the 8–10 landmarks that name places on the banner (ADR-063); the shell mesher keeps everything else | L | Side-by-side with reference photos from the same viewpoints |
| VQ-011 | The 2014 survey's deep hollows | 14 hollows over 100 m² and 3 m deep (`ground.json`), e.g. behind the Minorite monastery | Recorded by ADR-045 as the owner's call for a 1990s map | Medium | **BLOCKED** (owner decision) | Fill as 1990s courtyards, or keep | M | — |
| VQ-012 | Heaps of "retaining wall" against buildings | e.g. `street_titov_most_2` | Steep landscape drawn in the stone-wall layer (ADR-040, >45°) forms triangular grey heaps at building feet | Medium / P2 | **NEEDS VERIFICATION** | Locate all with a slope query on `heights.bin` next to footprints; decide wall vs earth bank per case | M | Street views at each |
| VQ-013 | Bridge colours | `lent_stari`, `titov_most_titov`, `titov_most_zelezniski`, `pristan_studenska` all use `MI_steel_painted` (red) and `MI_concrete_utility` | Titov most is a concrete road bridge; the railway bridge's 1990s paint is unknown. The dark look of Titov most from the street (`street_titov_most_0`) is mostly backlight | Low / P3 | **NEEDS VERIFICATION** | Per-bridge material instances from photographs | S | Photo comparison |
| VQ-014 | Façades are one pattern everywhere | all 870 shells | One window type in rows, plinth, string course and cornice (ADR-054); no shop fronts, doorways, window surrounds, balconies, old-town vs post-war difference | Medium / P2 | **TO BE DONE** | Façade grammar per building class and period; shop fronts on the old-town ground floors where ADR-069 places shops | L | Street-level survey |
| VQ-015 | 2026 streets in a 1990s game | streets, crossings, markings | OpenStreetMap shows today's streets (ADR-040). Known period changes (e.g. pedestrianisation of the old town) are not applied | Medium / P2 | **NEEDS VERIFICATION** | 1990s corrections from period maps (needs sources) | M | Per-street review |
| VQ-016 | Bare courtyards and patchy grass | courtyards and islands in all windows (`air_*`) | Hard-standing in yards and yellow dry patches from the orthophoto mask (ADR-053) read as dirt from above | Low / P3 | **TO BE DONE** | Courtyard surfaces by land use; trees (VQ-022) cover most | S–M | Aerial survey |
| VQ-030 | Bridge decks are bare slabs | `Bridge_stari` and the neighbours' bridges | The Stari most deck is one flat concrete surface between block parapets: no carriageway, pavements, markings, railings or lamps (`stari_most_deck`); the paved meshes stop at the bridge ends (ADR-040) | Medium / P2 | **TO BE DONE** | Lay carriageway, pavements and markings on the decks from the OSM bridge ways; railings and lamps from photographs | M | Deck view |

## C. Existing assets

| ID | Title | Where | Observation and evidence | Sev. / prio. | Status | Correction | Effort | Verification |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| VQ-017 | Unreferenced legacy assets | `/Game/World/Orthophotos` (25 MB: the 2020–22 photo, its normal map and `M_lent`), `/Game/World/Roads/lent` (the pre-ADR-040 road sheet), `/Game/World/Semantics/T_lent` (used only by `M_lent`) | Nothing in the level or code refers to them; they are not cooked. Kept by ADR-040 for provenance | Low / P3 | **TO BE DONE** | Remove after the owner agrees, keeping the source records | S | Audit shows no references, cook unchanged |
| VQ-018 | Ground normal maps in the wrong texture group | 11 `T_*_N` under `/Game/World/Ground/Textures` | Imported as `TEXTUREGROUP_World`, not `WorldNormalMap` (the building textures are right) | Low / P3 | **TO BE DONE** | Set the group in `MarburgumRebuildGround`'s texture import | S | `textures.tsv` |
| VQ-019 | The surface mask is always resident | `T_LentSurface` 4096² | `NeverStream`, 4096² `TC_VectorDisplacementmap` (64 MiB uncompressed) | Low / P3 | **NEEDS VERIFICATION** | Check whether the landscape material needs it unfiltered; otherwise compress or stream | S | Memory report |
| VQ-020 | Roof tiles at 512² | `T_roof_tile_clay_*` | The most visible texture from every aerial is the smallest; tiling shows from the air, and close-ups (`sign_shop_1` earlier) are soft | Low / P3 | **TO BE DONE** | 2048² CC0 set with variation | S | Close-up and aerial |
| VQ-021 | Water is one colour | all `Drava*` sheets | Even turquoise, no shore foam, flow or depth variation (ADR-053 not covered) | Low / P3 | **TO BE DONE** | Foam and flow from the shore distance field | M | River views |
| VQ-027 | A mannequin normal map compressed as colour | `/Game/Characters/Mannequins/Textures/Manny/T_Manny_02_N` | `TC_Default`, sRGB off, while named as a normal map; the engine's sample content, used by the mannequin-based characters | Low / P3 | **NEEDS VERIFICATION** | Check whether the material samples it as a normal; if so set `TC_Normalmap` | S | `textures.tsv`, character close-up |

## D. Missing content

| ID | Title | Where | Observation and evidence | Sev. / prio. | Status | Correction | Effort | Verification |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| VQ-022 | No trees or shrubs in the playable square | whole square; the ring shows canopies as soft mounds, so the edge is green and the city bare | `actors.tsv`: no vegetation; every aerial | High / P1 | **TO BE DONE** | Port the original game's vegetation (VA-01/02): its `vegetation.json` species and regions, its survey-based placement and its 20 Marburgum-owned species meshes, instanced; leave carriageways, walk-graph clearance and sightlines free | L | Aerial and street survey; frame time measured on a free GPU |
| VQ-023 | Almost no street furniture | whole square | Only the runtime street lamps (ADR-065). No benches, bins, bollards, fences, bus shelters (ADR-069 bus stops have none), kiosks, traffic signs, lights or street-name plates | High / P1 | **TO BE DONE** | First the original's surveyed furniture (VA-05: stop posts, shelters, benches, fences, walls, bollards, 507 placements); then VA-06 | M–L | Street survey |
| VQ-024 | No quay wall or railing along Lent | the Drava banks | ADR-040 recommendation 4: the promenade ends in a paved edge over the water | Medium / P2 | **TO BE DONE** | Stone quay wall and railing mesh along the mapped bank (VA-08) | M | River views |
| VQ-025 | Road markings | carriageways | Only centre lines and zebras (ADR-040); no stop lines, lane arrows, parking bays | Low / P3 | **TO BE DONE** | Extend the ground tool's marking pass from OSM lanes/turn tags | M | Street survey |
| VQ-026 | No parked or moving traffic | streets | One parked hatchback; traffic waits for its own milestone (ADR-068) | Medium / P2 | **TO BE DONE** | Parked cars from the five-car catalogue on parking areas | M | Street survey |

## E. Performance and continuity

| ID | Title | Where | Observation and evidence | Sev. / prio. | Status | Correction | Effort | Verification |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| VQ-028 | Everything drawn at full detail at every distance | 1,500 static meshes: 870 shells (3.17 M triangles, 7 sections each), 521 paved chunks (5.39 M), 119 ring meshes (1.04 M) | `folders.tsv`: no Nanite, one LOD, no draw distance on any of them; 9.63 M LOD0 triangles always loaded. On a free RTX 5080 in editor mode it costs 2.6–3.2 ms of GPU time per 1080p frame, so it is no problem there; target hardware is unmeasured | Medium / P1 | **TO BE DONE** | Measure first, on a packaged client on a free mid-range GPU (P0 performance gate, ROADMAP). Then Nanite for shells and paving (they are opaque, closed and static), or HLOD for the ring and far windows. Set in the generators, not by hand | M | `stats.tsv` before/after on the same views with the GPU free |
| VQ-029 | No streaming | the level | One always-loaded level (ADR-045); streaming seams, HLOD and load distances cannot be checked yet | — | **TO BE DONE** (with World Partition, Stage 3) | — | L | Repeat section 5 checks when streaming exists |

## Asset requirements inventory

Nothing new was modelled in this pass. New meshes are made with Microsoft TRELLIS, the project's only selected image-to-3D generator ([ADR-071](DECISIONS.md), [ASSET_GENERATION_TRELLIS.md](ASSET_GENERATION_TRELLIS.md)), by its vertex-colour → retopology → Blender/Cycles bake route; Hunyuan3D-2.1 is superseded and its EU licence gap no longer matters. Every reference image needs its own rights record. Before any new asset is made, the original game's Marburgum-owned content comes first: it already has the species, placements and procedural meshes, built by the original's own import tools (`tools/unreal_import_coverage.py`: "street_furniture … 507 stops and barriers in 90 windows, 6 shared"). GTA/Rockstar placeholders in the original stay out (as for ADR-029/039).

| ID | Asset | Purpose | Target location | Technical requirements | Existing alternatives | Proposed method |
| --- | --- | --- | --- | --- | --- | --- |
| VA-01 | Urban trees: lime, Norway maple, London plane, horse chestnut | Streets, squares, parks; place recognition and shade | Survey canopy points in the square (`urban`, `park`, `residential` regions) | Instanced, 2–3 LODs or Nanite foliage after measurement, wind, trunk collision only, 0.75–1.3 scale | Original `UnrealMarburgum/Content/Vegetation/Species/*` (20 species meshes) and `project/data/vegetation.json`; the original's own roadmap calls the archetypes placeholders | Port original meshes and placement first; better silhouettes later from rights-cleared photographs through TRELLIS (ADR-071) |
| VA-02 | Riparian trees and shrubs: white willow, black alder, black poplar, reeds | The Drava banks, fishing spots | `drava` region | as VA-01; no collision on reeds | Same original species | Same |
| VA-03 | Mission people | Every mission NPC (VQ-006) | Server-spawned at mission spots | The street people's character and looks | `lent_angler` and the other ADR-044/068 looks | Code change only |
| VA-04 | Mission props: debris, crate, notice board, service-room door, fishing gear, kiosk counter | Places of ADR-049–052/060 | Mission spots | ≤5k triangles, simple collision, readable at 10 m | None in either project beyond primitives | CC0 libraries (Kenney, Quaternius, Poly Haven) or TRELLIS |
| VA-05 | Surveyed street furniture: stop posts, bus shelters, benches, fence and wall panels, bollards | Streets, bus stops of ADR-069 | Original placements (90 windows; the nine needed) | Collision for walls/fences/shelters; snapped to paving | Original `World/StreetFurniture/SM_Street_*_01` (6 meshes) | Port with ADR-045's window transforms |
| VA-06 | Period signage: Maribor street-name plates, 1990s Slovene traffic signs, traffic lights, shop signs | Street identity, VQ-007 | Junctions and named streets (`lent_places.json`) | `F_SignRoboto` text on plates; decals for small signs | None; CitySample on disk is US-styled and Epic-licensed | Author simply from photographs; text generated from the place register |
| VA-07 | Landmarks: cathedral tower, Rotovž, Sodni/Vodni/Židovski stolp, Grad, Stari most details | VQ-010 | Their footprints | Hand-modelled, LODs, façade materials matching the shells | None | Manual modelling from reference photographs and drawings; needs reference material (owner) |
| VA-08 | Quay wall and railing | VQ-024 | Mapped Drava bank in the square | Spline mesh, collision, fishing-spot compatible | None | Procedural from the bank line |
| VA-09 | Façade parts: shop fronts, doorways, window surrounds, balconies | VQ-014 | Old-town ground floors first | Mesher modules, closed shells kept | None | Extend `MarburgumBuildingMesher` |

## Roadmap integration

[ROADMAP.md](ROADMAP.md) "Current engineering order" now names the visual priorities: VQ-022 vegetation and VQ-023 surveyed street furniture (original content), VQ-006 mission people/props, VQ-010 landmarks, and VQ-028 measured before any Nanite/HLOD change.
