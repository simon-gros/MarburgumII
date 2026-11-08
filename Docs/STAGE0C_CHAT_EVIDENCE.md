# Stage 0C evidence — UI-03 `chat_panel` and CHAT-001 three-client test, Linux, 2026-11-09

**Result:** general and private chat **pass an automated dedicated-server + three-client test** with packaged Linux builds, and the themed `chat_panel` renders in packaged clients. Per [ADR-008](DECISIONS.md) this is `implemented_awaiting_owner_playtest`; CHAT-001 is not complete until the owner approves it.

## Environment

| Item | Value |
| --- | --- |
| Source | `cda5415a1f995b419bc7f61c2865cbe6de336a17` (clean `Source`/`Config`/`Content`; all targets rebuilt and repackaged from it) |
| Engine / host | UE **5.8.3** source build; CachyOS Linux 6.18.60 x86_64; NVIDIA GeForce RTX 5080 |
| Processes | Packaged headless `MarburgumIIServer`; three packaged `MarburgumIIClient` processes. A and C render offscreen at 1920×1080, B runs `-nullrhi` |
| Command | `scripts/chat-smoke-linux.sh staged` (run `Saved/ChatSmoke/20261108T233551Z`) |

## What the test does

Each client renames itself `MB2ChatA`, `MB2ChatB` or `MB2ChatC` through the engine's `ServerChangeName`, only so the others can learn its session ID. Routing still uses server-resolved PlayerState IDs. Message bodies carry a per-run token so leaks can be searched for.

1. A sends a general message containing `čšž ČŠŽ Šentilj`.
2. A sends a private message to B; B replies privately to A.
3. A sends a private message to a nonexistent ID (999999), then a general message containing a line break, then two general messages back to back.
4. A sends an end marker, then submits one idempotent operation twice.

Each client checks its own deliveries and exits nonzero on any mismatch. The script also counts server-accepted messages and searches the server log and all three client logs for the private bodies.

## Results ([summary](Captures/stage0c-chat/summary.txt))

| CHAT.md matrix case | Observed | Result |
| --- | --- | --- |
| 1 General | A, B and C each received the 3 accepted general messages exactly once, from A's session ID, with the Slovene text byte-identical | PASS |
| 2 Private | A and B each received A→B and B→A exactly once; C received 0 private messages and no private text by content | PASS |
| 3 Invalid recipient | A got one SYSTEM refusal; nothing was routed | PASS |
| 4 Validation | Line-break text refused; second burst message throttled ("sending messages too quickly"); A got exactly 3 SYSTEM notices, B and C none | PASS |
| 6 Robustness (partial) | The server derives the sender from PlayerState; it logged exactly 5 accepted messages (`channel`, `sender`, `sequence` only); no private body appears in any server or client log | PASS |
| HUD receipt dedupe | A's operation came back `applied`, then `duplicate`; the HUD shows one keyed SERVER RECEIPT toast, not two | PASS |

Captures, all JPEG quality 88 except the PNG crops:

- [Sender A during the private exchange](Captures/stage0c-chat/sender-A-private-exchange-1920x1080.jpg) ([panel crop](Captures/stage0c-chat/sender-A-panel-crop.png)): the general message, then teal `PRIVATE · You → #256` and `PRIVATE · MB2ChatB (#256) → You`.
- [Bystander C at the end](Captures/stage0c-chat/bystander-C-final-1920x1080.jpg) ([panel crop](Captures/stage0c-chat/bystander-C-panel-crop.png)): general messages only.
- [Sender A at the end](Captures/stage0c-chat/sender-A-final-receipt-1920x1080.jpg): the amber SYSTEM throttle notice in the panel and a single "Already applied" receipt toast.

## Same-commit regression checks

| Check | Result |
| --- | --- |
| Editor, Server, Client builds; `scripts/package-linux.sh` | Succeeded |
| `Marburgum.Chat.TextPolicy` automation test | Success |
| Stage 0B packaged network smoke (`Saved/NetSmoke/20261108T233524Z`) | PASS; joins 4, refused 3, applied 3, duplicates 4 |
| Stage 0C UI captures (`scripts/ui-capture-linux.sh staged`) | PASS, 9/9 |
| CI source checks (.NET manifests, UI ontology, chat contract, Rust, playtest-freshness tests) | Pass |

## Defects found and fixed during native qualification

- **Chat panel never visible.** It built its widget tree in `NativeConstruct`, after `RebuildWidget` had already turned the empty tree into Slate, so the mounted panel drew nothing. It now builds in `NativeOnInitialized`.
- **Chat panel never mounted on clients.** The `BeginPlay` hook fires before the engine marks a client's replicated controller as local, so the local-controller check rejected it. The UI subsystem now also mounts the panel when the session reaches verified admission.
- **`?Name=` ignored.** The engine replaces the URL name with its own generated name at login, so the test uses `ServerChangeName` instead.
- **Test-driver race.** The first runs failed because A required all three players online until its final check while B and C had already finished and left; the roster is now only needed until session IDs are known.

## Not proven by this evidence

- **Owner playtest** (ADR-008).
- **Interactive input (case 5):** Enter-to-focus, typing, `/w` parsing in the composer and focus return were not exercised; the test calls the controller API directly.
- **Reconnect:** chat across a disconnect, and stale recipient IDs after rejoin, are not tested.
- **UI quality (case 7):** only 1920×1080; no 720p, 1440p, ultrawide or 4K chat captures; shell strings are English only.
- **Security beyond the basics:** no protocol fuzzing, flood/DoS testing, mute/block/report or account identity.
- **Windows** and non-loopback networks.
