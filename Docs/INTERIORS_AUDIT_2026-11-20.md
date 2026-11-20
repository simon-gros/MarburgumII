# Interiors and doors audit, 19–20 November 2026

The ground floors and doors of ADR-076 ([INTERIORS.md](INTERIORS.md)), audited in the real level and in the running game. They had not been entered by anybody before this audit. Branch `qa/interiors-audit-2026-11-19`, Unreal Engine 5.8.3, Linux, RTX 5080.

**Evening follow-up (20 November):** This file's initial *editor-only* audit remains historical. **Later** clean `a0d0b89` staged evidence records a multi-client **two-building door smoke PASS**, including a lagged vine museum, Hotel Lent rooms, rival/late join and streaming back in; see [P0 evidence](P0_PACKAGED_EVIDENCE_2026-11-20.md). Only **five** shells are physically opened out of **586** eligible floor-data candidates, **581** remain sealed and **346** exceptions persist. Furnishing, full building rollout and personal owner acceptance are still open.

## What was checked, and at which level

| Level | Run | Result |
| --- | --- | --- |
| Source review | Layout core, mesher cut, façade commandlet, subsystem, volume, door, controller, prompt | Seven defects, below |
| Automated rules | `Marburgum.Gameplay.Interiors`, `Marburgum.Editor.InteriorDoorCuts`, `Marburgum.Editor.BuildingMesher` (`Saved/Interiors/automation-3.log`; `automation-4.log` after the rebase); `mb2 validate all` | 3/3 pass; validate passes |
| Level audit, all 932 footprints | `MarburgumInteriorAudit` against the level's collision (`Saved/Interiors/audit/{before-cuts,floors-measured-2,sample-cut}`) | Table below |
| Game, editor build | `scripts/door-smoke-linux.sh editor` on Hotel Lent (`Saved/DoorSmoke/20261120T002213Z`, `…T002743Z`, and `…T004127Z` after the rebase onto ADR-074) and the vine museum (`…T003240Z`) | Pass, 4 of 4 |
| Packaged build | Client/Server compiled after UFS correction | Staged door smoke still unverified |
| Owner playtest | Not run | — |

## Coverage of the city

From `Saved/Interiors/audit/sample-cut/buildings.tsv`, one row per footprint with its id, name, use, door coordinates in Unreal centimetres, status, reason, rooms, the capsule probes, what stands in its rooms, the walk and the leaves.

| | Before the fixes | After |
| --- | --- | --- |
| Footprints in `buildings.json` | 932 | 932 |
| With a shell actor in `/Game/World/lent` | 870 | 870 |
| Eligible for a ground floor | 762 | 586 |
| Rooms with something inside (ground, another building, a prop) | 741 | 0 |
| A leaf, shut or open, that hits something | 762 | 0 |
| Entrance a capsule passes if the door is cut | 235 | 586 |
| Full walk (street, every room, back out) if the door is cut | 3 | 586 |
| Shell rebuilt with the door cut, entrance open as built | 0 | 5 |
| Full walk as built, every leaf clear (functional doors) | 0 | 5 |
| Shell still solid, no rooms streamed | 762 | 581 |

Before the fixes the 762 were not usable at all: the rooms sat on `buildings.json`'s ground, which runs under the level's terrain, so terrain stood inside 741 of them and at 348 doors the street was above the floor.

### The 346 exceptions

| Reason | Count |
| --- | --- |
| Interior lower than a person (eaves too close to the street) | 120 |
| Ground inside rises more than a step (40 cm) above the street, even confined | 106 |
| No shell: under 30 m² or not a simple outline | 60 |
| No street in front of the door | 17 |
| Doorway blocked: street more than a step below the threshold | 15 |
| Not an occupiable building (roof, construction, grandstand, transformer) | 15 |
| Another building stands in the rooms (`r1630922`, `w155011795`, `w1421582588`, `w226259904`, `w546883145`, `w662662130`, `w112599090`) | 7 |
| Inscribed room too small | 2 |
| No street wall can take a door | 2 |
| Something stands in front of the door (`StaticMeshActor_83`) | 1 |
| Generated rooms not all reachable | 1 |

