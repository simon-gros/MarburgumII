# Magna Raedarum: Marburgum II

**Marburgum II** is a clean-room technical rebuild of *Magna Raedarum: Marburgum*: an authoritative shared-world **MMORPG** set in a fictionalized **1990s Maribor, Slovenia**, preserving the original project's core game design and systems.

> **Status:** Initial source scaffold. No Unreal Editor build, packaged executable, imported city map, or gameplay vertical slice has yet been validated in this repository.

## Required engine and languages

| Component | Version / purpose |
| --- | --- |
| **Unreal Engine** | **5.8.3 (required)** |
| **C++** | Primary Unreal-native gameplay, simulation integration, networking and engine code |
| **C# / .NET 8** | Standalone development utilities, project validation and asset/data pipelines; UnrealBuildTool also uses C# build rules |
| **Rust (stable)** | Isolated deterministic native simulation components with a versioned C ABI; integration is staged, not yet linked to the Unreal runtime |

The exact patch requirement is tracked in [`.unreal-engine-version`](.unreal-engine-version). Epic's `EngineAssociation` is a local engine registration identifier or major/minor launcher association and **must not** be treated as proof that patch 5.8.3 is installed. The build scripts check `Engine/Build/Build.version` to enforce the actual engine patch.

## Project vision

Explore a fictionalized 1990s Maribor and build a persistent life through missions, work, social relationships, travel, trading, vehicles, investigations and skills. Inspiration from systemic RPGs concerns depth of progression and interactions rather than a fantasy setting. Combat and PvP are not the default progression loop. The game is **MMORPG-first** architecturally: even its initial one-player testing uses a dedicated authoritative server, while solo-friendly and 2–5-player cooperative gameplay are central experiences. High-population servers must be proven by measurements, not assumed.

- **Skills, not talents.** The previous project's generated talent system is not part of this restart.
- **Missions, not quests.** Ordinary urban experiences, choices, and consequences establish the tone.
- **A long campaign is a design goal, not a current implementation.** First prove a compact playable vertical slice.
- **Early showcase-quality graphical UI/HUD.** Preserve the original's place/interaction/objective/skills/inventory/journal structure in a polished UE 5.8.3 client interface, built alongside the networking milestone. **Wanted stars and police chases are excluded.**
- **Client/shared/server separation exists from Stage 0.** Real connection, replication and persistence tests still remain to be implemented and passed.
- **Historical/geospatial data and art** must have documented provenance and appropriate use rights. No copyrighted game-engine code or third-party commercial-game assets are copied into this repository.

## Repository layout

```text
MarburgumII.uproject         Unreal Engine project descriptor
Source/MarburgumII/         Common game module (GameMode, replicated GameState / PlayerState)
Source/MarburgumIIShared/   Shared gameplay/network contracts
Source/MarburgumIIClient/   ClientOnly input/UI/presentation foundation
Source/MarburgumIIServer/   ServerOnly authority/service foundation
Source/*.Target.cs          Client, dedicated Server, Editor and dev Game build targets
Config/                     Unreal project settings
native/marburgum-sim-core/   Rust native library and C ABI header (not yet linked)
tools/MarburgumII.Tools/    .NET 8 project validation utility
scripts/                    Portable engine/version/build entry points
Docs/                       Vision, architecture, setup and development milestones
.github/workflows/          Rust + C# checks, *not* a substitute for an Unreal build
```

## Getting started

1. Install **Unreal Engine 5.8.3**, its platform toolchain, Git LFS, .NET 8 SDK and stable Rust.
2. Clone the repository and run `git lfs install`.
3. Point `UE_ROOT` to the **5.8.3** engine root.
4. Check the installed patch and build the Unreal Editor target on Linux:

```bash
export UE_ROOT=/path/to/UnrealEngine-5.8.3
./scripts/check-engine-version.sh "$UE_ROOT"
./scripts/build-linux.sh MarburgumIIEditor
./scripts/build-linux.sh MarburgumIIClient
./scripts/build-linux.sh MarburgumIIServer
```

