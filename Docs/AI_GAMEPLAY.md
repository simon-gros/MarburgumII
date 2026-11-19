# Gameplay AI — status and backlog

Audit of the AI that is actually in the repository on 2026-11-19, through ADR-070 (base commit `d2e083ce`). This is not a claim that Marburgum II runs Unreal Behavior Trees, StateTree, EQS or MassEntity. It does not. The living street, the fights and the missions are server-owned C++ with shared pure rules. Replacing that with the engine AI framework would be a new architecture and needs approval.

Statuses: **FIX NOW** (done in this pass where marked), **IMPROVE NOW**, **TO BE DONE**, **NEEDS VERIFICATION**, **REQUIRES APPROVAL**.

## What is implemented

| Subsystem | Where | Authority | What it does |
| --- | --- | --- | --- |
| Street people | `AMarburgumPedestrian`, `UMarburgumStreetSubsystem`, `FMarburgumWalkGraph`, `MarburgumStreetContract.h` | Server. Mood replicates. Clients do not choose destinations. | Walk the pavement graph, keep right, dodge cars, answer fights/run-overs/bodies/bumps from the personality table, visit stops, doors and sights (ADR-044, 046, 067–069). Each player keeps their own district's count, held for 5 m at a border, under the cap of 48 (ADR-073). RVO avoidance is on. Population is culled by distance and view. |
| Lent fight cast | `AMarburgumFighter`, `MarburgumCombatContract.h` | Server. Blows are `ReceiveBlowAuthority` / `ResolveSwingAuthority`. | One hostile (the drinker) fights whoever hit them, within pursuit range. Civilians step away. Jab, heavy, guard, stagger, down, get up (ADR-041). |
| Authored mission NPCs | `AMarburgumNpc` | Server interaction. The figure does not walk or perceive. | Talk targets. Placeholder mesh. |
| Missions | `MarburgumMissionLogic`, `MarburgumMissionRules` | Server world store. Client prompt uses the same pure plan. | Eight original missions. Start, stage, choice, one reward, restart. Fishing is an activity the mission requires, not AI. |
| Rust sim | `native/marburgum-sim-core` | Not linked to these actors. | Not the street or combat brain. |

There is no behavior tree, no perception component, no navigation mesh, and no World Partition. The nine-window level is always loaded. Distant pedestrians are not simulated more cheaply than near ones except by not existing (spawn/cull).

## Fixed in this pass

| Item | Status | Cause | Change |
| --- | --- | --- | --- |
| A hostile shuffled forever against a wall | **FIX NOW** | A refused sidestep called `Stop()`, and the next tick treated “not walking” as progress and cleared `Stuck`. | Refused closes count, including a held refusal. After `UnreachableCloseTicks` (20, two seconds) `DecideHostile` gives up. Waiting at the river does not count. In reach with a clear line, the blow still lands. |
| Pedestrians reversed a blocked edge every think | **FIX NOW** | Water or a fight flipped `ToNode` every 100 ms, so they shuffled in place. | `BlockedWalk`: reverse once, then hold. The existing stuck timer may still try the other way. |
| A fight that ended could start another in the same tick | **FIX NOW** | `GiveUp` on a missing target fell through into bump sensing. | Return after giving up. |
| People on one edge shared one goal | **IMPROVE NOW** | `EdgeGoal` was the node plus 30 cm right, for everyone. | Three lanes, ±18 cm, still inside `NodeArriveCm` (static assert). RVO remains the overlap solver. |
| Remembered incidents grew for as long as a person lived | **FIX NOW** | `Rolled` kept every id forever. | Above 32, drop ids that are no longer live. Ids are never reused, so a gone incident is not rolled again by mistake. |
| Destroyed cars stayed in the sweep map | **FIX NOW** | `LastCarCentres` kept weak keys. | Drop invalid keys each frame. |

## Backlog

