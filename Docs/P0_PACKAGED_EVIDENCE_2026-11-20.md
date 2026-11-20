# P0 packaged integration evidence — 20 November 2026

Task 3 of the [morning review](DEVELOPMENT_REVIEW_2026-11-20_MORNING.md): qualify the open interiors with real clients on a packaged build, verify door replication and streaming, rerun the account, mission, vehicle, combat and networking regression on **one** packaged pair, and address the admission-ticket findings (QA AR1, AR13). Run by the senior-programmer agent on Simon's host (Linux 6.18, RTX 5080), on ports 279xx (never 7777 or 17777).

## The build

| | |
| --- | --- |
| Source | branch `qa/p0-packaged-integration`, commit `a0d0b89` (on `main` `735dce7`), tree clean (`dirty=0`) |
| Engine | UE 5.8.3, Development Client and Server, `scripts/package-linux.sh` |
| Cook | 2,326 packages, 0 remaining; `BUILD SUCCESSFUL` |
| Server | `sha256 8eb8ddc1b2369c7b48a55aaf58b0ad79b81e931b249c458f5ba585bbb15d3a9e` |
| Client | `sha256 45234284041af19811533be2093acdb1b8ab3547ded9f0745233b2cd2dfea376` |
| Interior data | Both paks take `Content/Marburgum/Interiors/buildings.json` and `interior_floors.json` from this tree (UAT pak lists). They are byte-identical to `Data/World/lent_ground/` (sha256 `09e68d57…` and `833c02be…`). |

Every staged smoke below checked this stamp (`build_fresh: yes`). Commits after `a0d0b89` change only scripts and documents.

## Interiors and doors (staged)

`scripts/door-smoke-linux.sh staged` — **PASS** (`Saved/DoorSmoke/20261120T060722Z`). Two of the five cut shells ran **at the same time** on one dedicated server:

- **Hotel Lent (`r1539756`).** Rendered walker, rival, late observer and tourist. The walker walked 26 legs through six rooms, opening the street door and four room doors with E, and left all five doors open. It captured nine screens.
- **Vine museum (`w539479841`, 172 m away).** Headless walker, rival and observer, all under emulated lag (`-PktLag=120 -PktLagVariance=40 -PktLoss=1`). The walker walked 8 legs; the street door opened in 1.39 s and a room door in 1.25 s.

| Check | Hotel Lent | Vine museum |
| --- | --- | --- |
| Rival shuts the door, then reverses it mid-swing | PASS | PASS (lagged) |
| Shutting the door on a player standing in its swing is refused (`door_refused_blocked`) | 1 | 1 |
| A door asked for out of reach is refused (`reason=reach`) | 1 | 1 |
| Late joiner sees the doors as the walker left them | `open×5` | `open×2` (lagged) |
| After a server restart, every door is shut | `closed×5` | `closed×2` |

**Streaming.** A tourist joined Hotel Lent while the walker held it. The server (`-MB2DoorTour`) moved it 268 m to `w158684433`'s door, then back.

- **Away:** Hotel Lent left the tourist's client 5.19 s after the move, and all five door actors left its relevancy (`doors_relevant=0`).
- **Back:** the building streamed in again within 4 s, with the doors replicated as `open×5`.
- **Emptied building:** once every player had left, the server unstreamed Hotel Lent. A new observer on the same server found every door shut, built afresh (`stream=2`, `unstream=1`).

**Defect found and fixed (`a0d0b89`).** The emptied-building check first failed in the editor. When the last player left an otherwise empty server, `UMarburgumInteriorSubsystem::UpdateNear` returned early because there were no people, so Hotel Lent's five doors stayed open until the next arrival. With anyone else on the server, the same building reset. It now unbuilds every interior when the world is empty. The editor run after the fix passed too (`Saved/DoorSmoke/20261120T055421Z`).

**Not covered:**
- the other three cut shells;
- latency to a real remote host, as opposed to emulated lag;
- door state across an account-admitted (non-development-key) session;
- the owner's personal walkthrough.

Rooms are lit and walkable but bare, as [INTERIORS.md](INTERIORS.md) says.

## Regression on the same pair (staged)

`Saved/P0/run-regression.sh` ran each smoke in turn against the build above.

