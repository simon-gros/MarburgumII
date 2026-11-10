# Stage 1 evidence — player condition: running, breath and its save, Linux, 2026-11-10

**Result:** the player condition **passes an automated two-phase test** on a packaged dedicated server with packaged clients, on `/Game/Maps/Lent`.
- **Running costs breath:** holding Shift runs at 4.4 m/s. After ten seconds the body is winded and walks at 2.2 m/s, even while the player still asks to run. It runs again once stamina is back to 15.
- **The server decides:** a client that ignores its breath and claims a full bar is still winded after ten seconds, corrected, and held to walking speed.
- **Saved:** the condition is saved when a player leaves or the server stops. A winded character comes back winded after a restart, and a rested one is stored as no entry.
- **On screen:** the HUD shows a `player_condition` card, and C opens a first Character panel.

Per [ADR-008](DECISIONS.md) the status is `implemented_awaiting_owner_playtest`. The rule is the original game's ([ADR-020](DECISIONS.md)). Vitality is stored and shown, but nothing in this build changes it.

## Environment

| Item | Value |
| --- | --- |
| Source | `4b288110d83cfe5a0b0cfb84cfbd61571f0329f3` (clean `Source`/`Config`/`Content`; Editor rebuilt and Client/Server repackaged from it) |
| Engine / host | UE **5.8.3** source build; CachyOS Linux 6.18.60 x86_64; NVIDIA GeForce RTX 5080 |
| Processes | Packaged headless `MarburgumIIServer`, started twice on one world-store file; four packaged `MarburgumIIClient` sessions. R and R2 render offscreen at 1920×1080, one at a time; C and S run `-nullrhi` |
| Command | `scripts/condition-smoke-linux.sh staged` (run `Saved/ConditionSmoke/20261110T154317Z`) |

## What runs

- **Rule** (`MarburgumGameplayContract.h`, checked against `Data/Gameplay/gameplay.v0.1.json` and its provenance by `mb2 validate gameplay`): walk 2.2 m/s, run 4.4 m/s, 10 stamina/s while running on the ground, 11/s recovery otherwise, winded at empty until 15.
- **Movement** (`UMarburgumCharacterMovement`): the run request is a compressed move flag; every machine that simulates a move applies the rule. Each `ServerMove` carries the client's predicted stamina; the server corrects any difference over one point, and a correction carries the server's stamina and winded state.
- **State** (`AMarburgumPlayerState::Condition`): owner-only, whole points rounded down; running and winded changes are sent at once.
- **Persistence** (`UMarburgumWorldAuthority`, world-store schema 3): restored at login, saved at logout and at world teardown, only if it differs from the store. A rested body is stored as no entry.
- **Client:** the `player_condition` card (lower left, above chat) shows vitality and stamina meters and a RUNNING/WINDED pill. It shows a waiting state until the server has restored the character and dims when the body is rested. The `character_panel` (C) shows identity, condition, whereabouts, saved-record summaries and an explicit "Not part of this build" equipment card.
- **Driver** (`UMarburgumConditionSmokeSubsystem`, `-MB2ConditionSmoke`): holds the same run request as Shift and moves east along the promenade. It measures speed from the client's own pawn, which the server corrects.

## Results ([summary](Captures/stage1-condition/summary.txt), store after [phase 1](Captures/stage1-condition/world-store-after-phase-1.json) and [phase 2](Captures/stage1-condition/world-store-after-phase-2.json))

### Phase 1: fresh store

| Case | Observed | Result |
| --- | --- | --- |
| R, a new character, arrives | Server restores `source=rested`: vitality 100, stamina 100, not winded; prediction 100; HUD card 100/100 and dimmed; Character panel "100 / 100", "Rested" | PASS |
| R runs | 4.40 m/s over the 2 s window ending 3 s into the run; predicted stamina 70.0; HUD pill RUNNING | PASS |
| Breath runs out | Predicted winded after 10.00 s; the server logs `winded` at `run_s=10.00` | PASS |
| Still asking to run while winded | 2.20 m/s; not running; HUD pill WINDED, stamina bar in the warning tone | PASS |
| Breath back | After 1.35 s, at 15.0; runs again at 4.39 m/s | PASS |
| Honest prediction | 0 stamina corrections from the server over the whole run | PASS |
| R leaves the moment it is winded again | Server logs `condition_saved why=logout … stamina=0 winded=1 revision=1` | PASS |
| C, the adversary, claims a full bar on every move | Its prediction stays at 100. The server winds it after 10.04 s (`run_s=10.00`, the same as an honest body) and sends 128 corrections | PASS |
| C still predicting a run while the server has it winded | Its corrected pawn moves at 2.19 m/s | PASS |
| C leaves | Saved: stamina 1, winded, revision 2 | PASS |
| Store after phase 1 | Schema 3, revision 2, conditions for R (0, winded) and C (1, winded) only, no custody, missions or receipts | PASS |

### Phase 2: the same store, server restarted

