# Stage 1 evidence — server-bounded movement time (ADR-019), Linux, 2026-11-10

**Result:** a client whose clock runs fast **no longer covers ground faster than `MaxWalkSpeed` on the dedicated server**, and honest test clients no longer run fast clocks. This was qualified on packaged builds by the extended Lent smoke and by two negative controls that fail as designed. The Lent, Gate 1 interaction, Stage 0B network and chat smokes all pass. The movement-time change is server infrastructure, not a player-facing feature; movement feel is still part of the owner playtest (ADR-008).

## The finding

Found in the Lent smoke of `df2e04f` (`Saved/LentSmoke/20261110T133011Z`). The `-nullrhi` client W walked 12.7 m in 1.1 s of wall time while its world clock advanced 2.9 s (`event=walk_arrived ... seconds=1.1 world_seconds=2.9`). The server placed W in Lent about a second after it set off. Range checks and location derivation follow that pawn, so a fast client clock worked like a speed hack.

**Why the test client's clock ran fast.** It was neither a fixed time step nor a frame cap: no `-UseFixedTimeStep`, `-benchmark` or `t.MaxFPS` is used, and the frame rate is uncapped. W did ~5,760 frames in 1.13 s (frame counter 411 → 170), about 5,000 fps, because a `-nullrhi` process has nothing to render. `AWorldSettings::FixupDeltaSeconds` clamps each frame to at least `MinUndilatedFrameTime`, whose engine default is 0.0005 s ("2000 fps"). So every ~0.2 ms frame advanced world time by 0.5 ms: 5,760 × 0.5 ms ≈ 2.9 s. The client's `ServerMove` timestamps are its world time, and the server's movement time-discrepancy checks were off by default. The server therefore simulated 2.9 s of walking. Any client above 2,000 fps would do the same, including a rendered one in a trivial scene with no cap.

## Change ([ADR-019](DECISIONS.md), commit `650d6c0`)

- **Server** (`Config/DefaultGame.ini`, `[/Script/Engine.GameNetworkManager]`):
  - `bMovementTimeDiscrepancyDetection=True`, `bMovementTimeDiscrepancyResolution=True`. The UE 5.8.3 keys have a `b` prefix.
  - Max/min margins ±0.25 s, resolution rate 1.0, drift allowance 0, forced corrections while resolving.
- **Clients:** `[/Script/Engine.WorldSettings] MinUndilatedFrameTime=0`, so world time follows real time at any frame rate.
- **Evidence hooks:**
  - `UMarburgumCharacterMovement` logs `MB2INTERACT role=server event=time_discrepancy`, at most once a second per character.
  - The server's `event=area` lines now carry `x_m`/`y_m`.
- **Regression test** (`scripts/lent-smoke-linux.sh`):
  - **Adversary F:** a deliberate fast-clock client. It dilates only its own world clock to 2.5× (`-MB2LentClockRate=2.5`) and walks from Stari most into Lent.
  - **Movement bound:** `mb2 movement-check` (`tools/mb2/src/movement_check.rs`) bounds every walker (W, F, T) by the server's own positions: covered ≤ `MaxWalkSpeed` × (elapsed + 0.25 s) + 0.5 m. Elapsed runs from the client's `walk_start` to the server's Lent-entry sample.
  - **Detection:** the server must detect F and no one else.
  - **Honest clocks:** an honest walker fails if its world clock ran ahead of real time by more than its longest frame plus 2 %. World and wall time are sampled at different points in a frame.
  - **Unit tests:** `cargo test --manifest-path tools/mb2/Cargo.toml` covers the bound, including the 2026-11-10 finding.

## Environment

| Item | Value |
| --- | --- |
| Source | `b01f41a` on branch `adr-018-movement-clock` (git worktree `/mnt/data/MarburgumII-adr018`), clean. The branch is `650d6c0` (the change) on top of `c9d8961` (Rust tooling, ADR-018); this evidence commit adds documentation only |
| Engine / host | UE **5.8.3** source build; CachyOS Linux 6.18.60 x86_64 |
| Build | `UE_ROOT=/mnt/data/unreal/5.8.3 scripts/package-linux.sh` (Development), separate `LinuxServer`/`LinuxClient` stages |
| Port | 7801 (`MB2_*_PORT`), because another qualification run was using 7777 on the same host |

## Results

### Lent smoke, final configuration ([summary](Captures/adr019-movement-time/lent-summary.txt), run `Saved/LentSmoke/20261110T142222Z`): PASS

| Walker | Client clock (world / wall s) | Server: covered in elapsed | Speed | Allowed | Result |
| --- | --- | --- | --- | --- | --- |
| W, `-nullrhi`, honest | 2.61 / 2.61 | 11.24 m in 2.61 s | 4.31 m/s | 13.36 m | ok |
| F, `-nullrhi`, clock 2.5× | 6.69 / 2.68 | 11.98 m in 2.68 s | 4.48 m/s | 13.66 m | ok |
| T, rendered, honest (phase 2) | 2.73 / 2.50 (longest frame 0.23 s) | 11.76 m in 2.49 s | 4.72 m/s | 12.84 m | ok |

