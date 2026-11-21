# Legacy RPG systems and character progression import roadmap — Marburgum II

**Created:** 21 November 2026  
**Status:** Proposed implementation roadmap; no game code, migration, interface, or owner acceptance is claimed by this document.  
**Priority:** Character foundation and UI/HUD integration after the current integrated-package and owner-playtest P0 gates.  
**Delivery status (21 November 2026):** LRP-01 and the server, store and UI parts of LRP-02 are **implemented on branch `feat/lrp-attributes-equipment`** ([ADR-085](DECISIONS.md), evidence in [LRP_01_02_EVIDENCE.md](LRP_01_02_EVIDENCE.md)): the four attributes with Wind read only as a legacy alias, store schema 7, the eight slots with server-decided equip/unequip, owner-only replication, the recomposed Character sheet and Inventory, and Slovene for every new string. **Not accepted:** the owner has not played it; the Slovene attribute and slot names await native-speaker review; equipped appearance, bag capacity, clothing assets and every gameplay effect of attributes stay out of scope. SPECIAL-00 (talents/specializations) is untouched.
**Authority:** [Vision](VISION.md), [Milestones](ROADMAP.md), [Legacy parity](LEGACY_PARITY.md), [Decisions](DECISIONS.md), [UI/HUD ontology](UI_HUD_ONTOLOGY.md), [UI/HUD roadmap](UI_HUD_ROADMAP.md), [UI/HUD design](UI_HUD_DESIGN.md), [Network architecture](NETWORK_ARCHITECTURE.md), and [Playtest freshness](PLAYTEST_FRESHNESS.md).

## 1. Intent and non-negotiable import policy

Recover the original *Magna Raedarum: Marburgum* game's strongest RPG identity: meaningful character attributes; a fully usable character sheet; equipment and personal possessions; the 0–1990 skill progression model; deliberate, owner-authored specializations; and the interconnected Research, Athletics, Reputation, Housing, Mechanics, Electronics, Trading, Logistics, Photography, Medicine, Cooking, Music, Chemistry and related systems. Integrate these with the existing fishing, story, inventory, combat, driving, photography, interiors and UMG interface of Marburgum II.

**Terminology decision for this roadmap: the original attribute `Wind` is replaced by `Endurance` everywhere in new Marburgum II player-facing design, data contracts and implementation.** The canonical four attributes are **Might, Endurance, Eye and Poise**, with IDs `might`, `endurance`, `eye`, `poise`. `wind` is only a *legacy input alias* for migration, never a fifth attribute, a new displayed label, or a second saved score.

**Perfectly integrated import requires deliberate UI/HUD readjustment.** A feature is not complete when its server data compiles: the corresponding Character, Skills, Inventory, Journal, HUD, interaction, settings and accessibility surfaces must be examined, re-laid out where necessary, adapted to real server-owned values, localized, visually inspected and tested with actual input before the import can be marked accepted. UI engineering is in the same feature delivery slice as the underlying gameplay, not a deferred polishing phase. Preserve the native Unreal C++/UMG v2 design language while redesigning panel composition where needed.

This is a **design, content and contract port**, not a source-code copy from Godot GDExtension. Use authored data with source provenance, new UE 5.8.3 C++ server-authoritative logic, shared definitions and owner-only replicated presentation. No proprietary GTA/San Andreas assets, code, mechanics or audio; no unlicensed historical imagery. New image-to-3D candidate assets use Microsoft TRELLIS under its own rights and quality gates.

**Existing exclusions remain binding until separately revised:** the 8 November restart explicitly excluded the old automatically generated Talent framework and generated Talent UI. This roadmap **proposes** a carefully authored specialization system (Research → Investigation, Photography → Nature Observation, and Athletics physical disciplines); it does **not** immediately authorize the old 15 AI-authored talents or a generated talent tree. Before implementation, obtain the owner's explicit design approval, record an ADR changing only the necessary talent/specialization exclusion, and reconcile `VISION.md`, `LEGACY_PARITY.md`, `Data/Parity/source-matrix.v0.1.json`, UI ontology/design and Rust validators that currently disallow `talent` concepts. Do not circumvent validators by renaming forbidden mechanics. Wanted stars, wanted/heat levels, police pursuit/chases, roadblocks and pursuit HUDs remain excluded without exception.

## 2. Evidence baseline — what is real now

