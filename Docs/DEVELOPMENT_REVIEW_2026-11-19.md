# Development review — 19 November 2026 (last 24 hours)

**Repository:** [simon-gros/MarburgumII](https://github.com/simon-gros/MarburgumII) · **Branch assessed:** `main` · **Head before documentation changes:** `d2e083ce769fd5da753bcb8a38cac4e11b4a8af5` (ADR-070) · **Window:** 18 November 2026 20:40 UTC to 19 November 2026 20:40 UTC (19 November 21:40 in Ljubljana). **Scope:** six first-party commits visible in the repository's commit history for this interval, with their changed files, existing ADRs, code and evidence documents. Earlier work was examined to identify stale cross-references. This is a **documentation and source-evidence review**; it did not run an Unreal build, automation, fresh packaged smoke or owner playtest.

## Commit reconciliation

| Commit (UTC) | Change | Recorded evidence and qualification |
| --- | --- | --- |
| [`409f322c`](https://github.com/simon-gros/MarburgumII/commit/409f322ca63ee29dc56fff88d290670bd8d4b49d), 12:00 | ADR-068 district/activity information on walk-graph nodes, locality-specific population mix and hours, busy-street spawning and test route | One-client editor-mode district smoke; see [district evidence](STAGE1_STREET_DISTRICTS_EVIDENCE.md), not packaged multiplayer |
| [`55f8236c`](https://github.com/simon-gros/MarburgumII/commit/55f8236c61b218a0bd619fd72b1c430931670d6c), 12:00 | ADR-068 documentation, captures and parity/development updates | Evidence document distinguishes editor smoke from owner approval |
| [`2316b918`](https://github.com/simon-gros/MarburgumII/commit/2316b9184bce945a3c3dff7302bac702bcc72938), 14:07 | Fixes clean-checkout unity compilation by avoiding a `Street::ZoneNames` symbol shadow | Commit records a build issue fix; no fresh cross-platform certification inferred |
| [`09e229da`](https://github.com/simon-gros/MarburgumII/commit/09e229da7e5de7088b637ea18cd3fd1acbda6d9f), 19:45 | ADR-069: 239 pinned bus stops, venues, shops and sights with hour/group visit behavior, door entry/exit and incident probe improvement | One-client editor-mode destination smoke and earlier street regressions, not production urban routines |
| [`02c68cff`](https://github.com/simon-gros/MarburgumII/commit/02c68cff5c184da5f15911b6db2843b9240a571b), 19:45 | ADR-069 captures, street evidence and data-parity documentation | [Places evidence](STAGE1_STREET_PLACES_EVIDENCE.md), whose negative controls and limitations remain relevant |
| [`d2e083ce`](https://github.com/simon-gros/MarburgumII/commit/d2e083ce769fd5da753bcb8a38cac4e11b4a8af5), 20:38 | ADR-070: blocked hostile pursuit give-up, held pedestrian reverse, pass lanes, incident/car bookkeeping; [AI audit/backlog](AI_GAMEPLAY.md) | New `CombatRules`/`StreetRules` assertions were **added but not run**; no rendered or packaged regression in that pass |

The chronological change is **regional street population → useful destinations → navigation/behavior hardening**. All three are implemented under the existing server-authoritative walk-graph architecture, not an Unreal Behavior Tree/StateTree/EQS/NavMesh/MassEntity system.

## What the evidence establishes

**Districts (ADR-068):** server-managed people now depend on the zone and activity assigned to each walk node. The recorded route spans old town, shopping street, residential blocks, houses, works and school at different weekday/weekend times. The [district smoke](STAGE1_STREET_DISTRICTS_EVIDENCE.md) reports a successful editor-mode one-client route, with district population counts and group distributions checked against server targets. These are controlled test observations, not real player crowd benchmarks or proof of historically correct 1990s source categories.

**Places (ADR-069):** the current nine-window graph includes 239 destinations. The [places smoke](STAGE1_STREET_PLACES_EVIDENCE.md) reports visits to cafés, restaurants, shops, bars, bus stops and sights, people entering/exiting and Sunday/late shop closures. The smoke also documents the formerly marginal `answered_body` case, control seeds and a dedicated passer-by probe. A passing controlled seed is not proof that all procedural incident cases are resolved.

**Reliability (ADR-070):** failed hostile approaches now count toward giving up; a blocked walkway edge reverses only once; three nearby pass lanes reduce direct overlap; stale incidents and car weak keys are pruned. These are source-level changes with pure-rule assertions authored. Their author explicitly reports that **the new assertions were not run** and no rendered smoke was conducted. They must not inherit ADR-069's earlier PASS status.

**Clean build:** `2316b918` addresses a unity-build variable shadowing issue. Treat this as a corrected source defect, while separately checking the final head on a clean checkout and UE 5.8.3.

## Documentation reconciliation

The main README had remained pinned to the **12 November ADR-059 snapshot**, despite substantive ADR-060–070 delivery. The roadmap had newer engineering notes but its integrated-readiness header and source-evidence lane were outdated. Architectural and UI prose also contained pre-implementation claims about absent accounts/persistence and Skills UI.

This review updates the public [README](../README.md); [milestones](ROADMAP.md); [UI roadmap](UI_HUD_ROADMAP.md) and [UI visual contract](UI_HUD_DESIGN.md); [development guide](DEVELOPMENT.md); [AI status/backlog](AI_GAMEPLAY.md); [legacy parity](LEGACY_PARITY.md); [network](NETWORK_ARCHITECTURE.md) and [module architecture](ARCHITECTURE.md); [core ontology status note](ONTOLOGY.md); [first launch](FIRST_LAUNCH_ONTOLOGY.md); [game vision](VISION.md); [map adoption](WORLD_MAP_ADOPTION.md); and [fresh playtest procedure](PLAYTEST_FRESHNESS.md).

Historical day-by-day reviews, evidence documents and accepted architectural decisions are retained as *dated evidence*, rather than rewritten to imply the latest source was tested then. Normative gameplay/UI JSON contracts are unchanged: this update adds **no new game semantics** and performs no code or asset migration. The current source and captured evidence do not show that UI Settings, production email, mature historical reconstruction, World Partition, production NPC schedules, or large-scale shards have been delivered.

## Priority order after the review

| Priority | Task | Exit proof |
| --- | --- | --- |
| **P0, next** | Reproduce and execute ADR-070's previously unrun rule assertions; inspect a blocked chase, blocked pavement/river edge and pass-lane overlap using the current editor | Exact clean SHA + UE 5.8.3 UHT/UBT result, deterministic test log, visual capture, negative/control cases and bug register |
| **P0** | Seal and freshly package a matched Linux dedicated server and two clients with the zoned+attractor walk graph and current data | Fresh cooked hashes, server/client build manifests, no untracked/licensed input omissions, exact map/content versions |
| **P0** | Integrated two/three-client street regression: distinct districts, day/night/Sunday/weather, visits/door and stop behavior, incident propagation, combat pursuit and restart/reconnect | Client/server authority and replication, counts/density, no path oscillation, timing and bandwidth/CPU/performance logs |
| **P0** | Re-run known gameplay gates: >150 m driver/passenger vehicle, first-time/returning login and denied tickets, fishing/all eight mission flows with store restart and contention | Reproduced original owner defect and verified fix; negative/replay cases; explicit evidence |
| **P0** | Complete Settings/focus/controller/responsive/Slovene UI matrix, then personal owner playtest of the exact fresh build | Screenshot/interaction matrix, no stale screenshot claims, signed owner acceptance decision |
| **P1** | Detour pathfinding with bounded graph search, authored NPC routines, multi-agent density and traffic only as separately scoped work | New design/ADR approval where required, deterministic cost budget, measured crowd behavior and independent acceptance |

**Recommended immediate coding prompt:** validate, instrument and harden the **current street AI plus integrated package** before implementing a replacement AI framework. Start from the gaps left unexecuted in ADR-070; use the [AI backlog](AI_GAMEPLAY.md), [development commands](DEVELOPMENT.md) and [acceptance roadmap](ROADMAP.md). No new core architecture, assets or gameplay policy is implicitly approved by this review.

## Release/acceptance boundary

- **Implemented in source** is a narrower claim than **observed in an editor smoke**.
- An **editor smoke** is narrower than the **latest asset-fresh packaged two/three-client test**.
- A recorded automated pass does **not** replace the project's mandatory **personal owner playtest and explicit approval** (ADR-008).
- Earlier successful packages prove their earlier SHA/assets only.
- Third-party city and generator rights remain subject to the documented separate intake gates.

The present review changes documentation alone. It does not certify a release.
