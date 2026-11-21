# P0 transport hardening evidence — 21 November 2026

P0 item 1 of the [20 November evening review](DEVELOPMENT_REVIEW_2026-11-20_EVENING.md): close the two open security points on ADR-083 (replayed encrypted packets, a stolen hello ticket burning the player's join), then requalify the whole staged matrix on one package from the latest head. Run by the senior-programmer agent on Simon's host (Linux 6.18, RTX 5080), one smoke at a time, on ports 279xx (never 7777 or 17777).

Branch `feat/transport-hardening`, rebased onto `main` `f4d5e89`. The decision is [ADR-088](DECISIONS.md). It was drafted as ADR-085 and renumbered twice while this branch was being tested: ADR-085 went to the character sheet (LRP-01/02) on `main`, ADR-086 to the investigation input, and ADR-087 is claimed by the vegetation branch.

## The builds

| | `b988455` (full matrix) | `34bbf44` (condition fix reruns) |
| --- | --- | --- |
| Source | `rebase/transport-hardening` on `main` `f4d5e89`, tree clean (`dirty=0`) | `b988455` plus one test-only commit |
| Editor | `-DisableAdaptiveUnity` build OK (71 actions); `Automation RunTests Marburgum`: **51/51 Success** | — |
| Cook | 2,325 packages; `BUILD SUCCESSFUL` | 2,325 packages; `BUILD SUCCESSFUL` |
| Server | `sha256 b3ad9ef8dd61c64421bc0ecadaa90487e4723faebd877985414eab52ea6d41ec` | same |
| Client | `sha256 048122d31aa0a9efb129c98d3449239b74a8e70f9982dbd64b824d0cdd371ac0` | `sha256 a514f5e310c368ff401749b85ab6c9ea652012dc889b1f921a081af4f315e8b3` |

Every staged smoke checked its stamp (`build_fresh: yes`). The account service's `cargo test`: 38/38 on the rebased tree.

The automation run includes `Marburgum.Net.ReplayWindow` and `Marburgum.Net.StrictEncryption` from this branch, and `EquipmentRules` from `main`.

## Staged matrix on `b988455`

All on one Client/Server pair. Raw output: `Saved/P0/head-b988455/`.

| Smoke | Result | Evidence (`Saved/…`) |
| --- | --- | --- |
| account (encrypted; thief redeems the ticket first, the player still joins) | PASS | `AccountSmoke/20261121T025055Z` |
| encryption-negative (unencrypted server, service down, service stalled) | PASS | `EncryptionNegative/20261121T025229Z` |
| door | PASS | `DoorSmoke/20261121T025547Z` |
| net | PASS | `NetSmoke/20261121T030018Z` |
| interaction | PASS | `InteractSmoke/20261121T030040Z` |
| chat | PASS | `ChatSmoke/20261121T030121Z` |
| mission | PASS | `MissionSmoke/20261121T030142Z` |
| story (English) | PASS | `StorySmoke/20261121T030710Z` |
| story (Slovene) | PASS | `StorySmoke/20261121T031113Z` |
| negative (English, Slovene) | PASS, PASS | `NegativeSmoke/20261121T031514Z`, `…T031947Z` |
| shop | PASS | `ShopSmoke/20261121T032409Z` |
| vehicle, vehicle with 5 m cull | PASS, PASS | `VehicleSmoke/20261121T032444Z`, `…T032521Z` |
| combat | PASS | `CombatSmoke/20261121T032557Z` |
| condition | **FAIL** (`restored_hud_not_winded`; see below) | `ConditionSmoke/20261121T032652Z` |
| swim | PASS | `SwimSmoke/20261121T032755Z` |
| weather | PASS | `WeatherSmoke/20261121T033107Z` |
| lent | PASS | `LentSmoke/20261121T033553Z` |
| place | PASS | `PlaceSmoke/20261121T033628Z` |
| street-multiplayer day, night | PASS, PASS | `StreetMulti/20261121T033743Z`, `…T034141Z` |
| street-district | PASS | `StreetDistrictSmoke/20261121T034208Z` |
| street-hours | PASS | `StreetHoursSmoke/20261121T040906Z` |
| street-places | PASS | `StreetPlacesSmoke/20261121T042011Z` |
| sheet (`main`'s character sheet) | PASS | `SheetSmoke/20261121T045256Z` |

**Offscreen.** Every client in these runs, and in the reruns below, logged SDL video driver `dummy` (140 launches) or ran with `-nullrhi` (crowd and encryption clients). No run opened a window on the desktop. This branch carries `main`'s `97cd12b`.

## The condition failure, fixed in `34bbf44`

This is the intermittent failure [INTEGRATED_MULTIPLAYER_2026-11-20.md](INTEGRATED_MULTIPLAYER_2026-11-20.md) left open. `restored_hud_not_winded` failed 4 of the 28 recorded R2 runs from 10 to 21 November, in four worktrees. In every run, persistence and the server were right.

- **Cause.** The smoke read the condition widget's flag on the frame after bind. The widget adopts the replicated state in its own tick, which is not ordered against the smoke's tick.
- **Fix.** Client R2 now waits while the body is still winded, up to one second, and logs how long the HUD took. If breath comes back and the HUD never showed winded, it still fails. The game code is unchanged.

| Run | R2 | HUD showed winded after |
| --- | --- | --- |
| editor, port 27937 (×3) | PASS 3/3 | 0.042 s, 0.072 s, 0.061 s |
| staged `34bbf44` (×3) | PASS 3/3 (`ConditionSmoke/20261121T045602Z`, `…T045703Z`, `…T045804Z`) | 0.004 s, 0.005 s, 0.006 s |

The editor timings are past the old one-frame check, so all three editor runs would have failed it.

On the same `34bbf44` package, door (`DoorSmoke/20261121T045906Z`) and the Slovene story (`StorySmoke/20261121T050337Z`) also passed.

One editor run also failed client R on `honest_corrections=5`. That is an unrelated flake: 2 of 30 runs since 10 November, in editor mode while the staged matrix shared the host. It is not addressed here.

## Final head `460a008` (on `main` `9814f26`)

While the runs above were going, `main` took the investigation input work (ADR-086, 26 Source files). The branch was rebased onto `9814f26`, the replay window was renumbered ADR-088, and everything was run again on one fresh package.

| | |
| --- | --- |
| Source | `rebase/transport-hardening` `460a008`, tree clean (`dirty=0`) |
| Editor | `-DisableAdaptiveUnity` build OK; `Automation RunTests Marburgum`: **55/55 Success** |
| Cook | 2,325 packages; `BUILD SUCCESSFUL` |
| Server | `sha256 e0e2c621877b9303988c07de4108fcb37a5ff34042914895c0842c75ad62a859` |
| Client | `sha256 6c162cfd007ddc978d84f819de5a3fdc35dcc6144ddf989bd322b9d7e153fed8` |

**Staged matrix: 25/25 PASS.** account, enc-negative, door, net, interaction, chat, mission, story (English and Slovene), negative (English and Slovene), shop, vehicle (and 5 m cull), combat, condition, swim, weather, lent, place, street-multiplayer (day and night), street-district, street-hours and street-places all passed. `main`'s sheet smoke (`SheetSmoke/20261121T074806Z`) and input smoke (`InputSmoke/20261121T074904Z`) also passed. Raw output: `Saved/P0/head/SUMMARY-final-matrix.txt` and `SUMMARY-final-extra.txt`.

- The condition smoke's R2 saw the HUD show winded after 0.041 s (`ConditionSmoke/20261121T062131Z`). The old one-frame check would have failed that run.
- 109 client launches logged SDL `dummy`, and the crowd clients ran with `-nullrhi`. No window was opened.
- The account service's `cargo test`: 38/38 on this tree.

## Earlier failures on this branch, resolved

- **Slovene story, Velika kavarna photo** (`3618bb6` package): rejected as "nothing in frame". The photo driver aimed 1.5 s after login, from a pawn still being placed about 12 m above the start. The driver now waits for the body to settle and aims again before the shutter. Slovene PASS ×3 and English PASS since.
- **street-places, all clients** (`3618bb6` package): host fault, not game code. An NVIDIA userspace update (620.70 → 620.73) left the old kernel module loaded, so Vulkan init failed until a reboot. After the reboot it passed on `e0e2736` and on `b988455`.

## Not claimed

- No owner acceptance.
- No datagram-level replay injection on a live connection. Replay is unit-tested only (`Marburgum.Net.ReplayWindow`).
- No session-key rotation and no per-direction keys.
- No production deployment of per-server keys. Development-key servers stay unencrypted.
- A thief who redeems a ticket three times first still exhausts it, and the player then asks for a new one.
- No 250-player capacity. Door state is still not in the world store.
