# Original Marburgum → MarburgumII parity register

**Status:** Design traceability, version **0.1.0**, established 8 November 2026.  
**Companion data:** [`Data/Parity/source-matrix.v0.1.json`](../Data/Parity/source-matrix.v0.1.json).  
**Authority:** [Vision](VISION.md), [Ontology](ONTOLOGY.md), [Network architecture](NETWORK_ARCHITECTURE.md), [Decisions](DECISIONS.md) and [Roadmap](ROADMAP.md).

## Inheritance rule

**The majority of original Marburgum game design is carried forward.** MarburgumII is a **new technical implementation of substantially the same intended game**, rather than a new and unrelated concept. We preserve the period setting, cultural/geographical identity, major world systems, RPG disciplines, original narrative direction, economy, social simulation, vehicle mobility and expected persistent shared-world experience. The initial vertical slice is deliberately small because we must prove the new MMORPG foundation, not because the full project scope has been discarded.

Changes made explicitly in this restart take precedence: **Unreal Engine 5.8.3**, new C++ runtime with isolated C# and Rust roles, server authority from Stage 0, separate ClientOnly/ServerOnly/Shared modules, and no inherited auto-generated talent trees. The legacy `PROTOCOL_V3.md` is historical reference; we do not automatically port that Godot-era wire protocol to Unreal.

**Legacy implemented** means code/data existed in the former repository, **not** that a feature already exists in MarburgumII. Likewise, **legacy planned** does not mean approved shipping behavior without review.

## Parity inventory

| ID | Inherited design area | First target stage | MarburgumII status |
| --- | --- | --- | --- |
| `world_geography` | Marburgum geography and district identity | Stage 3 | Legacy design retained; implementation pending |
| `historical_state` | 1990s culture and period correctness | Stage 3 | Legacy design retained; implementation pending |
| `characters_population` | Characters, NPCs and virtual population | Stage 3 | Legacy design retained; implementation pending |
| `skills` | Skill progression | Stage 2 | Legacy design retained; implementation pending |
| `fishing` | Fishing | Stage 2 | Legacy design retained; implementation pending |
| `missions_story` | Missions, story and evidence | Stage 2-5 | Legacy design retained; implementation pending |
| `reputation_social` | Reputation, relationships and organizations | Stage 2-3 | Legacy design retained; implementation pending |
| `economy_trading` | Economy and trading | Stage 1-2 | Legacy design retained; implementation pending |
| `inventory_items` | Items, clues and inventory | Stage 1 | Legacy design retained; implementation pending |
| `housing` | Housing and property | Stage 3 | Legacy design retained; implementation pending |
| `vehicles` | Vehicles and transportation | Stage 3 | Legacy design retained; implementation pending |
| `weather_time` | City time, seasonal weather and environment | Stage 3 | Legacy design retained; implementation pending |
| `interactions` | Unified urban interaction verbs | Stage 1 | Legacy design retained; implementation pending |
| `jobs_opportunity` | Employment, opportunity scarcity and fairness | Stage 2-4 | Legacy design retained; implementation pending |
| `user_interface` | Player-facing UI, HUD and journal (visually rebuilt) | Stage 0C–1 | Legacy interface semantics retained; polished new appearance pending |
| `network_persistence` | Dedicated authoritative MMORPG world | Stage 0-4 | Legacy design retained; implementation pending |

The companion JSON contains the original source paths and explicit migration rules for all 16 areas. This table is **not** a claim that the whole content catalogue, source assets, data imports, maps, or network services have been ported.

## Default map-source replacement

The original **generated city geometry/map-production pipeline is superseded as the default**. The newly selected [licensed Maribor geometry source](WORLD_MAP_ADOPTION.md) is **planned for purchase and must pass import and owner-playtest gates**. This change does **not** supersede the city's place IDs, authored mission contacts, vehicle routes, 1990s identity, vegetation/fish species or relevant ecology records. All those migrate by spatial crosswalk and provenance review. The original map pipeline remains reference/fallback, not the production default. Purchased commercial mesh/textures stay out of public repository and downloadable source artifacts.

Every migrated major playable feature requires a **personal in-game owner playtest and explicit approval** (ADR-008) before acceptance. CI or screenshots from an agent alone do not satisfy this decision.

## Explicit owner-approved exclusions

The preservation principle does **not** apply to everything: the original **wanted stars**, **wanted/heat levels**, **police-chase/pursuit loop** (including escalation, roadblocks or chase indicators), and **legacy generated talents/Talent UI** are intentionally excluded from MarburgumII. No migration ticket should restore them indirectly through the HUD, NPC behavior, missions or copied gameplay data. NPCs serving institutional/narrative roles are a separate design question, not permission to rebuild the wanted system. See [ADR-006](DECISIONS.md) and [UI_HUD_DESIGN.md](UI_HUD_DESIGN.md).

The original HUD's *interaction, place, condition, objective, notification, skills, inventory and journal semantics* remain in scope, but their visual assets and architecture must be freshly designed, cohesive and high quality from the first public-facing builds. The migration and presentation semantic contract is now [UI_HUD_ONTOLOGY.md](UI_HUD_ONTOLOGY.md), with staged implementation and proof in [UI_HUD_ROADMAP.md](UI_HUD_ROADMAP.md).

## High-value original references

- Original [general framework](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/GENERAL_FRAMEWORK.md): design identity and urban RPG model.
- Original [skill roadmap](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/SKILLING_ROADMAP.md) and [registered skills](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/project/data/skills.json): five previously registered definitions versus the fifteen-skill design roster. The 0–1990 identity and documented XP curve remain references for explicit balance acceptance, not invented new rules.
- Original [Narrative Bible](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/NARRATIVE_BIBLE.md), [main storyline](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/MAIN_STORYLINE.md) and [missions](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/project/data/missions.json): preserve established narrative, relationships, clues and mission intent, subject to renewed gameplay/data acceptance.
- Original [fictionalization design](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/MARBURGUM_FICTIONALIZATION_ROADMAP.md) and [1990s evidence](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/NINETIES_AUTHENTICITY_WORLD_STATE_ROADMAP.md): do not mistake contemporary geodata for verified period state; use approved transformations.
- Original [multiplayer scalability plan](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/MULTIPLAYER_SCALABILITY_ROADMAP.md): 250 concurrent players per shard was an engineering goal with additional headroom tests, not an achieved capacity.

## Approval criteria for carrying over a feature

Each migrated feature should identify its **legacy specification and data**, intended **invariant gameplay behavior**, new **owner module**, schema and persistence semantics, authoritative **two-client test**, UI acceptance, and reviewed source/asset rights. Differences from legacy behavior should be deliberate and recorded. Reimplement one representative feature end-to-end before copying an extensive catalogue of untested definitions.

For geographically grounded content, retain source IDs and transformation history. For authored story content, retain canonical identifiers and investigate dependencies before rewriting. For mechanics, retain observable outcomes rather than legacy engine-specific internals. For all persistence, compare reconnect, retry, crash-recovery and duplicate-command behavior.

## Next immediate engineering obligations

1. Compile Client, Server and Editor targets with **Unreal Engine 5.8.3**.
2. Prove one dedicated server, one then two separate clients, replicated GameState and reconnect behavior.
3. Prove one real authoritative interaction → inventory/XP or mission state → replication/UI → save/reload path.
4. Only then scale content/data import and progressively complete the parity matrix.

A checklist or source file alone cannot close a feature as implemented.
