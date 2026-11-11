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
