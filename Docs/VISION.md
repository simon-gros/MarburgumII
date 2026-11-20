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
- **Current and future city geometry.** The active default is the owner-selected original Unreal `/Game/World/lent` (ADR-027), with later façade/roof corrections. A licensed [Maribor model](WORLD_MAP_ADOPTION.md) is a separate unpurchased candidate for future expanded city geometry, requiring explicit qualification and owner acceptance. The same city's original vegetation, fish, mission identities and 1990s historical treatment are retained; the new model's 2022 representation must be period-corrected. No purchased assets are yet in the repository.
- **Owner-tested gameplay.** Every major feature, including combat and world traversal, requires an actual personal owner playtest and affirmative approval before acceptance.
- **An evolving city.** Local time, seasonal conditions, local geography and institutions should affect gameplay. The original art-reference catalogue and metadata-only historical imagery/map leads are now imported for research (see [HISTORICAL_ARCHIVES.md](HISTORICAL_ARCHIVES.md)); their use in historic environment reconstruction remains evidence- and rights-gated. Broader climate, geospatial and narrative datasets require individual parity and provenance review.
- **No proprietary game-engine reuse.** The new engine foundation is Unreal, and third-party commercial game assets or code are not imported without appropriate rights.

## What exists today (reconciled 20 November 2026, ADR-075)

Native UE **5.8.3** Editor/Client/Server, genuine dedicated multiplayer, private/general chat, durable interaction and first mission, condition and swimming, original hatchback driving, unarmed server-owned fighting, a first ambient-street population and graphical UMG/character panels are implemented in **specific Linux development slices**. This is an urban MMORPG prototype, not a released game.

The default original Unreal `/Game/World/lent` now covers **nine original 512 m windows (1,536 m square)**, with 870 closed-shell buildings and joined neighboring bridges/river sheets, survey-derived ground and collidable OSM streets, and a walk graph for people. This is still **one always-loaded map** without World Partition. The [20 November review](DEVELOPMENT_REVIEW_2026-11-20.md) now covers the merged QA fixes, ADR-072 nine-window visual audit, local/shared crowds, bridge graph repairs (ADR-073–075) and partial clean packaged two/three-client crowd evidence. The opposite-bank scenario **times out at 11/28** and owner approval remains open. The [11 November audit](DEVELOPMENT_UPDATE_2026-11-11.md) remains historical.

**Outstanding public-player milestones:** Email-code accounts and first-character creation, fishing and the eight original mission flows are already implemented in development slices. Still open are deployed email/security (AR1), a complete fresh packaged gameplay matrix, Settings and accessible/scalable/localized UI, period-correct city art and missing vegetation/furniture, stronger NPC routines, proven streaming/capacity, and personal owner acceptance. A clean packaged crowd test exists but the full daytime matrix is **not green** (opposite banks 11/28). The commercial TurboSquid candidate remains **unpurchased and nonblocking**. Major features require explicit owner approval after test evidence.

**3D authoring policy (ADR-071, 19 November 2026):** new image-to-3D model candidates use **Microsoft TRELLIS**, replacing the earlier Hunyuan3D-2.1 proposal. The base city, original fish and plant semantics, provenance controls and owner visual acceptance remain separately authoritative; see [ASSET_GENERATION_TRELLIS.md](ASSET_GENERATION_TRELLIS.md).
