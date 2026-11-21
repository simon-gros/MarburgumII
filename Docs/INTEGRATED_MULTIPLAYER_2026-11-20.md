# Integrated multiplayer pass — 20 November 2026

Staged Client/Server evidence for packages that already contain ADR-079 through ADR-083. Not an owner playtest, and not a claim that the game is release-ready.

Three runs, in order:

1. Full suite on the `44106bc` package: 17 of 21 passed (tables below).
2. Full suite on the `34d6711` package, which has the door and photograph fixes: 20 of 21 passed. Only `door` failed, because of the smoke's setup.
3. Door smoke alone on the `35b6944` package, which only changes the door smoke harness: PASS.

See [Follow-up runs](#follow-up-runs).

## Build

| | |
| --- | --- |
| Branch | `fix/grok-integrated-multiplayer-2026-11-20` |
| HEAD when the suite ran | `1aaad5cf1a82a022c3f34135971d39f520e9b6b9` (docs only after the package) |
| Packaged commit | `44106bcad65a8c05c1460edd06a548a714615735`, dirty=0, Development, packaged 2026-11-20T16:46:45Z |
| Why it is current | `scripts/build-stamp.sh check` reported `build_fresh: yes`. `Source`, `Config`, `Content`, `Data`, `Plugins`, `services/accounts` and `tools/mb2` do not differ between the packaged commit and HEAD. |
| Engine | UE 5.8.3 at `/mnt/data/unreal/5.8.3` |
| Server sha256 | `0d780b86bd757d21d6beab83c0efb5661e53251e9f465abef8fb29593d773f5b` |
| Client sha256 | `c84c4638fb3e13e9843f2640c15fc76b51d59f4505b8dffe59a0598a8377e30c` |
| Cooked pak/ucas/utoc identity | `1501416ec05ae901c38bb3d563512e16b3f735ee47346d98c353b05b66915346` (sha256 of the sorted file list) |
| World store | schema 6 |
| Runner | `scripts/integrated-multiplayer-linux.sh`, evidence `Saved/Integrated/20261120T180113Z` |
| Ports | 281xx. Not 7777 or 17777. |

A source compile was not treated as a packaged result. The runner refuses a stale executable, a cooked-content mismatch, a world store other than schema 6, or an account service / `tools/mb2` tree that is not the packaged commit.

## Matrix

| Smoke | Result | What it showed |
| --- | --- | --- |
| accounts-unit | PASS | 38 Rust account-service tests |
| mb2-unit | PASS | playtest guard, wire relay/scan |
| encryption-negative | PASS | see below |
| street-day | PASS | ADR-079 matrix, including opposite ends **28/28** |
| street-night | PASS | two clients, **2** people, same ids on both |
| account | PASS | ADR-080/083 packaged account smoke, including rendered-client argv |
| door | **FAIL** | Hotel Lent passed; lagged vine museum did not |
| story | **FAIL** | Chapter IV tower/bridge photos passed; three lettering shots did not |
| story-sl | **FAIL** | same lettering shots in Slovene |
| mission | PASS | original mission set and fishing on schema 6 |
| shop | PASS | Kiosk 67 |
| combat | PASS | server-ruled swings |
| vehicle | PASS | driver and passenger |
| chat | PASS | three clients, private bodies not in logs |
| net | PASS | Gate 0B, reconnect, two peers |
| net-fault | PASS | same smoke with `-PktLag=120 -PktLagVariance=40 -PktLoss=1` (not the clean-network result) |
| interaction | PASS | receipts, restart, admission refusals |
| lent | PASS | Lent slice |
| weather | PASS | dry, wet, dusk, moon, lamps |
| condition | **FAIL** | server restore is correct; the winded HUD check failed |
| swim | PASS | save and restart |

## Follow-up runs

### Full suite on `34d6711`

`34d6711` (fix: door use follows the camera; photographs carry the look) changes two things. `FindUseCandidate` now biases toward the control rotation's yaw instead of the body's forward vector. `ServerRequestPhotograph` now carries compressed pitch and yaw, and the server clamps the pitch. Reach, swing, the blocked-door refusal, and the photograph eye, range and sight trace are unchanged.

| | |
| --- | --- |
| Packaged commit | `34d6711f0080e935dcede393e7bc536ff29e7590`, dirty=0, Development, packaged 2026-11-20T20:55:11Z, `build_fresh: yes` |
| Server sha256 | `b55cb4213971960ea9210f299d7c93efb22abdf66c4062c98c60158f1b5e769c` |
| Client sha256 | `776c3c69d25b8b2af1e48a56b063e70c99ddcb068103bfa75197d4488d411d6f` |
| Cooked identity | `98243c00ab89d18f7635932977b1b80f931403f170a7d04f740408bd3f03d4d6` |
| Evidence | `Saved/Integrated/20261120T205609Z` |

**20 of 21 passed.** Every smoke that passed in the first run passed again. Changes from the first run:

- **story and story-sl: now PASS.** The server framed all six shots, each `off=0.0`: `kvik_lettering` (view pitch 13.8°), `velika_kavarna_lettering` (13.6°), `vodni_stolp`, `sodni_stolp`, `stari_most` and `prehod_sign_melje`. `photo-sinagoga` and `photo-sinagoga-again` passed in both English and Slovene.
- **condition: PASS this time, but not fixed then** (fixed in the smoke by `34bbf44`, 21 November). Client `R2` passed `restored_hud_not_winded` (`Saved/ConditionSmoke/20261120T213217Z`). Neither commit touches the condition widget or the smoke. The race described below is still there, and this run happened to land the widget refresh first.
- **door: still FAIL** (`Saved/DoorSmoke/20261120T210524Z`). The smoke placed the vine museum rival outside the door's reach, so the run failed on the setup and not on the door code.

### Door smoke on `35b6944`

`35b6944` (test: door smoke rival stands within reach of the doorway) changes only `MarburgumDoorSmokeSubsystem.cpp`. The rival now stands 80 cm out from the doorway centre and 70 cm aside, outside the swing. The `use_candidate` line now logs the distance, the height difference and the reach.

| | |
| --- | --- |
| Packaged commit | `35b6944e7edccab253f3358d62b196c4b640d1b2`, dirty=0, Development, packaged 2026-11-20T21:40:25Z, `build_fresh: yes` |
| Server sha256 | `b55cb4213971960ea9210f299d7c93efb22abdf66c4062c98c60158f1b5e769c`, identical to `34d6711` |
| Client sha256 | `77a76ae1cad36b8713d37b8a84cd4326ab49bdd9164d9263408ce2c0afa9e34c` |
| Evidence | `Saved/DoorSmoke/20261120T214055Z`, port 28120 |

**PASS.** All roles passed for Hotel Lent (`r1539756`) and the lagged vine museum (`w539479841`, `-PktLag=120 -PktLagVariance=40 -PktLoss=1`): rival, late-join observer, tourist stream out and back, walker, the observer after the building emptied, and the observer after a restart. On both buildings the server counted at least one opening, closing, `refused_in_swing` and `refused_reach`. The rival's use candidates were the vine museum at `street_dist_cm=108` and Hotel Lent at `street_dist_cm=114`, both within `reach_cm=160`.

The full suite was not run again on `35b6944`, and there is no need to. The only change from `34d6711` is in the door smoke subsystem, and the server binary is byte-identical. Every smoke except `door` already passed on `34d6711`.

## Crowd (ADR-079)

Null RHI, `/Game/World/lent`, Tuesday 18 June 1996, clear, places off. Day `Saved/StreetMulti` via `street-day.out`. Opposite ends (`D`) settled **28/28 in 20 s** (wants 12+16, residential_blocks and school, 0 collision rejects). Shared shopping street (`B`) settled at **18**, not 36. Separate commercial and houses (`A`) settled at **18+8**. Rejoin settled at 24. Three old-town clients settled at **48** (street step average **0.28 ms** over 452 steps). After one left, the lifecycle sample was **38 alive against a target of 40**, still under the cap of 48, and the harness marked that sample pass. Night settled at **2**; both clients reported `Street_3` and `Street_4`. The script also required `cross_river=1`, `bridge_edges=185/185` and `edges_wet=0` before either matrix.

Last net sample on the day run, after the cap drop: server out **9740 B/s**, busiest connection **4922 B/s**, 2 connections, 37 people. That is one sample, not a tick p50/p95/p99, and it is not a 250-player result.

## Encryption

`scripts/encryption-negative-linux.sh` on the same pair (`Saved/EncryptionNegative/20261120T180118Z`):

- A signed-in client (`-MB2Accounts=https://…`) that offers a ticket to a development server that never encrypts gets `PendingConnectionFailure` / `EncryptionFailure`, session `phase=error fault=refused`, then `front_end_enforced`. It does not keep Lent as a local authority world. The post-join log `refused_unencrypted_world` is not this path: the engine fails the hello before admission.
- Nothing listening on the account-service port: redeem is refused (`The account service is unavailable`), nobody is admitted, and the ticket is not in the server log.
- A TLS listener that does not complete an HTTP answer also failed closed, but as a connection error in **0.00 s**, not as the 10 s HTTP timeout. That timeout path was not reached.

The account smoke (`Saved/AccountSmoke/20261120T180815Z`) passed the existing ticket, relay, replay and log checks. Rendered client process arguments held no ticket and no password. Null-RHI admission clients still put `EncryptionToken` on the URL on purpose; the engine then prints that command line, so those client logs contain the test ticket. That is the harness, not the sign-in screens.

`Marburgum.Net.StrictEncryption` was not re-run in the editor in this pass (this worktree has no editor binary). It remains the tamper, wrong-key and plaintext-after-key test. This pass does not claim an independent AES-GCM nonce or replay proof.

## Failures in the first run

These were the failures on the `44106bc` package. The door and photograph failures are resolved (see [Follow-up runs](#follow-up-runs)). The winded HUD race is still open.

**Vine museum doors, lagged clients (resolved by `34d6711` and `35b6944`).** Hotel Lent (`r1539756`) passed: walker, rival, blocked swing, out-of-range refusal, late joiner, stream out to `w158684433` and back, empty-building reset, restart. The vine museum (`w539479841`) ran with `-PktLag=120 -PktLagVariance=40 -PktLoss=1`. Its rival failed `rival_street_door_not_the_use_candidate` and the close never started (`street_door_did_not_start_closing=open`). The lagged walker then failed `reversal_not_seen=open`. Reproduce: `MB2_DOOR_PORT=28120 scripts/door-smoke-linux.sh staged`.

**Lettering photographs (resolved by `34d6711`).** Nika's Then and Now shots of Vodni stolp, Sodni stolp and Stari most were applied, with one Photography XP grant each. Brane stayed on `shoot_kvik`. `kvik_lettering`, `velika_kavarna_lettering` and (in English) `sinagoga` were rejected `nothing_in_frame`. Slovene stored a `sinagoga` discovery and 175 Photography XP against 150 in English, and still failed the same client checks. No second grant of a print that did succeed was seen. Reproduce: `MB2_STORY_PORT=28131 scripts/story-smoke-linux.sh staged` and the same with `MB2_STORY_CLIENT_ARGS=-culture=sl`.

**Winded HUD after restart (resolved by `34bbf44`, 21 November; see [P0_TRANSPORT_HARDENING_EVIDENCE_2026-11-21.md](P0_TRANSPORT_HARDENING_EVIDENCE_2026-11-21.md)).** The dedicated server reloaded the runner winded (`runner_restored_from_store=1`). The client player state at ready was `stamina=0 winded=1`, and the first breath back matched the server. The smoke still failed `restored_hud_not_winded` on that same ready tick. Persistence is right; the HUD did not show it in time. The smoke ticker and the condition widget's `Refresh` are not ordered, so on the frame after bind the smoke can read the widget flag before the widget has adopted the replicated state. It passed on `34d6711` by timing alone. The fix belongs in how the widget adopts a newly known winded state, or in a smoke that waits for `Refresh`, not in the condition itself. Reproduce: `MB2_CONDITION_PORT=28107 scripts/condition-smoke-linux.sh staged`.

The engine's generic hello `Failure` is shown to the player as "This world could not admit your character. It may already be in the world." That text is the wrong reason for an encryption failure.

## Not claimed

No owner acceptance. No 250-player capacity. No session-key rotation, no production deployment of per-server keys, and no proof that a stolen hello ticket cannot be burned. Development-key servers stay unencrypted. Door state is still not in the world store. Five interiors are open, not 586.
