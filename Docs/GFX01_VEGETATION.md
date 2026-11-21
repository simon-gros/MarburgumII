# GFX-01 — Trees and bushes on the grass

**21 November 2026 · branch `feat/gfx-01-vegetation` · [Graphics roadmap](GRAPHICS_ROADMAP.md) GFX-01 · [VQ-022](VISUAL_QUALITY_BACKLOG.md) · [ADR-087](DECISIONS.md).**
**State: implemented and editor-verified; not packaged or measured on a free GPU; owner look open.**

## The rule

The owner's instruction: **plants grow only on grass**, ground that is not pavement, road, square, path, building or water. So the map gets far fewer plants than the original game, which stood a surveyed tree wherever the 2014 survey found one, on pavements, yards and car parks included.

`lent-ground --mode plants` (`tools/lent-ground/src/plants.rs`) reads the original's `<window>_plants.json` for the 25 windows of ADR-090 (the nine of ADR-045 when this was first built; see [the five by five](#the-five-by-five-adr-090)). They are on the same plan origin, the middle of Stari most. It keeps a plant only when all of these hold:

| Test | Tree | Bush |
| --- | --- | --- |
| Grass is the landscape's dominant surface (`surface.png`) at the trunk and at ≥ 8 of 9 points round it | within 1.0 m | within 0.5 m |
| No paving drawn within that reach (`top.bin` more than 0.1 m over `heights.bin`) | 1.0 m | 0.5 m |
| Clear of buildings | 1.0 m | 0.3 m |
| Clear of the street people's walk graph (ADR-044) | 1.0 m (the trunk blocks) | 0.6 × crown radius, 0.6–3 m |
| Not in the river, on a bridge, or within 1.5 m of the square's edge | yes | yes |

A plant that fails is **dropped, never moved**. The original had already settled each trunk and pruned each crown to its site. Positions, crown radius, height and species are the original's: the ARSO 2014 survey, with species assigned by ecological region and not measured. Yaw and the ±6 % height jitter use the original's Unreal import hash, so a tree turns as it did there.

**Result:** 9,959 of the 35,380 surveyed plants in the square stand on grass: **4,972 trees and 4,987 bushes** (28 %).

| Refused because | Trees | Bushes |
| --- | --- | --- |
| Paving within reach | 9,196 | 6,532 |
| Not grass (hard standing, earth, gravel, river stones) | 4,656 | 4,520 |
| On a bridge | 133 | 11 |
| Near the square's edge | 192 | 149 |
| Against a building | 19 | 0 |
| In the river | 7 | 4 |
| On a walk line | 2 | 0 |

By species: Norway maple 1,906; privet 1,710; dogwood 1,303; lime 977; elder 766; silver birch 741; bramble 667; horse chestnut 507; hazel 393; London plane 345; white willow 181; reeds 148; pedunculate oak 115; black poplar 96; ash 58; black alder 46.

## The five by five (ADR-090)

When the map grew to 25 windows, the same rule ran over all of them (`WINDOWS` in `plants.rs`, the 2,560 m square of the 5×5 ground): **40,604 of the 124,392 surveyed plants stand on grass, 20,144 trees and 20,460 bushes** (33 %). Plants per window, north up:

| | far west | west | middle | east | far east |
| --- | --- | --- | --- | --- | --- |
| far north | 2,404 | 3,495 | 2,929 | 3,062 | 1,450 |
| north | 1,062 | 781 | 333 | 367 | 633 |
| middle | 1,136 | 692 | 400 | 862 | 1,081 |
| south | 1,532 | 1,043 | 1,072 | 4,550 | 2,506 |
| far south | 1,386 | 1,394 | 1,268 | 3,416 | 1,750 |

The middle nine keep within 3 % of their earlier counts; the 5×5 ground's finer surface mask (0.3125 m against 0.375 m) moves a few grass edges. Refused: paving within reach 27,756 trees and 22,710 bushes, not grass 15,610 and 16,695, near the square's edge 351 and 395, on a bridge 134 and 13, against a building 68 and 14, in the river 16 and 6, on a walk line 19 and 1. The street furniture was placed again against the new trunks ([GFX01_STREET_FURNITURE.md](GFX01_STREET_FURNITURE.md)).

## In the game

- **Data:** `Content/Marburgum/Vegetation/lent_plants.json` (staged with the build), read by `MarburgumVegetation.h` in the shared module.
- **Meshes:** the original's 16 species meshes, copied unchanged to `/Game/Vegetation` ([provenance](../Content/Vegetation/README.md); always cooked).
- **Materials:** the glTF import's instances drew every crown and trunk near-black in Lent's light ([before | after](Captures/gfx01-2026-11-21/materials-gltf-vs-ours-street_pristan_0.jpg)), although their colours and normals were right (the step logs at most 59 of 2,860 triangles facing away from their winding). `scripts/create-weather-content.sh plants` replaces them:
  - `M_PlantFoliage`: two-sided foliage shading, 60 % of the leaf colour let through;
  - `M_PlantBark`: plain lit wood;
  - one instance per species, keeping the original's own colour.
