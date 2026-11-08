# Milestone roadmap

## Stage 0 — Engine foundation

Delivered as source: project descriptor, initial Unreal C++ game mode and three targets, exact **5.8.3** engine pin, developer build/check scripts, .NET validation utility, isolated Rust crate and tests, and basic GitHub CI.

**Still required:** compile with UE 5.8.3 on a real development machine; validate the generated project files and UHT; launch Unreal Editor; verify startup game mode and a minimal play-in-editor session. Green C# or Rust CI alone does not close this milestone.

## Stage 1 — Playable district

An appropriately sourced miniature urban map, camera and character controls, useful HUD, interactable objects and a test save/load loop. The UI should represent state graphically and clearly rather than accumulating textual debug indices.

**Gate:** reproducible player interaction and visible state change in the Editor and a packaged smoke build.

## Stage 2 — Missions, skills, state

One end-to-end mission and associated skill progression, a reviewable XP curve, local economic exchange, basic NPC responses and save/reload with idempotent transactions. Keep the framework focused on **skills**, with **no talent system**.

**Gate:** automated consistency tests, recovery tests and manual gameplay acceptance.

## Stage 3 — City, simulation, mobility

Provenance-reviewed Maribor street/topology/terrain/building data; city streaming, navigation, vehicles, district activity, time, weather, population and social simulations.

**Gate:** no known map-seam integrity defects, measured streaming/memory budgets and robust navigation within the implemented content.

## Stage 4 — Authoritative online services

Dedicated server runtime, reconnect/resume, persistence, multiplayer replication, permissions, rate limits, operational visibility and staged concurrency testing.

**Gate:** documented server soak/load and failure-recovery tests. Hundreds of players per shard remain a possible design target rather than a promised achieved capacity.

## Stage 5 — Narrative and scale

Iterate from a well-finished **3–6-hour vertical slice** toward the longer campaign aspiration (historically approximately 75 hours), more skills, city content, co-op and validated platform targets.

## Working rules

An implementation is not complete until it builds, runs and meets a written acceptance gate. The previous Marburgum project is a source of requirements and candidate datasets, not an assumed compatible engine/asset bundle. All engine upgrades require a deliberate version-pin update and requalification.
