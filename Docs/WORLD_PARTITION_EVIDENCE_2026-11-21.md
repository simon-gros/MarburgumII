# /Game/World/lent as a World Partition map — evidence, 21 November 2026

ADR-091: the five by five (ADR-090) streams on clients in 128 m cells within 512 m, with merged Nanite HLODs of 256 m
cells beyond; the dedicated server keeps the whole map. The owner merged it on 2026-11-21 without a playtest ([ADR-008](DECISIONS.md)'s
playtest waived).

## The builds

| | |
| --- | --- |
| Partitioned | branch `feat/world-partition`, commit `713ae17`, tree clean (`dirty=0`), staged 2026-11-21T11:59:29Z; client `sha256 ac5dd5e170eb7971eff49581a8b2a6f3e304985d4fcfd2450330ca8587c127c1`, server `sha256 bd62474340ede0721add9e0291fa02e977f9a3c5d7d2995f4df3c730c049a2cc` |
| Always-loaded | branch `feat/map-5x5`, `8ca3e76` with `d7b99ea`, staged 2026-11-21T11:22Z (the same level before conversion) |
| Engine | UE 5.8.3, Linux Development Client and dedicated Server |
| GPU | RTX 5080, NVIDIA 620.73.04, Vulkan SM6, 1920 × 1080, every client `-RenderOffScreen` |

## Smokes

All eight staged smokes pass on the partitioned build (ports 27991–27998; `Saved/WP/smokes.log`): swim, vehicle,
street, combat, mission, negative, story and door, each recording `git_sha 713ae17` and `build_fresh: yes` (`Saved/SwimSmoke/20261121T120535Z`, `Saved/StreetSmoke/20261121T121000Z`, `Saved/DoorSmoke/20261121T123349Z` and the rest of 12:05â12:39 UTC). The survey of that build grounds 3,344 of 3,349 lamps on the server and the client from the ground file, and the server loads the whole map for the street in 3.8 s. Peak resident memory over the smokes: server 4.9 GB, client 5.7 GB. The A/B below ran on the same source before the docs were committed (`Saved/WP/ab`).

The first partitioned package failed two of them, and both causes are fixed:

- **street**: `street_off reason="no node of the walk graph is on walkable ground"`. The street subsystem and the lamps
  traced the ground at begin play, before any cell had loaded. They now call `LoadWholeWorldOnServer` first; the server
  logs `MB2WORLD role=server event=whole_world caller=street partition_begun=1 streaming_complete=1 seconds=4.90`, and the
  walk graph grounds exactly as on the always-loaded level (33,897 of 34,220 nodes).
- **door**: the rival client was not admitted within 20 s. Its admission clock ran through a 12.3 s map load; `d7b99ea`
  stops it during loads. On the partitioned map the load takes 4.7–4.9 s.

## Survey: always-loaded against partitioned

The 107 views of the 25 windows (`Saved/WP/views-final.txt`), run back to back in the order A B B A
(`Saved/WP/ab`, `ab.log`), so the other load on the host (a screen recorder ran throughout) weighs on both alike.
Medians over the views; maxima in brackets.

| | always-loaded (1, 4) | partitioned (2, 3) |
| --- | --- | --- |
| client resident memory | 8.50 / 8.24 GB (8.80) | 4.64 / 4.58 GB (5.04) |
| VRAM in use | 5.04 / 5.07 GB | 4.32 / 4.29 GB |
| streaming textures | 617 MB | 515 MB |
| frame time | 6.9 / 7.5 ms | 8.3 / 8.6 ms |
| frame p99 | 13.0 / 14.4 ms (95.7 / 82.7) | 14.8 / 15.2 ms (18.9 / 21.4) |
| GPU time | 6.0 / 6.1 ms | 7.2 / 7.1 ms |
| draw calls | 252 | 238 / 233 |

The partitioned client holds 46 % less memory and 15 % less VRAM, and its worst frames are a fifth as long: the
always-loaded level's 80–95 ms hitches are gone. Each view costs **1.1 ms more GPU time**, the same on aerial, street,
edge and river views (air +1.1, street +1.1, edge +1.2 / +0.6 ms), so it is a fixed cost, not more geometry.

