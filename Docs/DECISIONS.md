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

Evidence: [STAGE1_MOVEMENT_TIME_EVIDENCE.md](STAGE1_MOVEMENT_TIME_EVIDENCE.md).

**Not covered.** This bounds movement time only. It does not validate client-claimed velocity modes, teleports beyond the engine's position-error correction, or `ClientAuthorativePosition` (still off). It does not rate-limit `ServerMove` RPCs, and it does not report or penalise a detected client beyond correcting it. Account sanctions, anti-cheat telemetry and lossy-network tuning of the margins are later work. With 250-player shards the 0.25 s margin and the absence of drift allowance need re-measuring under real latency and packet loss.

## ADR-020 — Server-simulated condition: running costs breath, and breath is saved (accepted 2026-11-10)

**Problem.** The `player_condition` HUD module was design-only because nothing in the build produced a condition, and the ontology forbids inventing one. The original game did have a real one. Its server (`src/server/world_simulation.cpp`, constants in `src/shared/combat.h` and `protocol.h`) gives every body vitality and stamina. Walking is 2.2 m/s and running 4.4 m/s. A run spends 10 stamina a second, so a full bar is ten seconds of running, and breath comes back at 11 a second whenever the body is not running. A run that empties the bar leaves the body *winded*, and a winded body walks until stamina is back to 15. The body (vitality, stamina, winded) is saved with the character, so logging out does not refill it.

**Decision.**

- **Carry the rule over unchanged, in the movement simulation.** `UMarburgumCharacterMovement` decides each move whether the body runs: the run flag (held Shift, sent as `FLAG_Custom_0`) plus breath, and it caps the speed at 4.4 m/s. Breath is spent only on the ground. The owning client, the server and every replay run the same code, so the client predicts its own stamina and nothing rubber-bands. Walking drops from the placeholder 4.5 m/s to the original 2.2 m/s.
- **The server decides; the client's claim is only checked.** Each `ServerMove` carries the stamina the client predicted, through a custom `FCharacterNetworkMoveData`. The server compares it with its own simulation and sends a correction if they differ by more than one point. The correction carries the server's stamina and winded state, through a custom `FCharacterMoveResponseDataContainer`. The speed of the server's pawn always follows the server's breath, so a client that claims breath it does not have still walks. Saved moves record their starting breath, so combining two moves does not count the first one twice. The run request is restored after a replay, as the engine does for crouch.
- **Owner-only replicated condition, whole points.** `AMarburgumPlayerState::Condition` (vitality, stamina, winded, running) replicates `COND_OwnerOnly` like custody and location. The server mirrors the simulation into it, rounding down, so the HUD never shows breath the body does not have. Running and winded changes go out immediately; point changes ride the normal update rate. The HUD's stamina bar follows the owning client's corrected prediction, so it moves smoothly.
- **Saved at logout and at shutdown, not on every change.** `UMarburgumWorldAuthority` restores the saved condition at login; the pawn adopts it before it runs. It saves the condition when the player logs out, and for everyone still connected when the world tears down. World-store schema 3 adds `conditions`. A full, rested body is stored as **no entry**, so characters that never run never cause a commit, and the Gate 1 and Lent smokes keep their byte-identical store checks. A winded save only counts below 15, as in the original.
- **Vitality is stored and shown, but nothing changes it yet.** Falls, drowning and fights, the original's ways to lose vitality, are not in this build. The Character panel says so rather than hiding the bar.
- **ADR-019 bound adjusted.** The Lent movement check now allows the extra ground of the seconds the server itself simulated a pawn running: `run_s` on the server's `event=area` lines. That is walk × (elapsed + margin) + (run − walk) × server running seconds + slack. A client cannot buy running time by claiming it.

Evidence: [STAGE1_CONDITION_EVIDENCE.md](STAGE1_CONDITION_EVIDENCE.md).

**Not covered.** Crash-safety of condition between logouts: a server killed with SIGKILL loses the breath spent since the last save, which is acceptable for breath and must be revisited when vitality can fall. Falls, guarding and combat stamina costs (the rest of `combat.h`); swimming and drowning are ADR-032. Stamina for skills or work. Sprint animation and camera feedback. Re-measuring the one-point correction tolerance under real latency and packet loss.

## ADR-021 — Characters use the UE5 mannequin skeleton; the template mannequin is committed (accepted 2026-11-10)

**Problem.** The project had no character art. A first protagonist prototype was made outside the tree: an owner-supplied concept image, TRELLIS image-to-3D, skin weights transferred from Manny in Blender, then an FBX import into UE 5.8.3. Its skeletal mesh needs a skeleton asset in the project.

**Decision.**

- **One skeleton for human characters: `/Game/Characters/Mannequins/Meshes/SK_Mannequin`.** The template locomotion animations, Control Rig and IK Rig then work on every character without retargeting.
- **Epic's template mannequin is committed** under `Content/Characters/Mannequins/`, byte-identical to the engine's `Templates/TemplateResources/High/Characters/Content/Mannequins`, at the owner's direction, under the Unreal Engine EULA.
- **Source art lives in `SourceArt/`** (concept, generator output, rigged FBX); the imported assets live in `Content/`. `*.glb` joins the Git LFS patterns.
- **The authoring pipeline stays outside the repository.** Its steps are Blender and Unreal editor Python, which ADR-018 keeps out of the tree. Its procedure and checks are recorded in [CHARACTER_PROTAGONIST.md](CHARACTER_PROTAGONIST.md).

**Not covered.** Whether the research-only licences of the rasterizers used for TRELLIS's texture bake reach the baked texture in a commercial game (the protagonist no longer depends on it, ADR-031). The player pawn now uses the mesh (ADR-023); hands are ADR-028, retopology and the rebake ADR-031.

## ADR-022 — Email-confirmed account, first character and server-gated world entry (product direction accepted 2026-11-10; implementation pending)

The player's first launch must start at a login/title screen. Like the original Marburgum in user-facing design, a new account requires **email confirmation** before it can obtain usable username/password credentials; passwords must be created/set via a secure verification-bound flow, never emailed or stored as plaintext. The player signs in with username/password. If that account has no character, **First name, Last name and a selected approved character model are mandatory** on a dedicated full-screen character creator. The server durably creates and binds that character to the authenticated account before enabling Enter World. A returning user resumes the existing owned character. No playable pawn/world authority may be granted to an unauthenticated, unverified, characterless or mismatched account.

The initial product policy is one character per account (expandable only by a later explicit decision), opaque account and character IDs, and distinct login username vs in-world first/last names. UI-02A and Stage 0F are added as a **public Stage 1 entry prerequisite**; Stage 4 remains responsible for operational hardening, scale and production deployment. The ADR-014 MB2Character test key remains a development-only compatibility path, not production login or an account ownership guarantee. Do not treat this accepted direction as an implemented or owner-playtested service. The credential provider, storage/email provider, exact naming policy and approved character model roster remain open implementation choices. Design and acceptance are specified by [FIRST_LAUNCH_ONTOLOGY.md](FIRST_LAUNCH_ONTOLOGY.md) and [first_launch.v0.1.json](../Data/Ontology/first_launch.v0.1.json); see [ROADMAP.md](ROADMAP.md) and [UI_HUD_ROADMAP.md](UI_HUD_ROADMAP.md).

## ADR-023 — The player is the protagonist in third person (accepted 2026-11-10)

**Problem.** `AMarburgumCharacter` was a first-person camera with a cylinder that only other players saw. The protagonist prototype (ADR-021) existed but nothing used it, so a playtest could not show the character. The owner chose a third-person camera.

**Decision.**

- **The pawn's body is `SKM_Protagonist`** on `ACharacter`'s own skeletal mesh component, feet at the bottom of the unchanged 34 × 88 cm capsule, turned −90° because the mannequin faces +Y. The placeholder cylinder is gone.
- **Animation is Epic's template `ABP_Unarmed`** from the committed mannequin: idle/walk/run blend space, jump and fall, and foot IK through `CR_Mannequin_FootIK`. It reads only the engine `Character` and its movement component, so it needs no project Blueprint. Running at 4.4 m/s and walking at 2.2 m/s (ADR-020) drive the blend space directly; no root motion is used.
- **Third-person camera:** a 350 cm spring arm, 50 cm above the capsule centre, follows the control rotation; the mouse orbits it. The body no longer follows the controller's yaw; it turns toward its movement at 540°/s. WASD stays relative to the camera's yaw. Interaction already picks the nearest object in range, not the one in view, so it is unchanged.
- **Bodies nobody sees skip their animation** (`OnlyTickPoseWhenRendered`). A dedicated server renders nothing and nothing on it reads bone poses, so it never animates. This must change when a montage with root motion or bone-based hit tests arrives.

**Not covered.** Owner playtest. Rendered evidence of how another player's body animates on a remote client (the smokes render one client at a time). Camera collision tuning in tight interiors, camera settings in the Settings screen, and a first-person option. The texture licence remains open under ADR-021; hands are ADR-028.

## ADR-024 — Authored Lent streetscape, still not the licensed city (accepted 2026-11-10)

The TRELLIS map roadmap (`marburgum-trellis-roadmap`, 10 November 2026) says image-to-3D can supply props and candidate details, and must not generate the terrain, the Drava, bridges or ordinary building shells. The first slice it asks for is one playable district, and the playable-map roadmap already names Lent as that pilot. The licensed Maribor model (ADR-009) is still unpurchased, and the OSM/LiDAR Lent footprints stay in the original project under ODbL rather than being copied into this public tree.

`/Game/Maps/Lent` remains the independently authored fallback of ADR-016, regenerated by the same C++ commandlet. It is no longer a single flat row of boxes. The promenade has shopfronts with invented names, roofs, a back lane, a water tower, a far bank, a railed fishing jetty west of the angler, benches and lamp lights. The `lent` area box, `lent_angler`, and the `arrival`, `angler` and `bridge` starts are unchanged, and the bridge street at x = 0 stays open. The location note now says this is authored Lent, not the licensed city model. Nothing here is a survey, a TRELLIS city mesh, or an accepted production map. Owner playtest is still required (ADR-008).

## ADR-025 — Lent is the default map for now (accepted 2026-11-10)

The owner asked that the authored Lent streetscape replace the previous launch map. `GameDefaultMap`, `EditorStartupMap` and `ServerDefaultMap` are `/Game/Maps/Lent`. NetSmoke stays the explicit map for the network and Gate 1 tests. `/Game/Maps/FrontEnd` still exists for the connection shell (ADR-013); the UI capture opens it by name instead of relying on the default.

**Temporary.** A client started with no server address now loads Lent under the local game mode, which is the offline-authority case ADR-013 avoided. The front-end returns as the client default when login is the way into the world. This does not recook the build that is already running. Superseded as the launch map by ADR-027.

## ADR-027 — The launch map is the original Unreal Lent level (accepted 2026-11-10)

The owner rejected the generated LiDAR shells. Those commits are reverted. The map to upgrade is the Lent level already used by Unreal Marburgum, copied from `magna-raedarum-marburgum` into this project: `/Game/World/lent`, with its landscape, the 133 Lent building meshes, the road mesh, the water mesh, Stari most and the shared plaster and clay-tile materials. The editor loaded that level with map check 0 errors and 0 warnings.

`GameDefaultMap`, `EditorStartupMap` and `ServerDefaultMap` are `/Game/World/lent`. `/Game/Maps/Lent` stays so the older mission smokes still have their own actors. Building meshes derived from OpenStreetMap stay under ODbL, © OpenStreetMap contributors.

The opening-route pass (`-FictionLent`) does not move those meshes, the landscape, the Drava or Stari most. It adds late-afternoon sun, sky, a light valley fog and a restrained grade; player starts and the riverside angler seated on the Lent bank beside the bridge; and F1 names only: the street reads Obreška pot, and three riverfront signs read Kavarna Pri Toku, Ribarnica Na Bregu and Pekarna Pri Mostu.

`-RebuildFacades` then rebuilt all 133 Lent façades in place. Footprints, eaves and roofs stayed. Each wall plane that was a blank plaster shell now has a cornice, a window on every floor, and one door on the building's longest wall. Frames are dark painted wood and the glass is a dark low-roughness instance of the building material. The pitched roof was a rectangle laid over the footprint, so on a complex plan it did not meet the walls. Roofs are rebuilt from the footprint. A plan that is nearly rectangular becomes that rectangle, and the pitch sits on those four walls. A complex plan keeps its outline: the eaves are the wall tops, and the ridge rises from the middle instead of a loose box. Eight footprints under 30 m², including the one triangle, are removed from the level. Windows sit on the outside of a solid wall instead of being cut through it. Each roof is a slab that shares the wall-top edge, overhangs it, and has an underside, so the join is not a see-through crack. It is not the final art, but after the `0c06ce8` playtest the owner made this level the standing default: the game, the editor and the dedicated server open `/Game/World/lent` from now on. `/Game/Maps/Lent` is no longer a launch map. It remains only as the map the older mission and condition smokes name explicitly.

The building geometry described here is superseded by ADR-034.

## ADR-028 — Characters wear the mannequin's hands (accepted 2026-11-10)

**Problem.** TRELLIS fuses the fingers into one lump, so the protagonist's finger bones smeared the hand whenever a hand animation curled or relaxed it. The fitting step also left Manny's elbows bent from his bind pose, so the hand and finger joints sat about 16 cm in front of the generated hands and pulled the wrong vertices.

**Decision.** At the owner's choice, the pipeline replaces generated hands with the template mannequin's (`SKM_Manny`) hands. They are on the same skeleton and already weighted per finger, and they are Epic content already committed under ADR-021. Before transferring weights, the pipeline straightens Manny's elbows and aims each arm at the generated hand. It cuts the generated forearm at the wrist joint, centres and widens the mannequin wrist over it, and gives the hand one forearm-skin texel of the generated texture, so the mesh keeps a single material. Mannequin hands are decimated to 30% (about 2k vertices each).

**Not covered.** Hand skin detail and an exact tone match with the baked body; a small step where the low-poly forearm meets the hand. Retopology and a final texture bake, which would replace both.

## ADR-029 — NVIDIA render features are client-local, optional and resolved at runtime (accepted 2026-11-10)

**Context.** Unreal Marburgum never shipped DLSS. It probed for NVIDIA's official UE plugin and the community DLSS5ForUE5 plugin, gated an experimental profiling mode to Windows/D3D12, and launched Linux builds under NVIDIA Smooth Motion. Its roadmaps fixed the rules: TSR is the reference, every NVIDIA feature is optional, there is one setting per feature, a missing plugin never breaks startup, and rendering never reaches the server. NVIDIA's UE 5.8 package is DLSS 4.5 (v8.9.0). It has no Linux support and is not installed here. No supported DLSS 5 Neural Rendering package exists for UE 5.8.

**Decision.** The client module owns `UMarburgumRenderFeatureSubsystem` and a pure policy, `MarburgumII::Render::Resolve`. Upscaler (TSR or DLSS Super Resolution, with DLAA as its full-resolution mode), DLSS Frame Generation and Reflex are separate settings saved in `GameUserSettings.ini`. Each feature is available only if its plugin is enabled, the GPU is NVIDIA, its console variables exist and its support query returns true. The plugin names, variables and queries are config, and the query is called through reflection, so the plugins are never a build or packaging dependency. Anything unavailable falls back to TSR, no frame generation or no Reflex, with the reason logged on one `MB2RENDER` line. Frame generation keeps Reflex on and never runs with Smooth Motion. A change cuts temporal history, and `-MB2SafeRender` turns every vendor feature off. Neural Rendering had no setting here; ADR-030 adds one through DLSS5ForUE5. `scripts/mb2.sh render-probe` and `scripts/smooth-motion-linux.sh` replace the original Python probe and launcher. See [RENDERING_NVIDIA.md](RENDERING_NVIDIA.md).

**Consequences.** On Linux every DLSS feature resolves to TSR until NVIDIA ships Linux support in its plugin. Smooth Motion is the Linux generated-frame path. The installed v8.9.0 package confirms the configured binding names (ADR-030). NVIDIA binaries are never committed.

## ADR-030 — NVIDIA's DLSS package is installed locally; DLSS 5 comes through DLSS5ForUE5 source, without its runtime (accepted 2026-11-10)

