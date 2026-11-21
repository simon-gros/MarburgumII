# Building interiors

Ground floors for the Lent shells (ADR-076). The façades stay the outside of the building. A player walks in through one cut in a street wall. The rooms are not a second copy of the mesh sent over the network: every machine builds them from the footprint and the measured floor. The audit of 20 November, its evidence and the backlog are in [INTERIORS_AUDIT_2026-11-20.md](INTERIORS_AUDIT_2026-11-20.md).

**21 November, five by five (ADR-087):** the map now has 3,363 footprints and 3,182 shells. Floors are measured for 2,656 of them; **2,111** are eligible and **1,252** exceptions. Still **only the same five** are open: a full façade rebuild cuts only the shells listed in `Data/World/lent_ground/open_shells.txt` (`-Only=<id>` one more, `-AllDoors` every eligible one), and the interior audit finds those five walkable as built and 2,105 eligible ones walkable if cut. `-WriteFloors` now refreshes the game's copy of `buildings.json` before it measures, so a changed set of footprints is measured in one run. The paragraph below is the 20 November count.

**Evening status:** Of **586 deterministic floor-data candidates**, **only five** have actually opened/cut and walkable entrances, **581** eligible remain sealed and **346** are exceptions. The clean staged `a0d0b89` package exercised concurrent Hotel Lent and vine museum door traversal, rival actions, late joining, 120-ms lag/1%-loss and stream-out/back, **PASS**. It did not furnish all rooms, open all candidates, certify general city performance or replace an owner walkthrough. [Staged evidence](P0_PACKAGED_EVIDENCE_2026-11-20.md). [Research proposals](GAMEPLAY_RESEARCH_ROADMAP_2026-11-20.md) recommend furnishing the existing five interiors first.



## What is generated

`MarburgumInteriorCore.h` is plain C++. The same plan is the same rooms on the server, on a client, in the façade mesher and in the tests.

- One street door, 1.10 m wide and 2.10 m tall, centred on the longest wall that is neither a party wall nor faces another building across less than a step. The façade mesher takes that wall and rectangle from the layout, not from a rule of its own, cuts it and leaves the plinth open across it. A mismatch between the two is logged and left uncut.
- The floor level is measured in the level, not taken from `buildings.json`'s `ground`: `MarburgumInteriorAudit -WriteFloors` writes `Data/World/lent_ground/interior_floors.json`. The floor clears the highest ground under the rooms and the street at the door by 3 cm. On a slope the rooms are confined to the largest low part of the footprint that touches the door (`rooms_within`); the rest stays a closed void.
- A vestibule behind the street door, deep enough for the leaf to open clear of the inner opening and its lintel, then a front room spanning the doorway and up to four rooms behind it. Every room is reached from the street door, every room door has a hinged leaf, and no open leaf crosses a wall.
- Upper storeys are counted and not built. There is no stair.

A building that cannot take this is kept with a reason and gets nothing. Of the 932 footprints, 586 are eligible and 346 are exceptions; the reasons and counts are in the audit report.

## What is in the world

`UMarburgumInteriorSubsystem` reads the plans on `/Game/World/lent`. It builds a ground floor when a player (or, on a client, its camera) is within 22 m, or inside the footprint, and removes it past 40 m. Before the first build it sweeps a player's capsule from the street through the doorway against the level's static collision and checks the step from the street. A shell that has not been rebuilt with its door cut fails that probe and stays closed: no hidden rooms, and no door hung on a solid wall. `-MB2NoInteriors` builds none.

Floors, partitions, ceilings and a warm lamp are local instanced pieces of the façade plaster, socle and trim (`M_Building` carries the instanced-mesh usage for them). Doors are spawned by the server and replicated. E opens or closes the nearest door in reach, facing it, unless a car or a nearer interactable has the key. A swing that is already going reverses. Only a shut leaf blocks people; the server will not shut a door, or stops a closing one, while somebody stands in its swing; and it refuses a door out of reach. Door state is not written to the world store: streaming a building out, or restarting the server, shuts its doors.

A server started with `-MB2DoorStarts` places two player starts in front of every eligible street door, tagged `door_<id>`, for test drivers and playtests (`?MB2Start=door_r1539756` is Hotel Lent).

## Which shells are open

