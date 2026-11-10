# Architecture decision log

## ADR-001 — Clean Unreal-native restart (accepted 2026-11-08)

Magna Raedarum: Marburgum II is created as a fresh source project. Previous Marburgum work supplies design requirements and may supply reviewed reusable data, but no previous code or assets are assumed to be present. Unreal C++ owns runtime gameplay.

## ADR-002 — Pin Unreal Engine 5.8.3 (accepted 2026-11-08)

The project uses **Unreal Engine 5.8.3**, not a generic `UE5` requirement. `.unreal-engine-version` pins the patch; local checks inspect `Engine/Build/Build.version`. Project engine association can remain `5.8` because that is a launcher-style association, not an exact patch guarantee. Any patch or minor upgrade requires an explicit acceptance decision and native re-build.

## ADR-003 — C# and Rust separation (accepted 2026-11-08)

C# runs Unreal's build rules and independently developed .NET toolchain tasks. Rust provides independent, testable native primitives via a versioned C ABI. Neither is prematurely integrated as a gameplay scripting language inside Unreal.

## ADR-004 — Preserve systems and skills, reject talents (accepted 2026-11-08)

The game's design centers on a persistent fictionalized 1990s Maribor with skills, missions, jobs, vehicles, economic/social systems and a solo-friendly experience. The previous automatically generated talent subsystem is intentionally excluded.

## ADR-005 — MMORPG-first authority and module boundaries (accepted 2026-11-08)

The title is a server-authoritative **MMORPG from initial architecture**, with solo-friendly and small-group play occurring under the same world/authority rules. The original project's Client/Shared/Server module separation is retained conceptually in a fresh implementation: `MarburgumIIClient` (ClientOnly), `MarburgumIIShared` (Runtime) and `MarburgumIIServer` (ServerOnly). The dedicated-server runtime and client/network acceptance are Stage **0** requirements, not Stage 4 optional work. Shared definitions are engine-independent wherever possible, and the new runtime initially uses Unreal-native replication rather than assuming compatibility with the original TCP protocol v3. Gameplay and narrative systems from the original remain the majority of the planned game, but their implementation is rebuilt and verified incrementally.

## ADR-006 — Early showcase-quality UI; exclude wanted stars and police chases (accepted 2026-11-08)

The original Marburgum's **semantic UI components** (transient location banner, contextual interaction prompt, one pinned mission objective, condition/progression feedback, client connection states and Character/Skills/Inventory/Journal panels) are retained, but their appearance is rebuilt as a polished, responsive, high-quality graphical Unreal **5.8.3** client interface from the first development milestones. This is prioritized alongside Stage 0B networking and required for Stage 1 public-showcase gameplay acceptance, **before** wide-map or gameplay feature proliferation. GUI assets and UI design remain wholly client-side, while substantive values originate from authoritative replicated state. The original **wanted stars, wanted level/heat, police pursuit, police chase, chase HUD and associated escalation mechanics are explicitly excluded** from MarburgumII; they are not migration parity obligations. This exclusion does not by itself ban police/institution NPCs in authored stories. The old generated Talent UI also remains excluded. See [UI_HUD_DESIGN.md](UI_HUD_DESIGN.md).

## ADR-007 — Semantic modular UI and HUD with staged customization (accepted 2026-11-08)

Adopt [UI_HUD_ONTOLOGY.md](UI_HUD_ONTOLOGY.md) and the companion versioned [UI/HUD vocabulary](../Data/Ontology/ui_hud.v0.1.json) as the semantic baseline for the UE **5.8.3** client presentation. The interface is modular, low-clutter, activity-aware, accessible and event-driven; every module has an explicit authority/privacy provenance, visibility policy and layout slot. Build a coherent premium default through UI-01–04 in parallel with NET-001, then add UI-05 contextual activity/party modes as real systems arrive and UI-06 movable/resizable saved layout profiles only after the default is visually accepted. The online truth comes exclusively from authoritative replication and receipts, while local UI settings and labeled fixtures remain client-only. The original location/action/objective/condition/skills/inventory/journal semantics are retained. No police pursuit/wanted UI or generated Talents are reintroduced. The [UI_HUD_ROADMAP.md](UI_HUD_ROADMAP.md) specifies acceptance gates and public showcase evidence.

## ADR-008 — Personal owner playtest required for all major playable features (accepted 2026-11-08)

