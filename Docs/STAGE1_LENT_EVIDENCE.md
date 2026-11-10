# Stage 1 evidence — Lent slice: location, NPC and first mission, Linux, 2026-11-10

**Result:** the first district slice **passes an automated two-phase test** on a packaged dedicated server with packaged clients, on `/Game/Maps/Lent`.
- **Location:** the server places each player in a named area, and the client shows it in a location banner.
- **NPC and mission:** the original riverside angler offers the original first story mission. Accepting it saves the mission and its start item in one commit. The HUD pins the objective and the journal lists the mission.
- **Restart:** the mission survives a server restart, a replayed request never applies twice, and another player never sees this character's progress.

Per [ADR-008](DECISIONS.md) the status is `implemented_awaiting_owner_playtest`.

The district is a **deliberate fallback blockout** ([ADR-016](DECISIONS.md)), not the licensed Maribor model and not a survey of the real Lent, and the banner says so on screen. Identity and persistence are still the development stand-ins of [ADR-014](DECISIONS.md). Mission progress is limited to what this build can reach ([ADR-017](DECISIONS.md)): the first step needs a fish, and fishing is Stage 2.

## Environment

| Item | Value |
| --- | --- |
| Source | `df2e04f7b8256694fcf37c77b651adc76be1d5ba` (clean `Source`/`Config`/`Content`; all targets rebuilt and repackaged from it) |
| Engine / host | UE **5.8.3** source build; CachyOS Linux 6.18.60 x86_64; NVIDIA GeForce RTX 5080 |
| Processes | Packaged headless `MarburgumIIServer`, started twice on one world-store file; five packaged `MarburgumIIClient` sessions. M, M2 and T render offscreen at 1920×1080, one at a time; W and K run `-nullrhi` |
| Command | `scripts/lent-smoke-linux.sh staged` (run `Saved/LentSmoke/20261110T133458Z`) |

## What runs

- **World:** `/Game/Maps/Lent`, generated at `df2e04f` by the Python editor script `scripts/editor/create_lent_map.py`, which ADR-018 later replaced with the C++ commandlet `-run=MarburgumCreateMaps`, uses the original coordinate frame (X = x east, Y = z south, Z = y up, from the middle of Stari most).
  - **Placed scenery:** a 440 m promenade with a 1.1 m quay railing, the Drava, a flush Stari most deck with railings, 14 placeholder buildings with a street off the bridge, lamps and plane trees.
  - **Area:** the original `lent` area box.
  - **NPC:** `lent_angler` at his original position, with his original 4 m range and dialogue line.
  - **Player starts:** tagged `arrival`, `angler` and `bridge`; the test picks one with `?MB2Start=`.
- **Server:**
  - **Location:** `UMarburgumWorldAuthority` derives every pawn's area four times a second and replicates it to its owner only.
  - **Talking:** E sends the same `ServerRequestInteraction` as Gate 1. For an NPC, the request goes through the shared checks (operation id, store, character, receipt replay, target, range) and then the pure mission rules (`MarburgumMissionRules`).
  - **Saving:** accepting the mission builds one record (mission at `catch_first_fish`, fishing rod +1, typed receipt), writes it with `fsync` and an atomic `rename`, and only then replicates the mission and inventory.
- **Client:**
  - **`location_banner`:** shows the server's area for 4.35 s on every change.
  - **`interaction_prompt`:** reads `TALK · Has a job for you · Something in the Water`. Once the mission is under way it reads `SHOW THE CATCH`, greyed out, with "Needs one fish…".
  - **Receipts:** become notifications for the dialogue line, the mission start and the item.
  - **`pinned_objective`:** shows the saved stage, top left.
  - **`journal_panel` (J):** lists the mission.
  - Nothing is predicted before the server replies.

## Results ([summary](Captures/stage1-lent/summary.txt), [store after the restart](Captures/stage1-lent/world-store-after-restart.json))

### Phase 1: fresh store, one injected disk-write fault; M, W and K online together

