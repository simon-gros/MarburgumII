# Gameplay AI — status and backlog

Audit of the AI that is actually in the repository on 2026-11-19, through ADR-070 (base commit `d2e083ce`). This is not a claim that Marburgum II runs Unreal Behavior Trees, StateTree, EQS or MassEntity. It does not. The living street, the fights and the missions are server-owned C++ with shared pure rules. Replacing that with the engine AI framework would be a new architecture and needs approval.

Statuses: **FIX NOW** (done in this pass where marked), **IMPROVE NOW**, **TO BE DONE**, **NEEDS VERIFICATION**, **REQUIRES APPROVAL**.

## What is implemented

| Subsystem | Where | Authority | What it does |
| --- | --- | --- | --- |
| Street people | `AMarburgumPedestrian`, `UMarburgumStreetSubsystem`, `FMarburgumWalkGraph`, `MarburgumStreetContract.h` | Server. Mood replicates. Clients do not choose destinations. | Walk the pavement graph, keep right, dodge cars, answer fights/run-overs/bodies/bumps from the personality table, visit stops, doors and sights (ADR-044, 046, 067–069). RVO avoidance is on. Population is culled by distance and view. Cap 48. |
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
| Hostile stuck against geometry that is not a failed step (for example a moving crowd that still counts as progress) | **NEEDS VERIFICATION** | The new give-up only fires when closes are refused. A slow creep might never trip it. |
| Multiple opponents | **REQUIRES APPROVAL** | A hostile fights the last person who hit them, one at a time. Switching policy is a design choice. |
| Mission NPCs that walk, wait or react | **REQUIRES APPROVAL** | `AMarburgumNpc` is a stationary interactable. Moving the angler or Petra is player-facing. |
| New mission framework | **REQUIRES APPROVAL** | The eight missions already have prerequisites, stages, choices, one reward and restart. Do not replace them. |
| Mission sites in Melje and Tabor on real ground | **TO BE DONE** | Logic is done. The sites are temporary floors (ADR-052). |
| Packaged two-client mission and street qualification | **TO BE DONE** | Editor smokes exist. Not a fresh packaged suite. |
| Distance-based think rates, pooling, relevance budgets beyond cull | **TO BE DONE** | Population is capped and culled. No measurement of think cost at the cap in this pass. |
| Owner playtest of street and combat | **REQUIRES APPROVAL** | ADR-008. This pass did not play the game. |

## What was checked and left alone

- Mission plan order (work in hand, then a new offer, then why not) and one-time rewards. No stuck stage or double reward found in the rules.
- Downed bodies are not swing targets (`IsDown`). A blow on an already down NPC or player reports a hit and does not take more vitality.
- Seeing a fight never starts one. Fighting back is only a bump or a blow, from the table.
- Clients do not grant mission progress, blows or pedestrian destinations.

## Verification

The new assertions live in `Marburgum.Gameplay.CombatRules` and `Marburgum.Gameplay.StreetRules`. They were not executed in this pass: a street-places smoke was already using the engine on this host, and rebuilding the editor during that run is not allowed. A rendered street or combat smoke was not re-run either.


## Next verified delivery

**P0: execute and integrate existing fixes before adding behavior.** Run the previously unexecuted `Marburgum.Gameplay.CombatRules` and `Marburgum.Gameplay.StreetRules` cases on a clean UE 5.8.3 checkout, then render blocked-pavement, blocked-chase, crowd-pass-lane and river-boundary probes. Recheck ADR-068 zone mixing and ADR-069 place visiting at day/night/Sunday plus body/run-over answers. On genuinely freshly cooked Linux Client/Server binaries, test two independent clients on the same crowd, one other client in a different zone, transitions, disconnection and resumption; capture source and cooked asset hashes, server-owned state, screenshots, logs, CPU/replication metrics and failures. Escalate reproducible defects to small fixes with negative controls; record outstanding route-search, traffic/perception and author-NPC behavior separately. Mark a row owner-accepted only after an actual owner playtest. See [19 November review](DEVELOPMENT_REVIEW_2026-11-19.md) and [roadmap](ROADMAP.md).
