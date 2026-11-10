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

**Not covered.** Crash-safety of condition between logouts: a server killed with SIGKILL loses the breath spent since the last save, which is acceptable for breath and must be revisited when vitality can fall. Drowning, falls, swimming, guarding and combat stamina costs (the rest of `combat.h`). Stamina for skills or work. Sprint animation and camera feedback. Re-measuring the one-point correction tolerance under real latency and packet loss.

## ADR-021 — Characters use the UE5 mannequin skeleton; the template mannequin is committed (accepted 2026-11-10)

**Problem.** The project had no character art. A first protagonist prototype was made outside the tree: an owner-supplied concept image, TRELLIS image-to-3D, skin weights transferred from Manny in Blender, then an FBX import into UE 5.8.3. Its skeletal mesh needs a skeleton asset in the project.

**Decision.**

- **One skeleton for human characters: `/Game/Characters/Mannequins/Meshes/SK_Mannequin`.** The template locomotion animations, Control Rig and IK Rig then work on every character without retargeting.
- **Epic's template mannequin is committed** under `Content/Characters/Mannequins/`, byte-identical to the engine's `Templates/TemplateResources/High/Characters/Content/Mannequins`, at the owner's direction, under the Unreal Engine EULA.
- **Source art lives in `SourceArt/`** (concept, generator output, rigged FBX); the imported assets live in `Content/`. `*.glb` joins the Git LFS patterns.
- **The authoring pipeline stays outside the repository.** Its steps are Blender and Unreal editor Python, which ADR-018 keeps out of the tree. Its procedure and checks are recorded in [CHARACTER_PROTAGONIST.md](CHARACTER_PROTAGONIST.md).

**Not covered.** Whether the research-only licences of the rasterizers used for TRELLIS's texture bake reach the baked texture in a commercial game. Hands, retopology and the final texture. The player pawn now uses the mesh (ADR-023).

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

## Open decisions

- Clear the TRELLIS texture-bake rasterizer licences (nvdiffrast, Inria) for shipped textures, or rebake characters with vertex colours (ADR-021).
- Verify production rights for the locally installed Hunyuan3D-2.1 version, specifically use within Slovenia/EU, generated outputs and subsequent game distribution.
- Public license and rules for distributing source/art; no license has been selected.
- Minimum shipping platforms and tested build matrix.
- Production persistence engine, migrations, account/session admission, login security and service topology; initial Unreal-native replication is accepted for the first vertical slice. The ADR-014 development key and file store are stand-ins, not answers.
- Rust static-library integration across Windows/Linux and ABI test approach.
- Geospatial/historical data provenance and acceptable licensing.
- Run and profile DLSS on a Windows build (ADR-029); whether DLSS is offered on Linux depends on NVIDIA adding Linux support to its plugin.
- A DLSS 5 runtime the project is licensed to use, or an official NVIDIA DLSS 5 package for UE 5.8 (ADR-030).