- **Detection:** the server logged `time_discrepancy` for F 3 times, covering 7 detections, at discrepancies of 0.25 s. It logged none for W, K, M, M2 or T.
- **Rest of the smoke:** everything else is unchanged and passed: mission, fault, replay, privacy, keyless refusal, restart, byte-identical store.
- **T above 4.5 m/s:** expected and inside the margin. The rendered client's first walking frame took up to ~0.23 s, and part of that frame preceded the logged `walk_start`. Its move legitimately claims that time, so the speed measured from `walk_start` reads high.

### Negative controls: both FAIL as designed

| Control | Run | What happened |
| --- | --- | --- |
| Server detection off (`MB2_LENT_SERVER_ARGS='-ini:Game:[/Script/Engine.GameNetworkManager]:bMovementTimeDiscrepancyDetection=False'`) | `20261110T142313Z` ([summary](Captures/adr019-movement-time/control-detection-off-summary.txt)) | F covered **11.95 m in 1.24 s (9.64 m/s)**, over the 7.20 m allowed: `TOO FAST`; no detections. W and T stayed honest. This is the original hole, reproduced deterministically |
| Engine frame-time floor restored on clients (`MB2_LENT_CLIENT_ARGS='-ini:Game:[/Script/Engine.WorldSettings]:MinUndilatedFrameTime=0.0005'`) | `20261110T142400Z` ([summary](Captures/adr019-movement-time/control-engine-frame-floor-summary.txt)) | Honest `-nullrhi` W's clock ran at **6.97 s world in 2.37 s wall** (2.9×) and W failed `clock_runs_fast`. The server logged W **6 times** (as well as F) and still held it to 4.66 m/s. The server bound works without the client fix, but it would treat honest high-frame-rate clients as cheaters |

### Same-build regression smokes (packaged, port 7801)

| Smoke | Run | Result |
| --- | --- | --- |
| Gate 1 interaction (`scripts/interaction-smoke-linux.sh staged`) | `Saved/InteractSmoke/20261110T142450Z` | PASS, all nine sessions; store revision 3, A 2 + B 1 rods, store unchanged by phase 2; no `time_discrepancy` |
| Stage 0B network (`scripts/net-smoke-linux.sh staged`) | `Saved/NetSmoke/20261110T142542Z` | PASS; logins 4, refused 3, applied 3, duplicates 4; no `time_discrepancy` |
| CHAT-001 (`scripts/chat-smoke-linux.sh staged`) | `Saved/ChatSmoke/20261110T142609Z` | PASS; accepted 5, no private bodies in logs; no `time_discrepancy` |
| Source checks: `scripts/mb2.sh validate all` (UI ontology, chat, gameplay, Python-free), `cargo test` for `tools/mb2` (19 tests, 6 of them for the movement bound), .NET manifests | — | Pass |

All six runs used one package of the clean `b01f41a` tree. An earlier round of the same runs on the pre-rebase branch (based on `df2e04f`, Python movement check, `Saved/LentSmoke/20261110T141527Z` and neighbours) gave the same outcomes.

## Defect found and fixed during qualification

- **The honest-clock check flagged a rendered client.** The first packaged run, on the pre-rebase branch (`Saved/LentSmoke/20261110T140306Z`), passed, but rendered T showed 2.67 s of world time in 2.50 s of wall time. The cause is that world time is read after the world ticks and wall time later in the same frame, so one long frame at the start shows up as clock lead. T's first walking frame took 0.21 s. The check now allows the longest frame seen during the walk plus 2 %. A fixed-step or floor-clamped clock still fails, as the second control shows.

## Not proven by this evidence

- **Lossy or high-latency networks.** Everything ran on loopback. On a real connection, a gap of more than 0.25 s followed by a burst of moves could push an honest client past the margin and make it pay back time, which would show as a brief stutter. The margins and the zero drift allowance need measuring under latency and packet loss, and at Gate 4 load.
- **Other movement exploits:** client-claimed movement modes, teleport-sized moves beyond the engine's position-error correction, `ServerMove` flooding. No sanctions or reporting beyond the log line exist.
- **A wall-clock speed hack outside the engine** (hooking the OS clock). F stands in for it by dilating world time, which produces the same `ServerMove` timestamps the server sees; the process clock itself was not tampered with.
- **Windows** and Shipping builds. Shipping clients ignore `-ini:` overrides (`ALLOW_INI_OVERRIDE_FROM_COMMANDLINE` is `UE_SERVER || !UE_BUILD_SHIPPING`), so the client control runs only on Development clients.
- **The owner playtest** of movement feel.