| Smoke | Result | What it covers | Evidence |
| --- | --- | --- | --- |
| door | PASS | above | `DoorSmoke/20261120T060722Z` |
| account | PASS | First launch and returning player, rendered. Refused: development key, no ticket, forged, replayed, a second entry. Store survives a service restart. AR1 and AR13 checks below. | `AccountSmoke/20261120T061155Z` |
| net | PASS | 4 logins, 3 refused mutations, idempotent replays, reconnect, two peers | `NetSmoke/20261120T061323Z` |
| interaction | PASS | 9 sessions, an injected commit failure, restart on the same store, 2 admission refusals | `InteractSmoke/20261120T061346Z` |
| chat | PASS | 3 clients, 5 messages accepted, no private bodies in logs | `ChatSmoke/20261120T061425Z` |
| mission | PASS | The first mission. Fishing: 3 casts, 2 bites, `too_early`/`escaped`/`caught`. Restart on schema 5. | `MissionSmoke/20261120T061445Z` |
| shop | PASS | Kiosk 67 trade rules | `ShopSmoke/20261120T062002Z` |
| vehicle | PASS | Driver and passenger, seat refusal, throttle, steer, brake, reverse, exits, no impacts (about 13 m driven) | `VehicleSmoke/20261120T062035Z` |
| combat | PASS | 17 swings ruled, guard block, player beaten, cooldown and floor refusals, impact sounds | `CombatSmoke/20261120T062111Z` |
| condition | PASS | Run, winded, recovery | `ConditionSmoke/20261120T062203Z` |
| swim | PASS | Swimmer saved when down, no save failures, restart | `SwimSmoke/20261120T062304Z` |
| weather | PASS | Dry, wet road, dusk clock, moonlit, lamps | `WeatherSmoke/20261120T062613Z` |
| lent | PASS | The Lent slice | `LentSmoke/20261120T063037Z` |
| vehicle, `MB2_VEHICLE_NET_CULL_M=5` | PASS | The car is sent within 5 m only, below the 11 m seated camera; the occupants' relevancy rule keeps it updating (ADR-056) | `VehicleSmoke/20261120T063552Z` |
| street-multiplayer | **FAIL** (known) | 9 of 10 scenarios settled, including the 48 cap and the drops. **D, the strict opposite bank, timed out at 8/28 after 90 s**: 3,355 of 4,248 spawn tries found no eligible pavement node. That is the open Game AI P0 (morning review item 2). Its fix (`af5b0bd`, ADR-079: the scenario now turns both players towards the town) reached `main` after this package was built and is not in it; rerun on a package that includes it. | `Saved/P0/regression/street-multiplayer.out` |

**Not covered here:**
- **Driving past 150 m.** The vehicle smoke drives about 13 m. ADR-056's "own car keeps updating" rule is checked by `MB2_VEHICLE_NET_CULL_M`, which shrinks the cull distance below the seated camera; no smoke drives a car 150 m from an observer.
- The street-district, street-hours and street-places smokes.
- The UI capture matrix.
- An owner playtest.

## Admission tickets (ADR-080)

**AR1 — closed.** Tickets now name their server.

- The client names the address it joins, and the service issues tickets only for servers listed in `--servers`, each with its own key.
- `/v1/server/redeem` refuses a ticket meant for another server and does not spend it.
- **Service tests:** 37 pass, 4 of them new.
- **Packaged account smoke:**
  - no ticket for a request without a server, an unlisted address or another port;
  - this server refused a ticket asked for `melje`, and `melje` could then still spend it;
  - `melje` could not spend this server's ticket, and the real client was then admitted with it.

**AR13 — mitigated, not eliminated.**

- **Client:** it travels to the bare address and adds the ticket to the pending connection after the engine has logged `Browse`. Neither rendered client has a ticket in any `Browse` line; the first one in a client log is in `LoadMap`, after the server has admitted (spent) it.
- **Server:** its engine log still prints the ticket in `Login request` while it redeems it. After AR1 only that server's key can spend it, once, within 60 s.
- **Open:**
  - game traffic is unencrypted, so an on-path observer can still read a login ticket; the stronger route is the Hello encryption token with an AES-GCM packet handler;
  - AR11 ("newest wins" on reconnect) is unchanged.

## Status in the four-state model

| Item | Source | Editor | Packaged-staged | Owner |
| --- | --- | --- | --- | --- |
| Two cut interiors, doors, streaming | yes | PASS | **PASS** | open |
| Account, mission, vehicle, combat, networking regression | yes | earlier | **PASS on one pair** | open |
| AR1 ticket audience | yes | PASS | **PASS** | operator deployment open (per-server keys) |
| AR13 ticket logs | mitigated | PASS | **PASS (client)** | encryption follow-up open |