**Decision.** At the owner's request, NVIDIA's UE 5.8 DLSS 4.5 package v8.9.0 (archive SHA-256 `adeeb59b…380c`) is installed in the project's `Plugins/` under NVIDIA's licence. It is gitignored and referenced as optional plugins in the uproject, so it is never committed and its absence breaks nothing. Its source confirms every ADR-029 binding. The render-feature probe now runs after engine start-up, because NVIDIA's blueprint modules load at PostEngineInit. The community DLSS5ForUE5 plugin (v0.6.1, MIT) is vendored as source under `Plugins/DLSS5ForUE5`, enabled on Win64 only, and drives a fifth setting, Neural Rendering `off`/`safe`/`guided`. These are the original project's two one-pass profiles. It runs only with DLSS active and only when its runtime file exists.

**Not done.** The plugin's runtime, `nvngx_dlssnr.dll`, is an unsigned, community-modified NVIDIA binary from an SDK NVIDIA has not released, and its redistribution is not authorised. It is not fetched, committed or shipped. The original project's roadmap already ruled out such binaries as dependencies. Nothing here has run on Windows yet. On Linux, DLSS, frame generation and Reflex report `unsupported` through NVIDIA's own queries, and DLSS5ForUE5 is not enabled.
## ADR-031 — The protagonist is retopologised and rebaked without research-licensed tools (accepted 2026-11-10)

**Problem.** The first protagonist (ADR-021) was TRELLIS's own 4.5k-vertex mesh with a 1024² texture that TRELLIS bakes through nvdiffrast and Inria's rasterizer, both licensed for research and evaluation only. The owner asked for a full retopology and texture rebake.

**Decision.**

- **Source:** TRELLIS's vertex-colour mode on the same concept and seed: the decoded 241k-triangle mesh with colour per vertex, exported without the texture bake, so no research-licensed code produces anything in the character.
- **Retopology:** a watertight voxel shell of the dense mesh with a morphological close (2.2 cm), Instant Meshes (BSD) to about 12k quads, hole filling, shrinkwrap onto the shell. Blender 5.2's QuadriFlow rejects these meshes as non-manifold, so it is not used.
- **Bake:** Blender Cycles, selected-to-active from the dense mesh: 2048² base colour and tangent-space normal maps, with the concept projected onto front-facing visible texels (registered to the concept by a fitted scale and shift, refined for the head).
- **One material:** `M_Protagonist` with both maps; the UV strip u ≥ 0.97 stays flat in the normal map and carries the hand skin colour (ADR-028). The normal map is imported with `flip_green_channel`.
- **The pipeline scripts stay outside the repository** (ADR-018); [CHARACTER_PROTAGONIST.md](CHARACTER_PROTAGONIST.md) records the steps and checks.

This closes the rasterizer-licence question for this character. It stays open for any future asset baked through TRELLIS's textured export.

**Not covered.** The back and sides have only vertex-colour detail; the concept is a single front view. A hand-authored UV layout, a final hand texture, LODs and owner sign-off on the look.

## ADR-032 — Swimming, held breath and drowning from the original game (accepted 2026-11-10)

**Problem.** The owner asked for the original game's swimming in Marburgum II: its animation, sounds, code and whatever else it needs. The launch map has the Drava, but the engine did not know where the water was, the body could not swim, and vitality never changed (ADR-020). The original (`magna-raedarum-marburgum`) has three things to offer, and they do not all have the same standing:

- **Rules** (`src/server/world_simulation.cpp` `tick_water`, `tick_combat`, `send_to_graveyard`; `src/shared/combat.h`, `protocol.h`). Owner's own code.
- **The swim clip.** Marburgum's own is procedural: `tools/build_character.gd` `_swim_animation`, a 1.2 s cycle keyed as model-space Euler angles on the original Godot humanoid. The better-looking breaststroke in the original's Unreal client, San Andreas DE `Swim_Breast`, is a not-for-shipping GTA placeholder (`docs/PLACEHOLDER_ASSETS.md`).
- **Sounds.** The original has no water or swimming sounds of its own. Its water footsteps and fishing splashes are GTA III DE / San Andreas DE placeholders, and the Godot-era picks were matched back to GTA III files by waveform.

**Decision.**

- **Water is a set of water physics volumes** generated over the Drava in `/Game/World/lent` by a new C++ commandlet (`-run=MarburgumSwimContent -Steps=water`, `MarburgumIIEditorTools`). Every 1 m cell under the level's `Drava` sheet whose ground is at least 5 cm below the sheet is water. The cells are merged into 82 boxes whose tops are the drawn surface (Z = 6 cm), the 65,000 m² of river in the window, down to 1 m under the bed. The sheet and the landscape are unchanged.
- **The original's rule replaces the engine's buoyancy.** `UMarburgumCharacterMovement::PhysSwimming` steps the body as `tick_water` did: holding a direction swims at 1.1 m/s and raises the mouth (1.55 m above the feet) to 15 cm over the surface at 1.2 m/s. Letting go, the body sinks at 0.45 m/s until the mouth is 35 cm under, and never lower than 2.6 m. A fall into the river puts the mouth 40 cm under, and from 45 m the water is lethal. Steering near the surface, the swimmer climbs onto standable ground up to 1.6 m ahead, between 25 cm under and 1 m over the surface. Lent's bank is a slope, so a swimmer usually wades out before that rule is needed. Walking down the bank is wading until the capsule centre is under the surface.
- **Held breath is the stamina bar.** With the mouth under, stamina drains at 100/60 per second. None left, still under, is drowning: all vitality goes. Running, winding and recovery are unchanged from ADR-020. The owning client predicts all of this, and a correction now also carries the down state.
- **Down and recovery.** The original takes a dead player to the graveyard for 60 s. Here the server takes the body to the start tagged `recovery`, or the arrival start where there is none (this map). The body takes no input there, and after 60 s it is alive with full vitality and breath. The wait is saved with the condition (`recovery_s`, only on a down body; living entries keep their schema-3 shape), so leaving does not shorten it.
- **Condition and HUD.** `FMarburgumCondition` adds `bSwimming`, `bSubmerged` and `RecoverySeconds`; vitality 0 is down. The `player_condition` pill shows the most urgent state: DROWNED · N s, HOLDING BREATH, SWIMMING, WINDED, RUNNING.
- **The swim clip is Marburgum's own, carried over key for key.** `-Steps=animation` writes `/Game/Characters/Animations/A_Swim` on the mannequin skeleton. It applies the original model-space turns with the same hierarchy after turning Manny's A-pose limbs onto the original humanoid's rest directions. Read back from the saved clip, every arm and leg direction at the four key times matches the original evaluated on `player_male_a.glb` to within 0.02 (the torso to 0.03, because Manny's spine curves differently). The client plays it through the template `ABP_Unarmed`'s `DefaultSlot`, paced as the original paced it, and lifts the drawn body 20 cm while swimming so the face is out of the water. The mouth rule does not move. A dedicated server never plays it. The San Andreas clip is not imported. (The body and leg keys and the lift are superseded by ADR-033.)
- **No sounds are imported.** None of the original's water sounds is Marburgum's to ship. Swimming is silent until original or licensed sounds exist.
- **Evidence.** `scripts/swim-smoke-linux.sh` runs a dedicated server on `/Game/World/lent`, a rendered swimmer, a remote observer, and a restart.

**Not covered.** Owner playtest. A river current (the original had none for bodies). Underwater camera and post-processing. A dive or plunge from a height, and a bank step that needs the climb-out rule: Lent's slope never reached it in the smoke. Falls onto the ground and fights, the other ways to lose vitality. A drowned or down pose: the body stands at the recovery point. Swim, splash and breath sounds. Wading hysteresis at the volume top, where a body walking on the bed with its centre at the surface can alternate between wading and swimming. Remote bodies' swim clip is checked as playing, not rendered.

## ADR-033 — The swimmer lies along the water (accepted 2026-11-10)

**Problem.** On playtest the owner found that the swimmer looked as if it were drowning: the lower half of the body hung vertical. ADR-032 carried the original's swim cycle over key for key, and that cycle pitches the hips only 49° and swings both thighs 0.35 rad forward together, so the legs hang about 30° off vertical with the shins further under. The original drew the same pose.

**Decision.**

- **A prone pose with a flutter kick replaces the original's body and leg keys** in `-Steps=animation`. The hips pitch 77° (`Hips` X −1.35), the spine arches 3° back up and the upper chest follows the spine, so the chest is at 74°, close to the original's 70°. The original arm stroke is unchanged and still reaches ahead of the head. The head comes back up 0.8 rad so the face looks ahead. The thighs kick between +0.05 and −0.25 rad and the knees bend between 0.1 and 0.4 rad on the up beat, twice a cycle. The other leg is a quarter cycle behind, so the legs alternate; in the original they moved together. The hip sway and bob are the original's.
- **The drawn body is lifted 32 cm while it swims** (`SwimLiftCm`, was 20). Read back from the saved clip, the pelvis is 94–98 cm and the head joint 113–117 cm above the feet. While steering, the surface is 140 cm above the feet, so the back is about 10 cm under the surface and the face is out. The mouth rule and the movement are unchanged.
- **Evidence.** Read back from the saved clip at the four key times, the torso points 17° above horizontal and each leg 13° above or below it, in opposite phase. The commandlet now also logs bone heights. `scripts/swim-smoke-linux.sh editor` passes with the new clip; the rendered swimmer shows the head and shoulders out and the back along the surface. (The drawn legs did not follow the clip until ADR-042.)

**Not covered.** Owner playtest. Treading water (ADR-036). The river surface is opaque, so the captures do not show the legs.

## ADR-034 — Lent buildings are closed shells with straight-skeleton roofs (accepted 2026-11-10)

**Problem.** The `-RebuildFacades` buildings of ADR-027 never closed, and several rounds of fixes did not close them. There were four separate causes:

- **Unwelded triangles.** Every triangle had its own vertices, so no wall shared an edge with its roof.
- **Strip roofs.** Roofs were stacked from the source's LiDAR `roof_sections`. Those are fitted strips, often only 3–5 m wide, so neighbouring strips overlapped or left slots.
- **Reversed winding.** The generator assumed the opposite front-face winding to Unreal's (`FStaticMeshOperations` takes the normal as `(C − A) × (B − A)`), so every single-sided face pointed inward. It had to double each wall and roof slab to be seen at all.
- **New geometry never reached disk.** The generator created a fresh package for each building and called `BuildFromMeshDescriptions`, which only builds render data in memory. Saving fully loaded the existing file into that package, so the old mesh overwrote the new one. A freshly built 10,512-triangle mesh with five material slots read back from disk as the old 3,809-triangle, four-slot mesh. Earlier fixes could therefore look right in the editor session that made them and still be missing from the next load or cook.

**Decision.** `MarburgumBuildingMesher` (plain C++, editor-only) builds each footprint as one closed solid:

- **Shell.** A floor, the walls, a soffit and fascia under the eaves, and a roof.
- **Roof shape.** The roof is the weighted straight skeleton of the eave outline. Every roof face starts on a wall line, so roof and walls meet by construction.
- **Gables and party walls.** Walls shared with a neighbouring footprint (party walls) and the short ends of gabled roofs are edges that do not move inward. Their faces come out vertical and are plaster gables or fire walls.
- **Pitch.** Each roof peaks at its measured LiDAR rise, with the pitch held between 18° and 55°. A block too wide for that is cut flat at the rise.
- **Flat roofs.** A 0.3 m parapet around a bitumen deck (`MI_roof_bitumen`).
- **Openings.** Window frames, glass, sills and doors are separate closed solids set 3 cm into the wall.
- **Vertices and winding.** Positions are welded at 0.1 mm and written in Unreal's winding.

The `roof_sections` strips are no longer used.

The commandlet loads each existing asset and replaces its source mesh in place (`CreateMeshDescription` and `CommitMeshDescription`, then `Build`). It then checks every shell before saving:

- each edge is used exactly once in each direction, with no T-junctions;
- the volume is positive;
- the committed source has the mesher's triangle count and five material slots.

A building that fails is not saved, and the run fails. Other options:

- `-CheckOnly` builds and checks every building without saving anything.
- `-Only=<id>` rebuilds one building.
- `-Inspect=<id>` reports what a saved asset holds.

The automation test `Marburgum.Editor.BuildingMesher` covers a gabled rectangle, a hipped L, a gabled U, a flat block, a skewed clockwise outline and two row houses sharing a wall.

Of 125 placed footprints, 124 are rebuilt: 74 gabled, 30 hipped and 20 flat, 360,090 triangles in total with 6,159 framed windows. Every one passes the check, and the source read back from disk matches. One footprint the cleaner cannot keep simple is removed from the level: `w106076714`, a 4 m sliver cut off at the edge of the source data. The tiny footprints already removed under ADR-027 stay removed.

**Evidence.** The development switch `-MB2ViewShots` switches the running game's camera to fixed views and saves screenshots. Before and after, from the same views of `/Game/World/lent` in the editor build:

- [overview](Captures/lent-buildings/overview.jpg)
- [arrival](Captures/lent-buildings/arrival.jpg)
- [eaves](Captures/lent-buildings/eaves.jpg)
- [rooftops](Captures/lent-buildings/rooftops-near.jpg)
- [street](Captures/lent-buildings/street-up.jpg)
- [south bank](Captures/lent-buildings/southbank.jpg)
- [promenade](Captures/lent-buildings/promenade-street.jpg)

**Not covered.** Dormers, chimneys, cornices, and roof overhangs at gable ends (verges). Real façade detail per building. Lightmap UVs come from Unreal's automatic generation. No packaged build or owner playtest yet (ADR-008).

## Open decisions

- Clear the TRELLIS texture-bake rasterizer licences (nvdiffrast, Inria) before any asset baked through TRELLIS's textured export ships; the protagonist no longer uses it (ADR-031).
- Verify production rights for the locally installed Hunyuan3D-2.1 version, specifically use within Slovenia/EU, generated outputs and subsequent game distribution.
- Public license and rules for distributing source/art; no license has been selected.
- Minimum shipping platforms and tested build matrix.
- Production persistence engine, migrations, account/session admission, login security and service topology; initial Unreal-native replication is accepted for the first vertical slice. The ADR-014 development key and file store are stand-ins, not answers.
- Rust static-library integration across Windows/Linux and ABI test approach.
- Geospatial/historical data provenance and acceptable licensing.
- Run and profile DLSS on a Windows build (ADR-029); whether DLSS is offered on Linux depends on NVIDIA adding Linux support to its plugin.
- A DLSS 5 runtime the project is licensed to use, or an official NVIDIA DLSS 5 package for UE 5.8 (ADR-030).

## ADR-035 — The protagonist's generated back is calmed (accepted 2026-11-10)

**Problem.** The owner found the back of the shirt bothersome: meant to be wrinkly, it read as deep, excessive folds, and the third-person camera shows the back most of the time. The concept (ADR-021) is a single front view, so TRELLIS invents the back. Separating the material showed three sources: deep rolls and a spine groove in the geometry, the same folds painted again as dark vertex colours, and a normal map that added creases plus hard dark blotches where bake rays reached the inside of the hollow cloth.

**Decision.** `retopo_bake.py` calms the back before retopology and in the bake (strength `BACK_CALM`, default 0.8; 0 reproduces ADR-031):

- **Where:** dense-mesh vertices below the neck whose broadly smoothed normal faces away from the concept, on the torso and legs only (per height slice, out to the first empty column, so the A-pose arms keep their shape). The front, head, arms and open borders are untouched.
- **Geometry:** thin-plate fairing of that region with shapes under about 12 cm ironed out, then 20% of the generated relief added back, so the shirt still reads as cloth. The low mesh follows through the voxel shell; a light relax removes any fold-over.
- **Colour:** the vertex-colour luminance is pulled to 20% of its deviation from an edge-aware local mean (shirt and trousers do not bleed), which lifts the painted fold shadows without changing the hue.
- **Normal map:** back texels keep 60% of their tilt; texels tilted more than about 60° are flat.
- **Concept projection guard:** below the head, a concept texel whose colour clashes with the dense mesh's own colour there is not projected; the stronger back calm otherwise let the arm drawn beside the flank land on the shirt.

**Not covered.** The back is still invented, now quieter, not drawn; a back-view concept or hand sculpt would replace it. Owner sign-off on the look in game.

## ADR-036 — A swimmer that is not moving treads water upright (accepted 2026-11-10)

**Problem.** The owner asked for an idle swimmer to be vertical and a moving one horizontal. After ADR-033 every swimmer lay along the water, including one that had let go and was slipping under. The original had no idle swim pose.

**Decision.**