The Linux build script invokes UnrealBuildTool through the specified engine; it does not download/build Unreal Engine. For other environments, consult [Docs/DEVELOPMENT.md](Docs/DEVELOPMENT.md).

Run independent toolchain checks:

```bash
dotnet run --project tools/MarburgumII.Tools -- verify
cargo test --manifest-path native/marburgum-sim-core/Cargo.toml
```

## Verified local prerequisites (2026-11-08)

The development machine runs **CachyOS Linux, x86_64**. All requested
prerequisites are installed; shell environment selection is required:

| Prerequisite | Verified installation / result |
| --- | --- |
| Unreal Engine 5.8.3 | `/mnt/data/unreal/5.8.3`; `Engine/Build/Build.version` reports exactly **5.8.3**, and the repository's version check passes. |
| Linux platform toolchain | Bundled **v26_clang-20.1.8-rockylinux8**, matching `Engine/Config/Linux/Linux_SDK.json`; bundled Clang runs and UnrealBuildTool `-Mode=ValidatePlatforms -Platforms=Linux` reports **Linux VALID**. Use the engine-selected SDK, not the system Clang 23.1.4. |
| Git / Git LFS | Git **2.56.0**, Git LFS **3.8.1**; LFS filters configured. |
| .NET 8 SDK | **8.0.425**, with .NET runtime **8.0.31**, installed under `$HOME/.dotnet`. The default `/usr/bin/dotnet` sees only the separate system SDK **10.0.114**. |
| Stable Rust | `stable-x86_64-unknown-linux-gnu` is the default; `rustc` and Cargo **1.99.1**; all **3** simulation-core tests pass. |
| Python | **3.14.9**; engine metadata check passes. |
| Graphics runtime | Vulkan enumerates an **NVIDIA GeForce RTX 5080**, driver **620.70.06**; this is not an editor rendering test. |

For this machine, start from the canonical checkout and select the installed tools:

```bash
cd /mnt/data/MarburgumII
export UE_ROOT=/mnt/data/unreal/5.8.3
export DOTNET_ROOT="$HOME/.dotnet"
export PATH="$DOTNET_ROOT:$PATH"
./scripts/check-engine-version.sh "$UE_ROOT"
dotnet --list-sdks
```

The user-local .NET installation also contains newer SDKs. The PATH setting makes
.NET 8 available but does **not** pin SDK selection to 8; use a `global.json`
SDK pin when an exact .NET 8 SDK build is needed. During this audit, the validation
utility was compiled with SDK **8.0.425** using an isolated temporary SDK pin,
then run successfully from the repository with the user-local .NET host.
UnrealBuildTool was checked using the engine's own bundled .NET host.

**Validation boundary:** the earlier prerequisite audit passed, but the new client/shared/server modules have **not** been compiled. The prior audit did not build `MarburgumIIEditor`, launch the editor, validate a packaged executable, or run an in-editor gameplay smoke test. No packages were installed
and no persistent shell configuration was changed.

## Documentation

- [Game scope and design constraints](Docs/VISION.md)
- [Foundational game ontology (v0.1.0)](Docs/ONTOLOGY.md) and [machine-readable vocabulary](Data/Ontology/core.v0.1.json)
- [Original Marburgum design parity register](Docs/LEGACY_PARITY.md) and [machine-readable source matrix](Data/Parity/source-matrix.v0.1.json)
- [Architecture and language boundaries](Docs/ARCHITECTURE.md)
- [MMORPG client/server contract and milestone gates](Docs/NETWORK_ARCHITECTURE.md)
- [Early graphical UI and HUD design contract](Docs/UI_HUD_DESIGN.md)
- [Development prerequisites and commands](Docs/DEVELOPMENT.md)
- [Roadmap and acceptance gates](Docs/ROADMAP.md)
- [Engineering decisions](Docs/DECISIONS.md)

## Licensing

**No project-wide license has yet been selected.** Publication of this repository does not, by itself, grant permission to reuse source or assets. Do not add third-party content without verifying provenance and license compatibility.
