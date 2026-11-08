# Architecture — initial baseline

## Engine invariant

**Unreal Engine 5.8.3 is required.** `.unreal-engine-version` pins the exact patch. The portable `EngineAssociation` in `MarburgumII.uproject` uses `5.8` because official engine associations may be major/minor identifiers; source-built engines can use machine-specific GUIDs. Neither proves patch level. Verify `Engine/Build/Build.version` before each Unreal build.

## Technology boundaries

| Component | Language | Responsibility | Status |
| --- | --- | --- | --- |
| Unreal game module | C++ | Runtime actors, game mode, eventual gameplay and networking | Minimal scaffold |
| UnrealBuildTool | C# | Engine's `.Target.cs` and `.Build.cs` build rules | Configured |
| External tools | C# / .NET 8 | Manifest checks, future data pipelines and tooling | First validator |
| Simulation library | Rust | Pure deterministic native operations through stable C ABI | Crate and tests; not linked to UE |
| World/levels | Unreal Content | Streaming maps and authored levels | Not imported |
| Authoritative state/persistence | C++ and future reviewed services | State ownership, missions, economies and networking | Proposed only |

**C# is not being advertised as an Unreal gameplay scripting runtime.** UnrealBuildTool happens to use C#, while our .NET application is an independent executable.

**Rust is not yet part of the Unreal runtime.** `native/marburgum-sim-core` exports a versioned C ABI with fixed-width scalar types, no memory ownership transfer and no engine headers. Before adding a native bridge, build and test Linux/Windows static libraries, test ABI version negotiation, add explicit UnrealBuildTool linker instructions and run deterministic cross-language fixtures. Rust source must remain free of implicit engine dependencies.

## Unreal project module

- `MarburgumII.uproject`: runtime module registration.
- `Source/MarburgumII.Target.cs`: game target.
- `Source/MarburgumIIEditor.Target.cs`: editor target.
- `Source/MarburgumIIServer.Target.cs`: dedicated server *target*, not a tested service. UE installation and licensing/build configuration may influence its availability.
- `Source/MarburgumII/MarburgumII.Build.cs`: core engine dependencies.
- `AMarburgumGameMode`: minimal startup game mode configured in `Config/DefaultEngine.ini`.

No starter map or UI asset has been created. The game mode does not imply a player character, complete input setup or playable environment.

## Future runtime principles

1. **Server authority:** decide which state is authoritative; clients predict or display but do not directly grant rewards, currency or progression.
2. **Durable persistence:** migrations, state versioning, transactional receipts, idempotent writes and crash recovery precede irreversible economic rewards.
3. **Deterministic logic:** stable integer/fixed-point operations and replayable tests where distributed simulation requires them; avoid relying on wall-clock calls inside gameplay reducers.
4. **Data provenance:** do not use map, landmark, imagery, music or game assets without reviewable source, transformation, and license.
5. **Performance budgets:** city-scale streaming, virtual NPC population, network replication and memory overhead require measured tests.
6. **Playable milestones:** implement one complete loop and UI first, rather than exposing dozens of textual placeholders.

## Acceptance

A valid source tree and green C#/Rust CI are **not** a passed Unreal build. Stage 0 requires a **real UE 5.8.3** native build, UHT completion, module loading, Editor launch and documented play-in-editor smoke test.