| Case | Observed | Result |
| --- | --- | --- |
| M arrives at the angler | Server area `lent`; banner "DISTRICT · Lent"; the angler is the prompt's candidate at 3.7 m | PASS |
| Write fails while accepting the mission | `rejected / store_write_failed`; no mission and no rod on M, revision 0 | PASS |
| Mission accepted | `applied / mission_started`: `lent_first_catch` at `catch_first_fish`, `fishing_rod` ×1, revision 1. The objective card shows the stage text | PASS |
| Same operation id again | `duplicate`, reporting the original effect and item; still one rod | PASS |
| Next step without a fish | `rejected / requirement_not_met` at `catch_first_fish`; nothing changed | PASS |
| W talks to the angler from Stari most (126 m) | Server area outside every area ("Maribor"); `rejected / out_of_range` | PASS |
| Privacy: W, once M's mission is saved | W's copy of M's player state has no mission record, no inventory and no location (all owner-only) | PASS |
| W walks north off the bridge | The server logs W entering `lent` after about 13 m; W's replicated area changes to `lent` | PASS |
| K, without a character key | `rejected / no_character`; no mission, no items | PASS |
| Store file after phase 1 | schema 2, revision 1, one `mission_started` receipt under M's key with the item and stage, nothing for W; the fault counter was consumed | PASS |

### Phase 2: the same store, server restarted

