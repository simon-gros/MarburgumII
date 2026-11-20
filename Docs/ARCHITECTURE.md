# Architecture — initial baseline

**Morning integration warning:** Source CI failed after the imported Python archive tooling contradicted accepted ADR-018. [ADR-077](DECISIONS.md) resolved this by porting the tooling to Rust `mb2`; both workflows now run on every revision. Only five ADR-076 shell doors have editor walks; a packaged build is not yet staged door acceptance. See [morning review](DEVELOPMENT_REVIEW_2026-11-20_MORNING.md).

**Evening status, 20 November 2026:** UE 5.8.3 client/server boundaries remain binding. ADR-080 and ADR-083 implement server-bound one-use account tickets and per-session AES-256-GCM, with packaged smoke/wire evidence. ADR-081 implements Chapter IV, Photography and schema-6 persistent gameplay; ADR-076 generates data for 586 ground-floor candidates but has only **five** physically cut enterable shells, with staged multi-client proof for two. UI v2 includes functional Settings and English/Slovene text. No single-current-head complete game package or owner acceptance is claimed. [Latest review](DEVELOPMENT_REVIEW_2026-11-20_EVENING.md).



## Engine invariant

**Unreal Engine 5.8.3 is required.** `.unreal-engine-version` pins the exact patch. The portable `EngineAssociation` in `MarburgumII.uproject` uses `5.8` because official engine associations may be major/minor identifiers; source-built engines can use machine-specific GUIDs. Neither proves patch level. Verify `Engine/Build/Build.version` before each Unreal build.

## Technology boundaries

| Component | Language | Responsibility | Status |
| --- | --- | --- | --- |
| Unreal common game module | C++ | Server-only GameMode, replicated GameState / PlayerState, client→server request PlayerController, test character, interactables, abstract authority service | Builds; Gate 0B and Gate 1 smokes pass |
| Unreal shared module | C++ | Engine-neutral network/gameplay contract types and the shared network log category | Builds |
| Unreal client module | C++ | Presentation, input and client-only adapters; session, UI and test drivers | Builds; Stage 0C UI and Gate 1 HUD modules |
| Unreal server module | C++ | Interaction authority and the development world store (ADR-014/015); admission telemetry | Builds; Gate 1 smoke passes |
| UnrealBuildTool | C# | Engine's `.Target.cs` and `.Build.cs` build rules | Configured |
| External tools | C# / .NET 10 | Project manifest checks, future data pipelines | Manifest validator |
| Repository tooling | Rust (`tools/mb2`) | Source-contract validators, playtest freshness guard, smoke-test helpers (ADR-018) | Validators, guard and helpers with unit tests |
| Editor content tooling | C++ (`MarburgumIIEditorTools`, Editor target only) | Map generator commandlet for FrontEnd, NetSmoke and Lent | Generates all three maps |
| Simulation library | Rust | Pure deterministic native operations through stable C ABI | Crate and tests; not linked to UE |
| World/levels | Unreal Content | Planned licensed Maribor GIS geometry, independently authored test map, World Partition and reviewed historical/semantic overlays | No licensed map purchased/imported or tested |
| Authoritative state/persistence | C++ and future reviewed services | State ownership, missions, economies and networking | Gate 1 interaction on a development JSON store; production engine undecided |

**C# is not being advertised as an Unreal gameplay scripting runtime.** UnrealBuildTool happens to use C#, while our .NET application is an independent executable.

**Rust is not yet part of the Unreal runtime.** `native/marburgum-sim-core` exports a versioned C ABI with fixed-width scalar types, no memory ownership transfer and no engine headers. Before adding a native bridge, build and test Linux/Windows static libraries, test ABI version negotiation, add explicit UnrealBuildTool linker instructions and run deterministic cross-language fixtures. Rust source must remain free of implicit engine dependencies.

## MMORPG-first Unreal module and target boundaries

**Dedicated client/server architecture is now a Stage 0 obligation, not a Stage 4 feature.** See [NETWORK_ARCHITECTURE.md](NETWORK_ARCHITECTURE.md) for canonical authority, entity ownership, replication, AOI, admission and multiplayer acceptance requirements.