The cut is in the mesh, so a building is enterable only once `MarburgumRebuildFacades` has rebuilt its shell. On 20 November five were rebuilt and are open in the level:

| Id | Building | Why it was picked |
| --- | --- | --- |
| `r1539756` | Hotel Lent | Large, six rooms, floor confined on a slope |
| `w539479841` | Muzej najstarejše trte na svetu | Sloped ground, confined floor |
| `w1557764911` | Tekstil | Small plan with several rooms, party walls on three sides |
| `w219010220` | (unnamed) | Small seven-sided plan, a single room behind the vestibule |
| `w158684433` | (unnamed) | Large irregular twenty-sided plan |

The other 581 eligible buildings pass the same walk with the door cut (`walk_ok_if_cut`) but stay solid until their shells are rebuilt. That is one command, run once on `main` so the façade packages are not rebuilt on several branches at once:

```bash
UE_ROOT=/path/to/UnrealEngine-5.8.3
UnrealEditor-Cmd MarburgumII.uproject -run=MarburgumRebuildFacades -unattended -nosplash   # every shell
UnrealEditor-Cmd MarburgumII.uproject -run=MarburgumRebuildFacades -Only=<id> -unattended -nosplash   # one shell
UnrealEditor-Cmd MarburgumII.uproject -run=MarburgumInteriorAudit -unattended -nosplash   # then audit
```

Regenerate `interior_floors.json` (`-run=MarburgumInteriorAudit -WriteFloors`) after any ground, façade or level change, before rebuilding shells.

## How it is checked

| Level | What | Where |
| --- | --- | --- |
| Rules | `Marburgum.Gameplay.Interiors`: every layout inside its footprint, no overlaps, every room reachable, leaves clear, doorway width, the same plans read twice, the five synthetic cases | `Source/MarburgumII/Private/Tests/MarburgumInteriorTest.cpp` |
| Mesher | `Marburgum.Editor.InteriorDoorCuts`: every shell's hole matches its layout's door to 1 cm; `Marburgum.Editor.BuildingMesher` | `Source/MarburgumIIEditorTools/Private/Tests/` |
| Level | `MarburgumInteriorAudit`: for all 932 footprints in the real level, the capsule through the door as built and as if cut, what stands inside each room, a capsule walk through every room and back out, every leaf shut and open | `Saved/Interiors/audit/<run>/` |
| Game | `scripts/door-smoke-linux.sh [editor\|staged] [id]`: a dedicated server and three clients; a walker, a rival and a late joiner, then a server restart | `Saved/DoorSmoke/<run>/` |

## Packaged and rollout gates — 20 November morning

**Update, 20 November 06:11 UTC:** the staged door smoke **passed** on a clean packaged pair (`a0d0b89`). Hotel Lent and the vine museum ran at once, the museum's clients lagged. Covered: denial out of reach, the swing held open while someone stands in it, late joins, a tourist streaming out 268 m and back in with the doors replicated as left, the emptied building resetting on the same server, and a restart. It found and fixed one defect: the last player leaving an empty server left the interior's doors open. Evidence: [P0_PACKAGED_EVIDENCE_2026-11-20.md](P0_PACKAGED_EVIDENCE_2026-11-20.md). Still open: the other three cut shells on a staged run, latency to a real remote host, and the owner's walkthrough. The text below is the morning's gate as written.

A Client/Server package compiled after staged UFS data correction, **but there is no recorded passing staged three-client door smoke**. Test `scripts/door-smoke-linux.sh staged`, with two different occupied buildings, denial/out-of-reach, obstructed closing, late joins, server restart, stream out/in and actual network lag; stamp footprint, measured floor, cut façade and cooked asset revisions. Only **five of 586 eligible** buildings are physically open; the remaining **581** require incremental audited cuts and rollback, not a blanket acceptance. Explicitly handle the 346 named geometry/access exceptions. Owner playtest remains mandatory before an interior phase is approved. [Detailed priorities](DEVELOPMENT_REVIEW_2026-11-20_MORNING.md).

## What is not done

See the backlog in the audit report. In short: 581 shells still to rebuild, bare rooms with no furniture, skirting or window reveals, no stair to upper floors, no navigation for the street people indoors, no passing staged packaged three-client door smoke and no owner playtest.