- **A second generated clip, `/Game/Characters/Animations/A_SwimTread`**, is written by `-Steps=animation` with the stroke and in the same way: model-space turns on the original humanoid's hierarchy, carried onto the mannequin. It is Marburgum II's own, a 1.6 s cycle. The body is upright and the chest 8° forward. The legs pedal in turn: each thigh rises with the knee bent, then presses down as the knee straightens. The forearms scull out and in before the chest, under the surface. Each leg's push lifts the body 2 cm.
- **The client chooses the clip by the body's horizontal speed.** At or above 40 cm/s the swimmer strokes; under 20 cm/s it treads water; between the two it keeps what it was doing, so a swimmer that is slowing down does not flick between them. A new swimmer under 40 cm/s starts treading. The visible velocity decides, so a remote body chooses as its owner does. The two clips blend into each other over 0.45 s in `DefaultSlot`.
- **Treading water is not lifted.** Read back from the saved clip, the head joint is 161–164 cm above the feet, as it is standing, so the mouth is where the movement's mouth rule has it. The 32 cm stroke lift (ADR-033) eases off at 60 cm/s as the swimmer turns upright. The sinking rule and the server's rules are unchanged: a swimmer that lets go still slips under, now upright.
- **A stopped swimmer reports that it has stopped.** `PhysSwimming` kept the previous velocity whenever the engine had pushed the capsule out of a penetration (`bJustTeleported`). A swimmer that had let go was pushed like that every frame, so it went on reporting its last stroke, 1.1 m/s, on its own client and to everyone it was replicated to. On those frames the velocity is now the move that was asked for, the stroke plus the rise or sink. Otherwise it is still the distance the body moved.
- **Evidence.** Read back from the saved clip at quarter cycles, the torso points 8° forward of vertical and each leg within 6° of straight down. `scripts/swim-smoke-linux.sh editor` now requires the stroke, not treading, while swimming at speed, treading half a second after letting go, and still treading under. It captures `swim-w-treading`. It passes both phases in editor mode.

**Not covered.** Owner playtest. Treading water does not hold the swimmer up: under the original's rule only steering does. Remote bodies are checked as playing a swim clip, not which one. What a stopped swimmer's capsule overlaps every frame is not established; only its effect on the velocity is handled.

## ADR-037 — The protagonist's UV islands do not overlap (accepted 2026-11-10)

**Problem.** In the ADR-035 build the owner saw a skin-coloured "hole" in the shirt on his left side, under the arm. The dense mesh is dark shirt there. Smart UV Project had folded the tight crease between the flank and the inner arm into one island that overlapped itself: 133 faces shared texels with faces far away in 3D, so the bake wrote the flank and then the arm's skin into the same texels and the arm won.

**Decision.** `retopo_bake.py` checks the unwrap for overlap and repairs it before baking:

- **UV overlap:** faces that `select_overlap` reports, with a ring of their neighbours, are projected again as their own islands at a tighter angle limit (40° down to 15° over up to six passes). All islands are then scaled to the same texel density and packed again; without that the re-projected armpit islands take the space and the face loses half of its concept texels. The baked layout has 0 overlapping texels at 1024².
- **Wrong-surface guard:** after the colour bake, each texel is compared with the dense surface nearest to it that faces the same way. Where the bake disagrees (a ray reached the arm beside the flank instead of the shirt), the texel takes that surface's colour and the normal map is flat there.

**Evidence.** Blender flank renders before and after; `uv_overlap` 0 texels; the concept still projects onto 694k texels (513k in ADR-035), so the face is as sharp as before. The rig, FBX and UE import checks give the same results as ADR-035, and the editor loads all assets under `/Game/Characters`.

**Not covered.** Owner playtest of the flank in game.

## ADR-038 — The protagonist's material is matte (accepted 2026-11-10)

**Problem.** In game the protagonist's shirt, jeans and skin had a grey, plastic sheen. `M_Protagonist` is created by the legacy FBX importer, which wires base colour and normal but not Blender's roughness (0.8 in `retopo_bake.py`), so roughness, specular and metallic were unconnected and fell back to UE's defaults (roughness 0.5, specular 0.5).

**Decision.** `fix_protagonist_material.py` runs after every import and connects constants: roughness 0.85, specular 0.25, metallic 0. One value covers cloth and skin because the character is a single material with no roughness map; the base colour still carries the lighting TRELLIS baked into its vertex colours, so light streaks on the jeans remain and are not specular.

**Evidence.** Before the change the material's metallic, specular and roughness inputs were unconnected; after it they are the constants above. The packaged client in the condition smoke (PASS) on the Lent promenade, before and after: `Docs/Captures/character-protagonist/gloss_before_after.jpg`.

**Not covered.** Owner playtest of the look in game; a separate roughness value or map for skin.

## ADR-039 — Cars, driving and the prototype hatchback from the original game (accepted 2026-11-10)

**Problem.** The owner asked for the original game's car driving, its prototype hatchback and everything that goes with them in Marburgum II, adapted to it. The original (`magna-raedarum-marburgum`) has a driveable car in two forms: the Godot-era game (`src/shared/vehicle.cpp`, `vehicle_mesh.cpp`, `project/data/vehicles.json`, `vehicle_instances.json`) and its Unreal port (`UnrealMarburgum`, `MarburgumVehicleRules.cpp`, `SM_city_hatchback_a`). They do not all have the same standing:

- **Rules and data.** A rigid body on four suspension raycasts with spring, damper, tyre and engine forces; five catalogue cars; seats with doors; the driving keys and the driving camera. Owner's own code.
- **The hatchback mesh.** The original builds every car procedurally from a side-elevation profile in `vehicle_mesh.cpp`. The Unreal port's `SM_city_hatchback_a` is that mesh exported, so the generator is the source.
- **Clips and sounds.** The original's own enter/exit clips are procedural (`tools/build_character.gd` `_vehicle_animation`). The Unreal port's better-looking door clips are GTA III / San Andreas DE placeholders, and its engine sound banks are San Andreas DE's (`docs/PLACEHOLDER_ASSETS.md`, `MarburgumEngineSounds.cpp`).
- **Protocol.** The original drives through its own UDP protocol with client-side vehicle input packets; Marburgum II uses Unreal replication behind the player controller's RPC boundary (ADR-003, ADR-023).

**Decision.**

- **The catalogue is data, copied field for field.** `Data/Gameplay/vehicles.v0.1.json` carries the original five definitions (city hatchback, saloon, estate, van, truck) and the authored `lent_hatchback` instance. `MarburgumIIShared/Public/MarburgumVehicleContract.h` mirrors them in the original's units and Godot axes, with the conversions to Unreal centimetres and axes in that header only. `mb2 validate vehicles` keeps the two in step. The prototype is `city_hatchback_a`, the Renault 4 GTL of the original.
- **The handling is the original's, as pure functions.** `MarburgumVehicleDynamics.h` ports `Vehicle::_integrate_forces` and `apply_wheel` through the Unreal port's rules: suspension rays, load-limited lateral grip, front-wheel drive fading toward top speed, a brake that never reverses a wheel, the handbrake freeing the rear, drag and rolling resistance, the idle hold, river buoyancy and drag, crash detection, and the surface response (neutral until Lent has physical-surface semantics; no weather yet, so dry). `Marburgum.Vehicles.Dynamics` tests them.
- **One addition: a stopped car holds on a slope.** The original's brake and tyre are viscous, so a parked car settles at the speed where they balance gravity; the first smoke found the Lent hatchback sliding sideways at about 3.5 cm/s on a 1.4° cross-fall. Off the throttle, all but stopped and braking (the idle hold included), each wheel now applies the force that stops its share of the car within the step, the slope's pull included, up to its brake along the wheel and its tyre limit across it (`HoldForce`). Moving, the original's forces are unchanged.
- **Only the server simulates.** `AMarburgumVehicle` (game module) is a replicated actor whose root is the collision box, a rigid body only on the server, stepped every frame from the driver's held intent. The intent expires after 0.4 s, so a dropped driver leaves a braking car. Clients never simulate: the car is kinematic, blocks walkers, and slides toward the server's `Motion` (location, rotation, speed, steering, wheels on ground and in water, impact count) at 30 Hz, as the original's frozen client car did. Engine movement replication is off.
- **Seats are server state behind the controller's RPCs.** `ServerEnterVehicle`, `ServerExitVehicle` (reliable) and `ServerSetVehicleInput` (unreliable, resent while held, every 0.1 s) are the only ways in. The server checks range from the seat's door (the original's 3.5 m), a free seat, a body that can move (a drowned one cannot), and finds a clear spot to step out; refusals have wire names (`seat_taken`, `too_far`, `incapacitated`, `already_seated`, `no_seat`, `not_seated`, `no_room`). A seated character is attached to the car with its movement and collision off; `FMarburgumSeating` replicates to every machine, which applies the same state. Walking prediction is reset on both ends at every seat change, so no move saved before the seat is replayed after it (the smoke caught an owning client snapping back to where it got in). A player who leaves the world frees the seat.
- **Controls are the original's.** E gets in (the driver's seat whenever its door is in reach, else the nearest free seat) and out. W/S are throttle and one brake-then-reverse pedal, A/D the wheel, Shift the handbrake. The `interaction_prompt` offers the car before other interactables and names the controls while seated; a new `vehicle_status` card shows the car, the seat and the server's speed in km/h, with REV when reversing. The camera pulls back to 11 m and swings behind the car unless the player looked around in the last 2.5 s.
- **The cars are the original's own procedural bodywork.** A new commandlet (`-run=MarburgumVehicleContent`, `MarburgumIIEditorTools`) ports `vehicle_mesh.cpp` line for line into static meshes for all five definitions, wound to Unreal's front face: body with door hinge sockets, four wheels centred on their hubs, side doors that swing open while someone gets in or out, paint through `M_Vehicle` and one instance per car. The original's procedural enter and exit clips go onto the mannequin skeleton through the swim commandlet's Godot-to-mannequin retargeting, lifted into `MarburgumGodotClip` (the swim commandlet, reworked by ADR-033 and ADR-036, keeps its own copy for now). The body plays the enter clip at the door, is hidden in its seat as the original did, and plays the exit clip where it steps out.
- **The Lent car stands where the original authored it.** The original `LentHatchback` position is clear, level ground in `/Game/World/lent`, about 160 m west of the arrival start, so the commandlet places it there; a `lent_hatchback` player start (`?MB2Start=lent_hatchback`) stands beside its driver's door.
- **Nothing of Rockstar's is imported.** The GTA door clips and the San Andreas engine sounds stay out; cars are silent until original or licensed sounds exist.
- **Evidence.** `scripts/vehicle-smoke-linux.sh` runs a dedicated server on `/Game/World/lent`, a rendered driver and a passenger.

**Not covered.** Owner playtest. Engine, tyre and door sounds. Saving cars across a restart (the authored car returns to its spot), ownership, keys and fuel. Ambient traffic, parked-car promotion and carjacking, which the original had as later work. Damage, which the original's Unreal port started. Client-side prediction of the driver's own car: the driver sees the server's car about one round trip late, as in the original. Physical-surface semantics and weather for the surface response. Remote players' bodies in a seat are hidden, not seated in a sitting pose.

## ADR-040 — Lent's ground is rebuilt from the survey, with paved streets you stand on (accepted 2026-11-11)

**Problem.** The owner's playtest of ADR-034 found the buildings right and the ground wrong. Three things were wrong with it:

- **You walked through the roads.** The roads were one flat sheet (`Roads_lent`, 9,966 triangles) with collision switched off. The character stood on the landscape underneath it, which was sometimes above the road and sometimes below it.
- **The ground was a photograph.** One 2048² aerial photo (GURS DOF025, 2020–22) covered the 512 m landscape, which made 25 cm per pixel smeared over 0.9 m quads. It had the shadows of trees, buildings and cars printed into it, and it showed the city in 2020.
- **The ground had junk in it.** The landscape had been built from survey points classed as ground. Under Stari most that included the bridge deck, which left mounds by the ramp, and gaps elsewhere had been filled with lumps.

**Decision.** A new Rust tool, `tools/lent-ground`, builds the ground from source data. A new commandlet, `-run=MarburgumRebuildGround`, puts it in the level.

- **Shape.** The heights come from the ARSO 2014 LiDAR tile TM_549_157 (ground class, 1.17 M points), resampled at 0.5 m (1017² points, 64 components of 2 × 63 quads).
  - Under Stari most and the footbridge the survey classed the deck as ground and saw almost nothing beneath it. Every return inside a bridge outline is dropped, and each row is filled across from the survey on either side.
  - Raised blobs narrower than about 4 m are removed by a grey-scale opening, and the rest of the ground is filled smoothly.
  - The Drava bed is shaped from the shore: 0.4 m deep at the edge, falling 1 in 6 to 2.6 m.
- **Streets.** Streets come from OpenStreetMap (an Overpass extract committed as `Data/World/lent_ground/osm_lent.json.gz`, ODbL).
  - Carriageways are stroked to their mapped width, or else by lanes and class, and joined with 4 m kerb radii at junctions.
  - Pavements combine mapped footways, sidewalk tags, pedestrian areas, a 0.6 m band round every carriageway, the strip in front of houses, and a 1.5 m apron round every building.
  - Surfaces: asphalt carriageways, older asphalt for car parks, concrete pavers for pavements, and granite setts for squares and pedestrian streets.
- **Paved meshes.** The paved surfaces are separate static meshes (56 chunks of 64 m, 650k triangles) whose triangles are the collision (complex as simple). You stand on what you see.
  - Kerbs are 12 cm, dropping to 3 cm at the 40 mapped crossings.
  - Kerb faces run down to the road, and an edging runs 35 cm under the landscape.
  - Marked crossings have zebra stripes, and two-way main roads a dashed centre line.
  - No paved triangle is longer than 1.5 m, so none can bridge a dip.
- **Landscape and paving.** Under the paving, the landscape sits 15 cm below the drawn triangles, and next to a paved edge it is kept 5 cm below the lowest paving beside it. Where paving meets a wall it rises to cover the wall's bottom edge.
- **Materials.** Every texture is a CC0 set from ambientCG (`SourceArt/Textures/Ground/README.md`). The landscape material (`M_LentTerrain`) blends old asphalt, earth, gravel, grass and river stones by a surface mask, with noise on the edges and a large brightness variation.
  - The mask comes from OSM land use, the Drava's shore, slope (unmapped banks over about 20° are grass) and the orthophoto. The photo is used only to tell green from grey; it is no longer drawn.
  - Ground steeper than about 45° is drawn as a stone retaining wall.
  - The paved meshes use `M_LentPaved` instances with the same anti-repeat variation. Roughness is remapped per surface so dry ground is not glossy.
- **Junk removed.** `Roads_lent` and the photo landscape are removed. The player starts, the angler and the street signs keep their height above whatever they stood on. The bridge start, which sat under Stari most's deck, goes on top of it.
- **Rebuild order.** After the ground, `-run=MarburgumSwimContent -Steps=water` re-fits the river volumes to the new bed, and `-run=MarburgumVehicleContent -Steps=place` re-parks the car on it.
- **Parking (ADR-039).** The car's spot check now sees the paved meshes' collision. Where the level has paved streets, all four wheels must stand on carriageway asphalt and differ by at most 5 cm. The original spot straddles the promenade kerb, so the car parks 1 m from it on the carriageway, facing the original's way. Before this, the check traced the landscape under the paving, and the car stood half on the kerb, leaning 5.8°.

**Evidence.** `-run=MarburgumRebuildGround -Verify` checks every walkable paved triangle in the saved level two ways:

- A ray from above must hit that paving at its height, never the landscape.
- A character-sized capsule (34 × 88 cm) dropped onto it must not end up inside it.

All 476,517 pass: 0 landscape hits, 0 height mismatches over 1 cm, 0 capsules sunk. 1,292 samples hit something else first, a bridge deck or an eave. The Lent, swimming, condition, interaction and vehicle smokes pass in editor mode, and so do the six automation tests. The landscape reads back within 4 mm of the data. The tool has unit tests for the projection, distance transform, in-painting, polygon operations, triangulation and winding (`cargo test --manifest-path tools/lent-ground/Cargo.toml`). Before and after captures are in [Captures/lent-ground](Captures/lent-ground/).

**Not covered.** Street furniture, trees, decals, manholes and the stone quay wall along the Drava (recommendation 4). Lane markings beyond centre lines and zebras. OSM shows the streets as mapped in 2026; 1990s corrections are still to do. The paved meshes have no LODs and no Nanite. Owner playtest.