- `MarburgumII.uproject` registers `MarburgumII` (Runtime), `MarburgumIIShared` (Runtime), `MarburgumIIClient` (ClientOnly) and `MarburgumIIServer` (ServerOnly).
- `MarburgumIIClient.Target.cs` builds a player-facing client without server module code.
- `MarburgumIIServer.Target.cs` builds a dedicated server without client module code.
- `MarburgumIIEditor.Target.cs` loads both sides for testing; `MarburgumII.Target.cs` remains a combined local development target, not the deployed MMORPG client/server split.
- Both sides depend on shared contracts and the core module's replicated types but **never on each other**; core and shared never depend on a side.
- `AMarburgumGameMode` exists on authority and configures an `AMarburgumGameState` with a replicated version marker and an `AMarburgumPlayerState`. These are source foundations, **not a proven online session**.

A test map (`/Game/Maps/NetSmoke`, with two labeled test supply crates and four player starts) and the Gate 0B client/server smoke exist; see [STAGE0_EVIDENCE.md](STAGE0_EVIDENCE.md). The client starts on `/Game/Maps/FrontEnd`, whose `AMarburgumFrontEndGameMode` owns no gameplay, so an unconnected client never simulates rules locally (ADR-013). Both maps are regenerated by `scripts/create-maps.sh`. Development-grade first-launch identity **does exist** (ADR-055/057): a self-hosted Rust HTTPS account service, email codes in a development outbox, Argon2id credentials, one durable character and single-use dedicated-server admission tickets. The world uses the file-backed development world store; **neither service is production-deployed or a production data platform**. See [first-launch ontology](FIRST_LAUNCH_ONTOLOGY.md), [19 November review](DEVELOPMENT_REVIEW_2026-11-19.md) and the [fresh-build guard](PLAYTEST_FRESHNESS.md).

**Client presentation (`MarburgumIIClient`, Stage 0C):** `UMarburgumSessionSubsystem` observes the connection lifecycle (phases in shared `MarburgumSessionContract.h`) and reports `ready` only after verified admission. `UMarburgumUISubsystem` builds one `UMarburgumUIRoot` per world with ordered layers (HUD, notifications, screens, modal, diagnostic) and maps the session view onto the connection shell or the quiet in-world HUD. All widgets are C++ UMG assembled by `MarburgumUI::FBuilder` from the versioned tokens in `MarburgumUITheme`. The Server target contains none of these classes. See [STAGE0C_UI_EVIDENCE.md](STAGE0C_UI_EVIDENCE.md).

**Gate 1 interaction (Stage 1 entry):** `AMarburgumCharacter` is the third-person protagonist (ADR-023) on the engine's networked `CharacterMovementComponent`, so the server validates range against real pawn positions. E sends `ServerRequestInteraction` with a fresh operation id; the core module routes it to the abstract `UMarburgumAuthorityService`, whose only implementation, `UMarburgumWorldAuthority`, lives in `MarburgumIIServer` (ADR-015). It commits to `FMarburgumWorldStore` before any replicated state changes (ADR-014). The client mounts `UMarburgumInteractionPrompt` (HUD layer) and `UMarburgumInventoryPanel` (screens layer, I key) at admission, and turns receipts into keyed notifications. See [STAGE1_GATE1_EVIDENCE.md](STAGE1_GATE1_EVIDENCE.md).

## Server-owned street and gameplay AI (ADR-068–075)

`UMarburgumStreetSubsystem`, `AMarburgumPedestrian` and `FMarburgumWalkGraph` implement server-owned spawning, walk-graph travel, incidents, district/day-hour composition and visits to mapped destinations. The original game's groups, district-hour tables and visit weights feed shared pure C++ street rules; graph nodes carry zone/activity, while ADR-069 adds 239 destinations and door/bus-stop/sight behavior. `AMarburgumFighter` chooses server-side attacks through the shared combat contract; ADR-070 adds a blocked-chase give-up and blocked-edge recovery. Authored mission actors remain stationary server interactables, and mission outcomes are durable server transactions. Clients do not pick pedestrian goals or grant blows/rewards. There is **no** Unreal Behavior Tree, StateTree, EQS, NavMesh or MassEntity street implementation. The [AI status/backlog](AI_GAMEPLAY.md) is authoritative for unresolved detours, traffic, perception and authored NPC activity. ADR-068/069 editor smokes and ADR-070 automation pass. ADR-073/074 add player-local district budgeting, shared crowds and vertical/horizontal district holds; ADR-075 audits and corrects false paving/bridge edge rejection. The clean `c967c46` packaged two/three-client matrix passes most scenes but **opposite banks time out at 11/28**. See [20 November review](DEVELOPMENT_REVIEW_2026-11-20.md).

