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

## Client/server targets

The game is designed as an MMORPG from Stage 0. On Linux use `UE_ROOT` pointing to **exactly 5.8.3** and build separate executable targets:

```bash
./scripts/build-linux.sh MarburgumIIEditor
./scripts/build-linux.sh MarburgumIIClient
./scripts/build-linux.sh MarburgumIIServer
```

The unqualified build script invocation defaults to the Editor target. A combined `MarburgumII` Game target is for local development only. Source validation alone proves no network behaviour; use the network smoke below. See [NETWORK_ARCHITECTURE.md](NETWORK_ARCHITECTURE.md).

## Test map, packaging and network smoke (Linux)

```bash
./scripts/create-maps.sh              # regenerate Content/Maps/FrontEnd, NetSmoke and Lent (+ Content/Blockout/Materials) with the editor Python commandlet; build the Editor first
./scripts/package-linux.sh            # BuildCookRun: Saved/StagedBuilds/LinuxServer and LinuxClient
./scripts/net-smoke-linux.sh          # Gate 0B against the packaged builds
./scripts/net-smoke-linux.sh editor   # same test with uncooked UnrealEditor -server / -game processes
./scripts/ui-capture-linux.sh         # Stage 0C rendered-client UI captures against the packaged builds
./scripts/chat-smoke-linux.sh         # CHAT-001: dedicated server + sender/recipient/bystander clients
./scripts/interaction-smoke-linux.sh  # Gate 1: interaction, receipts, race, server restart, admission refusals
./scripts/lent-smoke-linux.sh         # Lent slice: location, NPC, saved mission, objective/journal, restart, privacy
```

The smoke script starts one headless dedicated server on port 7777 (override with `MB2_SMOKE_PORT`). It runs one client through a refused mutation, an idempotent operation and a disconnect/reconnect, then two concurrent clients. Logs and `summary.txt` go to `Saved/NetSmoke/<UTC timestamp>/`. A nonzero exit means a failed expectation. The PythonScriptPlugin and EditorScriptingUtilities plugins are enabled only for the Editor target.

The UI capture script runs real `MarburgumIIClient` processes with `-RenderOffScreen -ForceRes` (a GPU is required; no display is needed) and screenshots the composited window, UMG included, whenever the session settles in a new phase. It captures the front-end at four resolutions, the UI-01 component gallery, a connection failure against an unused port (`MB2_CAPTURE_DEAD_PORT`, default 7790), and a server-loss scenario: the client joins a packaged dedicated server, the script stops the server, waits for the client to show reconnecting, restarts the server and expects automatic recovery. PNGs, logs and `summary.txt` go to `Saved/UICapture/<UTC timestamp>/`.

The chat smoke starts a packaged server and three clients: A sends general, private, invalid and burst messages, B replies privately, C is the bystander. Each client checks its own deliveries and exits nonzero on any mismatch; the script also counts server-accepted messages and greps every log for private bodies. A and C render offscreen and save screenshots of their chat panel (`MB2_CHAT_RENDER` selects which roles render). Evidence goes to `Saved/ChatSmoke/<UTC timestamp>/`.

The interaction smoke runs two server sessions on one world store in `Saved/InteractSmoke/<UTC timestamp>/`. Phase 1 starts with one injected write fault (`-MB2StoreFaultInjection` and `world-store.json.fail`) and runs clients A and B: a refused faulty commit, a grant, a replay of the same operation id, an out-of-range request, a second grant and a race for the last unit. The script then checks the store file (3 grants, stock 0, 3 receipts) and restarts the server. Phase 2 checks restored inventories and stock, a replay answered `Duplicate`, a keyless client, a duplicate login and a malformed key (both refused at admission), and a later session of the same character. The store must be byte-identical afterwards. A, D and A3 render offscreen one at a time; on the test host a second offscreen Vulkan client could not create its device while another was rendering, so the script also retries a renderer whose Vulkan device creation fails before connecting.

The Lent smoke runs `/Game/Maps/Lent` twice on one world store in `Saved/LentSmoke/<UTC timestamp>/`. Phase 1 (one injected write fault): M starts at the riverside angler. Its faulty first talk is refused and leaves no mission and no item behind. The mission is then accepted with its start item in one commit, the same operation is replayed (`Duplicate`), the next step is refused for want of a fish, and the journal opens. Meanwhile W, on Stari most, is refused out of range, sees none of M's progress, inventory or location, and walks north until the server places it in Lent. K, without a character key, is refused. The script checks the store file (schema 2, one commit) and restarts the server. Phase 2: M2 sees its mission, objective and rod restored, and the replay is still a `Duplicate`. T walks from the bridge into Lent for the location banner. The store must be byte-identical afterwards. M, M2 and T render offscreen one at a time.

