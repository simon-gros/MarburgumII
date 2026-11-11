# Original Marburgum → MarburgumII parity register

**Status:** Baseline 8 November; implementation status updated through ADR-045 on 11 November 2026 ([earlier audit](DAILY_CHANGE_REVIEW_2026-11-10.md), [latest audit](DEVELOPMENT_UPDATE_2026-11-11.md)). Automated verification and owner approval remain separate.  
**Companion data:** [`Data/Parity/source-matrix.v0.1.json`](../Data/Parity/source-matrix.v0.1.json).  
**Authority:** [Vision](VISION.md), [Ontology](ONTOLOGY.md), [Network architecture](NETWORK_ARCHITECTURE.md), [Decisions](DECISIONS.md) and [Roadmap](ROADMAP.md).

## Inheritance rule

**The majority of original Marburgum game design is carried forward.** MarburgumII is a **new technical implementation of substantially the same intended game**, rather than a new and unrelated concept. We preserve the period setting, cultural/geographical identity, major world systems, RPG disciplines, original narrative direction, economy, social simulation, vehicle mobility and expected persistent shared-world experience. The initial vertical slice is deliberately small because we must prove the new MMORPG foundation, not because the full project scope has been discarded.

Changes made explicitly in this restart take precedence: **Unreal Engine 5.8.3**, new C++ runtime with isolated C# and Rust roles, server authority from Stage 0, separate ClientOnly/ServerOnly/Shared modules, and no inherited auto-generated talent trees. The legacy `PROTOCOL_V3.md` is historical reference; we do not automatically port that Godot-era wire protocol to Unreal.

**Legacy implemented** means code/data existed in the former repository, **not** that a feature already exists in MarburgumII. Likewise, **legacy planned** does not mean approved shipping behavior without review.

## Parity inventory

| ID | Inherited design area | First target stage | MarburgumII status |
| --- | --- | --- | --- |
| `world_geography` | Marburgum geography and district identity | Stage 1 nine-window world; Stage 3 streaming | Active `/Game/World/lent` now covers nine 512 m windows; 870 closed shells, surveyed ground/pavement, crossings and Drava (ADR-040/045); performance, period correction and owner review outstanding |
| `historical_state` | 1990s culture and period correctness | Stage 3 | Legacy design retained; implementation pending |
| `characters_population` | Characters, NPCs and virtual population | Stage 1 street slice; Stage 3 population | Angler/three original fighters plus server-controlled pedestrians walking pavement and reacting to fights, bodies, cars and panic (ADR-041/044); one-client editor verification, no packaged/multi-client population qualification |
| `skills` | Skill progression | Stage 2 | Legacy design retained; implementation pending |
| `fishing` | Fishing | Stage 2 | Legacy design retained; implementation pending |
| `missions_story` | Missions, story and evidence | Stage 1 start; Stage 2–5 completion/expansion | First mission starts, saves, grants rod and appears in Journal; fishing required to finish |
| `reputation_social` | Reputation, relationships and organizations | Stage 2-3 | Legacy design retained; implementation pending |
| `economy_trading` | Economy and trading | Stage 1-2 | Legacy design retained; implementation pending |
| `inventory_items` | Items, clues and inventory | Stage 1 | First runtime slice: `fishing_rod` (original id) held in owner-only server custody, durable across restarts; awaiting owner playtest |
| `housing` | Housing and property | Stage 3 | Legacy design retained; implementation pending |
| `combat_fighting` | Unarmed fighting and NPCs answering violence | Stage 1 PvE | Server jab/heavy/guard, three fighters, down/recovery and CC0 audio; prior packaged two-client smoke (ADR-041), IK-corrected feet editor-tested later (ADR-043). Weapons/PvP/expanded consequences absent |
| `vehicles` | Vehicles and transportation | Stage 1 drive and collisions; Stage 3 traffic | Five original cars, server driving/driver-passenger, parked hatchback on revised carriageway, foot-IK-fixed door clips and ambient NPC run-over/dodge (ADR-039/040/043/044); no persistence, ownership/fuel/traffic/vehicle damage/audio |
| `weather_time` | City time, seasonal weather and environment | Stage 3 | Legacy design retained; implementation pending |
| `interactions` | Unified urban interaction verbs | Stage 1 | First runtime slice: proximity prompt, client request → server validation → durable receipt on a test fixture; awaiting owner playtest |
| `jobs_opportunity` | Employment, opportunity scarcity and fairness | Stage 2-4 | Legacy design retained; implementation pending |
| `user_interface` | Player-facing UI, HUD and journal (visually rebuilt) | Stage 0C–1 | Live UMG shell/chat/prompt/location/objective/condition/swim/vehicle and Character/Inventory/Journal; Skills, Settings and auth/creator missing |
| `network_persistence` | Dedicated authoritative MMORPG world | Stage 0-4 | Dedicated server and clients qualified; development file store and test identity only (ADR-014) |