| System | Original Marburgum source | Marburgum II condition as documented 20 November 2026 | Import decision |
| --- | --- | --- | --- |
| Character attributes | `src/shared/character_stats.{h,cpp}`; `src/client/character_panel.cpp` | Native UMG Character panel shows identity, location, condition, saved records and skills; four legacy ability scores are not integrated | **P0: restore with Endurance rename** |
| Equipment and sheet | `src/client/character_panel.cpp`; `src/server/character_store.cpp` | Server-owned inventory exists; original eight equipment slots and complete equip/unequip behavior not established in current runtime | **P0: define persisted slots + complete UI** |
| Skill progression | `src/shared/skill_curve.*`, `project/data/skills.json`, `docs/SKILLING_ROADMAP.md` | UE progression curve and Skills UI exist; Fishing, Trading, Reputation and Photography have some awards; Housing registered without activity; broader roster not active | **P0/P1: expand meaningfully, preserve existing XP** |
| Talents/specializations | `project/data/talents.json`, `src/shared/talent_catalog.*`, later `docs/SKILLING_ROADMAP.md` | Generated talent UI and auto-authored trees intentionally excluded | **Decision-gated: owner-authored only** |
| Research and evidence | `docs/SKILLING_ROADMAP.md`; narrative, mission and item catalogs | Journal, evidence classes, photographs and story decisions present; standalone Research skill not established | **P0: first new profession-grade loop** |
| Athletics | `docs/SKILLING_ROADMAP.md`, combat and condition rules | Sprint, swim, stamina and unarmed combat already exist; Athletics as a developed skill not established | **P0: connect genuine actions to progression** |
| Reputation | `docs/REPUTATION_SKILL_ROADMAP.md` | XP and mission decisions persist; witnesses, NPC/social memory, neighbourhood relationships and broad consequences incomplete | **P0/P1: deepen social simulation** |
| Housing | `docs/HOUSING_SKILL_ROADMAP.md` | Five cut/enterable interiors; no full housing, ownership, furnishing or storage loop | **P1: one complete apartment first** |
| Mechanics/vehicles | `docs/SKILLING_ROADMAP.md`, `docs/VEHICLE_DAMAGE.md`, `project/data/body_shops.json` | Server driving exists; durable ownership/damage/repair not complete | **P1: automotive maintenance and repair** |
| Electronics/city technology | `docs/ELECTRONICS_SKILL_ROADMAP.md` | Narrative clues and 1990s equipment contexts exist; electronics skill loop not present | **P1: one repair/inspection activity** |
| Trading/economy | `docs/TRADING_SKILL_ROADMAP.md` | Kiosk 67 has a bounded atomic counter; city-wide market and personal ledger absent | **P1: small two-vendor loop** |
| Photography | `docs/PHOTOGRAPHY_SKILL_ROADMAP.md` | Camera, server-judged subjects, rewards and related mission content exist | **P1: album, Nature Observation proposal** |
| Other ten-skill design | Original specialist roadmaps | Most specialist skills not registered as active gameplay | **P2 or later, by measured value** |

Do not copy outdated statements from the legacy parity matrix over later implementation evidence. Verify the exact current branch/commit, runtime catalogue, schema and packaged build at kickoff; statuses above are planning references, not a fresh certification.

## 3. Four primary attributes — canonical contract

| Identifier | Player-facing English | Intended influence (subject to balancing) |
| --- | --- | --- |
| `might` | **Might** | Strength, carrying load, strenuous work and bounded unarmed-force effects |
| `endurance` | **Endurance** | Sustained effort, fatigue resistance, exertion recovery and bounded running/swimming efficiency |
| `eye` | **Eye** | Perception, fine observation, spotting details and precision-dependent work |
| `poise` | **Poise** | Composure, steadiness, controlled action and selected high-pressure social or physical checks |

**Original baseline for design review:** four scores begin at 10; original clamped each to 1–20, with modifier `floor((score - 10) / 2)`. Keep the original score curve as the initial comparison fixture, not an unexplained promise of final gameplay balance. Do not grant uncontrolled character-creation points or retroactively rebalance a live character without an approved progression policy.

- Distinguish **Endurance (persistent attribute)** from **Stamina (current spendable condition pool)** and **Athletics (learned skill)**. The three must have clear, nonduplicating responsibilities. Do not name a permanent attribute `Wind` or silently exchange stamina for Endurance in the existing HUD.
- Derived values belong to a single authoritative formula/contract. UI must label base attribute, temporary modifier, equipment effect and derived total separately; no optimistic numbers masquerading as server-confirmed state.
- Migrate any original record using `wind` to `endurance` once, with idempotent versioned migration; maintain original value within the chosen clamp. Define deterministic behavior if both keys are present (prefer an explicitly versioned canonical `endurance` when valid; audit conflict). Never lose the other three scores or skill XP.
- New Marburgum II records lacking attributes receive the documented defaults; unknown/corrupt records are reported and repaired safely by the server. Renaming is a semantic migration, not a second attribute or an XP reset.
- Localized Slovene attribute display terms require native-speaker review; do not silently present untranslated or falsely authoritative labels.

