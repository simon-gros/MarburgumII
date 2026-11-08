# Game vision

**Project:** Magna Raedarum: Marburgum II  
**Setting:** A fictionalized Maribor, Slovenia, primarily in the 1990s.  
**Genre:** A persistent, server-authoritative urban **MMORPG** with systems-heavy progression and a solo-friendly player experience, without fantasy themes.

## Core experience

A living Central European city enables exploration, missions, jobs, favors, vehicles, driving, social interaction, photography, trade, investigation, fishing, economic choices, housing and persistent consequences. Ordinary life is at least as important as the occasional confrontation. The player should experience sustained character growth through learning and skills, not relentless combat.

Design ambition includes a substantial authored main storyline (historical target around 75 hours), long-term skill progression and eventually a large explorable city. The first concrete content milestone is a **3–6-hour playable vertical slice** proving the systems and style before full-map production.

## Constraints carried into the restart

- **Skills, not talents.** Do not revive the earlier auto-generated talent framework. A level cap of 1990 was explored in the previous project but must be designed and tested afresh.
- **Missions, not quests.** The setting, vocabulary and writing must fit 1990s Maribor.
- **Early premium graphical interface.** Rebuild a polished, visually distinctive HUD and Character/Skills/Inventory/Journal screens early enough for real public-facing gameplay captures, while preserving the original's interaction and information architecture. No wanted stars or police-chase system.
- **MMORPG-first, solo-friendly.** Dedicated authoritative server/client separation is required from Stage 0. Solo play and 2–5-person cooperation remain important experiences within the shared-world architecture. High-population shards remain unverified targets subject to load testing.
- **Consequential actions.** Scarcity, fair opportunities for unsuccessful players, money, social position and reputation should shape meaningful decisions.
- **New default city-geometry source.** A licensed [Maribor 3D city model](WORLD_MAP_ADOPTION.md) is selected for purchase/qualification as MarburgumII's primary map base, replacing the original generated base-map path. The same city's original vegetation, fish, mission identities and 1990s historical treatment are retained; the new model's 2022 representation must be period-corrected. No purchased assets are yet in the repository.
- **Owner-tested gameplay.** Every major feature, including combat and world traversal, requires an actual personal owner playtest and affirmative approval before acceptance.
- **An evolving city.** Local time, seasonal conditions, local geography and institutions should affect gameplay. Past climate, historical, geospatial and narrative datasets are reference sources pending an explicit import and provenance audit.
- **No proprietary game-engine reuse.** The new engine foundation is Unreal, and third-party commercial game assets or code are not imported without appropriate rights.

## What exists today

This repository contains an initial UE 5.8.3 source scaffold, client/shared/server module skeletons and toolchain utilities. Its dedicated server/client runtime has **not** been built or validated. It does **not** contain the previous project's 121-window map, NPC database, prior missions, vehicles, persistent backend, packaged game or verified Editor build.

Avoid confusing a roadmap with an implemented system.