The companion JSON contains the original source paths and explicit migration rules for all 17 current domain entries. This table is **not** a claim that the whole content catalogue, source assets, data imports, maps, or network services have been ported.

## Current launch map and conditional future replacement

The **original Unreal `/Game/World/lent` level remains the default** (ADR-027), but ADR-040/045 rebuild its terrain/roads using 2014 LiDAR heights and OSM collidable paving and extend it across all eight adjoining original windows. **The reverted ADR-026 generated LiDAR building-shell replacement is distinct from the now-active LiDAR ground-height input**. The original level's spatial identities remain; period accuracy, geometry and licensed source provenance need separate review. The [licensed candidate Maribor map](WORLD_MAP_ADOPTION.md) is unpurchased, unimported and cannot replace the current level without separate owner acceptance. This change does **not** supersede the city's place IDs, authored mission contacts, vehicle routes, 1990s identity, vegetation/fish species or relevant ecology records. All those migrate by spatial crosswalk and provenance review. The original map pipeline remains reference/fallback, not the production default. Purchased commercial mesh/textures stay out of public repository and downloadable source artifacts.

Every migrated major playable feature requires a **personal in-game owner playtest and explicit approval** (ADR-008) before acceptance. CI or screenshots from an agent alone do not satisfy this decision.

## Explicit owner-approved exclusions

The preservation principle does **not** apply to everything: the original **wanted stars**, **wanted/heat levels**, **police-chase/pursuit loop** (including escalation, roadblocks or chase indicators), and **legacy generated talents/Talent UI** are intentionally excluded from MarburgumII. No migration ticket should restore them indirectly through the HUD, NPC behavior, missions or copied gameplay data. NPCs serving institutional/narrative roles are a separate design question, not permission to rebuild the wanted system. See [ADR-006](DECISIONS.md) and [UI_HUD_DESIGN.md](UI_HUD_DESIGN.md).

The original HUD's *interaction, place, condition, objective, notification, skills, inventory and journal semantics* remain in scope, but their visual assets and architecture must be freshly designed, cohesive and high quality from the first public-facing builds. The migration and presentation semantic contract is now [UI_HUD_ONTOLOGY.md](UI_HUD_ONTOLOGY.md), with staged implementation and proof in [UI_HUD_ROADMAP.md](UI_HUD_ROADMAP.md).

## Image-generated visual supplements (conditionally permitted)

The original plant and fish **species IDs and ecological semantics remain authoritative** even when their presentation meshes are newly authored. A local Hunyuan3D-2.1 image-to-geometry/PBR pipeline may be considered for individual visual variants after resolving its explicit EU license restriction, checking each input image's rights, retopology, material/import checks, accurate species identification and mandatory owner playtest. This workflow is distinct from transferring the original catalogues and is never a replacement for approved environmental data. See [ASSET_GENERATION_HUNYUAN3D.md](ASSET_GENERATION_HUNYUAN3D.md).

## New implementation parity added 11 November (ADR-040–045)

**Map and ground:** one 1,536 m square, nine adjoining original 512 m windows, closed-shell joined buildings, three additional crossings, connected Drava sheets, physical street/sidewalk collision and terrain derived from 2014 ARSO LiDAR plus separately sourced OSM geometry. The world remains **one always-loaded level**, with no streaming, finished period correction, packaged performance proof or owner acceptance ([review](DEVELOPMENT_UPDATE_2026-11-11.md)).

**Combat and animation:** original author-owned jab/heavy/guard, 3-person Lent fight cast, server-owned targeting/damage and CC0 impacts; distinct earlier packaged two-client combat versus later editor-only IK feet checks. Swimming, door and fight clips key IK bones so the drawn pose matches their motion. No weapons, PvP or wanted/police consequences.

**Ambient street people:** original author-owned groups, looks and incident decisions; new independent spawn/cull/response numbers replace Rockstar-derived material. The server moves replicated pedestrians on pavement and handles fights, bodies, panic, run-overs and dodge. **Only a one-client editor-mode runtime test currently qualifies street people**; not a full city-life or crowd simulation. No everyday routines, multi-client or staged validation.

## Additional original-game parity delivered during 10 November

The **third-person protagonist** is currently a single generated, retopologized and UE mannequin-rigged model with repaired hands, UV bake, back and matte material (ADR-021, 023, 028, 031, 035, 037, 038). This is not a multi-model character creator.

The **movement/condition/swimming** path includes server movement-time bounding, saved sprint/winded state, Drava water volumes, prone flutter stroke, idle upright tread, held breath, drowning and restart recovery (ADR-019, 020, 032, 033, 036; [evidence](STAGE1_SWIM_EVIDENCE.md)). Audio, underwater presentation and personal owner acceptance remain pending.