| Case | Observed | Result |
| --- | --- | --- |
| Restart | `store_loaded schema=2 revision=1 characters=1 missions=1 receipts=1` | PASS |
| M2 (M's character) rejoins at the promenade start | Mission at `catch_first_fish` and one rod restored; the objective card shows the stage | PASS |
| M's acceptance replayed after the restart | `duplicate / mission_started`, revision 1; nothing added | PASS |
| T, a new character, walks from the bridge into Lent | Outside, then `lent` after 11 m; banner "Lent" on screen; no objective (no mission) | PASS |
| Store file after phase 2 | Byte-identical to phase 1 (SHA-256 `ecb881be…c21571`): duplicates, refusals and walking never write | PASS |

### Captures

All JPEGs are 1920×1080 at quality 88, from real packaged clients connected to the packaged server.

- [Arrival](Captures/stage1-lent/M-arrival-banner-prompt-1920x1080.jpg): the "DISTRICT · Lent" banner with "Maribor · 1990s · fallback blockout, not the licensed city model", and the `[E] TALK · Riverside angler · Has a job for you · Something in the Water` prompt above the placeholder angler and his rod.
- [Mission started](Captures/stage1-lent/M-mission-started-1920x1080.jpg): four notifications, in order: the refused faulty commit; the angler's line; "MISSION STARTED · Something in the Water. Saved by the server…"; and "ITEM RECEIVED · Fishing rod ×1". Also visible: the pinned objective, and the prompt now greyed out as `SHOW THE CATCH`.
- [Next step needs a fish](Captures/stage1-lent/M-next-step-needs-fish-1920x1080.jpg): "SERVER RECEIPT · Already done…" for the replay, and "NOT YET · Riverside angler wants to see one European chub, Common roach or European perch first…".
- [Journal](Captures/stage1-lent/M-journal-1920x1080.jpg): STORY, IN PROGRESS and PINNED labels, the mission summary, the current objective and "Saved by the server · survives a restart".
- [After the restart](Captures/stage1-lent/M2-restored-after-restart-1920x1080.jpg): M's character on the promenade, with the objective restored and the replay receipt.
- [Entering Lent from the bridge](Captures/stage1-lent/T-entering-lent-from-bridge-1920x1080.jpg): T just off Stari most, with the banner over the promenade, buildings and the Drava.

## Same-commit regression checks

The packaged-build checks below ran first on `df2e04f`, the Lent commit. Its editor-mode runs were disturbed by a parallel session's builds in the same checkout, so the complete set was rerun on `c9d8961`. That commit is the Lent slice plus the ADR-018 tooling change: Rust tooling, the C++ map commandlet with regenerated maps, and .NET 10. Everything below passes on `c9d8961`, all targets rebuilt and repackaged from a clean tree.

| Check | Result |
| --- | --- |
| `dotnet run --project tools/MarburgumII.Tools -- verify` (.NET 10) | Manifests valid, including the editor-only `MarburgumIIEditorTools` module |
| `cargo test` for `tools/mb2` and `native/marburgum-sim-core`; `scripts/mb2.sh validate all` | 13 + 3 tests pass; UI ontology, chat contract, gameplay contract and Python-free checks pass |
| Editor, Server, Client builds; `scripts/package-linux.sh` | Succeeded |
| `Marburgum.Chat.TextPolicy`, `Marburgum.Server.MissionRules` automation tests | Success |
| Lent smoke, packaged and editor mode (`Saved/LentSmoke/20261110T140616Z`, `20261110T141022Z`) | PASS, all five sessions, both server phases |
| Gate 1 interaction smoke, packaged and editor mode (`Saved/InteractSmoke/20261110T140701Z`, `20261110T141117Z`) | PASS, all nine sessions; the store check now expects schema 2 |
| Stage 0B network smoke, packaged and editor mode (`Saved/NetSmoke/20261110T140754Z`, `20261110T141230Z`) | PASS; logins 4, refused 3, applied 3, duplicates 4 |
| CHAT-001 three-client chat smoke (`Saved/ChatSmoke/20261110T140821Z`) | PASS; accepted 5, no private bodies in logs (run with only A rendering; see the Gate 1 evidence for the test-host limit) |
| Stage 0C UI captures (`Saved/UICapture/20261110T140847Z`) | PASS, all cases |

**Map regeneration check.** The commandlet maps tested on `c9d8961` lit Lent with the sun's pitch at −78° instead of −32°, because SpawnActor dropped the directional light's pitch. `98686e2` re-applies the rotation. Compared with `-Describe=` against the Python-generated maps of `df2e04f`, Lent (119 actors) and NetSmoke (16 actors) now match actor for actor in class, label, transform and light direction. On `98686e2` the Editor was rebuilt and the package recooked. The Lent, Gate 1, network and UI-capture smokes pass again (`Saved/LentSmoke/20261110T142018Z`, `InteractSmoke/20261110T142105Z`, `NetSmoke/20261110T142159Z`, `UICapture/20261110T142226Z`), and `mb2 validate all` passes. The lighting in the Lent captures matches the curated captures above again.

## Defects found and fixed during native qualification

- **Unity-build name clashes.** Adding files reshuffled the client module's unity blobs, and identically named file-local layout constants (`PanelWidth`, `ShotTimeoutSeconds`, …) collided, as did a `using namespace` shadowing. The client module now builds without unity (`bUseUnity = false`), so every file compiles on its own includes.
- **Journal text overlapped.** Auto-wrapped text measured against a layout from before the panel was shown, so the summary overlapped the objective. Card text now wraps at a fixed width.
- **Bridge view faced a wall.** The first bridge start looked straight at a facade 26 m away; it now looks up the promenade. A street also opens off the bridge.
- **Log casing.** `FName` keeps the casing it first saw, so the area id `lent` prints as `Lent` in server logs; the test's log check is case-insensitive.

## Found, not fixed here

- **The server trusts the client's movement clock.** W, a `-nullrhi` client, covered 13.2 m in 1.2 s of wall time while its world clock advanced 3.0 s. The server accepted those moves because Unreal's movement time-discrepancy detection is off by default. A client with a fast clock therefore moves faster than `MaxWalkSpeed` allows, and range checks follow its pawn. This is recorded as a follow-up: enable and test time-discrepancy detection. The rendered client T walked at the normal 4.6 m/s.

## Not covered yet

- **Real key presses:** the driver calls the same controller entry point as E and J, but doesn't press keys.
- **The rest of the mission:** fishing (Stage 2) is needed for stage 1. The stage 2 debris, Petra Kralj, the service room and Vera Horvat are not placed. Stage completion is covered only by the `Marburgum.Server.MissionRules` unit test.
- **Pinning:** players cannot pin or unpin yet; the earliest mission is pinned.
- **Screens:** no `player_condition` module, no dialogue panel (the line appears as a notification), and no Character, Skills or Settings screens yet.
- **Capture coverage:** 1080p only; no focus or interface-scale matrix, and no 1440p/4K capture.
- **Content:** the angler is a placeholder figure from engine shapes. The district is massing only, with no period detail, signage, water surface or NPC population.
- **Ownership:** no owner playtest.