## ADR-041 — Unarmed fighting and the Lent fight cast from the original game (accepted 2026-11-11)

**Problem.** The owner asked for the original game's combat, with its sounds, animations and everything related, in Marburgum II, adapted to it. The original (`magna-raedarum-marburgum`) has unarmed fighting in two forms, and they do not all have the same standing:

- **Rules and people.** `src/shared/combat.h` (numbers and wire words) and the fight rules of `src/server/world_simulation.cpp` (`attack`, `set_guard`, `apply_damage`, `melee_target`, `tick_combat`): a jab and a committed swing, a held guard, stamina costs, cooldowns, stagger, reach and arc, and NPCs that answer violence (a hostile fights only whoever hit it; a civilian steps away); a fight ends at incapacitated, never death. The fighting cast is three people of `project/data/npcs.json` at Lent: the Belligerent drinker, the Evening walker and the Newspaper reader. Owner's own work.
- **Clips.** The guard loop, jab and committed swing are the original's own procedural clips (`tools/build_character.gd` `_guard_animation`, `_fight_animation`). Its hit-reaction and get-up clips, and every fight sound (blows, body falls), are GTA III / San Andreas DE placeholders (`docs/PLACEHOLDER_ASSETS.md`).
- **The Unreal port** (`UnrealMarburgum`) mirrors the same rules, and adds San Andreas' wanted level and police pursuit (`MarburgumWanted`, reverse-engineered from GTA San Andreas), which `Data/Ontology/ui_hud.v0.1.json` already excludes.
- **Protocol.** The original asks for a swing with a command and a guard with a repeated message that expires; Marburgum II's player moves through the engine's predicted, server-checked character movement (ADR-019, ADR-020).

**Decision.**

- **The rules and the people are data, copied unchanged.** `Data/Gameplay/combat.v0.1.json` and `MarburgumIIShared/Public/MarburgumCombatContract.h` carry the original's numbers (jab 7, heavy ×2.1, guard takes a quarter for 14 stamina, reach 2 m, arc 75°, jab 9 / heavy 22 stamina, guard drain 3.5/s, cooldowns 350/900 ms, stagger 200/650 ms, halved when guarded), the NPC behaviour constants and the three people with their positions, facings, vitality, stamina, reach, attack interval, pursuit range and `npc_visual_profiles.json` colours. The rules are pure functions in the contract (`SwingRefusal`, `InSwingReach`, `PayForGuard`, `ResolveBlow`, `DecideHostile`, the wander schedule seeded by Godot's `String::hash`). `mb2 validate combat` keeps data and contract in step and fails on a stun-lock (a stagger outlasting its cooldown) or any wanted/police vocabulary; `Marburgum.Gameplay.CombatRules` tests the rules.
- **The player fights through its own movement.** F throws a jab, R a committed swing and G holds a guard, the original's keys. The guard is a held move flag (`FLAG_Custom_1`), so every machine that simulates a move drains the same stamina. A swing is a request count in every move's data, thrown by the first move simulated with a new count: a one-move flag was lost whenever the engine skipped a move whose time came out zero, which the first smokes caught at high client frame rates. Swinging costs its stamina and starts its cooldown in the predicted simulation; corrections carry the cooldown and the handled count. The server alone picks the target (the nearest standing fighter in reach and arc off the body's facing; players never fight players), applies the blow and answers the player with a `FMarburgumCombatResult`; the notification feed shows the original's `on_combat` lines, plus one for a blow that floors somebody. Being hit by an NPC goes through `ReceiveBlowAuthority`: a held guard is paid for or breaks, vitality falls, a stagger refuses the next swing, and a body with none left is down (`beaten`), taken to the recovery point and back after `RecoverySeconds`, as a drowned one is (ADR-032). No vitality regenerates otherwise, as in the original.
- **The fighting NPCs are characters, spawned by the game mode.** `AMarburgumFighter` is a replicated mannequin character tinted with the person's colours, moved by the engine's character movement and run on the server by the original's NPC tick at 10 Hz: give up on a target that is down or past the pursuit range, stagger, close in (sidestepping when a step is refused), step back out of the other body, strike every attack interval and keep the hands up between blows; a civilian steps away from a fighting hostile, no further from home than the avoid limit; otherwise watch a nearby player, go home, or stroll around home on the original's schedule. On Lent the server spawns the three at their original positions on the ground found there, with a `fight_<id>` player start in front of each, so the map is not edited. Today's static talk NPCs are unchanged.
- **One addition: a downed NPC gets up.** The original's NPCs stay down until its server restarts; a persistent world never restarts, so after 60 s (a downed player's wait) the NPC gets up at home with full vitality and nobody to fight.
- **Clips are the original's, sounds are CC0.** A new commandlet (`-run=MarburgumCombatContent`, `scripts/create-combat-content.sh`) carries the guard loop, jab and committed swing onto the mannequin through `MarburgumGodotClip`, which now samples one-shots at their own uneven key times as Godot's cubic track does. Swings are one-shots in `DefaultSlot`; the guard loops only while the body stands still with its hands up, as the original shows; a body on the floor plays the template mannequin's `MM_Death_Back_01` and holds its last frame until it gets up. Blows sound from Kenney Impact Sounds 1.0 (CC0; `SourceArt/Audio/Combat/README.md`): a jab, a committed swing, a blocked blow and a body going down each pick one of five impacts; a miss is silent.
- **Nothing of Rockstar's or San Andreas' is imported.** The GTA hit/get-up clips and fight sounds stay out, and so do the wanted level and police.
- **Evidence.** `scripts/combat-smoke-linux.sh` runs a dedicated server on `/Game/World/lent`, a client that is beaten down and a rendered client that fights the drinker to the floor ([evidence](STAGE1_COMBAT_EVIDENCE.md)).