Server-side rules also have an editor automation test:

```bash
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/MarburgumII.uproject" -ExecCmds="Automation RunTests Marburgum;Quit" \
    -unattended -nullrhi -stdout -TestExit="Automation Test Queue Empty"   # Marburgum.Chat.TextPolicy, Marburgum.Server.MissionRules
```

### Playing the Lent slice by hand

```bash
Saved/StagedBuilds/LinuxServer/MarburgumIIServer.sh /Game/Maps/Lent -port=7777 -log
Saved/StagedBuilds/LinuxClient/MarburgumIIClient.sh -MB2Character=simon
```

You arrive on the Lent promenade, and the location banner names the district. The riverside angler stands 30 m west at the railing. Walk up and press **E** to talk: the server starts "Something in the Water", hands over a fishing rod and pins the objective (top left). Press **E** again and he asks for a fish, which cannot be caught until fishing exists (Stage 2). **J** opens the journal and **I** the inventory. Walk east onto Stari most and back to see the banner change between "Maribor" and "Lent". The store is `.../WorldStore/Lent.json` next to the NetSmoke file named below; delete it to start the mission afresh. The map is a fallback blockout, not the licensed city model (ADR-016). `/Game/Maps/Lent` is also the server's default map now.

### Playing the Gate 1 slice by hand

```bash
Saved/StagedBuilds/LinuxServer/MarburgumIIServer.sh /Game/Maps/NetSmoke -port=7777 -log
Saved/StagedBuilds/LinuxClient/MarburgumIIClient.sh -MB2Character=simon
```

Connect from the front-end, walk with **W A S D** and the mouse, press **E** at the test supply crate, **I** for the inventory and **Enter** for chat. The packaged server keeps its world store in `Saved/StagedBuilds/LinuxServer/MarburgumII/Saved/MarburgumII/WorldStore/NetSmoke.json` (override with `-MB2WorldStore=<file>`); delete that file to refill the crates and empty every inventory. The net, chat and UI-capture smokes load this default file but never write it; the interaction smoke uses its own file in its evidence directory. Without `-MB2Character=` the client uses a key generated once into its own `Saved/MarburgumII/DevCharacter.txt`, so two clients on one machine need different `-MB2Character=` values: the server refuses a character that is already connected. The key is a test identity, not a login (ADR-014).

Client UI options for manual runs: `-MB2Server=host:port` (front-end default address), `-MB2UIGallery` (component gallery instead of the connection shell), `-MB2UIDiagnostics` (labeled live replicated-state overlay), `-MB2UIScale=<0.5-3.0>` (interface scale on top of DPI scaling).

## Local asset production (conditional)

The project owner reports **Hunyuan3D-2.1** installed locally. The [local generator specification](ASSET_GENERATION_HUNYUAN3D.md) records the intended image → 3D shape → PBR texture → retopology → Unreal 5.8.3 import path. Installation location and execution have **not** been verified here. Its published community license excludes EU territory; production use remains blocked until an applicable license/permission is established. The model is not a dependency of the runtime or normal server build.

## Fresh owner playtests

Use [PLAYTEST_FRESHNESS.md](PLAYTEST_FRESHNESS.md) and the guard `scripts/playtest-freshness.py`, with a privately configured `Saved/PlaytestProfile/profile.json`. It validates Git/source revision, hashes tracked and locally licensed asset inputs, rebuilds and recooks into a new client/server package and verifies exact archives before launch. The example profile has not been qualified with real UAT output paths and **does not** prove that a native Unreal playtest exists. Always validate that generator outputs, custom import intermediate steps and local purchased assets are included in the actual profile.

## Messaging validation (CHAT-001)

General and private text chat source resides in `AMarburgumPlayerController`; client UMG resides in `MarburgumIIClient`. See [CHAT.md](CHAT.md). Use a genuine UE **5.8.3** Editor/Client/Server compilation plus the native automation test `Marburgum.Chat.TextPolicy`, followed by **three** separate clients to prove that a bystander never receives private text. The source-only chat checker in CI is a dependency/regression check, **not** a live network or owner-acceptance test. Chat source code currently exists, but no native compilation/runtime result is verified.

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