**Acceptance ATTR-01–05:** default/all-boundary modifier tests; no duplicate Wind/Endurance identity; migrated and new records survive restart/reconnect; two clients see permitted own/public information only; when stamina drains or restores, the attribute remains stable; no unapproved combat or fishing balance regression.

## 4. Character sheet, equipment and inventory — gameplay plus UI

Restore eight original slot identities: **Head, Coat, Trousers, Shoes, Hands, Right hand, Left hand, Bag**. Preserve these semantic identities while designing period-authentic equipment presentation and new Unreal attachment rules. Character sheet should expose character name/model, true status, readable vitality/stamina, four attributes, derived effects, equipment, inventory summary, current skill highlights, reputation summary and contextual location.

**Required server work:** authored item definitions and item instances; which slots an item can occupy; equip/unequip validations; unique custody and atomic move semantics; bag/capacity rules and overflow behavior; persistence and reconnect; visible carried/equipped distinction; denied action receipts; privacy/replication scope. No client-side equip authority, negative quantity or duplicated sale/trade/equipment item. Use existing owner-replicated PlayerState contracts rather than inventing client-side mirrors.

**Required UI work:** turn the current text-oriented Character overview into a professionally composed equipment-and-stats sheet. Keep the current UMG v2 typography, contrast, input patterns and shell; readjust layout into identity/3D character/slot groups, four-score panel, condition, derived values and navigable details. Each equipment slot must have an explicit disabled/empty/pending/error/equipped state and honest tooltip; show effect deltas only after the server confirms a change. A user must be able to identify the selected item and its slot without relying on color.

**Acceptance CHAR-01–08:** new and migrated characters; all eight slots with supported/unsupported items; equip → replicated updated body/slot/derived value; unequip and inventory-space refusal; overlapping requests and reconnect without double ownership; preview consistency across perspectives; keyboard/mouse/gamepad navigation; real 720p, 1080p, 1440p and 4K captures at 80%, 100%, 150% and 200% UI scale with localization and reduced-motion/high-contrast modes. Test actual UMG rendered interface, not a mock-up.

## 5. Skill progression and deliberately authored specializations

The original long-term roster comprises **Fishing, Trading, Photography, Housing, Reputation, Cooking, Electronics, Music, Chemistry, Logistics, Athletics, Research, Mechanics, Medicine, Occult**. Keep the original shared **0–1990** identity as a comparison baseline with integer XP semantics. For all new skills, define a legitimate repeatable world action, when XP is awarded, anti-farm/idempotent controls, milestone presentation and persistence *before* registering the skill as active. Do not grant XP just for opening a panel or repeatedly examining unchanged evidence.

**Initial new loops:**
1. **Research** — server-confirmed examination of unique evidence, comparing records, interviews and contradictions; links into existing Journal/evidence and mission branches. **Investigation** is the owner-approved conceptual specialization. Higher competency unlocks *tools or alternate approaches*, never automatic story answers.
2. **Athletics** — purposeful running/swimming/unarmed training and milestones; physical action controls remain those already validated by the server. Progression must avoid trivial AFK or infinite sprint exploits. Swimming/diving, endurance/running and unarmed discipline are proposed authored specialization areas, not three independent skill caps.
3. **Reputation** — retain reversible current level, record peak/lifetime gains/losses separately; NPC witness knowledge and institution/neighborhood attitudes are contextual and do not collapse to a simplistic good/evil scale.
4. **Housing** — property/tenancy, furnishings, storage, sleep/rest and specialized work areas. Housing levels affect capability, not arbitrary eligibility to rent or own; eviction must not erase stored goods.

**Talent design gate (SPECIAL-00):** the original `talents.json` includes 15 concrete but *unimplemented-effect* nodes across five older skills (e.g., Fishing Patience / A Sure Strike / Reading the Water). These are documentary reference **only** and are superseded by the original owner's later requirement for author-led design. After an explicit new ADR and owner decision, design a small first set of authored **specializations/proficiencies** with a fully specified action, prerequisite, rank cap, effect, UI explanation, skill point budget if any, reversible-Reputation handling, respec policy, persistence and multiplayer tests. Avoid an automatically filled giant tree. Until then, **do not expose a Talents tab, allocate talent points or change validators**.