| Case | Observed | Result |
| --- | --- | --- |
| R2 (R's character) arrives | Server restores `source=store stamina=0 winded=1`; R2 sees stamina 0 and winded, and its prediction and HUD are winded | PASS |
| R2 asks to run at once | The server recovers its breath from login on, so the client is bound 0.8 s into the 1.36 s window. The server's first `breath_back` for R comes with `run_s=0.00`, so it never ran the body while winded. The client never predicted a run while winded | PASS |
| Breath back | Runs at 4.40 m/s; one stamina correction (the login gap above) | PASS |
| R2 rests to full and leaves | `condition_saved why=logout … stamina=100 winded=0 revision=3`: the entry is removed | PASS |
| S is running when the script stops the server | `condition_saved why=shutdown … stamina=78 revision=4`; S's session is lost and it passes | PASS |
| Store after phase 2 | Revision 4, conditions for C (unchanged) and S; R is gone | PASS |

### Captures

All JPEGs are 1920×1080 at quality 88, from real packaged clients connected to the packaged server.

- [Arrival](Captures/stage1-condition/R-arrival-rested-1920x1080.jpg): the "DISTRICT · Lent" banner, and the dimmed condition card with vitality and stamina at 100 and the `[Shift] Run · [C] Character` hint.
- [Character panel](Captures/stage1-condition/R-character-panel-1920x1080.jpg): the development key, CONDITION (100 / 100, 100 / 100, Rested, and the note that nothing injures you yet), WHEREABOUTS Lent, SAVED RECORD 0 items and 0 missions, and EQUIPMENT "Not part of this build".
- [Running](Captures/stage1-condition/R-running-1920x1080.jpg): the RUNNING pill, with stamina at 69, three seconds into the run.
- [Winded](Captures/stage1-condition/R-winded-walking-1920x1080.jpg): the WINDED pill and a short warning-toned stamina bar at 10, a second after the bar emptied, while R still asks to run.
- [Rested after the restart](Captures/stage1-condition/R2-rested-after-restart-1920x1080.jpg): R's character back at full after being restored winded.

## Same-commit regression checks

Everything below ran on `4b28811`, with the Editor rebuilt and the client and server repackaged from the clean tree.

| Check | Result |
| --- | --- |
| `Marburgum.Chat.TextPolicy`, `Marburgum.Server.MissionRules`, `Marburgum.Server.WorldStoreCondition` automation tests | Success. The new test round-trips a condition and loads a schema 2 document with every body rested. It also clamps out-of-range values and refuses schema 4 |
| Lent smoke (`Saved/LentSmoke/20261110T154432Z`) | PASS, all five sessions. Walkers now move at 2.2 m/s: W 2.17, T 2.26, and the fast-clock adversary F was held to 1.98 m/s with 5 detections and none for anyone else. Every walker's `server_run_s` was 0.00. The store check expects schema 3 and no conditions, and the phase 2 store is byte-identical |
| ADR-019 negative control: Lent smoke with server detection off (`Saved/LentSmoke/20261110T154834Z`) | FAILS as it must: F covered 11.09 m in 2.21 s (5.01 m/s) against 5.92 m allowed, so it is `TOO FAST`. The running allowance cannot mask it, because the server never ran F |
| Gate 1 interaction smoke (`Saved/InteractSmoke/20261110T154520Z`) | PASS, all nine sessions; applied 3, commit_failed 1, two admission refusals; no condition commits |
| CHAT-001 three-client chat smoke (`Saved/ChatSmoke/20261110T154613Z`) | PASS; accepted 5, no private bodies in logs |
| Stage 0B network smoke (`Saved/NetSmoke/20261110T154640Z`) | PASS; logins 4, refused 3, applied 3, duplicates 4 |
| Stage 0C UI captures (`Saved/UICapture/20261110T154707Z`) | PASS, all cases |
| `dotnet run --project tools/MarburgumII.Tools -- verify`; `cargo test` for `tools/mb2` and `native/marburgum-sim-core`; `scripts/mb2.sh validate all` | Manifests valid; 23 + 3 tests pass; UI ontology, chat contract, gameplay contract (now including the condition rule and owner-only condition) and Python-free checks pass |

## Defects found and fixed during native qualification

- **The test assumed the client sees the restored state at once.** The first R2 run computed its "walk while winded" window from its own stamina at bind. The server had already been recovering that breath for 0.8 s, so the window was mostly over. The game behaved correctly: the server corrected the client and never ran it while winded. The check now uses the server's own record (`breath_back … run_s=0.00`).
- **Character panel title.** A long development key ran under "[C] Close"; the title now wraps.

## Not covered yet

- **Real key presses:** the driver sets the same run request Shift sets, but it doesn't press keys.
- **Vitality changes:** falls, drowning, swimming and fights, the original's other uses of the condition, are not in this build.
- **Crash safety:** a server killed with SIGKILL loses the breath spent since the last logout. This is acceptable for breath, but must be revisited before vitality can fall (ADR-020).
- **Network conditions:** loopback only. The one-point correction tolerance and the adversary's walking band are not measured under latency or packet loss.
- **Layout:** with the location banner still showing, the Character panel opens right under it. 1080p only; no focus or interface-scale matrix.
- **Feedback:** no running animation, footstep or camera cue; the body is still a placeholder.
- **Ownership:** no owner playtest.
