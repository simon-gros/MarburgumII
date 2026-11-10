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
- **An evolving city.** Local time, seasonal conditions, local geography and institutions should affect gameplay. Past climate, historical, geospatial and narrative datasets are reference sources pending an explicit import and provenance audit.
- **No proprietary game-engine reuse.** The new engine foundation is Unreal, and third-party commercial game assets or code are not imported without appropriate rights.

## What exists today (11 November 2026)

Linux UE 5.8.3 Editor, packaged dedicated Server and Client have been built. Tests show a real authoritative online session, public and private chat, validated/persisted inventory transactions, the original angler and first mission, running/stamina, Drava swimming/drowning/recovery, and the original hatchback driving with a passenger. The original Unreal Lent map is the launch default; its façades and roof shells have been reworked. A textured third-person protagonist and actual UMG HUD/Character/Inventory/Journal shells render. See the [daily review](DAILY_CHANGE_REVIEW_2026-11-10.md), [roadmap](ROADMAP.md) and evidence documents.

**Not yet implemented/approved:** production email-verified accounts, login and character creation/admission; complete fishing/first mission; production database; the unpurchased commercial city model; broad skills/economy/city population; car ownership/fuel/traffic/sounds; full Windows qualification; one fully freshness-sealed cross-feature package and the owner's personally recorded approval. A Linux smoke test of a particular revision is not a released public game.
