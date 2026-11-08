# Magna Raedarum: Marburgum II

**Marburgum II** is a clean-room restart of *Magna Raedarum: Marburgum*, a systemic, persistent urban role-playing game set in a fictionalized **1990s Maribor, Slovenia**.

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

Explore a fictionalized 1990s Maribor and build a persistent life through missions, work, social relationships, travel, trading, vehicles, investigations and skills. Inspiration from systemic RPGs concerns depth of progression and interactions rather than a fantasy setting. Combat and PvP are not the default progression loop. The game is designed to be satisfying alone, in small cooperative groups, and eventually in larger authoritative online sessions.

- **Skills, not talents.** The previous project's generated talent system is not part of this restart.
- **Missions, not quests.** Ordinary urban experiences, choices, and consequences establish the tone.
- **A long campaign is a design goal, not a current implementation.** First prove a compact playable vertical slice.
- **Persistent, authoritative state** is an architectural goal; no existing backend is claimed to be working.
- **Historical/geospatial data and art** must have documented provenance and appropriate use rights. No copyrighted game-engine code or third-party commercial-game assets are copied into this repository.

## Repository layout

```text
MarburgumII.uproject         Unreal Engine project descriptor
Source/                     Unreal C++ game module and build targets
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
./scripts/build-linux.sh
```

The Linux build script invokes UnrealBuildTool through the specified engine; it does not download/build Unreal Engine. For other environments, consult [Docs/DEVELOPMENT.md](Docs/DEVELOPMENT.md).

Run independent toolchain checks:

```bash
dotnet run --project tools/MarburgumII.Tools -- verify
cargo test --manifest-path native/marburgum-sim-core/Cargo.toml
```

## Documentation

- [Game scope and design constraints](Docs/VISION.md)
- [Architecture and language boundaries](Docs/ARCHITECTURE.md)
- [Development prerequisites and commands](Docs/DEVELOPMENT.md)
- [Roadmap and acceptance gates](Docs/ROADMAP.md)
- [Engineering decisions](Docs/DECISIONS.md)

## Licensing

**No project-wide license has yet been selected.** Publication of this repository does not, by itself, grant permission to reuse source or assets. Do not add third-party content without verifying provenance and license compatibility.