The **vehicle migration** retains original five-car authored data, procedural bodywork and the original self-authored enter/exit clips; Unreal server-only four-raycast physics, seat RPCs, hatchback and passenger work in the tested slice (ADR-039; [evidence](STAGE1_VEHICLE_EVIDENCE.md)). GTA/San Andreas placeholder content is intentionally not imported. A car's location is not yet saved across restarts.

The **fighting migration** carries the original's `combat.h` numbers, its `world_simulation` fight rules and the Belligerent drinker, Evening walker and Newspaper reader at their Lent positions, with its own guard/jab/swing clips (ADR-041; [evidence](STAGE1_COMBAT_EVIDENCE.md)). The GTA hit/get-up clips and fight sounds and the Unreal port's wanted level and police are not imported. One redesign: a downed NPC gets up after a minute, because a persistent world never restarts.

The **street people migration** carries the original Unreal port's ambient population (`MarburgumAmbientPopulation`, `MarburgumRunOver`, `population_groups.json`, `npc_visual_profiles.json`, with the panic-spread and react-to-bodies branches): groups, looks, the body/attacked/bumped tables and the run-over rules unchanged, on a walk graph rebuilt from Lent's pavement (ADR-044; [evidence](STAGE1_STREET_EVIDENCE.md)). San Andreas' fight weights, spawn/cull bands and fight-notice radius and GTA III's wander, signal clock, car following and crime/pursuit are not imported; Marburgum II's own numbers replace them.

**Optional NVIDIA rendering** (ADR-029/030) is client-side and legally gated, not evidence of full visual-platform parity or authorization to use third-party proprietary binaries. An ADR acceptance is not the owner's gameplay sign-off.

## High-value original references

- Original [general framework](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/GENERAL_FRAMEWORK.md): design identity and urban RPG model.
- Original [skill roadmap](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/SKILLING_ROADMAP.md) and [registered skills](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/project/data/skills.json): five previously registered definitions versus the fifteen-skill design roster. The 0–1990 identity and documented XP curve remain references for explicit balance acceptance, not invented new rules.
- Original [Narrative Bible](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/NARRATIVE_BIBLE.md), [main storyline](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/MAIN_STORYLINE.md) and [missions](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/project/data/missions.json): preserve established narrative, relationships, clues and mission intent, subject to renewed gameplay/data acceptance.
- Original [fictionalization design](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/MARBURGUM_FICTIONALIZATION_ROADMAP.md) and [1990s evidence](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/NINETIES_AUTHENTICITY_WORLD_STATE_ROADMAP.md): do not mistake contemporary geodata for verified period state; use approved transformations.
- Original [multiplayer scalability plan](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/MULTIPLAYER_SCALABILITY_ROADMAP.md): 250 concurrent players per shard was an engineering goal with additional headroom tests, not an achieved capacity.

## Approval criteria for carrying over a feature

Each migrated feature should identify its **legacy specification and data**, intended **invariant gameplay behavior**, new **owner module**, schema and persistence semantics, authoritative **two-client test**, UI acceptance, and reviewed source/asset rights. Differences from legacy behavior should be deliberate and recorded. Reimplement one representative feature end-to-end before copying an extensive catalogue of untested definitions.

For geographically grounded content, retain source IDs and transformation history. For authored story content, retain canonical identifiers and investigate dependencies before rewriting. For mechanics, retain observable outcomes rather than legacy engine-specific internals. For all persistence, compare reconnect, retry, crash-recovery and duplicate-command behavior.

## Next immediate engineering obligations (reconciled)

1. Existing Linux evidence shows actual native client/server, chat, durable interaction/mission, condition, swimming and driving slices. It covers different source commits and maps, not one final current release.
2. **Immediately:** rebuild one clean UE 5.8.3 client/server/cooked-world snapshot with latest protagonist, roof, swim and car assets, run complete mixed-system multiplayer and UI matrix on `/Game/World/lent`, record asset seal and obtain the owner's own game acceptance.
3. **Public-entry blocker:** secure email-verified accounts, username/password, First name/Last name/approved model character creation and authenticated world admission (Stage 0F). Development character keys are never production identity.
4. **Next complete game loop:** real Drava fishing and `lent_first_catch` completion with replay/reconnect/restart safety; then Fishing skill, trade and larger city systems.

**Updated immediate matrix:** repackage nine-window content and regenerated swim/door/fight assets, stage the latest street graph, run street/combat alongside all previous smokes with a real two/three-client integrated scenario, measure world performance and obtain the owner's fresh-build map and gameplay decision. A file, checklist, test result or ADR alone cannot close a major feature as owner-approved.
