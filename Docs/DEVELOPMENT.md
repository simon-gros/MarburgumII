# Development environment

## Required toolchains

Unreal Engine **5.8.3 exactly**, its corresponding platform-native C++ toolchain, Git and Git LFS, Python 3, .NET SDK 8 and Rust stable.

No Unreal binaries are distributed here. You must separately install the engine and satisfy its license and platform-specific dependencies.

## Linux / CachyOS

```bash
git clone https://github.com/simon-gros/MarburgumII.git
cd MarburgumII
git lfs install

export UE_ROOT=/absolute/path/to/UnrealEngine-5.8.3
./scripts/check-engine-version.sh "$UE_ROOT"
./scripts/build-linux.sh
```

`check-engine-version.sh` compares the engine's `MajorVersion.MinorVersion.PatchVersion` to `.unreal-engine-version` and rejects any mismatch. `build-linux.sh` invokes the Unreal-supplied `Engine/Build/BatchFiles/Linux/Build.sh` to build `MarburgumIIEditor Linux Development`. The script does not install or compile Unreal Engine from source; the specified engine must already be usable.

If the engine was built from source, a local installation GUID may be required for desktop double-click workflows. Register or choose the association on the local machine; don't replace the portable `EngineAssociation` in the committed project with your own GUID.

## Windows

Install/register Unreal Engine **5.8.3** and its supported C++ compiler toolchain. Generate IDE project files through your installed engine, then build `MarburgumIIEditor` in `Development Editor | Win64`. Confirm `Engine/Build/Build.version` reports 5.8.3, even if the desktop engine association says only `5.8`.

## Client/server targets (source scaffold)

The game is designed as an MMORPG from Stage 0. On Linux use `UE_ROOT` pointing to **exactly 5.8.3** and build separate executable targets:

```bash
./scripts/build-linux.sh MarburgumIIEditor
./scripts/build-linux.sh MarburgumIIClient
./scripts/build-linux.sh MarburgumIIServer
```

The unqualified build script invocation defaults to the Editor target. A combined `MarburgumII` Game target is for local development only. The first network milestone still requires launching an actual headless server, connecting one then two clients, observing replicated GameState and testing reconnect; source validation alone proves none of these. See [NETWORK_ARCHITECTURE.md](NETWORK_ARCHITECTURE.md).

## Local asset production (conditional)

The project owner reports **Hunyuan3D-2.1** installed locally. The [local generator specification](ASSET_GENERATION_HUNYUAN3D.md) records the intended image → 3D shape → PBR texture → retopology → Unreal 5.8.3 import path. Installation location and execution have **not** been verified here. Its published community license excludes EU territory; production use remains blocked until an applicable license/permission is established. The model is not a dependency of the runtime or normal server build.

## Fresh owner playtests

Use [PLAYTEST_FRESHNESS.md](PLAYTEST_FRESHNESS.md) and the guard `scripts/playtest-freshness.py`, with a privately configured `Saved/PlaytestProfile/profile.json`. It validates Git/source revision, hashes tracked and locally licensed asset inputs, rebuilds and recooks into a new client/server package and verifies exact archives before launch. The example profile has not been qualified with real UAT output paths and **does not** prove that a native Unreal playtest exists. Always validate that generator outputs, custom import intermediate steps and local purchased assets are included in the actual profile.

## Independent source checks

```bash
dotnet run --project tools/MarburgumII.Tools -- verify
cargo test --manifest-path native/marburgum-sim-core/Cargo.toml
cargo build --release --manifest-path native/marburgum-sim-core/Cargo.toml
```

The C# program checks repository manifest contracts, but does not inspect the local Unreal installation. The Rust library is an unlinked and independently testable experimental component.

## Required acceptance evidence

For an Unreal build claim, record the exact Git SHA, engine metadata, OS/compiler versions, command line, UBT/UHT completion, editor launch and in-editor smoke-test result. Save failures and remediation in a reviewable issue or document before extending gameplay.

Git LFS rules cover Unreal binary assets; generated `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/` and local credentials must not be committed. Third-party assets require rights and provenance review.
