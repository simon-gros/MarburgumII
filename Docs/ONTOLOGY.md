# Marburgum II — Foundational Game Ontology

**Version:** 0.1.0 (design baseline, 8 November 2026)  
**Project:** *Magna Raedarum: Marburgum II*  
**Engine target:** Unreal Engine **5.8.3**  
**Companion vocabulary:** [`Data/Ontology/core.v0.1.json`](../Data/Ontology/core.v0.1.json)  
**Status:** Proposed semantic model. This document does **not** assert that any listed gameplay system, historical reconstruction, persistent service, or map has been implemented in MarburgumII.

## 1. Purpose and identity

This ontology defines what exists **in the game world**, what relationships can hold between those things, and what facts or events can change persistent state. It is a domain language for design documents, content catalogues, data contracts, save migrations, Unreal gameplay code, future deterministic Rust routines, C# validation tools, and UI. It is **not** an Unreal class diagram, an implementation roadmap, a database schema, or an import of legacy source code.

The foundational world is **Marburgum**, a fictionalized interpretation of **Maribor, Slovenia, in the 1990s**. The core experience is ordinary and extraordinary urban life: exploration, learning skills, fishing, photography, jobs and missions, trading, housing, vehicles, investigation, relationships, institutions, reputation, and consequential decisions. Persistent systemic progression is the inspiration taken from MMORPGs; medieval fantasy conventions and a default combat treadmill are not.

**Vocabulary decisions inherited by the restart:** use **mission**, **job**, **favor**, **investigation**, **activity**, and **encounter** where appropriate; avoid treating all urban situations as fantasy “quests.” **Skills** are the progression framework. The old generated **talent** framework is not part of MarburgumII.

## 2. What constitutes an entity

A **definition** is authored/reference data specifying a kind of thing: `ItemDefinition`, `SkillDefinition`, `VehicleDefinition`, `MissionDefinition` or `InteractionDefinition`. An **instance** is an individually identifiable occurrence or owner-specific state: a vehicle in a street, a player-owned item, an ongoing mission or a relationship. An **event** is an immutable observation of a committed change: a completed interaction, an economic transfer, a discovered clue or a world-state transition. A **source assertion** describes what evidence says about the real world or the game's historical reconstruction; it must never silently become an established 1990s fact.

Every persistent entity/record should have a **stable, namespaced identifier** that is independent of the on-screen label and an explicit schema/version authority. Use references by ID, not object-display-name matching. A projected Unreal `AActor`, Data Asset, World Partition cell or gameplay tag can represent a concept without becoming the conceptual source of truth. A source feature ID is distinct from its fictional game identity.

The tables below describe the proposed **semantic types**, not classes that already exist.

## 3. Core domains

| Domain | Concepts | Meaning and examples |
| --- | --- | --- |
| Spatial world | `World`, `Place`, `District`, `Building`, `RoadSegment`, `WaterBody` | Marburgum; Lent, Tabor or Melje; a service room, street, bridge approach or the Drava. A place can have a fictional identity, geometry and temporal variants. |
| Historical identity | `SourceRecord`, `HistoricalAssertion`, `FictionalIdentity` | Contemporary GIS, archival evidence and a documented 1990s ruling are **different** records. An authored Marburgum name/function is a reviewed design choice, not a historical fact. |
| Persons and society | `Character`, `PlayerCharacter`, `NPC`, `Organization`, `PopulationGroup`, `Relationship` | People have roles, affiliations, schedules, knowledge and interactions. Virtual population groups are simulation abstractions, not necessarily spawned individuals. |
| Progression | `SkillDefinition`, `SkillState`, `ReputationState` | Learned competence, earned XP, and a separately modelled reversible public-reputation measure. Social relationships are not automatically additional skill meters. |
| Goods and economy | `ItemDefinition`, `ItemInstance`, `Inventory`, `MoneyAccount`, `MarketOffer`, `Transaction` | Physical or evidentiary goods; ownership, availability, prices, money and audit-safe exchange. |
| Narrative and opportunity | `MissionDefinition`, `MissionRun`, `Objective`, `Evidence`, `Choice` | Authored objectives, per-character progress, facts/objects that support investigation, and durable consequences. A job is a **mission kind**, not an unrelated progression framework. |
| Interaction | `InteractionDefinition`, `ActionEvent` | A permitted verb against a valid target and its committed result: talk, inspect, catch, purchase, open, drive or photograph. |
| Mobility | `VehicleDefinition`, `VehicleInstance`, `Journey` | A vehicle archetype, a particular vehicle with condition/occupants, and a trip that has origin, destination, cost and progress. |
| Changing world | `WorldClock`, `EnvironmentalState`, `WorldEvent`, `StateRecord` | Shared game time, season/weather, events, and authoritative durable state; separate from local rendering and background ambience. |

