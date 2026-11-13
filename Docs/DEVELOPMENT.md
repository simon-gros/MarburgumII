# Development environment

## Required toolchains

Unreal Engine **5.8.3 exactly**, its corresponding platform-native C++ toolchain, Git and Git LFS, .NET SDK 10 (pinned by `global.json`) and Rust stable. Python is not used (ADR-018).

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
./scripts/create-maps.sh              # regenerate Content/Maps/FrontEnd, NetSmoke and Lent (+ Content/Blockout/Materials) with the C++ commandlet `-run=MarburgumCreateMaps`; build the Editor first
./scripts/create-swim-content.sh      # regenerate A_Swim (the original swim cycle on the mannequin) and the Drava water volumes in /Game/World/lent (`-run=MarburgumSwimContent`, ADR-032)
./scripts/create-vehicle-content.sh   # regenerate the five catalogue cars (the original procedural bodywork), M_Vehicle, A_EnterVehicle/A_ExitVehicle and the lent_hatchback placement (`-run=MarburgumVehicleContent`, ADR-039)
./scripts/create-combat-content.sh    # regenerate A_FightGuard/A_FightJab/A_FightHeavy (the original fight clips on the mannequin) and the CC0 impact sounds in /Game/Audio/Combat (`-run=MarburgumCombatContent`, ADR-041)
./scripts/create-weather-content.sh   # the rain/snow curtain, cloud material and MPC_Weather under /Game/Marburgum/Weather (look, ADR-062); set PM_asphalt/PM_paving/PM_cobblestone/PM_concrete on Lent's paved materials and import the CC0 and wet footsteps into /Game/Audio/Footsteps (`-run=MarburgumWeatherContent`, ADR-061); the landscape surface map is `lent-ground surface-kinds`, the wet steps `scripts/mb2.sh generate wet-footsteps`. Run `look` before `-run=MarburgumRebuildGround` (its paving and landscape materials read MPC_Weather); `-run=MarburgumRebuildGround -MaterialsOnly` rebuilds just those materials
# Lent and its eight neighbouring windows (ADR-045), 1,536 m square round Stari most. Window names and data come from the
# original's project/assets/terrain ($ORIGINAL); the LiDAR tiles from its tools/fetch_lidar.sh.
WINDOWS=lent,ljudski_vrt,center,avtobusna,pristan,titov_most,kurilnica,magdalena,pobreska
./scripts/fetch-lent-osm.sh           # only to refresh the committed OpenStreetMap extract (Overpass)
# Buildings (ADR-034, ADR-045): join the windows' footprints across their edges, then rebuild every building as a closed shell,
# place the ones that are not in the level yet with the neighbours' bridges and river sheets, and write placed_buildings.txt.
# -CheckOnly builds and checks without saving.
cargo run --release --manifest-path tools/lent-ground/Cargo.toml -- merge-buildings --windows $WINDOWS \
    --terrain "$ORIGINAL/project/assets/terrain" --out Data/World/lent_ground/buildings.json
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/MarburgumII.uproject" -run=MarburgumRebuildFacades -unattended -nop4 -nosplash -nullrhi
# Ground (ADR-040, ADR-045): heights, paving and surface mask from the survey and OpenStreetMap, then the level, the river volumes and the car.
LAZ=$(printf '%s,' ~/marburgum-data/arso-2014/gkot/TM_{548,549,550}_{156,157,158}.laz)
ORTHO=$(for w in ${WINDOWS//,/ }; do printf '%s,' "$ORIGINAL/project/assets/terrain/${w}_albedo.jpg"; done)
cargo run --release --manifest-path tools/lent-ground/Cargo.toml -- --laz "$LAZ" \
    --osm Data/World/lent_ground/osm_lent.json.gz --buildings Data/World/lent_ground/buildings.json \
    --placed Data/World/lent_ground/placed_buildings.txt --ortho "$ORTHO" --out Data/World/lent_ground \
    --walk-graph Content/Marburgum/Street/lent_walk_graph.json   # and the street people's walk graph (ADR-044)
# The ground tool records deeper wall footings for buildings whose surveyed ground falls more than 2.5 m below their base
# (ground.json buildings.deep_footings_m, ADR-053); rebuild the façades once more so their walls reach it.
# The distant ring round the square (ADR-054): the same survey tiles out to their edge, meeting the new landscape's edge.
cargo run --release --manifest-path tools/lent-ground/Cargo.toml -- ring --laz "$LAZ" \
    --heights Data/World/lent_ground/heights.bin --out Data/World/lent_ground
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/MarburgumII.uproject" -run=MarburgumRebuildFacades -unattended -nop4 -nosplash -nullrhi
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/MarburgumII.uproject" -run=MarburgumRebuildGround -unattended -nop4 -nosplash -nullrhi
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/MarburgumII.uproject" -run=MarburgumSwimContent -Steps=water -unattended -nop4 -nosplash -nullrhi
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/MarburgumII.uproject" -run=MarburgumVehicleContent -Steps=place -unattended -nop4 -nosplash -nullrhi
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/MarburgumII.uproject" -run=MarburgumRebuildGround -Verify -unattended -nop4 -nosplash -nullrhi   # every paved triangle: ray and capsule check
# Place names (ADR-063): the register's districts and the heritage register's bespoke landmarks from the original's data,
# the named streets and squares from the OpenStreetMap extract, in one file the server and the client read; then put the
# launch map's Lent area back to the original's areas.json strip, so the quarters name the rest.
cargo run --release --manifest-path tools/lent-ground/Cargo.toml -- places --osm Data/World/lent_ground/osm_lent.json.gz \
    --districts "$ORIGINAL/project/data/districts.json" --landmarks "$ORIGINAL/project/data/landmarks.json" \
    --out Content/Marburgum/Places/lent_places.json
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/MarburgumII.uproject" -run=MarburgumCreateMaps -LentArea -unattended -nop4 -nosplash -nullrhi
# Fixed-camera screenshots of a level for before/after evidence (centimetres; name@eye@target@fov;...)
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor" "$PWD/MarburgumII.uproject" /Game/World/lent -game -RenderOffScreen -windowed -ForceRes -ResX=1920 -ResY=1080 \
    "-MB2ViewShots=overview@-6000,-6000,11000@17000,-6000,2000@60" -MB2ViewShotDir=/abs/dir
./scripts/package-linux.sh            # BuildCookRun: Saved/StagedBuilds/LinuxServer and LinuxClient
./scripts/net-smoke-linux.sh          # Gate 0B against the packaged builds
./scripts/net-smoke-linux.sh editor   # same test with uncooked UnrealEditor -server / -game processes
./scripts/ui-capture-linux.sh         # Stage 0C rendered-client UI captures against the packaged builds
./scripts/chat-smoke-linux.sh         # CHAT-001: dedicated server + sender/recipient/bystander clients
./scripts/interaction-smoke-linux.sh  # Gate 1: interaction, receipts, race, server restart, admission refusals
./scripts/lent-smoke-linux.sh         # Lent slice: location, NPC, saved mission, objective/journal, restart, privacy
./scripts/condition-smoke-linux.sh    # Player condition: running costs breath, server-decided, saved at logout/stop, restart
./scripts/swim-smoke-linux.sh         # Swimming (ADR-032): swim, sink, held breath, wade out, drown, recovery, saved wait, restart
./scripts/vehicle-smoke-linux.sh      # Driving (ADR-039): parked car, enter clip, driver + passenger, throttle, steer, brake, reverse, exits, exit view (ADR-056)
MB2_VEHICLE_NET_CULL_M=9 ./scripts/vehicle-smoke-linux.sh   # The same with the car sent within 9 m: only the occupants keep it live (ADR-056)
./scripts/combat-smoke-linux.sh       # Fighting (ADR-041): the drinker fights back, guard, jab, heavy swing, too soon, NPC and player floored
./scripts/street-smoke-linux.sh editor # Street people (ADR-044): pedestrian graph/spawn/walk, fights, incident responses, car run-over/dodge
```

The smoke script starts one headless dedicated server on port 7777 (override with `MB2_SMOKE_PORT`). It runs one client through a refused mutation, an idempotent operation and a disconnect/reconnect, then two concurrent clients. Logs and `summary.txt` go to `Saved/NetSmoke/<UTC timestamp>/`. A nonzero exit means a failed expectation. Map generation is the editor-only C++ commandlet in `MarburgumIIEditorTools`; the PythonScriptPlugin and EditorScriptingUtilities plugins are disabled.

The UI capture script runs real `MarburgumIIClient` processes with `-RenderOffScreen -ForceRes` (a GPU is required; no display is needed) and screenshots the composited window, UMG included, whenever the session settles in a new phase. It captures the front-end at four resolutions, the UI-01 component gallery, a connection failure against an unused port (`MB2_CAPTURE_DEAD_PORT`, default 7790), and a server-loss scenario: the client joins a packaged dedicated server, the script stops the server, waits for the client to show reconnecting, restarts the server and expects automatic recovery. PNGs, logs and `summary.txt` go to `Saved/UICapture/<UTC timestamp>/`.

The chat smoke starts a packaged server and three clients: A sends general, private, invalid and burst messages, B replies privately, C is the bystander. Each client checks its own deliveries and exits nonzero on any mismatch; the script also counts server-accepted messages and greps every log for private bodies. A and C render offscreen and save screenshots of their chat panel (`MB2_CHAT_RENDER` selects which roles render). Evidence goes to `Saved/ChatSmoke/<UTC timestamp>/`.

The interaction smoke runs two server sessions on one world store in `Saved/InteractSmoke/<UTC timestamp>/`. Phase 1 starts with one injected write fault (`-MB2StoreFaultInjection` and `world-store.json.fail`) and runs clients A and B: a refused faulty commit, a grant, a replay of the same operation id, an out-of-range request, a second grant and a race for the last unit. The script then checks the store file (3 grants, stock 0, 3 receipts) and restarts the server. Phase 2 checks restored inventories and stock, a replay answered `Duplicate`, a keyless client, a duplicate login and a malformed key (both refused at admission), and a later session of the same character. The store must be byte-identical afterwards. A, D and A3 render offscreen one at a time; on the test host a second offscreen Vulkan client could not create its device while another was rendering, so the script also retries a renderer whose Vulkan device creation fails before connecting.

The Lent smoke runs `/Game/Maps/Lent` twice on one world store in `Saved/LentSmoke/<UTC timestamp>/`. Phase 1 (one injected write fault): M starts at the riverside angler. Its faulty first talk is refused and leaves no mission and no item behind. The mission is then accepted with its start item in one commit, the same operation is replayed (`Duplicate`), the next step is refused for want of a fish, and the journal opens. Meanwhile W, on Stari most, is refused out of range, sees none of M's progress, inventory or location, and walks north until the server places it in Lent. K, without a character key, is refused. The script checks the store file (schema 2, one commit) and restarts the server. Phase 2: M2 sees its mission, objective and rod restored, and the replay is still a `Duplicate`. T walks from the bridge into Lent for the location banner. The store must be byte-identical afterwards. M, M2 and T render offscreen one at a time.

The Lent smoke also qualifies movement time (ADR-019). F, a `-nullrhi` adversary, runs its own world clock at 2.5× real time (`-MB2LentClockRate=2.5`) while it walks from the bridge into Lent. `scripts/mb2.sh movement-check` reads, for every walker (W, F and T), the server's spawn and Lent-entry positions and the client's walk start. Each walker must have covered no more than `MaxWalkSpeed` × (elapsed + `MovementTimeDiscrepancyMaxTimeMargin`) + 0.5 m. The server must log `event=time_discrepancy` for F and for no one else. Honest walkers also fail if their world clock ran ahead of real time. Results go to `movement.txt` and `summary.txt`. For a negative control, disable detection on the server: `MB2_LENT_SERVER_ARGS='-ini:Game:[/Script/Engine.GameNetworkManager]:bMovementTimeDiscrepancyDetection=False' ./scripts/lent-smoke-linux.sh` must FAIL with F `TOO FAST`. `MB2_LENT_CLIENT_ARGS='-ini:Game:[/Script/Engine.WorldSettings]:MinUndilatedFrameTime=0.0005'` restores the engine's frame-time floor on the clients and must FAIL with W `clock_runs_fast`. `cargo test --manifest-path tools/mb2/Cargo.toml` unit-tests the bound without Unreal. Since ADR-020 the bound also allows (run − walk speed) × the seconds the server itself simulated the pawn running (`run_s` on the server's `event=area` lines); the smoke walkers never run, so for them it is the walking bound.

The condition smoke (ADR-020) runs `/Game/Maps/Lent` twice on one world store in `Saved/ConditionSmoke/<UTC timestamp>/`. Every client starts at `arrival` and runs east along the promenade. Phase 1: R, a new character, arrives rested (server, prediction, HUD and Character panel). It runs at 4.4 m/s and is winded after about ten seconds. It walks while still asking to run, runs again once breath is back, and leaves the moment it is winded again. C, a `-nullrhi` adversary, ignores its breath and claims a full bar on every move. The server must still wind it after about ten seconds, correct it and hold it to walking speed. The script checks that the server saved both at logout and checks the store (revision 2, R winded with at most 5 stamina, no custody). Phase 2 restarts the server: R2 arrives winded from the store, walks although it asks to run, runs once breath is back and rests to full, which removes its entry. S runs and is still connected when the script stops the server; the stop must save it. The final store is revision 4. R and R2 render offscreen and save `rested`, `character`, `running`, `winded` and `rested-again` screenshots.

Server-side rules also have an editor automation test:

```bash
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/MarburgumII.uproject" -ExecCmds="Automation RunTests Marburgum;Quit" \
    -unattended -nullrhi -stdout -TestExit="Automation Test Queue Empty"   # Marburgum.Chat.TextPolicy, Marburgum.Server.MissionRules
```

### Development qualifications through ADR-059 (12 November 2026)

The commands above describe **regeneration order**, not proof that the whole map and all scripts pass as a single packaged release. The new default `/Game/World/lent` covers **nine 512 m original windows** in one 1,536 m square. Use an explicit original-game content root `$ORIGINAL` and locally available nine LiDAR LAZ tiles/orthophoto inputs. **Do not** commit unrelated licensed originals, full paid map source, raw proprietary files or private credentials when rebuilding.

`lent-ground merge-buildings` must run before `MarburgumRebuildFacades`; the placed-building manifest must exist before generating ground and pedestrian walk graph, and the façades are rebuilt once more after the ground tool so they take its deep footings (ADR-053). `lent-ground ring` runs after the ground tool, since it meets the new `heights.bin` at the square's edge, and before `MarburgumRebuildGround`, which builds and places the ring (ADR-054). Generate ground/paving, then resample **all** Drava water volumes and re-place the hatchback after the ground changes. Regenerate swim, vehicle and combat animation assets after ADR-042/043's foot IK fixes before cooking. Include `Content/Marburgum/Street/lent_walk_graph.json` and any other non-`.uasset` staged game data in the source/cook hash manifest. `-Verify -Every=1` is the full paving check; record result counts and source SHA.

**Evidence interpretation:**

- ADR-040 first Lent-only pavement verification: **476,517** samples, before the expansion.
- ADR-045 nine-window saved-level verification: **4,089,551** paved samples, 0 landscape-first and 0 sunk capsules; street, combat, swimming and vehicle **editor-mode** smokes passed. This is not a packaged/client-performance pass.
- ADR-041 combat: a **packaged dedicated-server/two-client** smoke passed on an earlier revision. ADR-043's fixed rendered feet have separate **editor-mode** evidence; repackage for the current world.
- ADR-044 ambient people: **one-client editor-mode** street smoke passed; `./scripts/street-smoke-linux.sh staged` and a separate two/three-client scenario are **future acceptance tests**, not recorded passes.
- Some older Lent/condition fixtures still use `/Game/Maps/Lent`, a distinct test world, but **the new `mission-smoke-linux.sh editor` plays all eight original missions on `/Game/World/lent`** (ADR-047–052), including temporary replicated pads for Melje/Tabor locations outside the nine-window map. A fallback test does not qualify today's world; the editor full-mission test does not equal a packaged two-client regression.

**Expanded-world P0 performance and multiplayer gate:** fresh client/server cook at an exact clean SHA, map and root-manifest hashes; run camera/travel and player/angler/car/swim routes across eight window edges; sample cold load, memory/VRAM, CPU/GPU frame-time p95/p99, draw calls and server tick. Stress a controlled 1/2/4/8-client population and capture remote street reactions, NPC duplication/relevance, body retention, collision, combat down/recovery, car dodge/run-over and reconnect. The expanded world is **a single always-loaded level with no World Partition**; do not claim streaming or MMO-scale performance until measured. Fourteen surveyed deep hollows require a recorded historical/gameplay decision, not an automatic fill.

See [the 12 November development review](DEVELOPMENT_REVIEW_2026-11-12.md), [the 11 November change audit](DEVELOPMENT_UPDATE_2026-11-11.md), [ground and map decisions](DECISIONS.md), [street evidence](STAGE1_STREET_EVIDENCE.md), [combat evidence](STAGE1_COMBAT_EVIDENCE.md), and [freshness acceptance procedure](PLAYTEST_FRESHNESS.md).

### Playing the Lent slice by hand

```bash
Saved/StagedBuilds/LinuxServer/MarburgumIIServer.sh /Game/World/lent -port=7777 -log
Saved/StagedBuilds/LinuxClient/MarburgumIIClient.sh -MB2Character=simon
```

You arrive on the Lent promenade, and the location banner names the district. Hold **Shift** to run: the condition card (lower left) shows stamina draining, and after ten seconds you are winded and walk until some breath is back. **C** opens the Character panel. The riverside angler stands 30 m west at the railing. Walk up and press **E** to talk: the server starts "Something in the Water", hands over a fishing rod and pins the objective (top left). Press **E** again and he asks for a fish. Walk to the Drava bank and press **E** to cast; when the line bites, press **E** again to strike, then show him the catch (ADR-048). **K** opens Skills. **J** opens the journal and **I** the inventory. Walk east onto Stari most and back to see the banner change between "Center", the quarter, with "Stari most" under it, and "Lent"; up into the old town it names the street or square you are on and the landmark you walk up to: the Rotovž on Glavni trg, the cathedral on Slomškov trg, the towers along Lent (ADR-063). The store is `.../WorldStore/Lent.json` next to the NetSmoke file named below; delete it to start the mission afresh. The game, editor and server now open the original Unreal Lent level, `/Game/World/lent` (ADR-027). Every Lent façade has been rebuilt with windows, a door and a cornice; the footprints and roofs are the original shells. `/Game/Maps/Lent` is only the older blockout, still used by the mission smokes. The front-end remains, and the UI capture opens it by name.

The world runs on Maribor time (ADR-064): one server clock in Europe/Ljubljana time at real speed, its date mirrored into the 1990s (the banner and the Character panel show it, "Wed 13 Nov 1996 · 20:15"), and the sun, the moon and the night over Lent follow it, so playing in the evening is playing at night. To look at another hour, start the server with `-MB2Clock=<ISO 8601 UTC>` (for instance `-MB2Clock=2026-06-21T10:57:00Z`, solstice noon) and optionally `-MB2ClockSpeed=<factor>` (1440 is a day a minute); ADR-061's `-MB2WeatherClock` and `-MB2WeatherSpeed` are the same options. A client started with `-MB2NoDaylight` keeps the level's own sun.

The original's city hatchback is parked where the original game put it, about 160 m west of the arrival start along the promenade (ADR-039). To start beside its driver's door, connect straight to the server:

```bash
Saved/StagedBuilds/LinuxClient/MarburgumIIClient.sh '127.0.0.1:7777?MB2Start=lent_hatchback?MB2Character=simon'
```

Press **E** at a door to get in (the driver's seat whenever its door is in reach), **W** to drive, **S** to brake and, once stopped, reverse, **A**/**D** to steer, **Shift** for the handbrake and **E** again to get out. The card in the lower right shows the speed. The mouse looks around; the camera swings back behind the car after a few seconds. A second client can ride along in a passenger seat. Cars are silent, and the car returns to its spot when the server restarts.

Three of the original's people stand where it put them (ADR-041). The Belligerent drinker is on the promenade beside the hatchback, with the Evening walker a few metres along; the Newspaper reader is up on the square. To start a few steps in front of the drinker:

```bash
Saved/StagedBuilds/LinuxClient/MarburgumIIClient.sh '127.0.0.1:7777?MB2Start=fight_lent_drunk?MB2Character=simon'
```

**F** throws a jab, **R** a committed swing and holding **G** raises the guard, the original's keys. A swing needs the body facing him within 2 m. Each one costs stamina and has a cooldown; one thrown too soon is simply refused. The feed (upper right) reports each blow, a miss, and a swing refused for want of breath. He leaves you alone until you hit him, then fights back until one of you is down. Your guard takes a quarter of his blow and costs stamina; with no stamina left it breaks. A body with no vitality left goes down, is taken to the recovery point and gets up after 60 s; nothing restores vitality otherwise, as in the original. The walker steps away from a fight. The drinker gets up at home a minute after he goes down. Players cannot hit players.

### Playing the Gate 1 slice by hand

```bash
Saved/StagedBuilds/LinuxServer/MarburgumIIServer.sh /Game/Maps/NetSmoke -port=7777 -log
Saved/StagedBuilds/LinuxClient/MarburgumIIClient.sh -MB2Character=simon
```

Connect from the front-end, walk with **W A S D** and the mouse, press **E** at the test supply crate, **I** for the inventory and **Enter** for chat. The packaged server keeps its world store in `Saved/StagedBuilds/LinuxServer/MarburgumII/Saved/MarburgumII/WorldStore/NetSmoke.json` (override with `-MB2WorldStore=<file>`); delete that file to refill the crates and empty every inventory. The net, chat and UI-capture smokes load this default file but never write it; the interaction smoke uses its own file in its evidence directory. Without `-MB2Character=` the client uses a key generated once into its own `Saved/MarburgumII/DevCharacter.txt`, so two clients on one machine need different `-MB2Character=` values: the server refuses a character that is already connected. The key is a test identity, not a login (ADR-014).

Client UI options for manual runs: `-MB2Server=host:port` (front-end default address), `-MB2UIGallery` (component gallery instead of the connection shell), `-MB2UIDiagnostics` (labeled live replicated-state overlay), `-MB2UIScale=<0.5-3.0>` (interface scale on top of DPI scaling).

## NVIDIA render features (ADR-029)

DLSS Super Resolution/DLAA, DLSS Frame Generation, Reflex and DLSS 5 Neural Rendering (unofficial DLSS5ForUE5, ADR-030) are optional, client-local settings. NVIDIA's DLSS package is installed locally in `Plugins/` and never committed. TSR with no generated frames is what runs whenever one is unavailable, which on Linux is always: NVIDIA's UE plugin has no Linux support. Every client logs one `MB2RENDER` line naming the RHI, GPU, the request, the active features and the reason for each fallback.

```bash
scripts/mb2.sh render-probe "$UE_ROOT"           # JSON: NVIDIA plugins, GPU/driver, NGX runtime, Smooth Motion layer
scripts/smooth-motion-linux.sh -- -MB2Start=arrival   # staged client under NVIDIA Smooth Motion (driver frame generation)
# client: -MB2SafeRender | -MB2Upscaler=auto|tsr|dlss -MB2DlssMode=dlaa|quality|balanced|performance|ultra_performance
#         -MB2FrameGeneration=off|dlss -MB2Reflex=auto|off|on|boost -MB2NeuralRendering=off|safe|guided; console mb2.Render.Status and mb2.Render.<setting> <value>
```

See [RENDERING_NVIDIA.md](RENDERING_NVIDIA.md) for the rules, the platform table and how to install and verify NVIDIA's package on Windows.

## Local asset production (conditional)

The project owner reports **Hunyuan3D-2.1** installed locally. The [local generator specification](ASSET_GENERATION_HUNYUAN3D.md) records the intended image → 3D shape → PBR texture → retopology → Unreal 5.8.3 import path. Installation location and execution have **not** been verified here. Its published community license excludes EU territory; production use remains blocked until an applicable license/permission is established. The model is not a dependency of the runtime or normal server build.

## Fresh owner playtests

Use [PLAYTEST_FRESHNESS.md](PLAYTEST_FRESHNESS.md) and the guard `scripts/mb2.sh playtest`, with a privately configured `Saved/PlaytestProfile/profile.json`. It validates Git/source revision, hashes tracked and locally licensed asset inputs, rebuilds and recooks into a new client/server package and verifies exact archives before launch. The example profile has not been qualified with real UAT output paths and **does not** prove that a native Unreal playtest exists. Always validate that generator outputs, custom import intermediate steps and local purchased assets are included in the actual profile.

## Accounts and signed-in entry (ADR-055)

The account service is `services/accounts` (ADR-055). It supplies code-based email verification, Argon2id passwords, a durable first character and short-lived one-use admission tickets. **The current service is a development implementation**, not a production installation; messages are written into an outbox folder, not delivered externally. Run it over HTTPS with a certificate the clients trust:

```bash
cargo test --manifest-path services/accounts/Cargo.toml
cargo build --release --manifest-path services/accounts/Cargo.toml
services/accounts/target/release/mb2-accounts --bind 127.0.0.1:7790 --store Saved/Accounts/accounts.json \
    --outbox Saved/Accounts/outbox --models Data/Accounts/character_models.v0.1.json \
    --server-key-file <file with 32+ random characters> --tls-cert <cert.pem> --tls-key <key.pem>
```

Start the dedicated server with `-MB2Accounts=https://127.0.0.1:7790 -MB2AccountsKeyFile=<the same key file>`, and the client with `-MB2Accounts=https://127.0.0.1:7790`. If the certificate is not signed by a public CA, add `-ini:Engine:[SSL]:OverrideCertificateBundlePath=<ca.pem>` to both. Emails, including confirmation codes, are files in the outbox folder until a mail sender is chosen. Without `-MB2Accounts`, a development server and client keep the ADR-014 key, and every other smoke runs as before. A shipping server always requires accounts.

`scripts/account-smoke-linux.sh editor` makes a throwaway CA and runs the service, a dedicated server and rendered clients through the first-time and returning flows (every body in the creator's 3D preview, turned and zoomed) and five refused admissions. Ports: `MB2_ACCOUNT_PORT` (game, default 7791) and `MB2_ACCOUNT_SERVICE_PORT` (service, default 7792). Evidence goes to `Saved/AccountSmoke/<UTC timestamp>/`, with screenshots of the screens. Evidence through ADR-059 is primarily **editor-mode**, including per-model rotate/zoom and close-up captures. The latest packaged FL-10 input/scale/Slovene and full owner FL-12 gates remain open. Keep outbox, CA secrets, passwords and tickets out of source control/captures. Engine join-URL logs currently include **spent one-time tickets** and need security-log review.

## Messaging validation (CHAT-001)

General and private text chat source resides in `AMarburgumPlayerController`; client UMG resides in `MarburgumIIClient`. See [CHAT.md](CHAT.md). Use a genuine UE **5.8.3** Editor/Client/Server compilation plus the native automation test `Marburgum.Chat.TextPolicy`, followed by **three** separate clients to prove that a bystander never receives private text. The source-only chat checker in CI is a dependency/regression check, **not** a live network or owner-acceptance test. Earlier **packaged** Linux dedicated-server/three-client delivery and recipient isolation are recorded in [CHAT-001 evidence](STAGE0C_CHAT_EVIDENCE.md). This documentation update did not rerun those tests; regression against the current assets and owner UI/focus acceptance remain outstanding.

## Independent source checks

```bash
dotnet run --project tools/MarburgumII.Tools -- verify   # .NET 10: modules, targets, side boundaries
scripts/mb2.sh validate all                            # Rust: UI ontology, chat, gameplay, accounts contract, no Python
cargo test --manifest-path tools/mb2/Cargo.toml
cargo test --manifest-path native/marburgum-sim-core/Cargo.toml
cargo build --release --manifest-path native/marburgum-sim-core/Cargo.toml
```

The C# program checks repository manifest contracts, but does not inspect the local Unreal installation. `scripts/mb2.sh` builds and runs the Rust repository tool (`tools/mb2`); it also provides the engine-version check, operation ids and world-store checks the smoke scripts use, and the playtest freshness guard. The Rust simulation library is an unlinked and independently testable experimental component.

### Reproducing the newest editor-level checks

```bash
./scripts/mission-smoke-linux.sh editor    # all eight original missions, decisions, store/restart; pads beyond map
./scripts/shop-smoke-linux.sh editor       # Kiosk 67: buy, replay, refusals, sell-back, reach, Sunday restart (ADR-060)
./scripts/weather-smoke-linux.sh editor    # weather: dry vs soaked braking, wet footsteps, live climate turning to rain (ADR-061); the drawn look and captures of clear, overcast, rain, snow, fog and a storm (ADR-062); the six day/night phases below (ADR-064)
./scripts/daylight-smoke-linux.sh editor   # day and night: solstice noon, a June sunset, November dusk, a full-moon and a moonless night, a day a minute; the 1990s date on the banner, the sun and night light drawn, with captures (ADR-064)
./scripts/place-smoke-linux.sh editor      # place names: the server sets one client down at ten stops through Lent and the old town; the banner's place, street or square and landmark at each, with captures (ADR-063)
./scripts/account-smoke-linux.sh editor    # HTTPS registration, first/returning login, 3D preview, ticket denials
./scripts/vehicle-smoke-linux.sh editor    # own-car relevancy; ADR-056 records a reduced-cull comparison separately
./scripts/street-smoke-linux.sh editor     # server-led pedestrians and incident reactions
```

These are **commands for reproducing earlier recorded results**, not tests executed as part of the documentation review. Before owner acceptance, rebuild/cook all current level/model/data assets for staged clients, repeat a >150 m vehicle drive with passenger after ADR-056, execute two/three-client street/account/mission integration, measure performance and record source SHA, cooked asset digest, screenshots, logs and the owner's explicit decision. See [the 12 November state review](DEVELOPMENT_REVIEW_2026-11-12.md).

## Required acceptance evidence

For an Unreal build claim, record the exact Git SHA, engine metadata, OS/compiler versions, command line, UBT/UHT completion, editor launch and in-editor smoke-test result. Save failures and remediation in a reviewable issue or document before extending gameplay.

Git LFS rules cover Unreal binary assets; generated `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/` and local credentials must not be committed. Third-party assets require rights and provenance review.
