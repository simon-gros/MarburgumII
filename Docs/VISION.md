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
- **MMORPG-first, solo-friendly.** Dedicated authoritative server/client separation is required from Stage 0. Solo play and 2–5-person cooperation remain important experiences within the shared-world architecture. High-population shards remain unverified targets subject to load testing.
- **Consequential actions.** Scarcity, fair opportunities for unsuccessful players, money, social position and reputation should shape meaningful decisions.
- **An evolving city.** Local time, seasonal conditions, local geography and institutions should affect gameplay. Past climate, historical, geospatial and narrative datasets are reference sources pending an explicit import and provenance audit.
- **No proprietary game-engine reuse.** The new engine foundation is Unreal, and third-party commercial game assets or code are not imported without appropriate rights.

## What exists today

This repository contains an initial UE 5.8.3 source scaffold, client/shared/server module skeletons and toolchain utilities. Its dedicated server/client runtime has **not** been built or validated. It does **not** contain the previous project's 121-window map, NPC database, prior missions, vehicles, persistent backend, packaged game or verified Editor build.

Avoid confusing a roadmap with an implemented system.