| Item | Status | Why it is not done here |
| --- | --- | --- |
| Navigation mesh / Behavior Tree / StateTree / EQS / Mass for the street | **REQUIRES APPROVAL** | The walk graph, hours, districts and places are the architecture. A framework swap is a rewrite, not a fix. |
| Path around a blocked edge instead of reversing | **TO BE DONE** | Hold-and-stuck is recovery, not a route. A real detour needs a graph search with a budget. |
| Pedestrian schedules beyond the hour mix and attractors | **TO BE DONE** | Groups, hours and places exist. Named daily routines per person do not, and would be a design. |
| Conversations, groups walking together, shared reservations beyond attractor slots | **TO BE DONE** | Slots at stops and sights exist (3). No dialogue, no formation. |
| Traffic AI and intersection behaviour | **TO BE DONE** | Cars are player-driven. People dodge and can be run over. No AI traffic. |
| Combat perception, hit reactions, weapons, consequences of a fight | **TO BE DONE** | ADR-041 left these open. Targeting is nearest standing body in arc, server-side. No sight cones. |
| Hostile stuck against geometry that is not a failed step (for example a moving crowd that still counts as progress) | **FIX NOW** (ADR-073) | A slide that still moved used to clear `Stuck`. A close now counts only when 35% of a stride is toward the target. Rules test only; not a rendered chase. |
| Multiple opponents | **REQUIRES APPROVAL** | A hostile fights the last person who hit them, one at a time. Switching policy is a design choice. |
| Mission NPCs that walk, wait or react | **REQUIRES APPROVAL** | `AMarburgumNpc` is a stationary interactable. Moving the angler or Petra is player-facing. |
| New mission framework | **REQUIRES APPROVAL** | The eight missions already have prerequisites, stages, choices, one reward and restart. Do not replace them. |
| Mission sites in Melje and Tabor on real ground | **TO BE DONE** | Logic is done. The sites are temporary floors (ADR-052). |
| Packaged two-client mission and street qualification | **TO BE DONE** | Editor smokes exist. Not a fresh packaged suite. |
| Side pieces of the walk graph, including a bridge piece | **EXPLAINED** (ADR-074, audit on the same branch) | The file's largest piece is 12,335. Of those, 121 nodes are dropped (no ground, no room, or wet). The rest were split by sweeps. After the loader steps past the bridge mesh and the paving volume, and past an upward face of the landscape, the loaded largest piece is 11,719 and still reaches both banks. The 23 edges still blocked hit building meshes (`w*`) or a steep landscape face. Wet edges stay 0. The 14 m piece was not given a new edge. `-MB2WalkGraphAudit=<dir>` writes the rejected nodes and edges. |
| Distance-based think rates, pooling, relevance budgets beyond cull | **TO BE DONE** | Population is capped and culled. No measurement of think cost at the cap in this pass. |
| Owner playtest of street and combat | **REQUIRES APPROVAL** | ADR-008. This pass did not play the game. |

## What was checked and left alone

- Mission plan order (work in hand, then a new offer, then why not) and one-time rewards. No stuck stage or double reward found in the rules.
- Downed bodies are not swing targets (`IsDown`). A blow on an already down NPC or player reports a hit and does not take more vitality.
- Seeing a fight never starts one. Fighting back is only a bump or a blow, from the table.
- Clients do not grant mission progress, blows or pedestrian destinations.

## Verification

Editor automation on 2026-11-19, after the modules were rebuilt (binaries 21:51, run 21:53, log `Saved/AIRules/rules.log`, exit 0; repeated at 21:54, log `Saved/Automation/adr070-rules-rebuilt.log`, exit 0): `Marburgum.Gameplay.CombatRules` Success, `Marburgum.Gameplay.StreetRules` Success. That covers the blocked-chase give-up, a clear blow still landing, and reverse-once / hold. It is not a rendered street or combat smoke, not a packaged two-client run, and not an owner playtest. A visit that used the whole 30 s without arriving did not log `visit_given_up`, and a second think before the street step could log `went_in` again. Both are fixed in source and still need a rendered places smoke.


## Runtime check, 20 November 2026, `fix/grok-quality-2026-11-19`

An earlier editor run (`Saved/BridgeCross/20261119T232455Z`) only walked one 28 m span (5 nodes, 26.7 s). That is not a full crossing.

