# General and Private Chat — MMORPG Communication Contract

**Status:** Routing and the UMG `chat_panel` **compile and run in UE 5.8.3 Linux builds**. Matrix cases 1–4, part of 6 and Linux 1080p screenshots pass an automated dedicated-server + three-client test ([evidence](STAGE0C_CHAT_EVIDENCE.md)). **Owner playtest/approval pending (ADR-008)**; cases 5 and 7 are only partly covered.  
**Milestone:** CHAT-001 / Stage 0B–1, integrated with [NET-001](https://github.com/simon-gros/MarburgumII/issues/2) and [UI-001](https://github.com/simon-gros/MarburgumII/issues/3).  
**Engine:** Unreal Engine **5.8.3**, authoritative network architecture.

## 1. Channels and expected player experience

**General:** a player sends a plain-text message to the **entire connected population of their current logical game server/shard**. General chat is not automatically region, team or proximity restricted; do not conflate it with a later local/party channel. There is no inter-server relay or global cross-shard channel in this slice. Offline users do not receive history.

**Private:** a sender addresses **one connected player** by their current **session PlayerState ID**. Only the sender and intended online recipient see the message and it is never placed on replicated GameState, an all-client multicast, or general chat. Server-side validation determines the recipient actor from the authoritative connected controller roster. A player's display name is **not** used as an authentication credential or unique routing key. In this provisional no-account phase, IDs are ephemeral: reconnect may assign another ID and a sender must refresh the ONLINE roster.

The interface has an always-available compact, dark-toned chat frame in the client's lower-left, built from the UI-01 tokens and mounted in the UI root's HUD layer once the client is admitted. It is a first themed implementation, not final visually accepted UI. Enter activates the composer. Free text sends to General; `/g text` explicitly sends to General; `/w <session-id> text` or `/whisper <session-id> text` requests a private message. The **ONLINE** button lists replicated connected PlayerState names and session IDs. Server-confirmed private messages are tagged **PRIVATE** and visually separated from general messages; system failures are tagged **SYSTEM** and never presented as accepted messages. A client can interact via Blueprint API `SendGeneralChat`, `SendPrivateChat`, and `OnChatReceived` without adopting the command-parser UI.

### Scope and future design

- This slice has **no account-level contacts, friends, persistent direct-message history, offline delivery, party channel, channels across server shards, voice, attachments, links, rich text formatting, or mobile notifications**.
- These may be considered in later phases, but only general and one-to-one private text are **required first**.
- Chat is primarily social; it is **not** a gameplay inventory/economy action and cannot grant XP, money or missions. It must not create a client-side authority bypass.
- The game takes place in the 1990s, but server/chat accessibility and communication are an intentional player-experience mechanism, not an in-fiction 1990s internet service.

## 2. Domain and authority boundaries

```text
Client-only native UMG chat panel (MarburgumIIClient)
       |     local text, /w session ID
       v
Owned AMarburgumPlayerController::ServerSendChat (MarburgumII core)
       |     server derives sender from PlayerState; validates payload/rate/recipient
       +---- General -> per-connected PlayerController ClientReceiveChat
       +---- Private -> ONLY sender + addressed PlayerController ClientReceiveChat
       +---- Invalid -> owning sender gets system refusal
       |
       v
Each client's bounded local message history -> OnChatReceived -> UMG
```

**Transport:** Unreal-owned PlayerController reliable RPCs with explicit per-recipient delivery, not an unsecured client multicast and not a shared GameState chat-history property. The server is the routing authority; client messages never specify sender identities, recipient name, message sequence or acceptance. The server assigns an incremental per-sender sequence number. Chat history is **session-local on each client only** and bounded to 80 recent deliveries. Server code does **not** log private message bodies. No persistence or encryption scheme beyond the engine's currently configured transport is claimed.

**Ownership:** core `MarburgumII` defines message types and PlayerController RPCs; `MarburgumIIClient` owns native UMG and UI hooks; `MarburgumIIShared` remains free of UMG; `MarburgumIIServer` is reserved for future session/moderation/persistence service integration. Dedicated server builds must not depend on UMG or `MarburgumIIClient`. The temporary core-owned routing logic should move to an explicit authoritative chat service once lifecycle, moderation and multiple-server delivery are implemented.

## 3. Current technical protections and open requirements

**Source implementation:**
- Text must be nonblank after trimming, limited to **240 UTF-16 code units**, single-line, plain text. Disallow ASCII/C1 controls and bidi/zero-width spoofing characters; preserve Slovene diacritics and ordinary Unicode.
- Server throttles accepted messages to no more than **one per 0.8 seconds** and **eight per rolling ten-second window**, with a server time source. This is an initial defense, not a production-grade global anti-spam system.
- General chat targets connected PlayerControllers **only on this server**.
- Private chat recipient is resolved via live authoritative PlayerState ID, rejects missing/offline recipients, and echoes to the sender once.
- No sender or message-body input is trusted as a server-authored receipt until validated and delivered. The client-only `RecentChat` is bounded and clears on a fresh session.

**Still needed before public MMO acceptance:** rate-limit **invalid** requests to prevent rejection-message amplification; per-account IP/session flood protections; mute/block/ignore and report tools; moderation operations and audit policy; display-name filtering; spam/link/phishing controls; account identity and safe reconnect target matching; explicit retention/deletion/privacy policy; accessibility including screen reader and text scaling verification; localization of shell/error strings; reliable handling of disconnect during send; formal consent/eligibility for special channels. Do not claim these implemented. Server administrators must eventually be able to mute or restrict abusive accounts without republishing private content.

**Privacy safeguard:** never add private text to a globally replicated array, broad multicast RPC, telemetry payload, public crash report or log file. Test using two or preferably three distinct clients; an otherwise connected bystander must **not receive** any private text.

## 4. UI/HUD treatment and visual quality

The chat panel is an independent `chat_panel` module in the [UI/HUD ontology](UI_HUD_ONTOLOGY.md). The final layout should complement the refined 1990s urban documentary look: slim charcoal/blue-grey background, warm light text, a distinct subdued teal treatment for private messages and amber for system errors, clean spacing and typography supporting Slovenian. Avoid an oversized permanent MMO chat wall.

**Production refinement:** user-adjustable opacity, font size and panel scale; show/hide/minimize; unread private-message indicator; distinct general/private conversation tabs; sender selection by name/ID from the current online roster; whisper reply, tab completion or equivalent input affordance; keyboard/controller focus consistency; autoscroll that does not pull users away from older messages; timestamp formatting and localized copy; scrollback bounds; filter/ignore/report accessibility; high contrast and low-distraction modes. The present panel is **a functional source-level first-pass** and lacks much of this polish.

The default HUD must not obscure nearby interaction prompts, the selected mission objective, driving mode or subtitles. All UI display events originate from server-accepted text where applicable. Private conversations do not leak via notifications to other players.

## 5. Test matrix and owner acceptance (CHAT-001)

The first qualification must include **real, separate packaged UE 5.8.3 client and dedicated-server processes** from a freshness-sealed build. Unit tests and repository CI are **not** runtime proof.

1. **General:** A, B and C connect to one server. A sends general chat; A, B, C see exactly one copy with correct sender ID and message body.
2. **Private:** A sends private to B; **only** A and B see it. C sees no event, log, notification, history or UI spill. A and B can reply privately; sender cannot impersonate another player.
3. **Invalid recipient:** A targets a nonexistent/disconnected ID; server rejects without routing a private message, and A receives an understandable system notice. Rejoin with new ID; stale recipient IDs fail cleanly.
4. **Validation:** blank, oversized, line-break, bidi controls, bogus channel and burst spam are rejected/throttled. Slovene diacritics and ordinary text survive intact. Private body never appears in server plaintext logs.
5. **Session lifecycle:** connect/reconnect, visibility and focus, scrolling, input Enter/Return, open/close of other management panels; no sticky input capture or duplicated subscriptions.
6. **Security/robustness:** malicious payload cannot pick a sender or arbitrary client; verify no NetMulticast for private content. Full protocol fuzzing/DoS tests and account bans remain later release blockers.
7. **UI quality:** screenshots at 720p, 1080p, 1440p, ultrawide and 4K with Slovene/English; maintain low-clutter look and readable private-message distinctions.

The project owner **personally plays** the accepted build, checks general/private conversation usability, privacy, typography and input focus, and explicitly records `approved`, `changes_requested` or `rejected` with the exact Git SHA and cooked asset digest. **CHAT-001 is not complete until the owner explicitly approves.**

## 6. Implementation tracking and limits

**Source already added:** `MarburgumChatTypes.h`, `MarburgumPlayerController.{h,cpp}`, `MarburgumChatPolicy.{h,cpp}`, `MarburgumChatWidget.{h,cpp}`, client module event hook, native automation test `Marburgum.Chat.TextPolicy`. The native UI automatically registers through a client-only event after local PlayerController initialization.

**Verified on Linux (2026-11-09):** UnrealHeaderTool, compilation and linking of all targets; `Marburgum.Chat.TextPolicy` passes; `scripts/chat-smoke-linux.sh` runs a packaged dedicated server and three packaged clients (sender, recipient, bystander) through matrix cases 1–4 and checks that no private body reaches the server or any client log; the panel renders in packaged clients ([evidence](STAGE0C_CHAT_EVIDENCE.md)). Native qualification found and fixed two defects that kept the original panel off screen (see the evidence).

**Not verified:** interactive keyboard input and focus (case 5), chat across reconnect, screenshots beyond 1080p and Slovene UI copy (case 7), protocol fuzzing, Windows, and owner testing.

**References:** [MMORPG networking](NETWORK_ARCHITECTURE.md), [UI/HUD roadmap](UI_HUD_ROADMAP.md), [source acceptance rules](DECISIONS.md), [fresh playtest policy](PLAYTEST_FRESHNESS.md), [Unreal RPC requirements](https://dev.epicgames.com/documentation/unreal-engine/remote-procedure-calls-in-unreal-engine).