Every major player-facing feature must be **specified, implemented, tested, personally played by the project owner and explicitly approved** before it can be described as "accepted", "finished" or "feature complete". Green CI, successful native build, agent-generated videos, documentation and simulated playtest outputs cannot substitute for the owner's in-game evaluation. Acceptance must identify an exact build SHA, engine patch, features/scenarios, expected/observed behaviors and the explicit owner decision (`approved`, `changes_requested`, `rejected`). Without such a record, a feature remains `implemented_awaiting_owner_playtest` or `awaiting_owner_approval`. Rework or material changes reopen the gate. This rule covers combat, movement, MMO connection, missions, UI/HUD, skills, economy, fishing, vehicles, ecology, environmental rendering and any large imported city map.

## ADR-009 — Purchased Maribor city model as planned default geometry (accepted direction 2026-11-08)

The planned **primary base geometry** for MarburgumII is [TurboSquid product 1639711](https://www.turbosquid.com/3d-models/3d-city-area-building-model/1639711), a licensed 3D model depicting Maribor, **subject to future purchase, asset/license verification, historical alignment, UE 5.8.3 runtime validation and the owner's explicit personal acceptance**. The original Marburgum generated map is **no longer the default import path**, but remains reference and fallback; original authored geography, mission/interaction identities, 1990s research, vegetation and fish data continue to inform the new city. Do not assume the seller's 2022 GIS model reflects the 1990s, is low cost at runtime, contains native C4D-only features in FBX, or can be redistributed in public source control. Local proprietary files are protected from Git commits. Stage 0B networking uses its own minimal world; Stage 1 should use a licensed and approved Lent/Drava slice when available, with city expansion after a representative acceptance test. See [WORLD_MAP_ADOPTION.md](WORLD_MAP_ADOPTION.md).

## ADR-010 — Conditional Hunyuan3D-2.1 local asset-generation pipeline (accepted as tool inventory 2026-11-08)

The project owner reports Hunyuan3D-2.1 installed locally for image-conditioned **3D mesh and PBR texture generation**. It is a development-time candidate for period-appropriate props, streetscape detail, vegetation and fish visual variants; it does not replace the planned licensed Maribor base geometry, established species IDs or authored map/history. **Its publicly published community license expressly excludes European Union territory**. Local installation is not sufficient evidence of production/distribution rights in Slovenia. Treat all Hunyuan-derived production assets as **blocked pending an applicable separate authorization/rights review**, including input-image rights. Once cleared, process outputs via manual quality review, game-ready geometry/material optimization, deterministic UE **5.8.3** import, fresh cooked client/server build and personal owner gameplay approval. No model output, installed checkpoint or Unreal import has been verified in this change. See [ASSET_GENERATION_HUNYUAN3D.md](ASSET_GENERATION_HUNYUAN3D.md).

## ADR-011 — General and one-to-one private chat from the first online slice (accepted 2026-11-08)

Include **general chat visible to all currently connected players on the same logical game server** and **private text conversations delivered only to the sending player and the intended online recipient**. Do not expose private bodies through globally replicated GameState, broad network multicast, or server logs. Begin with Unreal Engine **5.8.3** PlayerController-owned Server/Client RPCs, validated text/rate/recipient, client-only UMG, and ephemeral session PlayerState IDs until authentic account identity exists. A minimal C++ routing and UMG source scaffold is committed, but Unreal compilation/network validation and **personal owner playtest/approval are pending**. Later acceptance needs 3-client negative-privacy tests, moderation/mute/block/report, reconnect clarity, consistent visual quality, fresh cooked assets and owner signoff. See [CHAT.md](CHAT.md).

## ADR-012 — Side modules may consume the core module (accepted 2026-11-08)

`MarburgumIIClient` and `MarburgumIIServer` may depend on the core `MarburgumII` module, because presenting and serving replicated `GameState`/`PlayerController` types requires their declarations. Dependencies flow in one direction only: client → core/shared, server → core/shared, core → shared. Core and shared never depend on a side module, shared never depends on core, and the two sides never depend on each other. `tools/MarburgumII.Tools` enforces these edges at source level. Packaged-binary symbol checks confirm that no server-module code reaches the Client target and no client-module code reaches the Server target.

## ADR-013 — Offline clients sit on a no-authority front-end map (accepted 2026-11-08)

The client's default map is `/Game/Maps/FrontEnd`, whose world-settings game mode `AMarburgumFrontEndGameMode` uses the engine base GameState/PlayerController, spawns no pawn and owns no gameplay rules. A client that has not been admitted to an authoritative server, or has lost its session, therefore lands on the front-end and never simulates Marburgum rules locally; previously it fell back to a standalone copy of the test map with its own authoritative `GameMode`. The server's default map is unchanged. Connection state is observed by the client-only `UMarburgumSessionSubsystem` (phases in `MarburgumSessionContract.h`) and is reported as `ready` only after a live server connection, a replicated non-authoritative `AMarburgumGameState` with a matching network contract and a replicated `PlayerState` exist. Unexpected loss triggers up to three automatic reconnect attempts with linear backoff, then an explicit error with retry.

## ADR-014 — Development character key and file-backed world store for Gate 1 (accepted 2026-11-10)

Gate 1 needs state that survives a server restart and belongs to a character, before accounts or a production database exist. Two deliberately limited development mechanisms fill that gap:

- **Development character key.** A client joins with `?MB2Character=<key>` (3–40 characters of `a-z 0-9 _ -`). `AMarburgumGameMode` refuses a malformed key and a key that is already in play on the server, at `PreLogin` and again at `InitNewPlayer`, so one character never has two concurrent sessions. The key is held only on the server-side `AMarburgumPlayerState`; a connection without a key is admitted but every interaction is refused. The key is **not authentication**: anyone who knows it can play that character. The client sends the key from its command-line URL, `-MB2Character=`, or a key generated once into `Saved/MarburgumII/DevCharacter.txt`. This is a test identity, not an account, and does not settle the account/session/character model.
- **File-backed world store.** `FMarburgumWorldStore` (server module) keeps one JSON document per map (`marburgumii.world_store`, schema 1): interactable stock, per-character inventory and one receipt per committed operation id. Every commit writes a temporary file, `fsync`s it and `rename`s it over the previous document. The new state is applied to replicated actors only after that succeeds. An unreadable or unknown-schema document is never overwritten: the server refuses interactions until an operator intervenes. It is synchronous, single-process and unbounded (receipts are never pruned), so it is a development adapter, not the production persistence engine, which remains an open decision.

Committed receipts make operation ids idempotent across restarts: a replayed id is answered `Duplicate` with the original grant, and an id belonging to another character is refused. Refusals and duplicates never write.

## ADR-015 — Authoritative interaction logic lives in the ServerOnly module (accepted 2026-11-10)

The core module owns the replicated types (`AMarburgumInteractable`, owner-only inventory on `AMarburgumPlayerState`, the `ServerRequestInteraction` RPC and `FMarburgumInteractionReceipt`) and an abstract world subsystem, `UMarburgumAuthorityService`. `MarburgumIIServer` provides the only concrete subclass, `UMarburgumWorldAuthority`, which validates (operation id, store, character, receipt replay, target, range, stock), commits through the world store and then applies the result. The RPC finds the service through the world's subsystem collection, so the core module never depends on a side module (ADR-012). The Client target contains no validation or persistence code, and a world without the service (a client, or a standalone front-end) refuses every interaction with `authority_unavailable`. The service is active only in dedicated or listen-server worlds. Client presentation never predicts a grant: the prompt shows "waiting for the server" until the receipt arrives, and items appear only through the replicated inventory.

## ADR-016 — Lent fallback blockout as the Stage 1 district while the city model is pending (accepted 2026-11-10)

Stage 1 needs a playable district, and the licensed Maribor model (ADR-009) has not been bought. `/Game/Maps/Lent` is therefore a **deliberate fallback**: an independently authored blockout of engine shapes and 18 flat-colour material instances (`Content/Blockout/Materials`), generated by the C++ map commandlet (`MarburgumIIEditorTools`, `scripts/create-maps.sh`; originally a Python editor script, see ADR-018). It contains the Lent promenade with a quay railing, the Drava, a flush Stari most deck, a row of placeholder buildings, lamps and trees. It is neither the purchased model nor the original game's generated geometry, and it is not a survey of the real Lent.

The layout uses the original Marburgum coordinate frame (metres, x east and z south of the middle of Stari most, y up), mapped to Unreal as X = x, Y = z, Z = y. That way the original `areas.json` box for `lent` and the original `interactions.json` position of `lent_angler` carry over unchanged. Everything else is massing. The location banner states it on screen ("fallback blockout, not the licensed city model"), and the map is safe to publish. When the licensed slice is accepted it replaces this map as the showcase district. The fallback stays available for public checkouts, which cannot contain the licensed data. The dedicated server's default map is now `/Game/Maps/Lent`; the network, chat, UI-capture and Gate 1 tests keep naming `/Game/Maps/NetSmoke` explicitly.

Player starts carry a `PlayerStartTag`. The development join option `?MB2Start=<tag>` picks one, and so does the default `arrival`. Like the character key (ADR-014), it is a test affordance, not a spawn or travel design.

## ADR-017 — Server-owned missions and location (accepted 2026-11-10)

- **Missions** are registered in the shared contract (`MarburgumII::Gameplay::Missions`), and only the stages a build can reach are registered. `lent_first_catch` registers stages 1–2 of the original five. Stage 1 cannot complete until fishing exists, and the stage 2 debris is not placed, so there is no "mission complete" state until a final stage exists. No ending is ever invented.
- **Mission rules** are a pure function in the ServerOnly module (`MarburgumMissionRules`, unit-tested by `Marburgum.Server.MissionRules`). Talking to an NPC starts the mission it offers, with the mission's start items granted in the same record. Otherwise it completes the current stage if that stage targets the NPC and its `inventory_any` requirement is held, consuming one item. Otherwise it is refused (`requirement_not_met`, `nothing_to_do`). Range, character, store and replay checks are shared with item sources in `UMarburgumWorldAuthority`, following ADR-015.
- **World store schema 2** adds `missions` (character → mission → stage and starting revision) and typed receipts (`kind`: `item_granted`, `mission_started`, `stage_completed`, plus mission and stage). Schema 1 documents still load; their receipts are read as item grants, and the next commit rewrites the document as schema 2. Newer or older unknown schemas are refused without being overwritten, as before.
- **Replication:** mission progress replicates on `AMarburgumPlayerState` to its owner only, like custody, and is applied together with the inventory so a client never sees one without the other.
- **Location** is not client-reported. The server derives each player's named area from the pawn position and the placed `AMarburgumArea` boxes four times a second, and replicates it to the owner only. It is never persisted. Clients only display it through the location banner.

## ADR-018 — No Python: Rust repository tooling, a C++ map commandlet and .NET 10 (accepted 2026-11-10)

The project's tooling languages are C++, C# and Rust (ADR-003). Python had nevertheless crept in for validators, the playtest guard, smoke-test snippets and the Unreal editor map scripts. The owner directed that it be replaced as originally intended:

- **Rust repository tool.** `tools/mb2` (binary `mb2`, run via `scripts/mb2.sh`, which builds it on demand) replaces all standalone Python. It provides:
  - the UI/HUD ontology, chat and gameplay contract validators;
  - a check that the repository stays Python-free;
  - the engine-version check;
  - the operation ids and world-store checks that the smoke scripts use;
  - the fail-closed playtest freshness guard, with its unit tests ported.

  It is a development tool only and is not linked into any Unreal target. The deterministic simulation crate `native/marburgum-sim-core` remains separate.
- **C++ editor commandlet.** Unreal's embedded Python cannot be replaced by Rust. Map generation is therefore `UMarburgumCreateMapsCommandlet` (`-run=MarburgumCreateMaps`) in a new Editor-type module, `MarburgumIIEditorTools`. The module is listed only in the Editor target, so Game, Client and Server binaries never contain it. It ports the FrontEnd, NetSmoke and Lent generators piece for piece, including the Lent blockout material instances. The gameplay validator now checks the map layout against the commandlet's source. `PythonScriptPlugin` and `EditorScriptingUtilities` are explicitly disabled.
- **.NET 10.** The C# manifest validator targets `net10.0`. A root `global.json` pins SDK 10.0.x with feature-band roll-forward, and CI installs that SDK from it. UnrealBuildTool continues to use the engine's bundled .NET.
- **CI:** one Rust job runs the tool's tests and `mb2 validate all`, alongside the .NET 10 and simulation-crate jobs. Rust stable and .NET 10 are required; Python 3 is not.

## ADR-019 — The server bounds client movement time; world time follows real time (accepted 2026-11-10)

**Problem.** Character movement is server-authoritative in position but not in time. The engine's `CharacterMovementComponent` simulates each `ServerMove` for the time between the client's own timestamps, and Unreal's movement time-discrepancy checks are off by default. A client whose clock runs fast therefore walks faster than `MaxWalkSpeed` on the server too. Range validation (`UMarburgumWorldAuthority`) and location derivation (ADR-017) both follow that pawn. In the Lent smoke of 2026-11-10 (`df2e04f`), the `-nullrhi` client W covered 12.7 m in 1.1 s of wall time while its world clock advanced 2.9 s, and the server placed it in Lent about a second after it set off.

**Cause of the fast test clock.** It is neither a fixed time step (`-UseFixedTimeStep`/`-benchmark` are not used) nor a frame-rate cap. A `-nullrhi` client has nothing to render and no cap, so it runs at about 5,000 fps. `AWorldSettings::FixupDeltaSeconds` clamps every frame to at least `MinUndilatedFrameTime` (engine default 0.0005 s, "2000 fps"), so each 0.2 ms frame advanced world time by 0.5 ms. Any client above 2,000 fps does the same, including a rendered client in a trivial scene with no frame cap.

**Decision.**

- **Server: detection and resolution on** (`[/Script/Engine.GameNetworkManager]` in `Config/DefaultGame.ini`; the keys carry a `b` prefix: `bMovementTimeDiscrepancyDetection`, `bMovementTimeDiscrepancyResolution`).
  - `MovementTimeDiscrepancyMaxTimeMargin=0.25`: a client may get 0.25 s ahead before it is detected.
  - `ResolutionRate=1.0`: once detected, its moves are limited to server time until it has paid back all the time it took.
  - `MinTimeMargin=-0.25`: a slow or hitching client cannot bank more than 0.25 s to spend later.
  - `DriftAllowance=0`: any drift allowance is a permanent speed bonus for a fast clock.
  - `bMovementTimeDiscrepancyForceCorrectionsDuringResolution=True`: the client is corrected while it pays back.
  - Over any stretch of real time T a client moves for at most T + 0.25 s, so it can be at most `MaxWalkSpeed` × 0.25 s (1.1 m at 4.5 m/s) ahead of an honest client. Every position it reaches is one an honest client could reach too, at most 0.25 s later.
- **Clients: world time follows real time.** `[/Script/Engine.WorldSettings] MinUndilatedFrameTime=0`. An honest client at any frame rate stamps its moves with real time, so the server never mistakes it for a speed hack and resolution never rubber-bands it. `MaxUndilatedFrameTime` (0.4 s) stays: it can only slow a clock down.
- **Evidence.** `UMarburgumCharacterMovement` (core module) reports each detection on the server as `MB2INTERACT role=server event=time_discrepancy`, at most once a second per character. The server's `event=area` lines carry the pawn position.
- **Test clients behave like real clients, and one adversary is kept on purpose.** With the clamp removed, `-nullrhi` test clients keep real time, and an honest walker fails its own run if its world clock ran ahead. The Lent smoke adds a deliberate adversary, F. F dilates only its own world clock to 2.5× real time (`-MB2LentClockRate=2.5`), which is deterministic, unlike relying on a host's frame rate. The smoke then checks, from the server's positions, that no walker covered more than `MaxWalkSpeed` × (elapsed + 0.25 s) + 0.5 m. It also checks that the server detected F and nobody else.

**Not covered.** This bounds movement time only. It does not validate client-claimed velocity modes, teleports beyond the engine's position-error correction, or `ClientAuthorativePosition` (still off). It does not rate-limit `ServerMove` RPCs, and it does not report or penalise a detected client beyond correcting it. Account sanctions, anti-cheat telemetry and lossy-network tuning of the margins are later work. With 250-player shards the 0.25 s margin and the absence of drift allowance need re-measuring under real latency and packet loss.

## Open decisions

- Verify production rights for the locally installed Hunyuan3D-2.1 version, specifically use within Slovenia/EU, generated outputs and subsequent game distribution.
- Public license and rules for distributing source/art; no license has been selected.
- Minimum shipping platforms and tested build matrix.
- Production persistence engine, migrations, account/session admission, login security and service topology; initial Unreal-native replication is accepted for the first vertical slice. The ADR-014 development key and file store are stand-ins, not answers.
- Rust static-library integration across Windows/Linux and ABI test approach.
- Geospatial/historical data provenance and acceptable licensing.