## Defects

| # | Defect | Cause | Correction | Status |
| --- | --- | --- | --- | --- |
| D1 | Rooms and a door streamed in for every eligible building, though only Hotel Lent's shell had a hole: 778 doors opening into a solid wall, invisible rooms inside closed shells | Streaming never looked at the shell | The subsystem sweeps a player capsule through the doorway in the level's collision before the first build; a sealed shell gets nothing | FIXED AND VERIFIED (audit: 581 sealed shells, no rooms; smoke: `w151847675` logged `entrance_sealed`) |
| D2 | The mesher chose the door wall on its own and cut doors into buildings the layout rejected | Two rules for one door | The mesher and commandlet take the wall and rectangle from the layout, cut only eligible buildings, and log a mismatch | FIXED AND VERIFIED (`InteriorDoorCuts`: every hole within 1 cm of its layout; rebuild log `cut=586 mismatched=0`) |
| D3 | Front room's street-side wall dropped | Wall ownership between front room and vestibule | Rewritten partition builder | FIXED AND VERIFIED (rules test, capture `walker-03-in-doorway`) |
| D4 | Terrain inside 741 interiors, street above the floor at 348 doors | Floor from `buildings.json` `ground`, which is below the level's terrain | Floors measured in the level (`interior_floors.json`); on slopes rooms are confined to the low part touching the door; the rest are named exceptions | FIXED AND VERIFIED (audit: 0 invaded, 586 walks) |
| D5 | The open street leaf hit the 2.05 m lintel of the vestibule's inner opening; a player could not turn from a shallow vestibule into a front room narrower than the door | Vestibule depth and front-room span | Vestibule deep enough for the leaf; the front room spans the doorway | FIXED AND VERIFIED (audit: 0 leaves blocked, 586 walks) |
| D6 | A leaf blocked people while it swung; nothing stopped a door closing on somebody | Collision on in every state, no obstruction check | Only a shut leaf blocks; the server refuses to shut, or stops shutting, while a pawn is in the swing | FIXED AND VERIFIED (smoke: `door_refused_blocked`, walker never moved) |
| D7 | Walls, floors and ceilings rendered as the engine's grey checker in the game | `M_Building` lacked the instanced-static-mesh usage; a game cannot add it | The façade commandlet sets the usage on the master it instances; `M_Building` saved once | FIXED AND VERIFIED (smoke 1 log warned for three materials, smoke 2 none; captures before and after) |
| D8 | The prompt named the building by its OpenStreetMap id | Id passed as the name | The building's own name, or none | FIXED, source only |
| D9 | A packaged game had no interiors at all: the server logged `cannot read buildings.json` | The runtime read `Data/World/lent_ground`, which packaging does not stage | The runtime reads byte copies in `Content/Marburgum/Interiors`, staged as UFS; `-WriteFloors` writes both, and `Marburgum.Gameplay.Interiors` fails when they differ | FIXED; packaged verification below |
| D10 | `main` (ADR-074) did not build its game targets, so nothing could be packaged | `MarburgumWalkGraph.cpp` calls the editor-only `AActor::GetFolderPath` | The folder test only in the editor; a packaged game recognises bridges by name and mesh path | FIXED (packaged client and server built). Whether a bridge actor that is recognised only by its outliner folder is missed in a packaged game is NEEDS VERIFICATION for the street-people owners |

## Doors and multiplayer, in the game

`scripts/door-smoke-linux.sh editor` (editor build, dedicated server on `/Game/World/lent`, `-MB2DoorStarts`, three clients). Pass on Hotel Lent three times (once after the rebase onto ADR-074) and on the vine museum.