### 3.1 Spatial and historical semantics

`World` comprises a geographical network of `Place` records. `District`, `Building`, `RoadSegment` and `WaterBody` are kinds of place; district boundaries and building footprints are spatial evidence that may require correction. A building can contain interiors, business venues, interaction points and mission sites. Roads connect traversable places; the Drava is a geographical feature whose riverbanks may host interactions and activities. Location and navigation are meaningful even if no detailed mesh is loaded.

The **canonical geographic design pipeline** is: source/development-map evidence → selected spatial correction or compression → defensible period reconstruction → controlled fictionalization → production representation. Keep these provenance transitions explicit. Current cadastral or administrative data must not be labelled automatically as verified 1990s geography. A real-world place, a historical claim, a fictional Marburgum identity and a generated Unreal asset are four different concepts.

Every period-dependent assertion should carry a temporal scope or an explicitly **unknown** period. Place names and institutional functions may change over the decade. Fictionalization intensity (the original project's F0–F4 classification) is a *design decision on identity* and is distinct from geometric correction intensity.

### 3.2 People and social structures

A `PlayerCharacter` and an `NPC` are character kinds. A connected account is **not** itself a character and should not be conflated with character progress. NPCs may be individually authored, temporarily simulated, or represented in aggregate by `PopulationGroup`. An NPC's rendered actor lifecycle is not necessarily its persistent social lifecycle.

A `Character` may belong to or interact with several `Organization` records: employers, shops, associations, transport operators or public institutions. `Relationship` records associate a person with another person or organization and store **contextual knowledge, trust, access and history**. The single visible `ReputationState` is a broad and reversible social signal, while local reactions can be more specific and need not expose many competing reputation bars.

### 3.3 Skills and progress

The **original project's registered data catalogue** contained **five** definitions: `fishing`, `photography`, `housing`, `trading` and `reputation`. Its **longer-term design roster** named **fifteen** skills: Fishing, Trading, Photography, Housing, Reputation, Cooking, Electronics, Music, Chemistry, Logistics, Athletics, Research, Mechanics, Medicine and Occult. These two statuses must not be confused. **MarburgumII currently has no implemented skill gameplay.**

A `SkillDefinition` specifies an activity domain, a rule for earning experience and potential unlocks. A `SkillState` belongs to a character, references a definition and records progression. Ordinary learned skills are **nondecreasing by default**. `ReputationState` is a deliberate exception: current public standing may rise or fall while history/peak values remain inspectable. The previous project's shared **level 0–1990** model and XP curve are **legacy design references requiring renewed balance/acceptance**, not an automatically implemented MarburgumII numerical contract. **Occult** has an approved name in the old design roster but no authorized supernatural mechanics or detailed rule set; do not invent either.

### 3.4 Missions, activities and evidence

`MissionDefinition` encodes an authored narrative or opportunity contract; its kind may be `story`, `side`, `job`, `favor` or `investigation`. `MissionRun` records an individual's acceptance, ordered or conditional `Objective` progress, outcomes, and failure/retry state. The same mission definition can produce different runs or choices; a deadline is not automatically an irreversible story lockout.

`Evidence` can be an item, witness statement, photograph, record or observation, with provenance and discoverability distinct from its interpretation. `Choice` affects mission outcomes, trust, reputation, economy or the world when its effects are authoritatively committed. Scarce job openings and fairness protections are **future design candidates**; critical authored narrative routes must remain reachable.

An `Activity` such as fishing or photography can exist independently of a mission. A mission may require an activity but does not own the definition or the activity's core mechanics.

### 3.5 Inventory and economy

`ItemDefinition` describes item meaning and constraints. `ItemInstance` describes a particular persistent item, where instance identity is required; fungible goods may be represented as validated stacks. A character's `Inventory` is a custody container, not proof of permanent legal ownership. `MoneyAccount` uses explicit currency and integer minor units rather than floating-point financial balances. `MarketOffer` is a time- and place-sensitive proposed exchange.

`Transaction` is a durable, all-or-nothing economic transfer. A successful purchase changes money and inventory together, with an authoritative receipt and stable idempotency key. Retrying the same request must not duplicate money, items or XP. Trade, mission rewards, work and transport costs share this economic invariant.

### 3.6 Vehicles, time and urban simulation

`VehicleDefinition` captures a vehicle's driving/archetypal characteristics; `VehicleInstance` has condition, position, possession and occupants. `Journey` represents traversal or a transport service, including a possible **paid taxi** (a prior design direction rather than an implemented fast-travel feature). A journey has game consequences even if its visual duration is compressed.

`WorldClock` describes the single shared simulation calendar, with the historical in-world date and local civil time modelled separately from host wall-clock time. `EnvironmentalState` contains weather and other time-bound conditions. `WorldEvent` records a meaningful occurrence such as an institution closing, an opportunity being posted or a public event. Ambient pedestrians, weather VFX and rendered vehicles are projections of world state and simulation rules, not evidence that every one of those objects needs a fully persistent actor.

## 4. Canonical relationships

The relation vocabulary is also published in the companion JSON file. The `cardinality` column is the number of **targets per source**, not a declaration about inverse uniqueness.

| Subject → relation → object | Cardinality | Meaning |
| --- | --- | --- |
| `World` → contains → `Place` | 0..many | A place belongs to a world; it can overlap or intersect other places. |
| `Place` → situated_in → `District` | 0..many | Districts describe geography and may overlap other boundaries. |
| `Place` → documented_by → `SourceRecord` | 0..many | Source data informs geography, identity or chronology. |
| `Place` → has_identity → `FictionalIdentity` | 0..many | A place may have date-specific/approved authored identities. |
| `Character` → located_at → `Place` | 0..1 | Current semantic location; not an Unreal actor pointer. |
| `Character` → affiliated_with → `Organization` | 0..many | Employment, membership or another recorded affiliation. |
| `Character` → has_relationship → `Relationship` | 0..many | Contextual social ties to another party. |
| `Character` → has_skill_state → `SkillState` | 0..many | One current state for each skill definition per character. |
| `Character` → has_reputation → `ReputationState` | 0..1 | Reversible public standing. |
| `Character` → carries → `Inventory` | 0..many | Inventory/custody containers. |
| `Inventory` → contains_item → `ItemInstance` | 0..many | Each persistent item has one current authoritative custodian. |
| `Character` → participates_in → `MissionRun` | 0..many | An individual character's progress and outcome. |
| `MissionRun` → instance_of → `MissionDefinition` | exactly 1 | Progress refers to an immutable, versioned mission definition. |
| `MissionDefinition` → defines_objective → `Objective` | 1..many | Authored steps or conditions. |
| `MissionRun` → establishes → `Choice` | 0..many | Durable narrative decisions. |
| `ActionEvent` → targets → `Place` / `Character` / `ItemInstance` / `VehicleInstance` | 0..many | Legal interaction target(s), according to the verb contract. |
| `Transaction` → transfers → `ItemInstance` | 0..many | Item movement reconciled with a receipt and money deltas. |
| `VehicleInstance` → instance_of → `VehicleDefinition` | exactly 1 | Vehicle archetype and instance state remain separate. |
| `Journey` → uses → `VehicleInstance` | 0..1 | Some journeys are on foot or otherwise vehicle-free. |
| `WorldEvent` → changes → `StateRecord` | 0..many | Committed world consequences are durable and auditable. |

## 5. Cross-domain invariants

1. **Authority and durability.** Only the authoritative simulation may commit inventory, economic balances, progress, missions, reputation and irreversible choices. UI and client prediction display or propose changes; they do not award them.
2. **Unique identity and referential integrity.** Stable definition/instance IDs are unique within their namespace, references resolve against a versioned catalogue, and renaming a display label never changes identity. Avoid reusing removed IDs.
3. **Idempotent outcomes.** A command retry with the same operation identity must not apply the associated state transition or reward twice. Use transactional receipts and crash/reconnect-safe recovery for consequential operations.
4. **Time has provenance.** In-world year, civil time, source-observation date, evidence validity interval, and real submission time are separate fields. A 2020s observation is not automatically a 1990s fact.
5. **Knowability differs from truth.** The simulation's fact, a character's knowledge, an unreliable witness report, and the player's UI disclosure may differ. Mission evidence must preserve this distinction.
6. **Definitions and instances do not collapse.** A catalogue entry is never a particular player's saved item, vehicle, mission run or skill progress.
7. **Progression without talents.** The ontology has no `Talent` or `TalentTree` entity. Skills and discrete activity/interaction definitions cover the initial design.
8. **Visible UX before breadth.** Definitions should support legible prompts, journal/evidence, inventory, money and progress UI; dozens of raw text/debug indices are not an acceptable final interface.
9. **Single-player semantics first, network-compatible ownership.** Local play should work without assuming a live public MMO, while identity, authority and transaction design should support later dedicated-server sessions.
10. **No implied implementation.** A concept listed here does not imply a working C++ actor, Rust bridge, persistent database, vehicle, NPC population or map import.

## 6. Example semantic slice (design, not implemented)

Consider a player arriving at **Lent**, walking to the **Drava** and meeting a riverside angler:

```text
World:marburgum
 ├─ Place:lent (District / riverfront area)
 ├─ Place:drava_lent_bank (WaterBody-associated location)
 └─ PlayerCharacter:<character-id>
      ├─ SkillState:fishing (references SkillDefinition:fishing)
      ├─ Inventory:<inventory-id>
      └─ MissionRun:<run-id> (instance of MissionDefinition:lent_first_catch)

InteractionDefinition:fish_at_spot
  + character + eligible fishing place + applicable conditions
  → authoritative ActionEvent
  → inventory/skill changes recorded exactly once
  → optional MissionRun objective advancement
  → discoverable Evidence associated with the riverfront story
```

The example adapts concepts and identifiers from the original **“Something in the Water”** opening to demonstrate relationships. It does not import legacy mission dialogue, story assets, server protocol or gameplay implementation. For a first MarburgumII vertical slice, a small authored district can prove **location → interaction → item/XP → mission objective → UI → save/reload**, before any attempt to import the original 121-window city dataset.

## 7. Identifier and data-contract conventions (initial proposal)

- Use lowercase `snake_case` for stable content IDs; keep human-readable names in separately localizable fields. A semantic reference is `<type>:<id>`, for example `place:lent` or `skill_definition:fishing`. This syntax is **proposed**, not a compatibility claim for old saves.
- Each persisted record should carry `schema_version`, `id`, owner/authority, references and appropriate creation/update event IDs. Version the **type vocabulary** independently from record schemas and network protocols.
- Prefer authored, engine-neutral source catalogues. Unreal Data Assets, Gameplay Tags, Data Layers and World Partition are **adapters or projections**, not a second independent set of game rules.
- Keep `SourceRecord` and `HistoricalAssertion` provenance linked to licensing, temporal confidence and any authored fictionalization decision before generating production assets.
- C++ owns eventual Unreal runtime and authority adapters. C#/.NET 8 may validate/generate data offline. Rust may implement pure deterministic rules behind a reviewed, versioned C ABI; it is **not presently linked** to Unreal.
- The [JSON vocabulary](../Data/Ontology/core.v0.1.json) is **descriptive design data**, not yet a JSON Schema validator, compiled game catalogue, runtime wire format or persistent database contract. Any promotion to those roles requires tests and a separate decision.

## 8. Source traceability and scope

This ontology was synthesized from the **original private project**'s design/data vocabulary and the **current restart**'s accepted constraints; it does not copy the previous game's code, datasets or authored narrative. Owners with access may review the original sources:

- [Original general framework](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/GENERAL_FRAMEWORK.md) — setting, urban systems and engine-neutral content principles.
- [Original registered skill definitions](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/project/data/skills.json) and [skilling roadmap](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/SKILLING_ROADMAP.md) — actual five definitions versus fifteen long-range design names.
- [Original mission catalogue](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/project/data/missions.json), [mission roadmap](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/MISSIONS_ROADMAP.md) — missions and objective structure.
- [Original fictionalization roadmap](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/MARBURGUM_FICTIONALIZATION_ROADMAP.md) and [1990s world-state roadmap](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/NINETIES_AUTHENTICITY_WORLD_STATE_ROADMAP.md) — geography, chronology, provenance and authored identities.
- [Original October gameplay workshop](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/OCTOBER_2026_GAMEPLAY_DESIGN_BRAINSTORM.md) — solo-first experience, opportunity fairness, relationships, taxi design and virtual population as candidate directions.
- [MarburgumII vision](VISION.md), [architecture](ARCHITECTURE.md), [decisions](DECISIONS.md) and [roadmap](ROADMAP.md) — the controlling commitments for this restart.

### Open questions before freezing a v1.0 gameplay ontology

The 0–1990 level equation; currency/price and scarcity rules; precise character/account/session identity; per-NPC knowledge and relationship data; year-state transition model; taxi journey semantics; crime/policing; mission party ownership; save/replication schema; and how the reviewed geospatial source catalogue is imported remain subject to implementation-oriented design and acceptance testing.