**Acceptance SKILL-01–09:** XP/level parity fixtures; deterministic bounds and overflow; save/schema upgrades and legacy XP preservation; shared visibility where authorized; no illicit XP from repeated interactions or client fakes; verified tooltips/presentation of milestone and locked reason; stable English and Slovene skill terminology; specialization policy approved before any state-writing implementation.

## 6. Additional import work by value and dependency

| Priority | Import candidate | First vertical slice | Dependent UI/HUD readjustment |
| --- | --- | --- | --- |
| **P0** | Research/Investigation | One inspectable clue with a nontrivial discovery and Journal entry | Journal evidence grouping, inspection prompt, skill XP receipt, prerequisite messaging |
| **P0** | Athletics | Track effort during a real swim/run and one fair award | Character derived endurance/condition, Skills progress; contextual breath warning without extra permanent bars |
| **P0/P1** | Expanded Reputation | Witness-dependent result for one authored mission choice | Character social summary, event history, Journal consequence, legible reason for change |
| **P1** | Housing/property | One furnished, enterable rent/owned apartment with safe storage | Property surface, inventory transfer, door access and lease status; no world-HUD clutter |
| **P1** | Mechanics | Inspect and repair one vehicle in one validated workshop | Vehicle condition/service card, item/cost preview, server-confirmed repair receipt |
| **P1** | Electronics | Inspect or fix one historically credible 1990s device | Inspect/diagnose workflow, evidence attachment and contextual interaction cues |
| **P1** | Trading | Two vendors with local prices and personal price observations | Bounded vendor/buy-sell panel, personal ledger, confirmation/rejection |
| **P1** | Logistics | One parcel delivery with cargo, route and recipient | Job card, carried cargo, completion receipt and optional objective marker |
| **P1** | Photography | Personal album, date/place/subject provenance and an additional Nature Observation activity proposal | Gallery, Journal media link, capture feedback and privacy controls |
| **P2** | Medicine | Basic first aid or assessment loop | Condition details, treatment tooltips and refusal feedback |
| **P2** | Cooking | One usable recipe and kitchen station | Ingredient/recipe/prompt and result panels |
| **P2** | Music | One instrument/practice or local performance event | Contextual practice feedback, repertoire and audio settings |
| **P2** | Chemistry | One abstract safe examination/development bench | Process states, materials, evidence and completion feedback |
| **Defer** | Occult | Owner-led definition of scope first | No premature controls, fictional powers or generic fantasy meters |

The original body-shop catalogue gives **candidate locations**, not verified contemporary game-world coordinates; map/source/rights/period validation must precede placing repair points. Original gameplay rules borrowed from proprietary sources must be independently redesigned, not copied. The original user-authored mechanics and story concepts remain reference material, not evidence of a current runtime system.

## 7. Mandatory UI/HUD readjustment and integration matrix

**Every imported gameplay system requires a UI/HUD impact review and an implemented readjustment plan before its first acceptance build.** The review is a hard feature gate with a named owner, captures and documented decisions. New stats or capabilities may affect multiple existing screens; changing a data label alone is insufficient.

| Existing surface | Required readjustment |
| --- | --- |
| **Character (C)** | Reorganize identity, avatar, slots, four attributes including **Endurance**, derived totals, status and compact Reputation without stacking unbounded text; preserve avatar view/focus and performance |
| **Skills (K)** | Extend registered skills incrementally; distinguish active/trainable/planned, show XP and credible next actions, prerequisites and honest unlocks; correct the Photography help text so it does not claim there is no XP source |
| **Inventory (I)** | Consistent bag, worn equipment, evidence, tools, repair parts and housing storage; stack rules, selection, equipped markers and atomic transfer receipts |
| **Journal (J)** | Typed evidence, researched facts, photographs, contextual contacts, discovery source, mission state and evolving investigative capabilities, with clear authored provenance |
| **Exploration HUD** | Refine condition/stamina feedback to account for Endurance-derived capacity only when relevant; contextual prompts for research, housing and work; no permanent attribute wall or fake MMO ability bar |
| **Activity modes** | Inspect/research, fishing, photography, trading, repair, housing, athletics and driving each show activity-specific input, progress/eligibility and refusal/success; avoid competing key mappings |
| **Notifications/toasts** | Attribute/skill/equipment changes, reputation consequences, money/items and mission rewards deduplicated from authoritative receipts; error states distinguish pending versus denied versus committed |
| **Interaction/dialogue** | Visible prerequisites, legitimate choice effects, equipment needs, skill checks and server refusals; protect choice navigation from movement and chat actions |
| **Settings/accessibility** | Scalable text/UI, safe-zone constraints, mouse/keyboard/controller focus, readable contrast, reduced motion, captions, English/Slovene and input routing; no focus theft from chat, menus, E/F/R/G or vehicles |
| **Networking/identity** | Source and freshness labels for unknown, loading, stale, denied and server-owned values; no leakage of private inventory, skill, photos, investigation evidence, chat or personal relationships |