- Two players at the door. The walker opens it with E (1.27 s to open). The rival shuts it and, while it is closing, presses E again: the swing reverses and both clients see it open again.
- The walker stands in the swing; the rival presses E: the server logs `door_refused_blocked` and the door does not move on either client.
- The rival asks for the farthest room door from outside its reach, through a client that skips its own reach check: the server logs `door_refused … reason=reach`.
- The walker walks the route with ordinary movement input: in through the street door, through each of the four room doors (opened with E, about 1.2 s each) into each room and back, and out to the street. No point stuck for 5 s, never below the floor.
- A third player joins while the walker is still there and sees the same five doors in the same states. After a server restart a new player sees all five shut, as designed (not saved).
- The rival disconnects mid-run; the doors keep their state for the others.

Not covered in the game: a client's own rooms streaming out past 40 m and back in, two players inside different buildings, a packaged client, more than three players, and lag or packet loss.

## Performance

| Measure | Value | Where |
| --- | --- | --- |
| Classifying all 932 footprints at map start | 0.60–0.86 s, once, on the server and on each client | audit, server and client logs |
| Building one ground floor | 0.19–0.35 ms mean | audit `build_ms_mean` |
| Per streamed building | 5.5 rooms, 37 instances in three instanced components (walls, floors, ceilings), one shadowless point light per room, 4.5 door actors | audit |
| Replicated per building | Only the door actors: state, hinge, size, building id, name | `MarburgumDoor.h` |
| Distant or unoccupied buildings | Nothing: built within 22 m, removed past 40 m | subsystem |

The 0.6–0.9 s at map start is the one real cost found. No GPU or memory profile was taken; a ground floor is a few dozen instances and a shadowless light per room, only near a player.

## Backlog

| Id | Item | Status |
| --- | --- | --- |
| I-01 | Rebuild the other 581 eligible shells with their doors cut, once on `main`, then rerun the audit | TO BE DONE |
| I-02 | Cook and package; run `door-smoke-linux.sh staged`; owner playtest | NEEDS VERIFICATION |
| I-03 | A client's rooms streaming out and back in, and two players in different buildings, in the game | NEEDS VERIFICATION |
| I-04 | Rooms are bare boxes: no skirting, no window reveals behind the façade's windows, the lamp has no fitting, no furniture | TO BE DONE (assets) |
| I-05 | Back rooms come out as long, narrow strips in deep plans | TO BE DONE (layout) |
| I-06 | Up to a 40 cm step from the street to the threshold, with no tread | TO BE DONE (mesher) |
| I-07 | 106 buildings whose ground rises inside: excavate the ground under them in the lent-ground tool, or add an interior stair | TO BE DONE |
| I-08 | 120 buildings lower than a person, and 60 without a simple shell | BLOCKED (source geometry) |
| I-09 | Residential, commercial, industrial and public buildings differ only by wall colour | TO BE DONE (assets) |
| I-10 | Classify the city once at cook time instead of at every map start | TO BE DONE |
| I-11 | Upper floors and stairs; navigation for street people indoors (the project has no navmesh) | TO BE DONE (design) |
| I-12 | Door state is not saved; a restart shuts every door | By design (ADR-076); revisit with locks and keys |

## Evidence

- Level audits: `Saved/Interiors/audit/before-cuts`, `floors-measured-2`, `sample-cut` (`summary.txt`, `buildings.tsv`, `rooms.tsv`).
- Automation: `Saved/Interiors/automation-3.log`.
- Shell rebuilds: `Saved/Interiors/rebuild-<id>.log`, `rebuild-materials.log`.
- Game runs: `Saved/DoorSmoke/20261120T002213Z` (checker materials, motion-blurred captures), `Saved/DoorSmoke/20261120T002743Z` (after the material fix), and `…T003240Z` (vine museum), `…T004127Z` (rebased tree). Selected captures are in `Docs/Captures/interiors-2026-11-20/`.
