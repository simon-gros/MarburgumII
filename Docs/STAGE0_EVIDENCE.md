# Stage 0A / 0B evidence — Linux, 2026-11-08

**Result:** Gate 0A (native builds and role separation) and Gate 0B (minimal dedicated-server session) **pass on Linux over loopback**. This record covers what was run and what it does and does not prove.

## Environment

| Item | Value |
| --- | --- |
| Source | `74949dd87a83d81797de42583b800902dc54bd6a` (clean tree; binaries built from this source) |
| Engine | Unreal Engine **5.8.3**, source build at `/mnt/data/unreal/5.8.3`, verified by `scripts/check-engine-version.sh` |
| Toolchain | Engine-bundled `v26_clang-20.1.8-rockylinux8` (not the system Clang 23.1.4) |
| Host | CachyOS Linux 6.18.60, x86_64, 24 cores, 62 GB RAM |

## Gate 0A — builds and role separation

| Check | Command | Result |
| --- | --- | --- |
| Editor target | `scripts/build-linux.sh MarburgumIIEditor` | Succeeded; UHT ran, all four project modules compiled and linked |
| Dedicated server target | `scripts/build-linux.sh MarburgumIIServer` | Succeeded |
| Client target | `scripts/build-linux.sh MarburgumIIClient` | Succeeded |
| Cook and stage | `scripts/package-linux.sh` (BuildCookRun `-client -server`) | `AutomationTool exiting with ExitCode=0`; `Saved/StagedBuilds/LinuxServer` and `LinuxClient` produced |
| Editor launch | `UnrealEditor MarburgumII.uproject -unattended -nullrhi -ExecCmds=QUIT_EDITOR` | Engine initialized, loaded `/Game/Maps/NetSmoke`, exited 0 |
| No side-to-side linkage | symbol search in `Binaries/Linux/*.sym` | Client: 0 `FMarburgumIIServerModule` symbols; Server: 0 `UMarburgumNetSmokeSubsystem` symbols; each contains its own module |
| Source checks | `dotnet run --project tools/MarburgumII.Tools -- verify`; `cargo test` | Manifests valid; Rust 3/3 tests pass |

## Gate 0B — dedicated server with one, then two clients

Command: `scripts/net-smoke-linux.sh staged`. The server is the packaged headless `MarburgumIIServer`. Each client is a separate packaged `MarburgumIIClient` process with `-nullrhi`, connecting to `127.0.0.1:7777`.

| Requirement | Observed (authority-side log unless noted) |
| --- | --- |
| Same authoritative world and replicated contract | Server `ready map=NetSmoke port=7777 contract=1`; every client observed `contract=1` and a rising server heartbeat |
| Unauthorized client mutation rejected | 3× `contract_change_refused requested=999 contract=1`; clients still observed `contract=1` |
| Server-decided, idempotent operation | Each operation id gave `outcome=applied` once, then `outcome=duplicate`; `applied_total` rose by exactly 1 per client |
| Disconnect and reconnect | `solo` logged out (player 256) and rejoined (player 257). After reconnect the heartbeat continued (3 → 4, not reset), and replaying the original operation id returned `duplicate` |
| Two concurrent clients | Server `login … players=2`; both `peer1` and `peer2` observed `players=2`, the same server counters (`applied=3 rejected=3`) and the same heartbeat value (10) |
| Totals | `logins=4 refused=3 applied=3 duplicates=4` exactly as expected; script exit 0 |

The same test also passes with uncooked editor processes (`scripts/net-smoke-linux.sh editor`). Before the final run, the packaged test passed 3 consecutive times and the editor test twice. Full logs and `summary.txt` are written to `Saved/NetSmoke/<timestamp>/`, which is not committed.

## Defects found and fixed while qualifying

- `scripts/build-linux.sh` was committed without its executable bit.
- The staged launcher `MarburgumIIServer.sh` does not `exec` the binary. Stopping the launcher orphaned the real server, and the next run's clients silently joined the stale server, because Unreal moves a new server to the next free port. The smoke script now runs the server in its own process group, refuses an occupied port, and checks the port the server reports.
- During a pending reconnect, Unreal reports the client's local standalone world as `NM_Client`. The smoke driver now requires a live server connection and a non-authoritative `GameState`.

## Not proven by this evidence

- **Rendered clients:** every client here ran `-nullrhi`. Rendered joins are covered by [STAGE0C_UI_EVIDENCE.md](STAGE0C_UI_EVIDENCE.md).
- **Real networks:** only loopback was tested; no non-loopback, latency, packet loss or NAT testing.
- **Windows:** no Windows builds.
- **Persistence:** the idempotency ledger lives in server memory and is lost on server restart. Durable receipts are a Gate 1 requirement.
- **Login and admission:** none; any process that reaches the port can join.
- **Client fallback authority:** resolved by ADR-011. A disconnected client now returns to `/Game/Maps/FrontEnd`, which has no gameplay authority, instead of a standalone copy of the test map.
- **Load:** no load or soak testing.