**Implementation standards:** use the current `MarburgumUI::FBuilder`, UMG/Slate, shared design tokens, approved icons, typography, localization and event/lifecycle contracts unless a measured reason and separate ADR justify changing them. Update `Docs/UI_HUD_ROADMAP.md`, `Docs/UI_HUD_DESIGN.md` and `Docs/UI_HUD_ONTOLOGY.md` when UI behavior actually changes, rather than describing future work as delivered. Author any new module semantics in `Data/Ontology/ui_hud.v0.1.json` only at the appropriate design/implementation stage.

**Readjustment quality criteria (UI-IMPORT-01–12):**
- Four attribute labels match the canonical contract; **Wind must never appear as a current attribute** in UI, exported saves, tooltips or documentation of the new feature.
- Key information is legible at 720p–4K and scales 80–200%; inspect focus movement, text wrap/truncation, controller navigation, mouse hover and touch-safe spacing where supported.
- Verify both languages, diacritics, localized names and number formats; no clipped strings or untranslated UI with false shipping claim.
- Character sheet visually balances the 3D figure, eight slots, attribute/stat details and inventory without overlap, jitter, flicker or excessive scrolling.
- Maintain existing minimum contrast and high-contrast/reduced-motion modes; meaning may not depend on color alone.
- No ghost prompts, false XP toast, duplicated icon/currency display, inventory duplication or outdated attribute tooltip after server updates.
- In combat/water/vehicle/door/chat/modal flows, input focus never triggers gameplay or erroneous clicks.
- Rewards and losses display only after server receipts and appropriate owner replication; negative/timeout/reconnect states look intentional.
- HUD occupies minimal view space in ordinary city exploration; open information panels on demand, using progressive disclosure.
- No wanted stars, pursuits, automatic talent tree, borrowed third-party UI assets or debug indices exposed in public captures.
- Capture and inspect **before and after** images/videos from actual fresh packaged clients plus representative mission/fishing/commerce/repair flows.
- Owner personally evaluates presentation and interactions in a fresh build and records accept/reject; passing code tests alone does not make the UI import perfect.

## 8. Architecture, migration, test and performance contracts

1. **Data source:** keep portable, versioned catalogues and stable IDs with original path/provenance; use generated shared catalogue interfaces where already established. Do not create parallel hard-coded XP/attribute logic in client and server.
2. **Authority:** Unreal ServerOnly owns awards, stats, inventory, equipment, reputation, item custody, access and outcomes; Shared contains pure deterministic contracts; ClientOnly only predicts cosmetic display where safe and renders authorized results.
3. **Migration:** new schema fields optional at read and valid after an explicit migration. Include tests for missing fields, old `wind`, mixed `wind` + `endurance`, invalid values, historical earned XP and rollback/backups. Record schema version and one-time upgrade evidence.
4. **Economy and adversarial tests:** concurrent equipment, apartment storage and buy/sell require atomic durable receipts and retries, replay rejection, negative/overflow values, disconnection, late join, unauthorized use and two-client contention.
5. **Network/privacy:** replicate owner-only stats, items and private discoveries; publish only intentionally shared equipment appearance and approved public reputation. Measure bandwidth on multi-client tests; never leak private chat or photographs.
6. **Performance:** measure UMG widget rebuild cost, Slate/game-thread time, draw calls/overdraw, memory and UI stability; do not scale live HUD polling, dense widget trees or unnecessary 3D preview renders with the full fifteen-skill catalogue.
7. **Freshness and playtest:** compile and cook the same exact source and asset inputs, record commit/hash/schema, run packaged dedicated-server and 2–3-client smokes, negative and restart cases, capture real gameplay, then obtain explicit owner personal sign-off for each major feature. Test old functionality as well as new.
8. **Documentation:** reconcile Vision, Decisions, Ontology, Parity, central Roadmap, UI/HUD documents, README and Rust validation rules *when the relevant design is approved or code is implemented*. Never mark proposed work as merged or playable.

