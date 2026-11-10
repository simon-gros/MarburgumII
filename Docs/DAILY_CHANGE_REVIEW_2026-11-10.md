# Daily development change analysis — 10 November 2026

**Window:** 10 November 2026 00:06 CET to 11 November 2026 00:06 CET (2026-11-09 23:06 UTC → 2026-11-10 23:06 UTC). **47 commits** on `main`, beginning `9b626d36` (12:21:25 UTC) and ending [`af2d0b3a`](https://github.com/simon-gros/MarburgumII/commit/af2d0b3a12d2d3143ea658781847e43cf5ce6991) (22:46:44 UTC). This analysis audits commits, specifications and recorded tests; it does **not** claim that this documentation session independently ran Unreal or a new game build.

## Executive assessment

The game advanced in a single day from the authoritative networking/UI foundation to **several genuinely implemented MMO gameplay slices**: an item inventory transaction, original mission/NPC, movement and stamina, a selected original Unreal Lent world, third-person avatar, swimming/drowning and driveable city hatchback with a passenger. Linux dedicated-server/multiple-client tests now cover key operations and failures. These engineering achievements still need a single current-asset regression package, production first-launch identity, completed graphical UI and **personal owner playtests** before public Stage 1 acceptance.

## What changed

| Domain | Commit evidence | Actual result and outstanding boundary |
| --- | --- | --- |
| Authoritative inventory | `9b626d36`: Gate 1 server-only range/stock/character validation, atomic persistence, receipt replay, last-unit race | [Gate 1 evidence](STAGE1_GATE1_EVIDENCE.md): nine client sessions and restart; limited test crate, development identity, owner acceptance open |
| First urban mission | `df2e04f7`: original angler, server area, `lent_first_catch` mission and rod grant, objective + Journal | [Lent smoke](STAGE1_LENT_EVIDENCE.md): multi-client fault/replay/privacy/restart **on older `/Game/Maps/Lent`**; fishing to finish mission not built |
| Toolchains | `c9d89618`: Rust `tools/mb2`, C++ map commandlet, .NET 10 replacing Python tooling | [Development guide](DEVELOPMENT.md); Windows build not yet verified |
| Anti-cheat and condition | `1b85a398`: movement-time bounds; `4b288110`: server-simulated stamina, winded sprint and saved state | [Movement](STAGE1_MOVEMENT_TIME_EVIDENCE.md), [condition](STAGE1_CONDITION_EVIDENCE.md); automated evidence is not personal owner approval |
| World / map source | `6041dca2` and `079da932` authored fallback; LiDAR replacement `f63f1152` **reverted** `4d5493d0`; `d34dae47`/ `8182ca74` select original Unreal `/Game/World/lent`; `0c06ce82` / `686a3a72` rebuild façades and watertight footprint-fitted roofs | Current **default** is original Unreal Lent. Owner confirmed that map after an early façade pass; latest roof/façade results still require separate review. Commercial TurboSquid 1639711 **not purchased, imported or active** |
| Third-person protagonist | `9c48a293` and `34af3983` prototype+third person; `c24a5a1` mannequin hands; `f7e1328` own retopo and bake; `d13c977d`, `6633500`, `c8b5fa9` back, UV and matte fixes | [Protagonist details](CHARACTER_PROTAGONIST.md). One currently working avatar is **not** the planned user-selectable model creator |
| Swimming | `69829560` Drava physics volumes, swimming, breath/drowning and persisted recovery; `ecd8f828` prone flutter kick; `6306899c` idle upright tread | [Swimming evidence](STAGE1_SWIM_EVIDENCE.md): two-phase server/client smoke. Audio/underwater UX and owner review pending |
| Driving | `ff720850` port of five original vehicle definitions, procedural bodywork/door clips, four-raycast server dynamics, seats, door prompt and vehicle status/camera; `af2d0b3` evidence | [Driving evidence](STAGE1_VEHICLE_EVIDENCE.md): driver/passenger test. No saving of vehicle positions, ownership, keys, fuel, damage, traffic or cleared engine sounds |
| Optional render path | `f5382a70`: runtime-detected DLSS/DLAA/FG/Reflex with TSR fallback; `b2e199b`: ignored local NVIDIA UE plugin and unofficial DLSS5ForUE5 source | [Rendering limits](RENDERING_NVIDIA.md): Windows/D3D12 not qualified; proprietary unsigned community-modified runtime not obtained and must not be bundled |
| First-launch design / documentation | `70842822`: verified email → credentials → login → First name, Last name, allowlisted model → server-owned character admission; `734ca3e5`: README refresh | [First launch ontology](FIRST_LAUNCH_ONTOLOGY.md) is currently **design only**; no production auth database, first-character UI or admission yet |

## What the prior roadmaps misrepresented

**The UI roadmap was stale**: live interaction, location, objective, condition/swim and car modules are implemented, as are first Character/Inventory/Journal panels, while Skills/Settings, login/creator and full device/scale/focus quality remain open. **The legacy parity register was stale**: vehicle mechanics are now Stage 1 runtime work rather than solely Stage 3 plans; original angler/mission and default-world geometry also have implemented slices. **The map-source plan was ambiguous**: original Unreal Lent is the real current default, the generated LiDAR shell swap was reverted, and the purchased 2022 city model is only a future candidate. **The vision document was stale** in claiming that native builds had not been validated.

**Test evidence is not one current-build release:** the mission and condition smokes use the test map and earlier source states; later protagonist, swim, roof and vehicle changes have their own evidence. A complete same-SHA source and cooked-asset inventory with all regressions and a personal owner sign-off is still required. The user-selected original map remains the default pending any separately approved future switch.

## Revised critical path

1. **P0 — Truthful build and acceptance.** Rebuild the current UE 5.8.3 dedicated server/client and latest world, avatar, swim and car assets; seal *all* referenced cooked and ignored/local inputs ([freshness protocol](PLAYTEST_FRESHNESS.md)); run network, chat, interaction, mission, condition, swimming and vehicle tests together on `/Game/World/lent` while retaining explicit older fallback cases on `/Game/Maps/Lent`. Record exact SHA/map/asset digests, logs and screenshots.
2. **P0 — Actual public-entry chain.** Implement FL-A–FL-E (verified email, secure username/password/recovery, login, First/Last/model creator, saved owner binding, admission denial and returning-user restore). The `-MB2Character` development key may not bypass public auth.
3. **P0 — High-quality client UX.** Finish UI-03/04 and the Skills/Settings views, controller/keyboard focus across driving/chat/panels, responsive 720p–4K+ultrawide and multiple UI scales, Slovene localization, error/loading states and accessibility. Record owner approval of each major feature.
4. **P1 — Make the mission complete.** Implement authoritative Drava fishing/catch/rod/mission turn-in, idempotent inventory and progress with failure/concurrency/reconnect/restart tests and appropriate feedback.
5. **P1/P2 — Controlled expansion.** Rework remaining avatar/roof/river materials and rights-cleared sound; introduce car persistence/ownership/fuel/damage/traffic, skills/economy, larger population and world streaming as separate approved increments. Candidate commercial map, legally conditional Hunyuan3D and experimental neural rendering are **nonblocking**, not reasons to stall the playable game.

## Completion semantics and evidence protocol

Keep four independent state markers: `implemented` (runtime code), `automated_tested` (specified positive/negative test on known source/map), `fresh_build_verified` (same-SHA package with all cooked asset sources covered), and `owner_approved` (the owner's own gameplay test with recorded positive decision). ADR decisions, technical compilation, screenshot galleries and earlier smoke tests do not replace that final approval. The [freshness guide](PLAYTEST_FRESHNESS.md) still describes its example-profile qualification as outstanding; treat that distinct workflow honestly.

**Reconciled documents:** [stage roadmap](ROADMAP.md), [UI roadmap](UI_HUD_ROADMAP.md), [legacy parity](LEGACY_PARITY.md), [world-map adoption](WORLD_MAP_ADOPTION.md), [vision](VISION.md) and the companion source-selection/parity catalogues.
