# GFX-01 — Street furniture from the original game's kit

**21 November 2026 · branch `feat/gfx-01-vegetation` · [Graphics roadmap](GRAPHICS_ROADMAP.md) GFX-01 · [VQ-023](VISUAL_QUALITY_BACKLOG.md) · [ADR-089](DECISIONS.md) · after [the trees](GFX01_VEGETATION.md).**
**State: implemented and editor-verified; not packaged; owner look open.**

## What stands where

`lent-ground --mode furniture` (`tools/lent-ground/src/furniture.rs`) places the original game's street kit (its `feat/unreal-street-kit` port: stop post, bus shelter, bench, fence panel, wall panel, bollard). It reads them from this map's OpenStreetMap extract and follows the original's rules:

- **Stops.** Every `public_transport=platform` gets a stop post where it is mapped, facing the carriageway's edge within 12 m. Where the map records `shelter=yes`, a shelter stands 1.3 m behind the post, open to the road. A blocked shelter slides 2, 4 or 6 m along the kerb, right before left, and is otherwise left out. Where the map records `bench=yes`, a bench stands inside the shelter, or beside the post.
- **Runs.** Fences, gates and railings become 2 m fence panels, walls 2 m wall panels, and bollard lines a post every 1.5 m. Kerbs (the paving owns them), hedges (vegetation) and retaining walls (the ground owns slopes) are counted and not furnished.

This map adds two rules of its own:

