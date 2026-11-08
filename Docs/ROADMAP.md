# Milestone roadmap — MMORPG-first from Stage 0

**Controlling rule:** MarburgumII preserves the majority of original Marburgum world/gameplay requirements but reimplements them in fresh Unreal Engine **5.8.3** C++ code. Dedicated authoritative client/server must be present in the **first running milestone**. “Solo-friendly” describes game experience, **not client authority or an offline-only code path**. See [NETWORK_ARCHITECTURE.md](NETWORK_ARCHITECTURE.md), [ONTOLOGY.md](ONTOLOGY.md), [UI_HUD_ONTOLOGY.md](UI_HUD_ONTOLOGY.md), [UI_HUD_ROADMAP.md](UI_HUD_ROADMAP.md) and [LEGACY_PARITY.md](LEGACY_PARITY.md).

## Stage 0A — Native engine and role separation

Delivered **as source only**: `MarburgumII.uproject`, core/shared/client/server module descriptors, Client/Server/Editor/Game build targets, engine version pin, a server-owned GameMode and replicated GameState contract marker, .NET source-manifest verifier, Rust crate and baseline CI.

**Acceptance still outstanding:** compile the four module variants with real UE 5.8.3 on Linux, complete UHT and linking, launch Editor and headless dedicated server, and verify target/module loading. Green .NET/Rust validation does not close this stage.

## Stage 0B — Minimal actual online session (before gameplay)

One dedicated server plus one client, then two clients, join the same small test map using the same authoritative World/GameState. Verify the replicated contract version, joint visibility of server-owned state, clean disconnect and reconnect, and rejection of unauthorized client mutation. Record command line, binary build SHA and logs; run as distinct processes, not merely standalone PIE.

**Gate:** repeatable, observed server/client data path and networked authority. No account backend or persistence is claimed before they actually exist.

## Stage 0C — Premium modular graphical UI foundation (UI-00 through UI-03 in parallel with Stage 0B)

Begin the client-facing visual system as soon as the native Editor/Client builds are available, **alongside** the dedicated server and two-client work rather than after all networking or content production. Preserve original Marburgum HUD semantics but rebuild the appearance professionally in UE **5.8.3**: responsive UMG theme, branded connection/transition screens, low-clutter exploration HUD, contextual prompt, location banner, one pinned objective, condition display, graphical notifications, and coherent on-demand Character, Skills, Inventory and Journal shells. The UI must consume replicated/server-owned state when present, with honest empty/loading/error states; static fixtures cannot masquerade as live MMO gameplay.

**Exclusions:** do not import the original wanted stars, wanted/heat levels, police pursuit/chase system or chase-specific HUD. Do not restore generated talents or Talent screens.

**Gate:** complete UI-00 ontology/parity validation and proceed through UI-01 (actual UMG tokens/gallery), UI-02 (session/focus shell) and initial UI-03 (modular, authority-aware HUD). Each has a separate runtime and screenshot gate; the complete server-connected public capture remains Stage 1. See [UI_HUD_ROADMAP.md](UI_HUD_ROADMAP.md) and [UI_HUD_DESIGN.md](UI_HUD_DESIGN.md).

## Stage 1 — Networked playable district and showcase-quality UI

Provenance-reviewed compact urban district, player movement/camera, **high-quality visually finished initial graphical HUD**, interactable item and NPC, authoritative event/feedback, and a save/restart round trip. The original HUD's interaction/location/objective/condition/notification vocabulary and its Character/Skills/Inventory/Journal screens are carried into newly designed graphical interfaces rather than exposed as clumped debug text. Verify two clients cannot duplicate a limited item or gain unauthorized progress. Record a player-facing 1440p/4K gameplay capture after the networked loop works.

**Gate:** complete UI-03 live-HUD and UI-04 management-panel acceptance in [UI_HUD_ROADMAP.md](UI_HUD_ROADMAP.md): interaction → authoritative event/receipt → replicated state → restart → same outcome, with polished UI. Pass a responsive screenshot/interface-scale/focus matrix and a short *genuine* multiplayer-client capture. Static mockups or UI fixtures cannot close a gameplay gate.

## Stage 2 — Marburgum-specific missions, skills and economy

Expand UI-05 contextual modules for real fishing, dialogue, trade and cooperation as each mechanic lands; begin UI-06 non-editable profile experiments only after the excellent default layout is accepted. Rebuild a representative original game loop in new code: fishing, inventory, an authored mission, an ordinary skill, an NPC conversation, currency and a modest trader interaction. Add authoritative XP, reputation/relationship consequences as designed, idempotent ledger entries and reliable reconnect/resume. **Skills, not the original auto-generated talents.**

**Gate:** a real dedicated-server + two-client regression test, automated transaction/replay tests, and manual gameplay acceptance.

## Stage 3 — City, population, mobility and historical state

Advance UI-05 activity, driving and group overlays; evaluate full UI-06 profile editing after successful Stage 1 baseline without forcing every player to customize. Incrementally import reviewed Maribor topology, terrain and fictionalized 1990s data; introduce district streaming, navigation, vehicles, world clock, weather, virtual NPCs and social/environment state. **World Partition is independent of network AOI.**

**Gate:** measured streaming/navigation stability, server-owned state, useful replication relevance and no known important map seams in shipped content.

## Stage 4 — MMORPG operations and capacity (not first server)

Production-ready authentication/admission, anti-abuse/rate limiting, operational monitoring, schema migrations, backup/recovery and service deployment. Stress-test shard capacity through 1, 8, 32, 64, 128 and 250 players, with optional 300–350 connection headroom; include distant districts and dense crowds. These are original-project **planning targets, not achieved results**.

**Gate:** recorded throughput, latency tails, AOI, bandwidth, persistence, recovery and soak-test evidence at the claimed capacity.

## Stage 5 — Narrative and production scale

Grow a finished **3–6-hour MMO-architected vertical slice** toward the original's ambitious ~75-hour authored campaign, more professions/skills, missions and city content. Maintain solo accessibility and small-group cooperation without fragmenting authoritative gameplay rules.

## Working rules

- A stage is not complete until it builds and executes under **UE 5.8.3** with its acceptance gate.
- Never label a proposed system, data definition, source module or roadmap as working multiplayer.
- Reuse reviewed **requirements, ontology and provenance-audited game data** from original Marburgum; do not mechanically transplant Godot runtime code, server protocol, unreviewed assets or historical assumptions.
- No significant gameplay feature can bypass the client → authoritative server → replicated result boundary simply to simplify local testing.
- Every public-facing development capture must use a reviewed graphical UI and clearly distinguish functional gameplay from UI mockups.
- Intentionally excluded features (wanted stars, police pursuit/chases, legacy talents) are not implementation backlog items.