## 9. Sequenced deliveries with hard exit gates

### LRP-00 — Prerequisites and design reconciliation (P0)
- Close or explicitly preserve the current latest-head integrated package blockers; do not displace the full-client/server acceptance work.
- Verify latest repository state and build asset freshness; inventory all current character/skill/UI data contracts.
- Approve canonical `Endurance` replacement and define stat progression/balance; design owner review for specialization policy.
- Record a decision/ADR before any approved expansion of the excluded talent UI.
- **Gate:** written contract, migration/test fixtures and UI impact map; no unsupported shipping claims.

### LRP-01 — Four attributes and migration (P0)
- Server-owned Might, **Endurance**, Eye, Poise; default/validation/derived values; old Wind alias import with no duplication.
- Expose typed owner-only attributes to PlayerState/UI; avoid unapproved balance effects.
- **Gate:** ATTR tests, fresh package, restart and owner review.

### LRP-02 — Character sheet and equipment integration (P0)
- Implement eight slot types, real equip/unequip and persistence/receipts.
- Recompose current Character UMG in tandem with Inventory; preview equipped appearance when real assets exist.
- **Gate:** CHAR and UI-IMPORT matrix including controller/scale/languages.

### LRP-03 — Research and Athletics playable slice (P0/P1)
- Implement honest XP, progression, actions, persistence and Journal/condition links.
- Fix existing Photography skill-source guidance in the same Skills UI pass.
- **Gate:** mission regression, anti-farm, swimming/combat and 2-client tests plus on-screen owner test.

### LRP-04 — Specializations (decision-gated)
- Owner authors and approves first nodes, effects and point/unlock/refund policies; revise explicit exclusions/validator with traceable ADR.
- Implement only selected specializations and their optional UI; the original 15 generated entries remain non-authoritative.
- **Gate:** approved design, deterministic ranks, anti-abuse, saved state, in-game gameplay effect and full UI proof.

### LRP-05 — Reputation, Housing and Economy (P1)
- One witness-dependent social consequence; one furnished apartment with storage; one two-vendor ledger scenario.
- Coordinate Character, property UI, Journal, Inventory and shop/interaction surfaces.
- **Gate:** witness/privacy and goods custody proven with restart and multiple clients.

### LRP-06 — Vehicles, repairs and specialist activities (P1/P2)
- One mechanic workshop and repair transaction, one Electronics interaction, one Logistics job and a Photography album.
- Later add Medicine, Cooking, Music, Chemistry and further skill specializations; Occult remains undesigned pending owner direction.
- **Gate:** each activity has a genuine playable loop, localized readable presentation and source/rights sign-off.

## 10. Initial implementation task suitable for Claude Code

**Title:** LRP-01/02 — Restore canonical character attributes, equipment and completely readjust the Character/Inventory UI.

Read this roadmap and live `Docs/ROADMAP.md`, `Docs/UI_HUD_ROADMAP.md`, `Docs/DECISIONS.md`, `Docs/LEGACY_PARITY.md`; inspect both repositories' actual C++/data sources; verify latest current branch/commit before edits. Use the new canonical attributes **Might, Endurance, Eye, Poise**: migrate legacy `wind` to `endurance`, never present Wind as the current attribute. Implement versioned server-side character state and validated original eight equipment slots, owner-only replication, persistence and idempotent equip/unequip. Build on the existing native UMG v2 system, but genuinely redesign the Character sheet and corresponding Inventory layout, focus, tooltips, localization and contextual HUD feedback rather than dropping new text rows into the old widgets. Preserve condition/Stamina distinction and avoid unsupported derived effects. Build a playable latest-asset packaged server + two-client acceptance with migration/negative/restart tests, screenshots at key resolutions/scales, both languages and a clear owner playtest checklist. Do not implement generated talent trees or override their explicit exclusion; any specialization implementation requires separately approved ADR. Record accurate test results and changes in the relevant docs. Do not merge failing/unaccepted gameplay into a release.

---

**Completion definition:** the import is only **implemented and acceptance-ready** when authoritative game systems, character saves, relevant UI/HUD adjustments, accessibility/localization, cross-panel coherence, graphics/asset provenance and packaged multiplayer evidence are all present; only the owner's explicit in-game approval makes it accepted. A visually attractive mockup or an engine compilation alone does not close this roadmap.