**Not covered.** Owner playtest. Fighting other players (the original had none). Weapons, the Tabor walkers (no Tabor map), perception (NPCs noticing a fight at a distance), arrests and any consequence of violence beyond the fight itself; hit reactions on the struck body (the original's were placeholders); NPC navigation meshes (fighters steer straight and sidestep, they do not path round buildings); saving NPC condition across a server restart.

## ADR-042 — Generated clips key the IK foot bones (accepted 2026-11-11)

**Problem.** In the owner's playtest the swimmer still looked as if it were drowning, after ADR-033 and ADR-036. The clips were right: read back from the saved assets, A_Swim lies along the water. The drawn pose was not. `ABP_Unarmed` runs the template's foot IK, `CR_Mannequin_FootIK`, after `DefaultSlot`, and it stays on in every movement mode except falling. It pulls each foot to `ik_foot_l` and `ik_foot_r`. The generated clips never keyed those bones, so they stayed at the reference pose, under a standing body. Measured in the running game, the stroke's feet were 85 cm under the pelvis, both at the IK bones' height. With the river sheet hidden, the swimmer was bent at the waist over straight, standing legs. The opaque river had hidden that from every capture, and the evidence checked the clip asset, not the drawn pose.

**Decision.**

- **Generated clips key `ik_foot_l` and `ik_foot_r` onto the feet they draw.** `MarburgumGodotClip::FollowFeetWithIkBones` computes each frame's component-space feet from the clip's keys and writes the IK bones, under the unmoved `ik_foot_root`, to the same transforms. The foot IK then holds the feet where the clip put them. `A_Swim` and `A_SwimTread` are regenerated with it. The animation blueprint and the foot IK are unchanged, so walking keeps its foot placement.
- **The swim smoke checks the drawn pose.** It logs pelvis, head, feet and hand heights above the capsule's feet at every event, read from the mesh after the whole animation graph. It requires the stroke's feet within 40 cm of the pelvis and treading water's more than 60 cm under it. It also captures `swim-w-swimming-xray` and `swim-w-treading-xray` with the river sheet hidden.

**Not covered.** The vehicle door clips and the fight clips (ADR-039, ADR-041) come from the same builder without the IK bones. Their feet are pulled to a standing stance too; they need `FollowFeetWithIkBones` in `MarburgumGodotClip::Build` and regenerating (done in ADR-043). The foot IK's ground trace still runs while swimming: in shallow water it could reach the bed. Owner playtest.

## ADR-043 — The door and fight clips key the IK foot bones too (accepted 2026-11-11)

**Problem.** ADR-042 keyed `ik_foot_l` and `ik_foot_r` in the swim clips only. `MarburgumGodotClip::Build`, which writes the vehicle door clips (ADR-039) and the fight clips (ADR-041), still wrote the twelve mapped bones and left the IK bones at the reference pose. `ABP_Unarmed`'s foot IK then held those clips' feet at a standing stance. Measured in the running game with the old clips, the feet never got ahead of the pelvis while the body ducked into the car and sat down, or while it stood up again (at most 0 cm; the clips put them 52–54 cm ahead). The committed swing's step through never showed: the drawn right foot was at most 0 cm ahead of the left, where the clip puts it 50 cm ahead of the pelvis. The entering capture shows the body ducking over straight, standing legs.

**Decision.**

- **`MarburgumGodotClip::Build` keys the IK foot bones.** It calls `FollowFeetWithIkBones` before writing the tracks, and writes a track for every bone it holds keys for, so the two IK bones are written with the twelve. `FollowFeetWithIkBones` also logs how far each drawn foot gets ahead of the pelvis and above the ground over the clip. `A_EnterVehicle`, `A_ExitVehicle` (`scripts/create-vehicle-content.sh animation`), `A_FightGuard`, `A_FightJab` and `A_FightHeavy` (`scripts/create-combat-content.sh animation`) are regenerated; each now has 14 tracks. Their keys, limb directions and lengths are unchanged. The animation blueprint and the foot IK are unchanged.
- **The vehicle and fight smokes check the drawn pose.** They read the mesh's bones in component space after the whole animation graph, and log pelvis and foot heights with every report. While the enter clip plays and for a second after getting out, the driver's feet must get more than 25 cm ahead of the pelvis. While its committed swing plays, the fighter's right foot must get more than 20 cm ahead of the left. The driver also captures `vehicle-d-entering` once the body ducks in.
- **Evidence.** `scripts/vehicle-smoke-linux.sh editor` and `scripts/combat-smoke-linux.sh editor` (ports 27431 and 27432) failed only these new checks with the old clips (runs `20261111T150624Z`, `20261111T150719Z`) and pass with the regenerated ones (`20261111T151211Z`, `20261111T151010Z`): feet 52 cm ahead getting in and 53 cm getting out, a 53 cm stride in the committed swing ([vehicle](STAGE1_VEHICLE_EVIDENCE.md), [fighting](STAGE1_COMBAT_EVIDENCE.md)).

**Not covered.** Staged (packaged) runs; the vehicle and fight evidence pages still describe the packaged runs before this change. The jab's short step and the guard's bent knees are not checked in game. The drinker and the other NPCs play the same clips but their drawn legs are not measured. `MM_Death_Back_01`, the fall, is the template's own clip and is not regenerated. The passenger's door clips run without rendering, so only the driver's are measured. Owner playtest.

## ADR-044 — Street people from the original game (accepted 2026-11-11)

**Problem.** The owner asked for the original game's NPC logic in Marburgum II. NPCs should appear on the streets, walk around, and react to fights, to someone being run over, and so on. The original (`magna-raedarum-marburgum`) has this in its Unreal port's ambient population (`UnrealMarburgum/Source/MarburgumServer/Private/MarburgumAmbientPopulation.cpp`, `MarburgumRunOver`, `docs/AMBIENT_STREETS.md`, `project/data/population_groups.json` and `npc_visual_profiles.json`). The newest work sits on `main` and on the unmerged branches `feat/ambient-react-to-bodies` and `feat/ambient-panic-spread`. Its parts do not all have the same standing:

- **The owner's own work:** the population groups (archetypes, walk speeds, personality mixes), the looks, the old-town day mix, the reaction tables for being hit, being bumped and seeing a body, and the distances for fight clearance, watch standoff, crowd spread and noticing a body. Also the run-over rules: strike speed, margin, damage per m/s and down time.
- **Taken from Rockstar's games:** San Andreas' shipped decision weights for a fight (`R_Norm`/`R_Tough`/`R_Weak`), carjacking and ramming; the spawn and cull bands and the 40 m fight-notice radius read out of its executable; GTA III's 45-degree wander headings, signal clock and car following; and the crime points and pursuit.
- **The walk graph** came from `tools/build_road_graph.py`. It placed sidewalks a fixed 1.6 m beyond the carriageway and trusted the map. Marburgum II's Lent has its own paved meshes (ADR-040), so lines placed that way would cross walls and kerbs.

**Decision.**

- **The rules, groups and tables are data.** `Data/Gameplay/street.v0.1.json` and `MarburgumIIShared/Public/MarburgumStreetContract.h` carry the owner's tables and groups unchanged. The JSON's `provenance` block lists what was copied unchanged, what is Marburgum II's own and what is excluded. `mb2 validate street` keeps the two in step and checks the staged walk graph. `Marburgum.Gameplay.StreetRules` tests the decisions.
- **Nothing of Rockstar's is imported, as in ADR-041.** Each Rockstar number is replaced with Marburgum II's own, and the contract says so beside it. The new numbers: a fight table and a run-over table (the original had none), 20 people per player (48 at most), spawn and cull bands, a 30 m fight-notice radius, and flee, watch and dodge timings. Crime, pursuit, traffic, carjacking, attractors and time-of-day tables stay out.
- **The walk graph is built from Lent's pavement.** `tools/lent-ground --walk-graph` (`walk.rs`) builds lines along mapped footways and pavement-fitted sidewalks, joined at mapped crossings and across corners. Every line keeps 0.5 m clear of walls, the carriageway, the river and grass. The graph is deterministic: 1582 nodes, about 9 km. It goes to `Content/Marburgum/Street/lent_walk_graph.json`, staged as a loose file (`DirectoriesToAlwaysStageAsUFS`). The server finds the ground under every node when it loads the graph, and drops nodes and edges with no ground or no room.
- **The population is a server world subsystem.** `UMarburgumStreetSubsystem` runs on any map that has a walk graph. It keeps 20 people round each player, up to 4 new ones a second. They appear out of sight: 22–27 m away outside the player's 60° view, or 40–55 m away inside it, at least 15° in from the edge of the view. A person stays while within 35 m of a player, and up to 65 m while a player has seen them in the last 5 s. A body stays until nobody is within 30 m for 60 s and nobody sees it. The subsystem also keeps the incidents people answer: fights (every player and fighter swing reports one), run-overs and people on the ground. It is off with `-MB2Street=0` or `MB2_STREET=0`.
- **People are fighters who walk.** `AMarburgumPedestrian` subclasses ADR-041's `AMarburgumFighter`, so a person who fights back uses the same rules, clips and sounds. On the server, ten times a second, people follow the graph's edges at their group's pace. They keep 0.3 m right of the line, and at a branch they prefer the way closest to their own heading, which drifts slowly. An incident within reach is answered from the personality's table: flee along the graph, watch from a standoff, walk over and stand by a body, or carry on. Nobody walks into a fight. Panic spreads: someone fleeing or watching a fight or run-over alarms the people within 12 m, one hop a tick. A player walking into a person, or hitting them, is answered the same way. Those who fight back become hostile to that player.
- **Cars strike people.** Every frame, a car moving at 2.5 m/s or more strikes anyone standing inside its body. Its sweep runs back over the frame and a little ahead. A street person or a member of the Lent cast takes 10 vitality per m/s, goes down and gets up after 4 s where they lie, or is out. A person sees a car that will pass within 2.5 m in the next 1.5 s and jumps 3.5 m aside. Their personality decides whether they can. `AMarburgumFighter` gained `KnockDownAuthority`. Its fall also stops blocking pawns and vehicles, so a car rolls over a body rather than stopping dead.
- **Looks are the original's profiles on the mannequin:** colours and scale, with small variation within a look, replicated as indices.
- **Evidence.** `scripts/street-smoke-linux.sh` runs a server on `/Game/World/lent` with `-MB2StreetProbe` and a rendered client. The client walks, swings at the air and drives the hatchback. The probe puts one person who will not jump clear and one who will in the car's way ([evidence](STAGE1_STREET_EVIDENCE.md)). The first runs found two spawn bugs, both fixed:
  - A person placed out of view was culled in the same tick, before anyone could have seen them. New people are now counted as seen when they appear.
  - An "in view" spawn point could snap to a node the player could not see, and an "out of view" one could land at the edge of the kept band. Either way the person was gone 5 s later, and the spot was refilled in a loop. A snapped node must now lie on the side of the view it was picked for (in view: 15° inside the edge), and an out-of-view one at least 8 m inside the kept band. Spawns in the run fell from 65 to 31.

**Not covered.** An owner playtest and a staged (packaged) run. More than one player. Gunshots and weapons (the original's sidearm and gunshot reactions on `feat/ambient-react-to-bodies`). Traffic, time of day, attractors and everyday activities (`feat/ambient-everyday-activities`; there is no world clock yet). Factions: the tables' friend and enemy columns are left out. Path-finding round obstacles off the graph: a person cut off from the graph is lost, and goes once nobody sees them. In the game, the graph's largest connected network is 907 of the 1543 placed nodes, so some pavements are islands. Sounds for the people: no screams or chatter. Saving people across a restart (they are ambient, so none is intended).

## ADR-045 — Lent and its eight neighbouring windows (accepted 2026-11-11)

**Problem.** The owner asked for a wider area than Lent: if Lent is a square, import the eight squares of the same size round it. The original game already cuts Maribor into 512 m windows on one grid round the middle of Stari most, and all eight of Lent's neighbours exist there:

| | west | middle | east |
|---|---|---|---|
| north | ljudski_vrt | center | avtobusna |
| middle | pristan | **lent** | titov_most |
| south | kurilnica | magdalena | pobreska |

Bringing in the eight original window levels as they are would have brought their photo landscapes, flat road sheets and strip roofs back, the faults that ADR-034 and ADR-040 removed from Lent. It would also have left a seam at every window edge, with nine separately surveyed landscapes and buildings cut in two where an edge crosses them.

**Decision.** `/Game/World/lent` stays the one launch level. It now covers the 1,536 m square (plan ±768 m), and the eight neighbours go through the same pipeline as Lent:

- **Buildings.** `lent-ground merge-buildings` reads the nine windows' `<window>_buildings.json` and writes `Data/World/lent_ground/buildings.json` (ODbL). The original builds a footprint that crosses a window edge once in each window it reaches. The parts of one OpenStreetMap id are joined back into one outline in a single overlay. Polygons cleaned one by one are snapped to their own grids, and their shared edges would no longer meet.
  - Heights: eaves and ridge are the parts' heights weighted by their roof returns. The roof shape and pitch are the largest part's. The ground is the lowest part's.
  - 65 buildings are joined.
  - Three parts would close round a courtyard. The mesher has no holes, so these stay as separate parts at a party wall, with the joined heights.
  - Two pieces that do not touch stay separate buildings.
  - `-run=MarburgumRebuildFacades` reads that file. It builds every footprint as a closed shell (ADR-034) and places the ones the level lacks: meshes in world coordinates, actors at the origin, labelled by id, filed under `Buildings/<window>`. It then writes `placed_buildings.txt`. A building already in Lent keeps its asset under `Buildings/lent`; a new one goes under the window it mostly stands in. Of 932 footprints, 870 are built, and all 870 are closed with positive volume: 454 gabled, 162 hipped and 254 flat, 3.03 M triangles. 751 are new. The 62 dropped are under 30 m² or slivers.
- **Bridges and river.** The same pass places the neighbours' crossings from the original game, `pristan_studenska` (Studenška brv), `titov_most_titov` and `titov_most_zelezniski` (the railway bridge), at the origin each import recorded in its package metadata. It also places the Drava sheets of pristan and titov_most as `Drava_pristan` and `Drava_titov_most`, with the collision and material of Lent's own `Drava`. The railway bridge brings `MI_railway_ballast` and `M_Ground` from the original. `-run=MarburgumSwimContent -Steps=water` now fills every `Drava` sheet with water volumes: 407 volumes over 200,632 cells, where Lent alone had 91.
- **Ground.** `tools/lent-ground` takes the extent from the command line, defaulting to the square: 3025² points, 24 × 24 components of 2 × 63 quads, 0.508 m. Unreal builds a landscape from whole components only. Lent's 1017-point grid was 8.07 components wide, so its east and south 4 m were cut off; the new size is exact.
  - Heights: the nine LiDAR tiles TM_548..550_156..158 (27.5 M returns in the square, 11.7 M ground).
  - Grass and hard ground: each window's own orthophoto, placed by its bounds.
  - Streets: a new Overpass extract of the square (`scripts/fetch-lent-osm.sh`; buildings are excluded because the footprints come from the original), with 320,463 m² of carriageway, 468,025 m² of pavement and 386 crossings, 350 of them marked.
  - The surface mask is 4096² (0.375 m). There are 521 paved chunks with 5.39 M triangles.
  - Rules: bridge decks, blob opening, the Drava bed, kerbs and paving at walls are unchanged from ADR-040.
  - Speed: point-in-shape queries now go through a banded edge index, and building seating works on a patch round each footprint, so the whole square builds in 75 s.
- **Paving that dips between grid points.** At the new grid two paved triangles in 584,537 samples showed the landscape over them by 2 and 10 cm. In both, the paving dipped between grid points, where the existing neighbour rule could not see it. Every corner of a landscape cell now goes 5 cm below the lowest paved corner that reaches into the cell.
- **`-run=MarburgumRebuildGround`** reads the landscape size from `ground.json`. It deletes paved chunk assets that the new extent no longer has.
- **Street people (ADR-044).** The walk graph is built over the whole square. Its clearance raster stays at 0.25 m whatever the extent. It has 13,098 nodes and 74.7 km of line, where Lent alone had 1,582 nodes and 9 km.

**Evidence.**

- `-run=MarburgumRebuildGround -Verify -Every=1` dropped a ray and a capsule on every walkable paved triangle of the saved level: 4,089,551 samples, 0 on the landscape, 0 capsules sunk.
  - 6,612 hit a bridge deck or an eave first.
  - 11 rays land up to 12 cm above their triangle, where a sliver of carriageway runs under the kerb at the pavement's edge. This is not walkable or visible.
- The landscape reads back within 4 mm of the data.
- Inside the old Lent window the new heights match the old within 3 mm (median) and 6 cm (95th percentile). The differences come from the grid spacing and the newer street extract.
- The car parks at the same spot as before (ADR-040).
- The street, combat, vehicle and swim smokes pass in editor mode on ports 27851–27854. The street server placed 12,965 of the 13,098 walk-graph nodes on the ground.
- `cargo test` passes 20 tests, including:
  - the window-edge join;
  - the courtyard case, with the real r1630983 outlines;
  - the patch distance against the whole-grid transform;
  - the edge index against `contains`;
  - depression filling.

**Not covered.**

- **Deep hollows.** The 2014 survey has 14 hollows over 100 m² that have no outlet and are more than 3 m deep, away from streets and water (`ground.json` → `hollows_over_3m_deep_and_100m2`). Most are courtyards and light wells inside building blocks, for example behind the Minorite monastery (17 m, a 2014 excavation) and a 6,300 m² overgrown pit in pristan (9.5 m). They are reported, not filled. Filling them is the owner's call for a 1990s map.
- **Railways.** There are no rails; railway land is gravel.
- **Detail.** Street furniture, trees, the quay wall, lane markings beyond centre lines and zebras, and 1990s corrections of the 2026 streets are not done. Paved meshes have no LODs and no Nanite.
- **One level.** The square is one always-loaded level, with no World Partition or streaming, 576 landscape components and 8.4 M static triangles. Frame time on a packaged client and the owner playtest are still to do.

## ADR-046 — Nobody walks into the Drava (accepted 2026-11-11)

**Problem.** The owner saw NPCs walk into the river. Street people (ADR-044) and the Lent cast (ADR-041) steer straight at a goal with nothing between them and the water. The goal can be a graph node, a flee or dodge step, a watcher's step back, a place round a body, a way back onto the graph, a stroll round home, or the player being chased. The walk graph is built 0.5 m clear of the river, but the server's check of it only looked for ground and room. On the rebuilt ground (ADR-040) it kept 14 nodes that lie in the water volumes the swimming rules use (ADR-032).

**Decision.**

- **One answer to "is this water".** `UMarburgumWaterSubsystem` (`MarburgumWater.h`) collects the level's water physics volumes, the 91 boxes over the Drava. A point is wet when it is over one of them and nothing walkable lies above the surface there. A bridge deck or a quay over the river is dry; stepping off the quay's edge is not. It looks for that ground from 2.5 m above the feet down to the surface, through the water volumes, and only over a water footprint, so the test costs nothing away from the river.
- **The walk graph drops wet nodes and edges.** Every kept node and every 0.5 m of every kept edge must be dry. The server's `street_ready` line now reports `water_volumes`, `wet` and `edges_wet`; on `/Game/World/lent` it drops 14 nodes and no edges.
- **Every step is checked.** `AMarburgumFighter` does not take a step from dry ground into the water: it looks one capsule radius, 40 cm and 0.2 s of its speed ahead, and holds still at the edge (`bWaterAhead`). A body already swimming may move, so one knocked in can make for the bank. Each caller then chooses a dry way. A walker on the graph turns back (`water_edge`). Someone fleeing off the graph runs along the bank, up to 150° off straight away; with water every way they stand. A car-dodge jumps to the other side, or not at all. A watcher does not back into the river. A place round a body is taken on the dry side, or they stand where they are. A way back onto the graph goes to the nearest of eight nodes reachable without wading. The Lent cast stroll only where it is dry and step aside along the bank. A hostile does not follow a player into the water; it waits on the bank, guard up, until the player comes out or is beyond its pursuit range.
- **Evidence in the street smoke.** `-MB2StreetProbe` also places a person on the pavement nearest the player with the river 4 m off it, scares them from the land side, so that running straight away would be into the Drava, and watches them for 6 s (`water_probe`, `water_probe_done … swam=`). `street_summary` counts people in the water (`swimming=`). `scripts/street-smoke-linux.sh` wants the graph checked for water, the probe dry and nobody ever swimming. With the step check switched off, the same run fails: the probe is in the river within 6 s.

**Not covered.** An owner playtest and a staged (packaged) run. Water other than physics volumes marked as water. A body thrown into the river by a car swims to the nearest node it can reach; it does not choose the nearest bank.

## ADR-047 — The original skill curve, saved with the character (accepted 2026-11-11)

**Problem.** Fishing and missions award experience, and the original game already fixed how that experience becomes a level. Inventing a second curve would make the same XP mean two different things.

**Decision.** Levels run from 0 to 1990. The step from level n to n + 1 costs `20 + round(0.06 * (n + 1)^1.5)` XP, and an exact half rounds to even. The arithmetic is integer-only (`MarburgumSkillCurve.h`), so the server and every client agree. The server stores XP per skill (schema 4, absent means none). Clients derive the level. The five skills in the catalogue are the original's registered set. Purchasable ranks stay out (ADR-004).

**Evidence.** `Marburgum.Shared.SkillCurve` matches the original's own checks and golden totals (level 10 at 208 XP, level 100 at 4429, level 1990 at 4,282,249). `cargo test` covers the schema-4 store check.

## ADR-048 — Fishing the Drava (accepted 2026-11-11)

**Problem.** "Something in the Water" cannot be finished until a player can catch a fish and show it to the angler. The original's cast is a timed rule, not a menu.

**Decision.**

- A cast needs a fishing rod and a spot. The Lent bank spot and the Melje weir spot, their species, weights and XP, come from the original `fishing.json` unchanged.
- The wait is the original's 1.5 to 4 seconds. The bite stays open for 1.8 seconds. A strike before the bite loses the cast (`too_early`). Another cast inside 1 second is refused (`too_soon`). Walking more than 75 cm ends the cast; that allowance is Marburgum II's.
- The server draws the wait and the species. Landing the fish and its Fishing XP is one schema-4 commit. A cast itself is not saved. The purchasable patience, strike and reading-the-water ranks are excluded (ADR-004).
- The player uses the same E interaction as everything else. The prompt follows the line: cast, wait, strike.

**Evidence.** `Marburgum.Server.FishingRules` passes. The mission smoke (`scripts/mission-smoke-linux.sh`) casts three times: too early, a fish that gets away, then a bite and a catch.

## ADR-049 — The Lent missions, including the first catch (accepted 2026-11-11)

**Problem.** The angler already started "Something in the Water" and then stopped, because the next step needed a fish (ADR-017). The original's Lent missions after that, and the Kiosk 67 decision, were data without a rule.

**Decision.**

- One pure rule (`MarburgumMissionLogic.h`) decides what E means at a person or a place. Work already in hand comes before a new offer. A stage that is a decision cannot be completed by a plain interaction.
- The server applies that answer in one commit: start, stage, choice, items, money, skill XP and the journal entry. Schema 4 adds completed missions, the choice taken, its outcome, flags, money and XP. Older stores still load.
- The opening mission runs through: catch and show a fish, pull the cashbox from the debris, ask Petra Kralj, open the service room with the brass key, and show Vera Horvat the page. Completing it pays 50 and unlocks the Blue Ledger journal entry. Petra then offers Kiosk 67. Paying the customer's tab costs 20 (so 30 remains) and gives 15 Reputation.
- The choice is a panel, not a chat line. Number keys pick an option. The Skills panel (K) shows level and progress from the server's XP. The journal and the pinned objective follow the saved stage, and a finished mission is no longer pinned.
- People and places in the catalogue are spawned by the server on `/Game/World/lent`. Display text is the original's English.

**Evidence.** `Marburgum.Server.MissionRules`, `Marburgum.Server.MissionChoices` and `Marburgum.Server.WorldStoreCondition` pass, with the other eight `Marburgum.*` tests (11/11). `mb2 validate gameplay` matches the generated catalogue. `scripts/mission-smoke-linux.sh editor` on port 27871 plays the opening mission and the kiosk decision against a dedicated server, restarts, and checks that money (30), Fishing XP (16), Reputation (15), the decision and the clues are still there and that the store did not change. Evidence: `Saved/MissionSmoke/20261111T213612Z`.

**Not covered.** An owner playtest and a packaged run. Fishing has no animation or sound. The Melje yard and the Tabor and bank places sit outside the nine-window map, so those missions are not placed (ADR-050).

## ADR-050 — Names No Longer Match, after the first catch (accepted 2026-11-11)

**Problem.** Finishing "Something in the Water" is supposed to open the next story with Vera Horvat. The mission was in the catalogue, and the same rule could start it, but nothing played it through.

**Decision.** No new rule. Once `lent_first_catch` is complete, talking to Vera starts `raeda_names_no_longer_match`. The directory, the successor's sign, Marija Sever's returned mail, Petra's clipping and the newspaper archive are the stages, in that order. Telling Maja what was found (`tell_maja`) completes it: outcome `maja_informed`, the Blue Ledger journal entry, and the papers stay in the character's custody. Before the first catch, the same talk is refused as a missing prerequisite. The Melje yard, Irena Vogrin and the bank desk stay unplaced: their original positions are outside the nine-window map, so the two missions after this one cannot be walked yet.

**Evidence.** The editor mission smoke on port 27881 plays this chapter between Vera and the kiosk, then the restart still has the decision, the journal entry and the four papers. `Saved/MissionSmoke/20261111T215424Z`.

**Not covered.** A packaged run and the owner playtest. Placing the Melje and Tabor content needs a wider map.

## ADR-051 — The other missions that stand on this map (accepted 2026-11-11)

**Problem.** Three more of the original's missions have every place inside the nine-window map, and nothing played them: the late parcel, Hall Three's last shift, and the four names under the paint. Playing Hall Three also showed a rule bug. Matej can offer the Melje shift once Names No Longer Match is done. A plain talk while Hall Three's decision was due started that shift instead of saying a decision was due.

**Decision.**

- A decision due at a person is work in hand. It is answered before any new mission that person could offer. A stage that is only missing an item still yields to a new offer, as before.
- The editor smoke now also delivers the parcel and leaves the clerk his share (60, so 90 remains), negotiates Hall Three (Reputation 25), and keeps the painted layers (Reputation 20). Reputation ends at 85. The papers, the keyring and the maintenance book stay in custody. All of that is still there after a restart.

**Evidence.** `Marburgum.Server.MissionRules` covers Matej's decision coming before the Melje offer. The editor smoke on port 27893 plays the three missions after the kiosk and passes, including the restart. `Saved/MissionSmoke/20261111T221606Z`.

**Not covered.** A packaged run and the owner playtest. The Melje yard and the Tabor bank trail are still outside this map.

## ADR-052 — Melje and Tabor on temporary floors, and nobody stands in the Drava (accepted 2026-11-12)

**Problem.** The last two of the original's eight missions, The Last Shift and The City for Sale, need the Melje transport yard and the Tabor exchange and bank. Those places lie outside the nine-window map, so nothing could reach them. Playing The City for Sale also showed that the new map's river does not follow the original's bank everywhere: Milan Šolar's original spot in Pristan is now in the Drava. He stood on the riverbed, and his development start was in the water.

**Decision.**

- A catalogue placement `pad`: a place outside the built windows stands on a plain temporary floor at its original height. The server makes one floor per cluster of such places. `AMarburgumContentPad` replicates (always relevant), so every client stands on the floor the server simulates. The eight Melje and Tabor places use it until those windows are imported. Nika Vidmar and Alenka Pahor were already inside the map and now stand at their original spots.
- A person or place whose ground is wet stands on the nearest dry ground within 60 m instead, logged as `content_moved`. Fishing spots are exempt. Milan moves 16 m and three Hall Three places move 2 m. A development start beside one goes to the first dry side round it.
- The editor smoke now also plays The Last Shift (the yard, Matej and Vera) and The City for Sale (the counterfoil, Irena, the classifieds, the bank index, Milan and Petra), then trusts Milan. Reputation ends at 95. Every paper from both missions stays in custody and the archive boxes are handed over. `mb2 store-check missions` checks all of that, and treats a mission without a decision as storing no choice.

**Evidence.** `Marburgum.*` automation passes (11/11). `cargo test` for `tools/mb2` passes (29). The editor mission smoke on port 27911 plays all eight missions, restarts, and the store is unchanged afterwards. `Saved/MissionSmoke/20261112T000431Z`.

**Not covered.** The floors are grey boxes in an empty sky until the Melje and Tabor windows exist. A packaged run and the owner playtest.

## ADR-053 — A quality pass over the nine-window map (accepted 2026-11-12)

**Problem.** The owner asked for a high-quality pass across the whole of `/Game/World/lent` (ADR-045). An audit from 11 aerials, 36 street-level shots and close-ups found, in order of how much they hurt:

1. Paving followed the 2014 survey's bumps: pavers climbed mounds, and a row of teeth ran along some walls.
2. Grass islands and verges sat below the kerb, with the paving's dark edging showing round them.
3. Paving and grass were lifted against walls (ADR-040's wall lift and seat), which made ramps and berms. Some buildings stood on a base metres above the surveyed ground beside them.
4. The grass mask was noisy: hard-standing blobs in lawns, grass specks in yards.
5. The Drava was a cartoon of white stripes on turquoise.
6. Flat roofs were nearly black (`MI_roof_bitumen`, a texture of linear albedo 0.035).
7. Every building used the same beige plaster.

**Decision.**

- **Paving over mounds** (`tools/lent-ground`, `flatten_paving`). A paved point is on a mound when the paving round it falls away by more than 20 cm both ways along at least three of the four axes through it, the two grid axes and the two diagonals, within 6 m. A bank's lip, a convex corner of a terrace, a road on an embankment and a ramp onto a bridge all rise or stay level one way, so they are left alone. Round each mound, within 3 m, the paving comes down to an opening of the paving and the bridge decks that removes raised features narrower than about 10 m. It keeps 5 cm of the survey's texture and comes down by at most 3 m. 45,222 points are lowered.
- **Paving beside walls.** Within 3 m of a wall the paving eases to the level of the paving 3–6 m out: it comes down by up to 4 m and goes up by up to 30 cm, so it meets the wall on a clean line. 535,155 points are eased. The wall lift of ADR-040 is gone.
- **Outdoor stairs.** Pavement steeper than about 1 in 4 is the survey seeing a stair's treads. Such pavement is drawn as a smooth ramp (σ 2 m) instead of a row of teeth (52,698 points).
- **Kerbs on another level.** The kerb rule held every pavement within 9 m of a carriageway to 12–24 cm above it. It did this even when the road ran 4 m below, at the foot of a bank or a terrace wall, so the pavement on the terrace was pulled down in spikes. The rule now holds within 1.5 m of the road as before. Further out it lets go of a road more than 40–90 cm off the pavement's own level, and of a road the blur cannot reach.
- **Verges.** Unpaved ground within 3 m of paving that lies up to 60 cm below it comes up to 4 cm under the paving's edge, fading out between 1.5 and 3 m. It never goes over the rules that keep the landscape under the paving. A deeper fall, a bank dropping away from a path, keeps its fall. 694,276 points are raised.
- **Wall footings** (`MarburgumRebuildFacades`). Every wall now reaches 2.5 m below its base (`Footing`), so paving and grass meet it at their own height. Where the surveyed ground beside a building falls further than that, the ground tool records a deeper footing (`ground.json` → `buildings.deep_footings_m`, in 0.5 m steps, at most 12 m), and the façade commandlet builds that one deeper. Four buildings need one: the University library and the University next to the 2014 Minorite dig, at 10 and 9.5 m, and two more at 3 and 4 m. 0 paved points are below a footing, and no landscape is raised to a wall. The façades are now rebuilt once more after the ground tool (DEVELOPMENT.md).
- **Grass mask.** The orthophoto is averaged over about 1.5 m (3 × 3 samples, 0.5 m apart) before the excess-green test. The grass weight is blurred 0.5 m, where it was 0.35 m.
- **The Drava** (`M_DravaWater`, built by `MarburgumRebuildGround` and set on every `Drava*` sheet). Single-layer water: scattering 0.01, 0.03, 0.022 and absorption 0.65, 0.2, 0.28 per metre. The ripples are two scales of the engine's tiling `water_n` (12 m and 4.5 m), drifting downstream (plan east) at different speeds, at 40 % strength, with roughness 0.05. Base colour is black and opacity 0: on single-layer water these describe a layer on top of the water such as foam. Left unconnected, opacity is 1 and the river renders as an opaque black sheet.
- **Building colours and flat roofs.** There are nine plaster instances under `MI_plaster_mineral` and three flat-roof instances under `MI_roof_bitumen`, each with `BaseColorTint` set to a target colour over the texture's linear mean (the parents' tint is 1):
  - plaster: the original's beige, ochre, pale yellow, cream, warm grey, salmon, sage, off-white and the grey render of post-war blocks;
  - flat roofs: gravel, weathered grey and dark.
  Each building picks a plaster and a flat-roof finish from the CRC of its id, weighted towards beige, ochre, yellows and greys, so a rebuild keeps them.

**Evidence.**

- `-run=MarburgumRebuildGround -Verify -Every=1` on the saved level: 4,094,582 samples, 0 on the landscape, 0 capsules sunk. 6,868 hit a bridge deck or an eave first. 11 rays land up to 12 cm above their triangle, the same carriageway slivers under the kerb as in ADR-045. An intermediate build with the kerb rule let go right beside the road gave 140 rays off by up to 1.32 m. This is why the rule still holds within 1.5 m.
- Regeneration: 870 façades rebuilt with 0 open and 0 failed (3,026,898 triangles). 521 paved chunks with 5,391,116 triangles. The landscape reads back within 4 mm. 422 water volumes, and the car placed.
- `cargo test` passes 23 tests. The new ones check that a mound in the paving comes down, that a terrace's edges and convex corner keep their height exactly, and that the masked opening ignores points outside its set.
- Before and after captures from the same 47 cameras (`main` against this branch) are in [Captures/map-quality](Captures/map-quality/): aerials, the Drava, colours and roofs, paving at walls, and paving on banks.
- The swim, vehicle, street and combat smokes pass in editor mode on ports 27861–27864. The street server placed 12,955 of the 13,098 walk-graph nodes; 14 are wet, as before.

**Not covered.**

- **Map edge.** The square still ends in a bare edge over the void.
- **Large mounds.** Mounds wider than about 10 m are followed, and so are paved areas that really slope up to a building over more than 3 m, such as plan (167, −96). So are unpaved banks.
- **Colours.** The plaster colours are chosen by id, not from the original's photographs or a 1990s survey. In shade the yellows read slightly olive under the blue sky light.
- **Water.** It has no shore foam, flow map or caustics. Its surface is the flat sheet of ADR-045 at 6 cm.
- **Not yet done.** Trees, street furniture, façade detail, LODs and Nanite. A packaged run, frame time and the owner playtest.

## ADR-054 — A second quality pass: islands, banks, façade bands and the distant ring (accepted 2026-11-12)

**Problem.** The owner asked for another quality pass over the whole map. A fresh audit used ADR-053's 47 cameras plus 34 new ones: 22 street-level positions from a different seed, 8 mid-height views and 4 views at the map edge. In order of how much they hurt:

1. **The map edge.** The square still ended in a bare edge over the void (ADR-053, not covered). Seen from the air, the map was a tile floating in grey.
2. **Grass at the paving's edge.** Grass in islands and beside the paving stood in pits up to several metres deep, or rose as mounds. Each met the paving in a cut, and the paving's edging showed as a dark wall. ADR-053's verge rule only lifts ground up to 60 cm below the paving, within 3 m.
3. **Flat façades.** Every wall rose as one flat plane from the street to the eaves.

**Decision.**

- **Islands** (`tools/lent-ground`, `fit_islands`). An island is unpaved ground of at most 5,000 m² that does not reach the window's edge and has paving on at least 60 % of its rim. It is fitted to the harmonic surface through the paving round it, 4 cm under the paving's top. A building on the rim is left free, so its wall does not hold the island to the ground under the building. The survey's shape is kept only within 4 cm, plus 12 cm for each metre from the paving, up to 2 m. A pit or a mound in an island therefore eases out from the kerb instead of meeting it in a cut. 736 islands are fitted, and 161,956 landscape points move by more than 5 mm.
- **Banks** (`BANK_*`). Elsewhere, unpaved ground within 2.5 m of paving falls away from 4 cm under the paving at no more than 0.8 m per metre. Beyond the 0.8 m where it must stay under the paving, it rises at no more than that rate. Ground is moved by at most 1.5 m, eased out over the last metre so a deeper bank keeps no step, and never within 6 m of the Drava or under a building. 3,814 points are raised and 57,163 lowered.
- **Façade bands** (`MarburgumBuildingMesher`). Every building gets up to three bands, each one closed ring round its outline, mitred at the corners. A band reaches 2 cm into the wall and stands 5 mm proud along a party wall, so no corner lands on the wall's own edges and the shell stays strictly closed:
  - a plinth from the bottom of the wall to 45 cm above the street, 5 cm proud, in a dark grey-brown render (`MI_facade_socle`);
  - a string course over the ground floor, 16 cm high and 6 cm proud, on buildings of two storeys or more;
  - a cornice under the eaves or the roof deck, 22 cm high and 14 cm proud.

  The string course and cornice are a light trim (`MI_facade_trim`), the 1990s Maribor pattern of off-white mouldings on a coloured render. A door now starts on the plinth. 557 buildings have all three bands, 229 have two and 80 have one. The 870 shells have 0 open edges and 3,168,858 triangles (3,026,898 before).
- **The distant ring** (`lent-ground ring`, `ring.bin`; built and placed by `MarburgumRebuildGround`). The nine survey tiles already read for the ground reach at least 1,432 m from the middle of Stari most on every side. Round the playable ±768 m square they become a low-detail ring of 4 m cells:
  - Bare ground and tree canopies are one smooth surface. A canopy cell stands at 80 % of its height.
  - A building cell (at least 3 roof returns 2 m above the ground) is a flat block at the median of its roof returns, with walls wherever it stands above its neighbour. Connected cells form a block, which gets one of three wall renders and a tile or flat roof by how much of it slopes.
  - The 2014 survey classes the Drava's surface as ground. Cells whose ground returns spread less than 8 cm, lie below 2 m, and join in sheets of at least 2 ha are the river. Dry holes in it up to 1,500 m² (boats, gravel bars) are closed in. The river is a water sheet in `M_DravaWater` at the survey's own level, eased to the square's 6 cm over 150 m, over a bed 1.5 m down.
  - The ring's inner edge takes the landscape's own heights, 2 cm down, so the two meet without a step.

  The ring is 119 meshes (`/Game/World/Ground/Ring`, 1,036,298 triangles): 85,364 building cells in 1,638 blocks, 143,355 tree cells and 11,539 water cells. Its meshes have no collision, nothing out there can be reached, and the walkability check skips them. The materials are the paved master over the grass textures for ground and canopies, and the buildings' own clay-tile, flat-roof and plaster instances. Its sources are the same ARSO LiDAR tiles already credited in `ground.json` (`ring.json` records the summary).

**Evidence.**

- Before and after captures from the same cameras are in [Captures/map-quality-2](Captures/map-quality-2/): ADR-053 against this pass.
- `-run=MarburgumRebuildGround -Verify -Every=1` on the saved level gives ADR-053's baseline: 4,094,582 samples, 0 on the landscape, 0 capsules sunk, and the same 11 carriageway rays up to 12 cm. 16,651 rays meet something else first, against 6,868. The new collision beside the paving is the plinth, 5 cm proud at the foot of every wall, so these are taken to be paving triangles there; the cause was not traced ray by ray. The first run also sampled the ring, which has no collision, and found 15 capsules "sunk" just outside the square; the check now skips the ring.
- Regeneration (DEVELOPMENT.md order, with the ring): 870 façades with 0 open and 0 failed, 521 paved chunks unchanged (`paved.bin` is byte-identical, so their re-saved assets were left as committed), the landscape reading back within 4 mm, 422 water volumes and the car placed.
- The swim, vehicle, street and combat smokes pass in editor mode on ports 27861–27864. All 11 `Marburgum` automation tests pass.
- `cargo test` passes 26 tests. The new ones check that an island meets the kerb round it while its pit comes up and its mound down, that ground at the window's edge or walled in is not an island, and the ring's median.
- The mesher automation test checks that the three-storey rectangle has its three bands.

**Not covered.**

- **Steep grass at the paving.** Banks deeper than the 2.5 m reach still meet paving at up to 0.8 m per metre, and the paving's edging still shows there. Large pits beside plazas that are not islands, more than 40 % of their rim being walls or open ground, keep their depth.
- **The ring.** It is the survey's surface, not modelled buildings. Its blocks are 4 m steps, and canopies read as soft green mounds near the edge. Its ground is one colour with no streets. There is a visible change of tone where it meets the square, and beyond 1,432 m the atmosphere's ground shows as grey below the horizon.
- **Bands.** They have one height and depth everywhere. There is no rustication, no window surrounds, and no difference between old-town and post-war buildings.
- **Not yet done.** Trees and street furniture in the square, LODs and Nanite, a packaged run, frame time and the owner playtest.

## ADR-055 — Accounts, the first character and signed-in entry (accepted 2026-11-12)

**Problem.** Stage 0F (ADR-022) needs an email-confirmed account, a username and password, a first character and a server that admits only that character. ADR-022 left the account provider, the email sender, the naming policy and the model roster open. The original game's login service is a 4,169-line part of its old protocol and sent passwords over plain TCP.

**Decision.**

- **A small self-hosted account service**, `services/accounts` (Rust), over HTTPS only. It owns email confirmation, credentials, sessions, the one character and admission tickets. The game server never sees a password, and the client never sees the server's key. No outside provider and no cost. The original's ideas carry over: owner-only durable writes (temporary file, fsync, rename, directory fsync), single-use expiring codes stored as SHA-256 verifiers, and a neutral answer to "forgot password". Passwords use Argon2id (19 MiB, two passes), as FIRST_LAUNCH_ONTOLOGY asks, not the original's PBKDF2. Old Marburgum accounts are not migrated.
- **Flow.** An email address gets an eight-character code, valid 30 minutes, five tries, at most one a minute and five an hour. An address that already has an account gets a note instead, and the screen says the same either way. The code becomes a 30-minute single-use setup ticket. The username (3–20 of a–z 0–9 . _ -) and password (10–128, not containing the username) create the account, and the client signs in with them. Five wrong passwords in 15 minutes lock that username for the window. A session lasts 12 hours, lives only in client memory, and a password reset signs every session out.
- **The first character.** One per account. First and last name are fictional, NFC-normalised, 2–24 Latin letters including Slovene and neighbouring diacritics, with single spaces, hyphens or apostrophes between them, and a few reserved words. The body comes from `Data/Accounts/character_models.v0.1.json`: the protagonist, Manny and Quinn, all on the mannequin skeleton. The client sends a request id, so a double click or a retry makes one character. The service assigns an opaque `c_…` id, which is also the character's key in the world store.
- **Entry.** Connect asks the service for a single-use ticket valid one minute, every attempt including reconnects, and joins with `?MB2Ticket=`. A server started with `-MB2Accounts=<url> -MB2AccountsKeyFile=<file>` spends it in `PreLoginAsync`. It refuses a development key, a missing, malformed, forged, spent or expired ticket, and a character already in the world, and then checks again in `InitNewPlayer`. The admitted character's name is the replicated player name, which the client cannot change. Its model is replicated and worn by the body on every client. A shipping server always requires accounts. A development server without `-MB2Accounts` keeps the ADR-014 key, so every existing smoke runs unchanged.
- **Screens.** With `-MB2Accounts` the client starts on the front end, never a local world. It shows sign in, create account, confirm code, username and password, forgotten password, reset, and the character creator (first name, last name, body). Then it shows the connection screen with who is signed in and a Sign out button. Errors are field-specific and keep what was typed, except passwords.
- **Email.** Until a sender is chosen, each message is a file in the service's outbox folder (owner-only).

**Evidence.** `cargo test` in `services/accounts`: 19 tests (policy, Argon2id and verifiers, store durability and permissions, the whole flow, limits, idempotent create, reset, a failed write changing nothing, the HTTP API). `mb2 validate accounts` keeps the C++ roster and limits in step with the service. `Marburgum.*` automation 11/11. `scripts/account-smoke-linux.sh editor` uses a throwaway CA, the real service, a dedicated server on `/Game/World/lent` and rendered clients. A new player registers, has a wrong code refused, confirms with the emailed code, has two different passwords stopped on the client and the name "R2D2" refused, creates Špela Kovačič as Quinn, and enters the world under that name in that body. The returning player has a wrong password refused, signs in without the creator, and enters as the same character. A development key, no ticket, a forged ticket, a spent ticket and a second ticket for a character already in the world are all refused. A service restart keeps the account. No password, code, session or ticket appears in the account store, and no password or session in any log. `Saved/AccountSmoke/20261112T081217Z`. The development-key mission smoke still passes on the same build (`Saved/MissionSmoke/20261112T063614Z`).

**Not covered.** Sending real email: choosing a sender and its retention rules is an owner decision. A 3D model preview in the creator: the body chooser is by name. Per-address rate limits and the service's deployment, backups and monitoring (Stage 4). Slovene screen text. A packaged run, the 720p–4K screen matrix and the owner's first-time and returning playtest (FL-10, FL-12). Spent tickets appear in the engine's own join-URL log lines; they are single-use and one minute long.

## ADR-056 — A player's own car keeps updating past 150 m (accepted 2026-11-12)

**Problem.** In the owner's playtest (`Saved/Playtests/20261112T063759Z-ec64f8fb-a4ac31dc`) the car stopped dead after some distance although the road went on. Getting out then put the body and the car a little further on. The server sends each player only what is near their view, a car within the engine's default 150 m (ADR-039 set no distance of its own). The view is the camera position the client reports. The engine reports it only while the pawn's movement has input or velocity, and a seated body's movement is off (ADR-039), so the server kept the view where the player got in. From 150 m on, the player's own car stopped updating while the server kept driving it on the held pedals. Getting out placed the body at the server's car, and the car came back into view there. All six exits of that playtest were 190–285 m from the spot where the player got in. One came at 23.1 m/s on the server while the client showed the car standing.

**Decision.**

- **The camera keeps reporting from the seat.** While the local player sits in a car, `AMarburgumPlayerController` asks the camera manager for the client-side camera update every frame, as walking movement does. The engine's own rate and position limits still apply. The server's view follows the driving camera, so the car and everything near it (street people, interactables, other players) are sent from where the player is.
- **A car is always relevant to its occupants.** `AMarburgumVehicle::IsNetRelevantFor` answers yes for a viewer whose player sits in it, and otherwise applies the engine's distance rule. A stale or late camera report can no longer freeze a player's own car.
- **Evidence in the vehicle smoke.** At each exit the server logs how far from the car it takes the player's view to be (`event=exit_view … view_m=`). `scripts/vehicle-smoke-linux.sh` wants both exits within 16 m, the seated camera being 11 m behind the car. `MB2_VEHICLE_NET_CULL_M=<m>` starts the server with `-MB2VehicleNetCullM=<m>`, which shrinks the car's send distance (`vehicle_ready … net_cull_m=`). At 9 m, inside the walking camera's reach and short of the seated camera's, only the occupants' rule keeps the car updating for the driver and the passenger.

**Evidence.** Editor runs on the change: `Saved/VehicleSmoke/20261112T084143Z` and, with the car sent within 9 m, `20261112T084243Z` PASS, exit views 11.9 m and 11.1 m; `Marburgum.*` automation 11/11. With both parts switched off in a control build, the default run FAILs on the exit views (29.5 m each, the spots where D and P got in; `20261112T083218Z`). The 9 m run FAILs as the owner saw it. Both clients' car freezes while braking out of the turn, 0 km/h and not moving, while the server's car, still driven by the held pedals, ends 7.5 m away. The driver's reverse fails (0 km/h, 0 cm), the passenger never sees it reverse, and neither gets out before the clients give up (`20261112T083307Z`).

**Not covered.** A drive of more than 150 m in a smoke (the 9 m run stands in for it), a staged (packaged) run and the owner's playtest. Other players' cars still use the engine's 150 m. Interest management for the whole city is still Gate 3.

## ADR-057 — The creator shows the body in 3D, and the email sender waits for a server (accepted 2026-11-12)

**Problem.** ADR-055's creator chose the body by name with ‹ ›. FIRST_LAUNCH_ONTOLOGY asks for a full-height 3D preview on a client-only preview scene, selectable model cards, and turning and zooming by pointer. ADR-055 also left the email sender open.

**Decision.**

- **The preview is a client-only stage.** `AMarburgumCharacterPreviewStage` is spawned in whatever world the front end has loaded, 2 km below its origin. It holds one roster body playing the mannequin idle, a plinth, a floor and a backdrop, and three lights: key, fill and rim. A scene capture renders only those parts (a show-only list) into a texture. The stage's lights and parts use lighting channel 2 only, so the world's lights do not reach them and theirs do not reach the world. Exposure is fixed, and Lumen and reflections are off in the capture. The bodies are the account contract's roster, the same meshes `AMarburgumCharacter` wears in the world, so what the player sees is what enters the world.
- **The screen.** On the character step the preview fills the right of the screen from the top gutter to the bottom one, beside the card. It renders at 1.5 times its drawn pixels, up to 2048, and resizes with the window. The vertical framing is fixed and the horizontal angle follows the picture's shape. Dragging turns the body. The wheel moves the camera from the whole body to head and shoulders. The ‹ › pager is replaced by one card per approved body, and the chosen card is outlined in copper and says "Selected", so the choice never rests on colour alone. The capture runs only while the creator is on screen, and the stage is destroyed with the screen. Only the roster id is sent, as before.
- **Email waits for a server.** Sending real email needs a deployed service with a sending domain, so the choice of sender moves to the service's first deployment. Until then, codes stay files in the service's outbox (ADR-055).

**Evidence.** `scripts/account-smoke-linux.sh editor` now pages through all three bodies in the creator and checks that each is shown. It turns the body by the drag path, zooms by the wheel path, and saves a screenshot of each body, the turn and the close-up. The rest of the flow is unchanged and passes, including a first-time player entering as Špela Kovačič in Quinn's body. `Saved/AccountSmoke/20261112T084449Z`. `Marburgum.*` automation 11/11; `mb2 validate all` passes. The development-key mission smoke still passes on the same build (`Saved/MissionSmoke/20261112T084751Z`).

The account smoke also asked for both admission tickets before four client starts. On a slow run the second ticket outlived its one minute, and the server refused it as expired instead of as a character already in the world. Each ticket is now asked for just before its join.

**Not covered.** Turning and zooming by controller or keyboard: pointer only. Thumbnails on the cards: they show names. A packaged run and the 720p–4K screen matrix (FL-10). The bodies' art quality and the roster itself, including the protagonist's blurred face texture, are the owner's review. The owner's playtest.

## ADR-058 — The protagonist's face is one concept-projected island, and the creator keeps every mip (accepted 2026-11-12)

**Problem.** In the creator (ADR-057) the protagonist was a smear: the face unreadable, the shirt pockets and the jeans' weave gone. Up close with full detail the face was still broken, with bands across the eyes, a split nose and a dark blotch on the cheek. Three causes:

- **Texture streaming ignores scene captures.** The engine streams a texture's mips for the player views it is told about, and a scene capture is not one. In the front end no player view is near the stage, so the preview drew the body from a low mip. In the world the third-person camera is the player view, and the face read there.
- **Smart UV cut the face into five islands.** The concept projection is continuous across the cuts, but at every island edge filtering took in the bake margin, which still held the coarse vertex colours.
- **The normal map drew TRELLIS's guess of the face.** The dense face's eye sockets and mouth are lumps, and baked as normals they lit as hard-edged plates and bands over the photo's own shading. A fold of the low mesh behind the cheek shared the face island's texels; where it was the layer baked into a texel, the front camera could not see it and the texel kept the dark inside-of-cloth colour.

**Decision.**

- **The creator keeps every mip of the body it shows** (`bForceMipStreaming` on the stage's body). Only the stage's one body is affected, and only while the creator is on screen.
- **The front of the head is one island.** `retopo_bake.py` gives the head's faces above the neck that face the concept camera (normal at least 0.25 toward it, `FACE_FACING`) a planar UV straight along that camera, at twice the body's texel density (`FACE_SCALE=2`), so concept pixels land evenly on texels and nothing cuts across the face. On the head the concept projection no longer asks whether the dense mesh hides the texel: in this island the concept colour at a position is the visible cheek's either way.
- **The face's normal map is flat where the head takes the concept** (`FACE_FLAT=1`), because the photo already carries the face's shading.
- **The margin is grown again from the final colours**, so mips and filtering do not bring the vertex colours back across any island's edge.

**Evidence.** `scripts/account-smoke-linux.sh editor` now also turns the body back to the front and saves the protagonist at head and shoulders (`creator-protagonist-close`). It passes before the rebake with the mip fix alone (`Saved/AccountSmoke/20261112T091649Z`) and after it (`20261112T093935Z`). In the creator: before, with every mip, and the close-up before and after the rebake: `Docs/Captures/character-protagonist/creator_face_before_after.jpg`. Blender, same light, the face before and after: `face_bake_before_after.jpg`. The bake's checks: holes 0, about 11.9k quads, 2 overlapping faces outside the face island as before, 15 inside it (the fold behind the cheek, coloured by the same projection). The rig's checks are unchanged (74 weighted bones, fingertip error 1.9 and 2.4 cm, wrist scales 0.99 and 1.12), as are the FBX's units, the pelvis, the import onto `SK_Mannequin` and the matte material (ADR-038). The editor loads all 140 assets under `/Game/Characters` with no failures and the same 12 inherited template references. `scripts/lent-smoke-linux.sh editor` passes with the new body (`Saved/LentSmoke/20261112T094224Z`).

**Not covered.** The face in the world from the front (the Lent smoke sees his back); a packaged run; the owner's playtest. The sides and back of the head keep the vertex colours, with a visible edge where the concept ends on the cheek. The neck and collar keep their pale patches. Below the head the body has about 11% fewer texels across (22% fewer in area), because the face island takes room.

## ADR-059 — The protagonist's head keeps TRELLIS's shape (accepted 2026-11-12)

**Problem.** The owner asked for the shape of the protagonist's head to be corrected. TRELLIS's dense head has a brow, a nose, a jaw and ears, but the game head was a smooth egg with the face painted on: no nose in profile, no ears, no jaw. The retopology fits the low mesh to a watertight shell made by a 2.2 cm morphological close, and gives it the body's uniform quad size, about 900 faces for the whole head; both sand the features off. The head's proportions were not the fault. Its widest point is about 10% of body height in both the mesh and the concept, and the chin is at the same height in both.

**Decision.**

- **The head is fitted to the dense head.** The dense mesh is closed and manifold above the neck, so `retopo_bake.py` subdivides the low head (each edge cut twice, `HEAD_CUTS=2`; about 900 faces become about 8,300) and fits it to the dense surface with three smoothing passes between fits (`HEAD_RELAX=3`), blending back to the shell's fit across the top of the neck.
- **The front of the face keeps 40% of the dense relief** (`HEAD_FACE_DEPTH=0.4`); the sides, back, jaw and ears keep all of it. TRELLIS's face relief is its own guess. At full depth its nose is heavier than the drawing and the photo's nose shadow lands under it, so the face reads as someone else.
- **The concept is registered to the head before it is projected.** TRELLIS's vertex colours sit on its own geometry, so the bake splats them into a front view at concept scale and computes dense optical flow (OpenCV Farneback, smoothed at 8 px, capped at 10 concept pixels, about 1.3 cm) from that view to the concept on the head. Head texels sample the concept through the flow, so the photo's eyes, nose and mouth land on the mesh's.

**Evidence.** The bake: `HEADFIT … faces 11880 -> 19278 … nonmanifold 0`; `HEADFLOW` median 4.2 px, grey error 33.3 → 28.1 on the head; holes 0. UV overlap at 1024²: 210 texels, against 280 in the body it replaces. The rig's checks are unchanged (74 bones, fingertip error 1.9 and 2.4 cm, wrist scales 0.99 and 1.12), as are the FBX's units, the pelvis and the import (bounds z 90.3, `SK_Mannequin`, material, physics, matte constants). The editor loads all 140 assets under `/Game/Characters` with no failures and the 12 inherited template references. Before and after in Blender: front, side and back geometry and the textured three-quarter view, `Docs/Captures/character-protagonist/head_shape_before_after.jpg`. `scripts/account-smoke-linux.sh editor` now also saves the protagonist's head turned 70° (`creator-protagonist-turned`) and passes (`Saved/AccountSmoke/20261112T102349Z`). `scripts/lent-smoke-linux.sh editor` passes (`Saved/LentSmoke/20261112T102220Z`).

**Not covered.** The face in the world from the front; a packaged run; the owner's playtest. The body is 38.6k triangles, against 23.8k before. The ears and the back of the head carry vertex colours. Up close the mouth is a little soft, and the nose casts a shadow on top of the one in the photo.

## ADR-060 — Kiosk 67: Petra Kralj's counter (accepted 2026-11-12)

**Problem.** Missions pay money, but nothing in the game takes it except one mission choice. The original's only start on trading is Kiosk 67's counter: `project/native/retail_rules.hpp`, `shops.json` and `docs/KIOSK_67_RETAIL.md` on the original's unmerged `integration/kiosk67-retail-reconciled-20261025`. It keeps the shelf in one file and the character in another, with a documented crash window between the two writes, and remembers only the last 64 transactions per shop, so an older replay can buy again.

**Decision.**

- **The original's counter, as data.** `gameplay.v0.1.json` gains three goods (`kind: goods`, the original's `trade: retail`): Mariborski Glas 20 (takes back for 8, 12 on the shelf), a Maribor picture postcard 35 (12, 8) and a ballpoint pen 15 (5, 6), and a `shops` entry for Kiosk 67 kept by Petra Kralj. Hours are Monday to Saturday 07:00–19:00 in Europe/Ljubljana, on the server's real clock with the original's EU summer-time rule. At most 5 of one good per request. The first purchase at the counter gives 10 Trading XP, once. Shelves are shared by every player and never restocked; a good sold back returns to the shelf, never past its starting stock. Prices remain the original's provisional gameplay numbers. `mb2 generate gameplay` emits the shop into the catalogue header and `mb2 validate gameplay` checks the keeper, goods, prices and limits.
- **One rule, one write.** `MarburgumRetailRules.h` (server only) is the original's `Apply` in its order of refusals: `shop_closed`, `invalid_quantity`, `not_on_counter` (buying what it does not sell), `item_not_accepted` (selling what it does not take back, such as a mission clue), `out_of_stock`, `insufficient_money`, `insufficient_items`. A purchase or sale changes the shelf, the purse, the goods and any first-purchase XP in the same world-store commit, so a crash cannot leave a shelf short with the character uncharged. A replay is answered from the store's receipts, which are kept for good.
- **World store schema 5** adds `shelves` (shop → item → copies left; an untouched line keeps its authored stock) and `first_purchases` (character → shops). Older documents load and are rewritten at schema 5 by the next commit. Counter receipts record the shop.
- **At the counter.** The keeper replicates the shelves and whether the counter is open; the server re-reads its clock every 10 seconds. E still talks to Petra and plays her missions; the prompt adds "[B] Counter, open/closed". B opens the counter panel: the goods with the price, what Petra pays back, the shelf and what you hold. A number buys one, Shift and the number sells one back. The panel stays open for the next request and closes on B, on another screen opening or on walking away. Receipts appear in the notification feed in Petra's words, with the closed-shutter line when shut.
- **Test clock.** `-MB2ClockHour=<h>` and `-MB2ClockWeekday=<0 Monday .. 6 Sunday>` on the server, as the original's `MARBURGUM_CLOCK_HOUR` / `MARBURGUM_CLOCK_WEEKDAY`.

**Evidence.** `Marburgum.Server.RetailRules` ports the original's rule checks: hours and an overnight window, price, shelf and first-purchase XP, a second purchase with no XP, buy-back capped at the starting shelf, the spread lost on buy-then-sell, each refusal leaving no next record, five at once allowed and six refused, and two buyers for the last copy. `Marburgum.Server.RetailClock` checks Ljubljana time across both summer-time changes and a leap day, and the 19:00 closing on a November Thursday. `Marburgum.Server.WorldStoreCondition` round-trips schema 5 and refuses negative or non-numeric shelves. `Marburgum.*` automation 13/13; `cargo test` for `tools/mb2` 30. `scripts/shop-smoke-linux.sh editor` seeds a schema 4 store (50 money, a brass key) and runs a dedicated server on `/Game/World/lent`: on a Thursday at 10:00 a rendered client buys a newspaper through the panel (−20, shelf 11, Trading +10), replays that operation (duplicate, nothing moves), is refused a postcard it cannot afford, six newspapers and selling the brass key, sells the newspaper back (+8, shelf 12), is refused selling it again and buys a pen (−15, no XP). A client at the arrival start is refused as out of range. `mb2 store-check shop` finds the store rewritten at schema 5 with exactly three counter commits. Restarted on a Sunday, a client finds 23 money, the pen, Trading 10 and shelves 12, 8, 5, the counter shut, and a purchase refused as `shop_closed`; the store is byte-identical afterwards. `Saved/ShopSmoke/20261112T150151Z` (first run `20261112T144115Z` also passed); panel captures in `Docs/Captures/kiosk-67/`. On the same code the mission smoke still plays all eight missions and reloads the store as schema 5 unchanged (`Saved/MissionSmoke/20261112T144632Z`), the Lent smoke passes (`Saved/LentSmoke/20261112T150301Z`), and the Editor, Server and Client targets build.

**Not covered.** A packaged run and the owner playtest. Two clients buying the last copy at the same moment over the network (the rule is tested; the server handles requests one at a time). A second counter, restocking, a moving market, reading the paper or using the pen. Petra does not stand behind a kiosk: the Lent level has no K67 kiosk model yet. The hours follow real Ljubljana time until a world clock exists, so the counter is shut for players on Sundays and evenings. Prices are not researched 1990s prices. Slovene screen text.

## ADR-061 — Weather from the Maribor climate, and wet roads under tyres and feet (accepted 2026-11-12)

**Problem.** Marburgum II has no weather. The car physics already carries the original's wet-surface table (`MarburgumVehicleDynamics.h` `SurfaceGrip`/`SurfaceTraction`), but it is always passed "dry", and Lent's ground has no surface semantics at all, so every road reads as unknown and stays neutral. Nobody on foot makes a sound. The original's automatic weather, on its unmerged `feat/dynamic-weather` branch (`MarburgumWeatherClimate.h/.cpp`, `MarburgumCalendar`, `project/data/climate`, `docs/CLIMATE_CALENDAR_WEATHER_ROADMAP.md`), drew a deterministic hourly plan per mirrored 1990s day from ARSO's Maribor 1971–2000 normals and eased the world's rain, thunder, wetness and snow toward it. Its footsteps (`MarburgumFootsteps.cpp`) played San Andreas placeholder banks, which are Rockstar's.

**Decision.**

- **The original's weather, ported.** `MarburgumWeather.h` (shared) is the original's climate code with its numbers and its deterministic salts: per mirrored date one of nine hourly states (clear, partly cloudy, overcast, light/steady/heavy rain, thunderstorm, fog, mist), wet days at the month's precipitation-day frequency with 40% carry-over, thunder, frontal rain and fog days, a temperature curve from the normals with a day-to-day anomaly, each hour blending into the next on CET all year. Precipitation and thunder ramp over 4 minutes; streets soak in about 15 minutes of moderate rain and dry over about an hour (faster warm, windy and sunny, barely in frost); snow settles below 0.5 °C, sleet up to 2 °C, and melts by degree-hours. A clock jump of more than 15 minutes rebuilds the last 36 hours instead of racing through them. The original's four fixed developer profiles (`clear_dry`, `overcast`, `rain_wet`, `winter_cold`) come along exactly. The climate files are copied unchanged to `Content/Marburgum/Weather/` and staged with the game.
- **The server runs it.** `UMarburgumWeatherAuthority` (server only) advances it once a second on trusted UTC and writes it onto the replicated game state (`AMarburgumGameState::Weather`, rounded so a slow drift does not resend it every second). Default `auto`; `-MB2Weather=<profile>` pins a profile, `-MB2WeatherClock=<ISO UTC>` and `-MB2WeatherSpeed=<factor>` give reviews and tests their own clock, `-MB2WorldSeed=` another history (default 1996). Without the climate files the server runs `clear_dry` and says so.
- **Lent's ground says what it is made of.** Physical materials `PM_asphalt`, `PM_paving`, `PM_cobblestone` and `PM_concrete` (`/Game/World/Ground/Surfaces`) are set, with the override flag a material instance needs, on the six paved materials: roads, yards and road paint asphalt, pavements paving, the old town's setts cobblestone, kerbs concrete. The landscape carries no physical material; its surface comes from `Content/Marburgum/Ground/lent_surface.bin`, which `lent-ground surface-kinds` reduces from the ground's own surface mask (`surface.png`) to the dominant kind per 2 m cell: hard standing (read as concrete), grass, earth (soil) or gravel and river stones (gravel). `MarburgumGround.h` answers "what is under this trace" for both. `scripts/create-weather-content.sh` assigns the materials.
- **Wet roads under tyres.** Each wheel reads its surface and the server's wetness: the original's table, so soaked asphalt keeps 0.82 of its traction and 0.78 of its lateral grip, setts 0.76/0.74, grass and earth less still; unknown ground stays neutral. Off the paving, the landscape's grass, earth and gravel now give their dry grip too, as the original's table always intended.
- **Footsteps.** A client subsystem plays a step whenever a foot bone of anyone on foot within 30 m comes down in the drawn pose (lifts 6 cm, lands within 2.5 cm of its low), so steps follow every gait, play rate and the IK-corrected feet; no clip has to be read in advance. The sound follows the ground and the weather: hard ground is Kenney Impact Sounds' `footstep_concrete` (CC0, the pack already used for punches), splashing once the streets are wet (all steps from wetness 0.6, none below 0.15); grass and earth `footstep_grass`; gravel and lying snow `footstep_snow`. The wet steps are those concrete steps with a generated splash (`mb2 generate wet-footsteps`: band-passed noise and a few droplets, never louder than the dry step). Walking is quieter than running; at most 12 steps play at once.
- **On the HUD.** The location banner gains a line of temporal context, "Light rain  ·  19 °C  ·  wet streets", and comes back for the current place when the weather turns.
- **Smokes stay deterministic.** The vehicle and street smokes pin `clear_dry`, so a real wet day cannot change their braking.

**Evidence.** `Marburgum.Shared.Weather.Climate` ports the original's checks: the mirror, loading the normals, refusing six malformed tunables, deterministic plans and another seed's other June, and forty simulated years per month converging on ARSO's precipitation, thunder and fog days and mean temperature. `Marburgum.Shared.Weather.Simulation`: two weeks in spring, summer and winter at 30 s steps with nothing faster than its ramp and temperature continuous over midnight; a July storm soaking the streets past 0.9 and a fine day drying them by noon; a cold wet winter day laying snow, never above 0.5 °C; a three-day clock jump landing where a fresh start does, and backwards. `Marburgum.Shared.Weather.Profiles` and `Marburgum.Shared.Surface` (physical-material names, paved slots, the landscape map's orientation and refusals, the committed 768 × 768 map, footstep sets, wet share, the footfall detector counting ten strides and no wobble). `Marburgum.*` automation 18/18 (one is a calendar printer that only prints when asked); `cargo test` for `tools/mb2` 32 and `tools/lent-ground` 28. `scripts/weather-smoke-linux.sh editor` (`Saved/WeatherSmoke/20261112T160238Z`) runs a dedicated server on `/Game/World/lent` three times with one rendered client at the hatchback: with `clear_dry` the client sees the profile and "Clear  ·  20 °C", traces asphalt under the car and the body, a 41 × 41 survey finds the paving as cobblestone, asphalt, paving and concrete and the landscape as hard standing, gravel, grass and soil, the car brakes from 8.4 m/s at 5.25 m/s², and ten steps on the setts are hard and dry; with `rain_wet` the same car brakes from 7.2 m/s at 4.50 m/s² (0.86 × dry; required ≤ 0.95) and nine steps all splash; on the live climate from 18 June 2026 10:00 CET at an hour a minute the street is overcast and dry, light rain sets in at 10:30 and the streets soak past 0.5 within the minute, the banner reading "Light rain  ·  19 °C  ·  wet streets", and nine steps splash. Earlier runs gave 0.80 × and 0.86 × (`20261112T155947Z`). The Editor, Server and Client targets build.

**Not covered.** What weather looks like: no rain, clouds, fog, wet sheen or lightning are drawn yet, so the HUD can say rain under a sunny sky. Weather sounds (rain, wind, thunder). The splash steps are generated and unheard by the owner. The original's fishing-bite and foliage-season effects. Snow on the ground beyond the footstep crunch. Buildings, bridges and other meshes stay unknown ground (hard steps, neutral grip). A packaged run and the owner playtest.

## ADR-062 — The weather drawn and heard: sky, rain, fog, wet and snowy ground, lightning and thunder (accepted 2026-11-12)

**Problem.** ADR-061 put the original's weather on the server, but nothing of it was drawn or heard: the HUD could say "Rain · 11 °C · wet streets" under Lent's sunny sky. The original drew its weather on the same unmerged `feat/dynamic-weather` branch (`MarburgumWeatherLook`, `MarburgumWeatherEffects`, `MarburgumWeatherShelter`, `MarburgumWeatherSound`/`Synth`, `MarburgumWeatherAssets`; roadmap section 12), and its Godot build gave every surface a wetness response (`wetness.gdshaderinc`).

**Decision.**

- **The original's look, ported.** `MarburgumWeatherLook.h` (shared) turns the replicated weather into numbers with the original's code: under cover the sun dims and widens (shadows soften), the clouds the client draws cast their own shade, the sky fill rises, colour and contrast drain; rain and fog thicken the height fog and turn it grey; the engine's simple volumetric cloud closes with the cover, darker in rain and with storm clouds under thunder; rain and snow lean with the wind by their fall speeds (8.5 and 1.2 m/s), downwind; thunder brings about three flashes a minute. Non-finite and out-of-range server values are clamped, never trusted. The look eases towards each new server step over about 3 seconds.
- **Recalibrated for Lent where Lent differs.** Lent's post-process uses histogram auto exposure with a bias of 0.4. That already opens the eye for a dimmer sun, so the original's exposure (+1.4 cover^1.5 − 0.2 rain) made Lent's promenade *brighter* under cloud than in sun (measured 1.14× clear). The weather's exposure is now added to the level's own bias rather than replacing it, and is −0.7 cover^1.5 − 1.0 rain: on the same view the promenade measures 185 of 255 clear, 172 overcast, 162 in steady rain and 159 in a thunderstorm. Lent's clear haze is thin (0.004), so full fog is 50 times it (the original's 15 left the far bank sharp at 150 m). Every other number is the original's.
- **Rain and snow round the camera.** The original's curtain: an open cylinder 6 m round and 24 m tall kept on the camera, leaned along the drops' path, with a procedural unlit material (its HLSL unchanged) drawing streaks and flakes scrolling at their fall speeds; a second copy 2.5 times larger draws finer, further rain. The colour goes through EyeAdaptationInverse, so the drops read the same whatever the exposure. A roof within 18 m above the camera (a static-world trace; a car or a body overhead is not a roof) stops the near curtain only, eased over a doorway.
- **Lightning and thunder.** A second, shadowless directional light that is dark except during a flash (a return stroke and up to two restrikes); it never takes forward shading from the sun. Each flash is at a random distance from 0.4 to 8 km and its thunder follows at 343 m/s, louder and cracking when close. Flashes are each client's own; the server says only how stormy it is.
- **The sound, synthesised.** The original's `FWeatherSound` unchanged (shared, plain C++): rain as a wide band-passed hiss with a patter of drops, wind as low noise swelling with the gusts, thunder as darkened brown noise in up to four rolling swells, soft-limited; played by a synth component as a stereo bed. There are no weather recordings to license.
- **The ground looks wet, and white under snow.** `MPC_Weather` carries the server's wetness and a snow cover (a centimetre of lying snow is a dusting, five centimetres hides the ground). `M_LentPaved` and `M_LentTerrain` read it with the Godot build's wetness response: sealed ground (all the paving, the landscape's hard standing) darkens to 0.6 and takes a film of water toward roughness 0.18; grass, earth, gravel, river stones and the retaining walls darken to 0.72 and barely change their sheen. Lying snow then whitens whatever faces up, patchily, and is matte. The snow cover is ours: the original's Unreal client drew snow only on its trees.
- **How it runs.** `UMarburgumWeatherPresentation` (client world subsystem; none on a dedicated server) spawns one transient `AMarburgumWeatherEffects` once the game state carries a weather and hands it every change. The actor finds Lent's own sun, sky light, height fog and grade, scales them from there and puts them back when it goes; its grade is its own post-process component at priority 10. `-MB2NoWeatherLook` leaves the level as built.
- **Content.** `scripts/create-weather-content.sh look` (`-run=MarburgumWeatherContent -Steps=look`, `MarburgumWeatherLookContent.cpp`) writes `/Game/Marburgum/Weather/` `SM_WeatherCurtain`, `M_WeatherCurtain`, `MI_WeatherClouds` (an instance of `/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst`, so a cooked client carries it; the step refuses if the engine material has lost the three scalars the client drives) and `MPC_Weather` (fixed parameter ids). `-run=MarburgumRebuildGround -MaterialsOnly` rebuilds the ground's materials alone, leaving the paved meshes, the ring and the level as they are. Without the content the client draws no rain, cloud or wet ground and says so in its log; the light, fog and grade still follow.
- **Not ported.** The trees' sway and season (`MPC_World` `world_wind`/`world_foliage`): Lent has no swaying vegetation.

**Evidence.** Unreal automation `Marburgum.*` 21/21 with `Marburgum.Shared.WeatherLook.Look` (clear, rain, snow, sleet, a trace called none, fog, thunderstorm, untrusted values, easing the short way round north, the flash envelope, thunder delay, wetness and snow cover), `.Shelter` and `.Sound` (the synth rendered and measured: silence when still; a downpour at RMS 0.21 with L/R correlation 0.00, a shower quieter; wind at 735 zero crossings a second against rain's 19,826; thunder silent for its travel time, then arriving, then dying away; a mono device; nonsense inputs silent). `scripts/weather-smoke-linux.sh editor` (`Saved/WeatherSmoke/20261112T170528Z`, [evidence](STAGE1_WEATHER_LOOK_EVIDENCE.md)) runs a dedicated server on `/Game/World/lent` nine times with one rendered client and checks the drawn look in each phase: ADR-061's dry, wet and live runs still pass (braking 5.39 against 4.51 m/s², 0.84×), and six look phases capture the same view: clear, overcast, rain, snow, a fog morning and a thunderstorm with a held flash. The Editor, Server and Client targets build.

**Not covered.** Whether the sound is any good: the smoke clients run with `-nosound`, so the bed is measured in tests but has not been heard in the game; nor have the footstep splashes. Puddles, splashes on the ground, ripples on the Drava, drips, wet façades, roofs and cars, and snow on roofs and walls: only the ground answers the weather. Night (there is no world clock), so the curtain's dimming by night is not needed yet. The flash at night, where it would matter most. Performance cost of the volumetric clouds and translucent curtain on lower settings. A packaged run. The owner's look and ear.