- **`UMarburgumPlantsSubsystem` / `AMarburgumPlants`** (game module): on `/Game/World/lent`, server and every client ground each plant by a trace and put the plants up as one instanced component per species.
  - Leaves never block.
  - A tree's trunk blocks through an invisible instanced cylinder (species trunk radius × crown scale, at least 10 cm; trunk height × growth, at least 2.5 m). Like the street lamps, the trunks are not replicated: every machine builds the same ones from the same file.
  - A dedicated server puts up only the trunks.
  - Draw distance is the original's budget: 30 m per metre of natural height, between 70 and 700 m, fading over the last 15 %.
  - Only plants over 6 m cast shadows, and only trees carry distance fields.
  - `-MB2NoPlants` leaves the plants out.
- **Checks:**
  - `lent-ground` tests: the rule on a fixture (grass, hard standing, paving, a building and a walk line), and the original's hash.
  - Automation `Marburgum.Shared.Vegetation.File`: the reader, scales, draw distance, trunk transform and the shipped file.
  - `mb2 validate plants`: format, rows in the square, a mesh for every species used, staging and cooking.

## Evidence (editor game mode, RTX 5080, 1080p, Epic, hardware Lumen)

Same build, same 47 views of `gfx-baseline-linux.sh` at June noon. **A**: `-MB2NoPlants`; **C**: plants with our materials (`Saved/GfxBaseline/veg-A-noplants`, `veg-C-materials`; B was plants with the glTF materials). Both the server and the client log `MB2PLANTS event=placed plants=9959 grounded=9959 no_ground=0 trunks=4972`; the client adds `species=16 missing_meshes=0`. Placement takes 0.03 s on the server and 0.09 s on the client.

| | A, no plants | B, plants (glTF materials) | C, plants (ours) |
| --- | --- | --- | --- |
| GPU p50, median of 47 views | 4.85 ms | 4.95 ms | 5.12 ms |
| VRAM, max | 3,670 MB | 3,682 MB | 3,692 MB |

**These numbers are contended:** another session's packaged smoke matrix shared the GPU in 40–45 of 45 samples of every run. They say the plants cost a few tenths of a millisecond in editor mode, not what a packaged client pays. Dusk was captured for A and B only.

Captures, A | C at noon:

- [Far bank from the quay](Captures/gfx01-2026-11-21/ab-noon-quay_upstream.jpg)
- [Pristan street](Captures/gfx01-2026-11-21/ab-noon-street_pristan_0.jpg)
- [Stari most from the river](Captures/gfx01-2026-11-21/ab-noon-river_to_stari_most.jpg)
- [Lent roofs](Captures/gfx01-2026-11-21/ab-noon-roof_lent.jpg)
- Aerials: [Lent](Captures/gfx01-2026-11-21/ab-noon-air_lent.jpg), [Pristan](Captures/gfx01-2026-11-21/ab-noon-air_pristan.jpg), [Ljudski vrt](Captures/gfx01-2026-11-21/ab-noon-air_ljudski_vrt.jpg)
- [Magdalena](Captures/gfx01-2026-11-21/ab-noon-street_magdalena_0.jpg)
- [North edge](Captures/gfx01-2026-11-21/ab-noon-edge_north.jpg)
- [The angler](Captures/gfx01-2026-11-21/ab-noon-angler_close.jpg)
- [Paved old-town street, unchanged as it should be](Captures/gfx01-2026-11-21/ab-noon-street_lent_0.jpg)

What they show:

- Trees stand on the grass banks, lawns and courtyards, and none on streets, squares or the promenade.
- The old town, mostly paved, stays nearly bare. The Ljudski vrt aerial is the clearest view of the rule.

## Open

- **Owner look and playtest**, day and night, including whether the density on the grass is right.
- **The meshes:**
  - The original calls these silhouettes placeholders: low-poly clumps, a little light and flat in full sun. Better species meshes come later through TRELLIS (ADR-071).
  - No wind sway; the weather look (ADR-062) still treats Lent as having no swaying vegetation.
  - One LOD, no Nanite (GFX-06).
- **Measurements:** a packaged client on a free GPU, night and rain scenes, and a mid-range GPU.
- **Behaviour and settings:** the street people do not yet steer round trunks; they only keep 1 m clear of their walk lines. No seasonal leaves (a December crown is as green as a June one). No per-preset density (GFX-07).