The fixture now requires the Stari most deck near the origin, at least 180 m, from pavement past one end to pavement past the other, then the same route back. Editor server and one offscreen client, port 27851, `/Game/World/lent`, `scripts/bridge-cross-linux.sh`, evidence `Saved/BridgeCross/20261119T233511Z`. South to north: 38 nodes, 283 m, from (−139.2, −12.4, 16.2) m to (141.3, −2.0, 13.2) m, **273.3 s**, `arrived`. North to south: the same 38 nodes reversed, **272.5 s**, back to (−138.6, −12.3, 17.1) m, `arrived_both`. Neither leg was wet or lost. Height stayed on the deck. The client logged the same motion and wrote `bridge-start.png` (20 m along), `bridge-deck.png` (80 m) and `bridge-far.png` (180 m). It did not stay up for the return; the server did. The client process was stopped after `arrived_both` (exit 143) so the harness would not wait out its own timeout. The script still required both arrivals and a client walking log, and it exited 0.

Two null-RHI clients on port 27852 at 00:26 Marburgum time, both read as `residential_blocks`. One player alone wanted 2. With both connected the budget stayed 2, split 1 and 1, and two people were in the world (`Saved/StreetMulti/20261119Ttwo/server.log`). They did not each bring a street. Night density only. Not a commercial-versus-residential pair, not a separation, not opposite banks, not a disconnect.

During the crossing the district lookup ran 156 times, 155 from the 5 m / 3 m hold, and the one search took 0.09 ms. One player.

## Multiplayer matrix, 20 November 2026

`scripts/street-multiplayer-smoke-linux.sh editor`, port 27853, null RHI, clock pinned to 10:00 on 18 June 2026, clear weather, places off. Evidence `Saved/StreetMulti/20261119T235248Z/` (`summary.json`, the script exited 0). The run was the driver on top of `1c8dcf7`, before that driver was committed.

| Scenario | Result | What the server kept |
| --- | --- | --- |
| A day, commercial and houses | Pass | wants 18 and 8, haves 18 and 8, 26 people |
| B day, two players 8–35 m apart on the shopping street | Pass | one crowd of 18, split 9 and 9, not 36 |
| C apart, then back | Pass | 18+8 while apart, 18 shared again when back. One budget change each way |
| D opposite banks | Pass for place and district | blocks and school, wants 12 and 16. After 15 s only 8 of 28 had spawned, 5 and 3 on the two banks |
| E drop and return | Pass | one viewer left at 12; both back, 12+12 and 24 people. Same zone name, far apart, so not a shared crowd |
| Cap, three old-town players | Pass for the budget | unconstrained 60, target clamped to 48 (16 each). 40 had spawned when the window closed. The most alive at once was 40 |

`-MB2NetRate` during the three-client cap, null RHI: server out 7.2–13.3 KB/s, the busiest client 3.2–5.8 KB/s, 17–39 street people. District lookups across the teleports: 2,527 of 2,541 from the hold, 0.70 ms in `NearestNode` altogether. The night shared-budget run was not repeated on this revision. No packaged build.

## Next verified delivery

**P0: execute and integrate existing fixes before adding behavior.** Run the previously unexecuted `Marburgum.Gameplay.CombatRules` and `Marburgum.Gameplay.StreetRules` cases on a clean UE 5.8.3 checkout, then render blocked-pavement, blocked-chase, crowd-pass-lane and river-boundary probes. Recheck ADR-068 zone mixing and ADR-069 place visiting at day/night/Sunday plus body/run-over answers. On genuinely freshly cooked Linux Client/Server binaries, test two independent clients on the same crowd, one other client in a different zone, transitions, disconnection and resumption; capture source and cooked asset hashes, server-owned state, screenshots, logs, CPU/replication metrics and failures. Escalate reproducible defects to small fixes with negative controls; record outstanding route-search, traffic/perception and author-NPC behavior separately. Mark a row owner-accepted only after an actual owner playtest. See [19 November review](DEVELOPMENT_REVIEW_2026-11-19.md) and [roadmap](ROADMAP.md).