- **Mapped benches and bollards.** Every `amenity=bench` and `barrier=bollard` node of the extract stands, not only those at stops. A bench faces its `direction` tag, or the walk line or road within 4 m of it. A mapped bench or bollard that is not clear backs away from the path by up to 1.5 m.
- **Nobody is blocked and the trees stand.** No solid piece stands within 0.4 m of the street people's walk lines (0.35 m for a post); a panel run across a walk line leaves a gap there. Nothing stands within 0.3 m of a tree's trunk ([the plants](GFX01_VEGETATION.md), the better measurement, as the original's D12 said). Nothing stands in the carriageway, a building, the river or on a bridge.

**Result: 948 pieces** on the nine windows. On the five by five (ADR-090), placed again against its 20,144 trunks: **2,339 pieces** (94 stop posts, 24 shelters, 267 benches, 1,458 fence panels, 439 wall panels, 57 bollards). The table is the nine windows' run.

| Piece | Placed |
| --- | --- |
| Stop posts | 49 |
| Shelters | 15 |
| Benches | 159 (28 at stops, 131 mapped) |
| Fence panels | 317 |
| Wall panels | 397 |
| Bollards | 11 |

Stops in detail:

- 73 platforms: 5 duplicates of a stop and 14 with no road within 12 m are left out, as are 5 posts that only had carriageway.
- 25 asked for a shelter: 15 stand, 1 of them slid along the kerb; 8 would have stood on a walk line and 2 on a building.

Runs: 1,873 m of barrier in the square. 7 panels were left out where a walk line crosses. Panels refused for carriageway: 58 fence and 23 wall. Panels refused for a building: 89 wall and 7 fence, mostly walls drawn along a building's own wall. 9 panels were refused for a tree. 17 mapped bollards stand in the carriageway (they close pedestrian streets to cars) and are left out, as the rule says. The file's `summary` has every count.

## The railing along the quays (VQ-024)

ADR-040 left the promenade ending "in a paved edge over the water". `tools/lent-ground/src/quay.rs` walks every paved walkable edge (pavements and squares) that has the Drava within 6 m outward (only river water, `water=river` or `waterway=riverbank`: the five by five also holds a mapped pond in Mestni park, whose edge stays open), and reads the ground outward in 0.25 m steps to the water. On the survey's ground the edge itself is rarely the drop: most of the paving stops on a flat verge, and the bank falls 1–5 m further out.

So the railing goes on the **bank's crest**, where the ground falls steepest:

- **Steep enough:** a crest at least 1 m above the river (z 0 is the Drava's surface, ADR-040) that falls at least 0.8 m within 1 m. A gentle slope fails that test, and stays open for wading in and fishing.
- **Open where people meet the water:** nothing within 8 m of the Lent angler (the fishing spot is placed relative to him) or of the swim smoke's way in, nothing within 3 m of a bridge (its own parapets), and nothing on a walk line.
- **One railing, not stubs:** a run carries on over up to 3 m of bank that is merely less steep (an open zone, a bridge, a walk line or the lack of water still ends it), and runs shorter than 6 m are left out. Without this the five by five got 72 runs, 44 of them a single module, which read as stray fences.
- **Modules:** whole 2 m modules along each run, 0.3 m back from the crest.

**Result on the five by five (ADR-090):**

- 5,209 edge points (0.5 m apart) lie by the river, and 2,089 of them are a quay.
- **357 modules on 27 runs, 737 m**, standing a median of 1.6 m above the water (1.1–1.8 m for nine in ten; four on the high bank in Greenwich at 8–9.2 m). By window: Smetanova 168, Studenci 86, Titov most 45 (Lent east of Stari most), Greenwich 44, Lent 9, Pristan 5.
- 61 runs shorter than 6 m are left out, as are 134 points by a bridge and 32 on walk lines.
- Counting every water area as river put 31 modules round the Mestni park pond, 20 m above the Drava; the river-only rule removes them.
- On the nine windows (21 November) the first version of the rule, without joined runs, gave 46 modules on 15 runs, 103 m.
- No point near the angler or the swimmers' way in was steep, so the rule itself keeps both open, not only the exclusion zones.
- On our survey the Lent bank right by Stari most is a slope, so it gets no railing.

**The railing** is our own `SM_Street_QuayRailing_01` (`scripts/create-weather-content.sh furniture`):

- painted steel in the street lanterns' dark green (`M_StreetLampMetal`), 60 triangles;
- posts at both ends, a top rail at 1.10 m and two mid rails;
- one collision box, so nobody walks or falls through.

**The quay wall itself is not built.** The landscape slopes from the crest to the water, and a vertical stone face needs that ground reshaped in the ground build (`lent-ground`, `MarburgumRebuildGround -Verify`). That is a separate change.

## In the game

- **Data:** `Content/Marburgum/Furniture/lent_street_furniture.json` (staged), read by `MarburgumStreetFurniture.h`.
- **Placement:** `UMarburgumStreetFurnitureSubsystem` / `AMarburgumStreetFurniture`: on `/Game/World/lent`, the server and every client ground each piece by a trace and put the pieces up as one instanced component per piece. Like the lamps, nothing is replicated. `-MB2NoStreetFurniture` leaves them out.
- **Meshes:** the original's six `SM_Street_*_01` and their vertex-colour `M_MarburgumPaint`, copied to `/Game/World/StreetFurniture` (always cooked) and `/Game/World/Materials`. `scripts/create-weather-content.sh furniture` changes three things on them:
  - **Collision:** the original's meshes collided against every triangle, so a shelter's roof stopped whoever stood under it. They now have simple boxes: the shelter's back and side walls (the original's own collision design) and each other piece's bounds.
  - **Nanite off:** the original had it on, but its paint was not compiled for Nanite, so the engine drew the default material.
  - **Instancing:** the paint is compiled for instanced meshes.
- **Checks:**
  - `lent-ground` tests: frames, compass, and footprints against the walk line, road, building, tree and edge.
  - Automation `Marburgum.Shared.StreetFurniture.File`.
  - `mb2 validate furniture`: format, pieces and sources against the C++ enumerations, rows, meshes, staging and cooking.

## Evidence (editor game mode, June noon)

12 close-ups at real pieces, without (`-MB2NoStreetFurniture`) | with, same build (`Saved/VegEvidence/furn-A`, `furn-B`). The server and the client both log `MB2FURNITURE event=placed pieces=948 grounded=948 no_ground=0 kinds=6 missing_meshes=0`, and the client logged no material warnings.

- [Sheet 1](Captures/gfx01-2026-11-21/furniture-sheet-1.jpg): shelter with its bench, open to the road; mapped bench on a lawn; a wall run; a fence run.
- [Sheet 2](Captures/gfx01-2026-11-21/furniture-sheet-2.jpg): shelter against a façade; bench among the trees; a bollard by a path; a stop.
- [The fence leaves the tree its trunk](Captures/gfx01-2026-11-21/furniture-furn_354_k3.jpg), [a wall run](Captures/gfx01-2026-11-21/furniture-furn_626_k4.jpg), [a bench](Captures/gfx01-2026-11-21/furniture-furn_177_k2.jpg), [a stop](Captures/gfx01-2026-11-21/furniture-furn_48_k1.jpg).

Frame cost was not measured separately: 948 instances of meshes of a few hundred triangles each, in six components. They belong in the packaged GFX-01 measurement together with the trees.

## Open

- **Owner look.**
- **The kit is the original's Tier C placeholder:**
  - The paint comes out uniformly pale (shelter glazing as opaque as its frame, a pale bench).
  - The original's later material-library binding was not ported.
  - The stop post stands in front of the shelter's opening, as in the original.
- **Not yet placed:** waste bins (53 mapped), post boxes, Maribor street-name plates and 1990s traffic signs (VA-06), kiosks, and furniture on bridges.
- **Accuracy and behaviour:**
  - Positions are today's OpenStreetMap, not the 1990s.
  - The street people walk round the pieces only because no piece is on their lines; they do not steer round them.