**Where the 1.1 ms goes: Nanite.** The 498 HLOD meshes are the level's only Nanite meshes, so the partitioned client runs
the Nanite pipeline and the always-loaded one does not. The same partitioned build with `r.Nanite=0`, which draws each HLOD's
fallback mesh instead:

| | always-loaded (6) | partitioned, Nanite (3 / 7) | partitioned, `r.Nanite=0` (5 / 8) |
| --- | --- | --- | --- |
| screen recorder | on | on / off | on / off |
| GPU time | 7.3 ms | 7.1 / 5.8 ms | 6.0 / 4.9 ms |
| frame time | 8.7 ms | 8.6 / 6.2 ms | 7.1 / 5.2 ms |
| VRAM in use | 5.04 GB | 4.29 / 4.29 GB | 3.35 / 3.30 GB |
| client resident memory | 8.18 GB | 4.58 / 4.60 GB | 4.64 / 4.56 GB |
| draw calls | 252 | 233 / 236 | 210 / 204 |

Without Nanite the partitioned client is at or under the always-loaded one on every count. The always-loaded runs differ
by up to 1.2 ms of GPU time among themselves (1, 4, 6) with the recorder's load; the Nanite difference holds within
each pair taken under the same conditions (0.9 and 1.1 ms).

The fallback HLODs look the same as the Nanite ones from every survey view. `r.Nanite` is the project's switch and stays 1:
GFX-06 plans Nanite for the shells and the paving, which pays the same fixed cost, and the HLODs need no rebuild to follow
either way.

## Look

Street, river and near views match the always-loaded level. Beyond 512 m, where the HLODs draw, some façades lose their
recessed window rows to the simplification (Nanite's and the fallback's alike). A first HLOD build that baked each cell into
one material drew the paving as a checkerboard (its texture follows the world position) and some façades and roofs the
wrong colour; the HLODs now keep the level's own materials. The lit windows at night from the HLOD distance are not yet
checked.

## Tests

`Marburgum.Shared.StreetLights.{File,Ground,Switch}` and `Marburgum.Editor.PartitionRules` pass; `mb2 validate all`
passes.

## Merged with main

main gained trees, bushes and street furniture over the five by five (ADR-087, ADR-089) while this was qualified. They
are placed from their files on the ground found by a trace, which a partitioned client mostly cannot find, so they take
it from the same ground files as the lamps (`MarburgumGroundFile.h`, `scripts/partition-world.sh ground`). On the merged
head, staged, `build_fresh: yes`:

- `b2affbd`: the editor without adaptive unity, 60 automation tests, `mb2 validate all`, and the 28-smoke matrix
  (`Saved/WP/merge/SUMMARY.txt`): 25 pass. A partitioned client places 40,604 of 40,604 plants, 2,339 of 2,339 pieces
  of furniture and 3,344 of 3,349 lamps, all `ground=file`, the always-loaded level's counts.
- The three that failed, fixed: every weather client crashed counting lit windows on the HLODs' merged meshes (`27d3ce3`);
  the server published its clock before begin play's whole-map load and corrected it only 30 s later (`8d0feeb`); both
  crowd smokes still wanted the nine windows' 185 bridge edges (`27d3ce3`), and crowd scenario D, failing alike on the
  always-loaded five by five, now keeps to the nine windows it was qualified on (`d6ddc3a`).
- `d6ddc3a`: weather, crowd day and crowd night pass (`Saved/WP/merge/RECHECK.txt`); a partitioned client counts 439
  buildings and 30,279 lit-window panes round its start, so the weather smoke wants 300 buildings there (500 whole).
  Then 60 automation tests, `mb2 validate all` and the street-hours, place and door smokes, which read the clock or restart
  the server, pass on that build (`Saved/WP/merge/FINAL.txt`).

## Not covered

- Night views of the far city (lit windows on the HLODs).
- Many-player sessions: the server holds the whole map, so its memory does not fall (4.9 GB peak in the smokes).
