# Development state review — 12 November 2026

**Reviewed:** [`main` at `c5d41efc`](https://github.com/simon-gros/MarburgumII/commit/c5d41efc77c80bb227d5e91b2585902ae8600c15), latest preceding change 12 November at 10:26 UTC. Engine: **Unreal Engine 5.8.3**. This is a repository-history, code-document and recorded-evidence review. **No independent Unreal build, test, package, personal playtest or owner sign-off was performed during this review.** Earlier test claims are scoped to their documented mode and revision.

## Assessment

**Pre-alpha:** real authoritative MMORPG infrastructure and several playable systems exist, but one integrated, asset-fresh, current packaged and owner-approved release candidate does not. Engineering breadth has outpaced end-to-end acceptance. An approved ADR means a technical design decision, not owner approval of the exact current build.

| System | Implemented / strongest recorded evidence | Remaining gate |
| --- | --- | --- |
| Engine/network/chat | UE 5.8.3 Linux dedicated server, Client and Editor; earlier packaged 1–2-client authority/reconnect tests and three-client private-chat isolation ([Stage 0](STAGE0_EVIDENCE.md), [chat](STAGE0C_CHAT_EVIDENCE.md)) | Current-source package, Windows parity, shard-scale load |
| Account onboarding | Self-hosted Rust HTTPS service, email verification codes, Argon2id, reset, sessions, one character/account, single-use 1-minute admission tickets, three-body 3D creator; **19 recorded service tests** plus editor rendered first/returning/denial smokes (ADR-055/057) | Real email sender and production deployment, packaged 720p–4K/controller/Slovene tests, owner approval; review spent-ticket URL logs |
| World | Nine original 512 m windows in **one 1,536 m square** `/Game/World/lent`, **870** closed building shells, **521** paved chunks, **422** swim volumes; two visual-quality passes and **119 non-colliding distant ring meshes** (ADR-045/053/054) | No World Partition, latest packaged frame-time/VRAM/cold-load or owner acceptance; 1990s accuracy, 14 hollows, ring transition |
| Fishing, skills, missions | Drava cast/bite/strike/catch, original **0–1990 skill progression**, persisted money/reputation/clues and **all eight original mission flows** tested through editor smoke and restart; Unreal automation **11/11** (ADR-047–052) | Packaged two-client mission/restart/negative tests; Melje/Tabor sites use temporary replicated floors, not authored districts |
| Driving and streets | Five-car server vehicle and seats, ambient walkers/fight and car reactions; ADR-056 editor reduced-cull probes fix the owner's reported own-car freeze after ~150 m | Packaged >150 m drive/passenger regression, multi-client population/crowd/relevancy/density tests; cars have no ownership/traffic/fuel/audio |
| Combat/survival | Server jab/heavy/guard, down/recovery, swimming and drowning; earlier packaged slices, later editor IK/world checks (ADR-039–046) | Fresh packaged integrated multi-client and personal owner playtest; broader combat scope/consequences unapproved |
| Avatar and UI | Protagonist retopo/rig, three approved model IDs, full-height drag/zoom preview; ADR-058/059 improve face/mips/head geometry; UMG session, chat, Character/Inventory/Journal/**Skills** and choice panels | Latest cooked avatar/face in-world, Settings screen, focus/scale/controller/localization/accessibility and owner visual acceptance |
| Assets and operations | TurboSquid 1639711 remains an **unpurchased** alternative; Hunyuan3D-2.1 is licensing-gated; current persistent game state is development-grade | Commercial intake/license review, production storage/backups/email/monitoring, proven AOI and 250-player scale |

## Since the 11 November audit

- **ADR-047–052:** Drava fishing, original skill XP and all eight story mission flows with durable outcomes; later missions at temporary remote floor pads. Older 'fishing unimplemented' and 'first mission cannot finish' text is obsolete.
- **ADR-053–054:** flattened/cleaned paving, fitted islands and banks, water shader, colour/roof variants, façade bands and a non-colliding distant ring. Editor visual/geometry proof is not a packaged performance pass.
- **ADR-055:** genuine development HTTPS account and mandatory first-character entry path; a real mail sender is deliberately deferred until deployment.
- **ADR-056:** an owner-observed driving freeze prompted a fix; **owner feedback on an old run does not approve the corrected build**.
- **ADR-057–059:** three-body rotating/zooming creator and repairs to protagonist's sampled mips, face UV/texture and 3D head relief. Latest editor smoke passed; no latest packaged/owner visual sign-off.

## Priorities and acceptance

1. **P0 current asset seal:** clean SHA, engine pin, all nine source windows, map/ring/walk graph/water/animations/model and local input digests; recook new Linux Client/Server artifacts and archive manifests under [playtest freshness](PLAYTEST_FRESHNESS.md). Run the full prior smoke matrix **and account/mission**, distinguishing editor from staged. No old package counts for later revisions.
2. **P0 first play and identity:** packaged first-time/returning logins, ticket denial/replay/restart and two independent clients, no shipping dev-key bypass, check log/token exposure, real email only after hosting and privacy decisions. Close FL-01–FL-12 including owner test.
3. **P0 integrated gameplay/performance:** packaged >150 m car trip and passenger visibility, bridge/river and all nine-window traversal, 2–3-client crowd/reactions, fishing/full-mission two-client restart and negative cases; profile CPU/GPU p95/p99, cold load, VRAM, bandwidth and 1/2/4/8-client stability.
4. **P0 UI:** finish Settings and truthful inventory/skills/mission/choice/status states; harmonize input priority across login, chat, E, F/R/G, driving, panels and disconnect; test six screen sizes, UI scales, controllers and Slovene characters; obtain **personal owner approval of exact assets/build**.
5. **P1 content/scale:** replace Melje/Tabor pads with real authored terrain, improve period accuracy, ecology, civilian routine, fishing audio, vehicle persistence/traffic, trade and professions. Add streaming or purchased geometry only after measured technical, rights and owner gates.

| Acceptance gate | Current state |
| --- | --- |
| Linux authoritative network and early private chat | Recorded packaged engineering evidence |
| All new world/avatar/account/mission code as one latest package | **Open** |
| First-time and returning account | **Editor-qualified; packaged/owner open** |
| Fishing, skills and eight mission flows | **Editor-qualified; packaged/owner open** |
| ADR-056 car relevancy correction | **Editor-qualified; packaged long-distance and owner retest open** |
| Large world performance, World Partition or shard capacity | **Not demonstrated** |
| Production email and UI Settings | **Not delivered** |
| Approved contemporary owner gameplay slice | **Not recorded** |

See the updated [README](../README.md), [milestone roadmap](ROADMAP.md), [UI roadmap](UI_HUD_ROADMAP.md), [first-launch ontology](FIRST_LAUNCH_ONTOLOGY.md), [development guide](DEVELOPMENT.md) and [legacy parity](LEGACY_PARITY.md). Earlier dated reviews remain historical.
