# Stage 1 evidence — Gate 1 first authoritative interaction, Linux, 2026-11-10

**Result:** one authoritative interaction **passes an automated two-phase test** with a packaged dedicated server and packaged clients. A walkable test character requests an item from a limited, server-owned stock. The server validates the request and stores the grant to disk before it replicates it. The grant survives a server restart, is never applied twice, and two clients contending for the last unit cannot both receive it. Per [ADR-008](DECISIONS.md) this is `implemented_awaiting_owner_playtest`.

The interactable is a **labeled test fixture** on the network test map, not authored Marburgum content. The item id `fishing_rod` comes from the original project's item catalogue. Identity and persistence are the development stand-ins of [ADR-014](DECISIONS.md), not accounts or a production database.

## Environment

| Item | Value |
| --- | --- |
| Source | `9b626d36dce78ad8e8fe735e805ff1394284792f` (clean `Source`/`Config`/`Content`; all targets rebuilt and repackaged from it) |
| Engine / host | UE **5.8.3** source build; CachyOS Linux 6.18.60 x86_64; NVIDIA GeForce RTX 5080 |
| Processes | Packaged headless `MarburgumIIServer`, started twice on one world-store file; nine packaged `MarburgumIIClient` sessions. A, D and A3 render offscreen at 1920×1080, the others run `-nullrhi` |
| Command | `scripts/interaction-smoke-linux.sh staged` (run `Saved/InteractSmoke/20261110T122507Z`) |

## What runs

- **World:** `/Game/Maps/NetSmoke` has four player starts around `dev_supply_crate` (3 units, 3 m reach) and `dev_supply_crate_far` (1 unit, 25 m away). Both grant one `fishing_rod`.
- **Client:** the player is `AMarburgumCharacter`, which moves with the engine's networked character movement, so the server checks range against the pawn position it owns. E sends `ServerRequestInteraction(operation id, target)`. The `interaction_prompt` shows the replicated stock and a "waiting for the server" state; nothing is predicted. The receipt becomes a keyed notification, and the item appears only through the owner-only replicated inventory (I opens the `inventory_view`).
- **Server:** `UMarburgumWorldAuthority` (ServerOnly module, [ADR-015](DECISIONS.md)) checks, in order: operation id, store, character, stored receipt, target, range and stock. It builds the next record (stock −1, custody +1, receipt), writes it with `fsync` and an atomic `rename`, and only then updates the replicated crate and inventory.
- **Driver:** the test driver (`-MB2InteractSmoke`) calls the same controller entry point as the E key, so every result below went through the real RPC and server path.

## Results ([summary](Captures/stage1-gate1/summary.txt), [store after the restart](Captures/stage1-gate1/world-store-after-restart.json))

### Phase 1: fresh store, one injected disk-write fault

| Case | Observed | Result |
| --- | --- | --- |
| Write fails during commit | A's first request: `rejected / store_write_failed`; stock stayed 3, A's inventory stayed empty, revision 0 | PASS |
| First grant | `applied`, revision 1; A then saw stock 2 and 1 fishing rod | PASS |
| Same operation id again | `duplicate` with the original grant (revision 1); nothing changed | PASS |
| Far crate (25 m) | `rejected / out_of_range`; its stock stayed 1 and was never written | PASS |
| Second grant | `applied`, revision 2, stock 1 | PASS |
| Race for the last unit | B's request committed at 12:25:15.816 (`applied`, revision 3); A's arrived 33 ms later and was refused `out_of_stock` | PASS |
| Store file after phase 1 | revision 3, stock 0, A 2 + B 1 = 3 rods, 3 receipts, A's first operation recorded under A's key; the fault counter was consumed | PASS |

### Phase 2: the same store, server restarted

| Case | Observed | Result |
| --- | --- | --- |
| Restart | `store_loaded revision=3 characters=2 receipts=3`; crate stock 0 from the store, far crate 1 from its authored value | PASS |
| A2 and B2 rejoin | Inventories restored: A 2, B 1 | PASS |
| A's first operation replayed after the restart | `duplicate` (revision 1), nothing added | PASS |
| New request on the empty crate | `rejected / out_of_stock` | PASS |
| Client without a character key (N) | Admitted; its request was refused `no_character`; it has no inventory | PASS |
| Second login as A while A2 is online (D) | Refused at admission: "This character is already connected to the server." The client shows the refusal screen with Retry | PASS |
| Malformed key `x` (X) | Refused at admission with the key rule | PASS |
| A's character after A2 logged out (A3) | Admitted again; restored inventory (×2) and the EMPTY prompt on screen | PASS |
| Store file after phase 2 | Byte-identical to phase 1 (SHA-256 `266a0908…69375`): duplicates and refusals never write | PASS |