## Server-routed chat boundary

A [general/private chat slice](CHAT.md) has been committed in C++ source: player-owned RPCs in `MarburgumII`, a client-only UMG chat panel in `MarburgumIIClient`, and per-client private delivery rather than globally replicated private state. Earlier Linux dedicated-server/three-client routing and private-message isolation passed their recorded smoke matrix; current-source packaged requalification, abuse controls, UI refinement and the owner's personal acceptance remain open. The current limited rate cap is not production moderation.

## Commercial map and city-source boundary

The primary **planned** map source is [TurboSquid Maribor city model 1639711](WORLD_MAP_ADOPTION.md), contingent upon purchase/license verification, coordinate/format inspection, 1990s historical correction, Unreal 5.8.3 import/playtest and explicit owner approval. It replaces the prior generated 121-window city as the default geometry production path. The old pipeline remains reference/fallback; original district, mission, vegetation, fish and authored world identities remain engine-neutral. Paid assets must never be committed to public source or public build artifacts; see the ignored `ThirdParty/Licensed/` and `Content/Licensed/` paths.

## Optional local image-to-3D authoring

**Microsoft TRELLIS (`microsoft/TRELLIS`) is the selected offline image-to-3D development pipeline from ADR-071 (19 November 2026); Hunyuan3D-2.1 is superseded and not an approved fallback.** The already-produced protagonist uses TRELLIS vertex-colour geometry and independent Blender/Cycles baking. TRELLIS is neither a runtime/server dependency nor a replacement for the surveyed city, map identities or licensed candidate map. Clear per-input/image/weight/export/baking rights, including the documented restrictions on some TRELLIS rasterizers, before producing content; optimize and owner-test all promoted assets. See [ASSET_GENERATION_TRELLIS.md](ASSET_GENERATION_TRELLIS.md).

### QA and packaged-build boundary (20 November 2026)

The [QA audit](QA_AUDIT_2026-11-19.md) records stronger HTTPS/account limits, RPC validation, durable world-store writes, package stamps, missing path-loaded cook content and executable hash sealing. Rendering and NPC replication optimizations were merged; developer probes are excluded from Shipping builds. `MarburgumIIClient` has the new Escape menu, scroll and text-focus improvements. Remaining risks include account ticket audience (AR1), creator overlap (U9), absent native UE CI and owner acceptance. A passing build or subset smoke does not certify an MMORPG release.

## Future runtime principles

1. **Server authority from Stage 0:** every consequential game-state transition is server-owned even when a single player is testing locally; clients request, predict or display but cannot directly grant rewards, currency or progression.
2. **Durable persistence:** migrations, state versioning, transactional receipts, idempotent writes and crash recovery precede irreversible economic rewards.
3. **Deterministic logic:** stable integer/fixed-point operations and replayable tests where distributed simulation requires them; avoid relying on wall-clock calls inside gameplay reducers.
4. **Data provenance:** do not use map, landmark, imagery, music or game assets without reviewable source, transformation, and license.
5. **Performance budgets:** city-scale streaming, virtual NPC population, network replication and memory overhead require measured tests.
6. **Playable, visually reviewable milestones:** build a professional graphical HUD and polished primary player panels in parallel with the first networked gameplay loop; do not expose raw debug indices or treat mock screenshots as functioning gameplay. UI ownership and dependency gates are defined in [UI_HUD_ONTOLOGY.md](UI_HUD_ONTOLOGY.md), [UI_HUD_DESIGN.md](UI_HUD_DESIGN.md) and [UI_HUD_ROADMAP.md](UI_HUD_ROADMAP.md).

## Acceptance

A valid source tree and green C#/Rust CI are **not** a passed Unreal build. Stage 0 requires **real UE 5.8.3** Client/Server/Editor native builds, UHT completion, module loading, Editor launch, a dedicated-server process joined by one and then two separate clients, a replicated GameState check and documented reconnect/authority smoke evidence.
