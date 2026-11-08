# Architecture — initial baseline

## Engine invariant

**Unreal Engine 5.8.3 is required.** `.unreal-engine-version` pins the exact patch. The portable `EngineAssociation` in `MarburgumII.uproject` uses `5.8` because official engine associations may be major/minor identifiers; source-built engines can use machine-specific GUIDs. Neither proves patch level. Verify `Engine/Build/Build.version` before each Unreal build.

## Technology boundaries

| Component | Language | Responsibility | Status |
| --- | --- | --- | --- |
| Unreal common game module | C++ | Server-only GameMode, replicated GameState / PlayerState and shared runtime registration | Source foundation, no network smoke |
| Unreal shared module | C++ | Engine-neutral network/gameplay contract types | Source foundation |
| Unreal client module | C++ | Presentation, input and client-only adapters | ClientOnly source foundation |
| Unreal server module | C++ | Server-side simulation and persistence adapters | ServerOnly source foundation; no services yet |
| UnrealBuildTool | C# | Engine's `.Target.cs` and `.Build.cs` build rules | Configured |
| External tools | C# / .NET 8 | Manifest checks, future data pipelines and tooling | First validator |
| Simulation library | Rust | Pure deterministic native operations through stable C ABI | Crate and tests; not linked to UE |
| World/levels | Unreal Content | Streaming maps and authored levels | Not imported |
| Authoritative state/persistence | C++ and future reviewed services | State ownership, missions, economies and networking | Proposed only |

**C# is not being advertised as an Unreal gameplay scripting runtime.** UnrealBuildTool happens to use C#, while our .NET application is an independent executable.

**Rust is not yet part of the Unreal runtime.** `native/marburgum-sim-core` exports a versioned C ABI with fixed-width scalar types, no memory ownership transfer and no engine headers. Before adding a native bridge, build and test Linux/Windows static libraries, test ABI version negotiation, add explicit UnrealBuildTool linker instructions and run deterministic cross-language fixtures. Rust source must remain free of implicit engine dependencies.

## MMORPG-first Unreal module and target boundaries

**Dedicated client/server architecture is now a Stage 0 obligation, not a Stage 4 feature.** See [NETWORK_ARCHITECTURE.md](NETWORK_ARCHITECTURE.md) for canonical authority, entity ownership, replication, AOI, admission and multiplayer acceptance requirements.

- `MarburgumII.uproject` registers `MarburgumII` (Runtime), `MarburgumIIShared` (Runtime), `MarburgumIIClient` (ClientOnly) and `MarburgumIIServer` (ServerOnly).
- `MarburgumIIClient.Target.cs` builds a player-facing client without server module code.
- `MarburgumIIServer.Target.cs` builds a dedicated server without client module code.
- `MarburgumIIEditor.Target.cs` loads both sides for testing; `MarburgumII.Target.cs` remains a combined local development target, not the deployed MMORPG client/server split.
- Both sides depend on shared contracts but **never on each other**.
- `AMarburgumGameMode` exists on authority and configures an `AMarburgumGameState` with a replicated version marker and an `AMarburgumPlayerState`. These are source foundations, **not a proven online session**.

No starter map, login, durable persistence service, UI or client/server network smoke test has been completed in this repository.

## Future runtime principles

1. **Server authority from Stage 0:** every consequential game-state transition is server-owned even when a single player is testing locally; clients request, predict or display but cannot directly grant rewards, currency or progression.
2. **Durable persistence:** migrations, state versioning, transactional receipts, idempotent writes and crash recovery precede irreversible economic rewards.
3. **Deterministic logic:** stable integer/fixed-point operations and replayable tests where distributed simulation requires them; avoid relying on wall-clock calls inside gameplay reducers.
4. **Data provenance:** do not use map, landmark, imagery, music or game assets without reviewable source, transformation, and license.
5. **Performance budgets:** city-scale streaming, virtual NPC population, network replication and memory overhead require measured tests.
6. **Playable, visually reviewable milestones:** build a professional graphical HUD and polished primary player panels in parallel with the first networked gameplay loop; do not expose raw debug indices or treat mock screenshots as functioning gameplay. UI ownership and dependency gates are defined in [UI_HUD_ONTOLOGY.md](UI_HUD_ONTOLOGY.md), [UI_HUD_DESIGN.md](UI_HUD_DESIGN.md) and [UI_HUD_ROADMAP.md](UI_HUD_ROADMAP.md).

## Acceptance

A valid source tree and green C#/Rust CI are **not** a passed Unreal build. Stage 0 requires **real UE 5.8.3** Client/Server/Editor native builds, UHT completion, module loading, Editor launch, a dedicated-server process joined by one and then two separate clients, a replicated GameState check and documented reconnect/authority smoke evidence.