### Captures

All JPEGs are 1920×1080 at quality 88, from real packaged clients connected to the packaged server.

- [Prompt before the first request](Captures/stage1-gate1/A-prompt-available-1920x1080.jpg): `[E] TAKE · Test supply crate · 3 left · shared by everyone on this server`, with a TEST FIXTURE badge, placed above the crate.
- [After the first grant](Captures/stage1-gate1/A-receipt-applied-1920x1080.jpg): a red "could not save the result… nothing was changed" notification from the injected fault, a green "ITEM RECEIVED · Fishing rod ×1" notification, and the prompt at 2 left.
- [After the race](Captures/stage1-gate1/A-inventory-after-race-1920x1080.jpg) ([HUD crop](Captures/stage1-gate1/A-hud-crop.png)): the inventory with Fishing rod ×2. The notifications show the duplicate ("Already received… nothing was added twice"), "Too far away", the second grant and "Nothing left to take. Someone else got there first". The prompt reads EMPTY.
- [A's character after the restart](Captures/stage1-gate1/A-restored-after-restart-1920x1080.jpg): the inventory restored from the store, and the crate still empty.
- [Duplicate login refused](Captures/stage1-gate1/D-duplicate-login-refused-1920x1080.jpg): the connection screen with "The server refused the connection · Detail: This character is already connected to the server" and Retry.

## Same-commit regression checks

| Check | Result |
| --- | --- |
| Editor, Server, Client builds; `scripts/package-linux.sh` | Succeeded |
| `Marburgum.Chat.TextPolicy` automation test | Success |
| Stage 0B network smoke, packaged and editor mode (`Saved/NetSmoke/20261110T122558Z`, `20261110T122625Z`) | PASS; logins 4, refused 3, applied 3, duplicates 4 |
| Gate 1 interaction smoke in editor mode (`Saved/InteractSmoke/20261110T123106Z`) | PASS, all nine sessions |
| Stage 0C UI captures (`Saved/UICapture/20261110T122939Z`) | PASS, all 8 cases (front-end at 4 resolutions, gallery at 2, connection failure, server loss and recovery) |
| CHAT-001 three-client chat smoke (`Saved/ChatSmoke/20261110T123303Z`) | PASS; accepted 5, no private bodies in logs. Run with only A rendering: the first run, with A and C rendering, failed because C could not create its Vulkan device (see below) |
| CI source checks (.NET manifests, UI ontology, chat contract, gameplay contract) | Pass |

## Defects found and fixed during native qualification

- **Prompt never appeared.** It collapsed itself while there was nothing to show, and Unreal does not tick collapsed widgets, so it never re-evaluated. Only its card collapses now.
- **Prompt covered its target.** In first person the object you face sits in the lower middle of the screen, where the prompt was. The prompt now projects the object's top to the screen and sits above it, falling back to the lower centre when that point is off screen.
- **Refusals of command-line connects were dropped.** A server refusal can arrive before the session tracker's first tick, so the client went to the idle front-end with no error. The tracker now handles failures in its starting and idle phases while a connection is pending, and maps server failure messages to "refused". The UI-02 refusal state is now exercised for the first time.
- **Inventory caption overflowed** the panel; it wraps now.
- **Test-host limit:** on 2026-11-10 a second offscreen Vulkan client could not create its device while another was rendering ("No dynamic Vulkan support in current SDL video driver (dummy)"), every time, before any network traffic. A desktop game was using the same GPU at the time. The same two-renderer setup worked for the chat test on 2026-11-09. The interaction test therefore renders one client at a time and retries a renderer whose device creation fails before connecting. The chat regression ran with `MB2_CHAT_RENDER=A`. This is a limit of the test host, not a game defect.

## Not proven by this evidence

- **Owner playtest** (ADR-008), including how walking up to the crate and pressing E feels.
- **Interactive input:** the driver calls the controller API; E, I and WASD were not pressed by a test. Inventory focus and controller navigation are not implemented.
- **Real identity and production persistence:** the character key is not authentication. The JSON store is synchronous, single-process and never prunes receipts; no crash-during-write test beyond the injected fault, no migration and no backup exist (ADR-014).
- **Scale and abuse:** two contending clients only; no load, flood or protocol-fuzzing test. The 8 requests/s cap is untested.
- **Reconnect mid-request:** a request in flight during a disconnect is not tested; the client's waiting state expires after 6 s.
- **Content and presentation:** a test fixture on a flat test map, a placeholder cylinder body, 1920×1080 only, English strings only. No Marburgum place, NPC or mission is involved.
- **Windows** and non-loopback networks.
