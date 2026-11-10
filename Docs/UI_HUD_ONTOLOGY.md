# Ontology of User Interface and HUD — MarburgumII

**Version:** 0.1.0 — 8 November 2026  
**Status:** Normative *design vocabulary*, not itself proof of a particular implementation. Actual Linux UE 5.8.3 UMG screenshots, server-fed modules and multiplayer tests are separately documented in the [UI roadmap](UI_HUD_ROADMAP.md).  
**Companion catalogue:** [`Data/Ontology/ui_hud.v0.1.json`](../Data/Ontology/ui_hud.v0.1.json).  
**Underlying game ontology:** [`ONTOLOGY.md`](ONTOLOGY.md) and [`core.v0.1.json`](../Data/Ontology/core.v0.1.json).  
**Implementation plan:** [`UI_HUD_ROADMAP.md`](UI_HUD_ROADMAP.md); [existing visual contract](UI_HUD_DESIGN.md).  
**Technical foundation:** Unreal Engine **5.8.3**, `MarburgumIIClient` (ClientOnly), `MarburgumIIShared` (common contracts), `MarburgumII` (replicated game state) and `MarburgumIIServer` (authority).

**Pre-world companion (10 November 2026):** [FIRST_LAUNCH_ONTOLOGY.md](FIRST_LAUNCH_ONTOLOGY.md) and [first_launch.v0.1.json](../Data/Ontology/first_launch.v0.1.json) formally specify login, email confirmation, character creation and world admission. These are full-screen identity surfaces distinct from the in-world HUD. Existing UI-02 connection states cannot claim account authentication; UI-02A adds that feature before any public player entry.

## 1. Fundamental conception

The interface is a **semantic projection of the game world, player capabilities and current activity**, constrained by what the server has confirmed, what the character knows, what the user needs to decide next, and what the display can comfortably communicate. It is not a decorative overlay glued onto a standalone game. The HUD is one **low-interruption presentation mode** within a broader modular, accessible, persistent MMORPG interface.

A strong large-scale online interface follows these independent principles:

1. **At-a-glance hierarchy.** Immediate safety, relevant action and current objective take precedence over broad information density. Information about skills, inventory, missions, economy and social systems remains accessible without permanently covering the world.
2. **Context-driven composition.** Exploration, dialogue, fishing, investigation, vehicle driving, trading, cooperative play, management screens and connectivity are different presentation situations. Showing the same modules in every situation creates noise and poor interaction design.
3. **Stable visual grammar.** All panels share a cohesive typographic scale, spacing, colors, icon language, borders, selection, status and animation semantics. The default layout itself must be excellent.
4. **Modular flexibility.** UI modules have stable identities, anchors, visibility rules and future per-player layout profiles. Player-customizable arrangement is architecturally anticipated, but drag-and-drop editing follows a validated default.
5. **Trustworthy information.** A preview, client-local prediction, server-acknowledged result, replicated durable state and uncertain historical label are different sources. They must never be presented interchangeably.
6. **Professional presentation.** The final client uses high-quality authored UMG visuals and honest gameplay captures, not unstyled text, screen-filling status lists or images pretending to be executable UI.

The aesthetic identity remains **Marburgum**: restrained, high-quality, documentary/urban Central European design informed by 1990s signage, industrial materials, civic paperwork, urban transport and the Drava cityscape. Decorative nostalgia must not harm usability. Minimal permanent chrome is especially important for public screenshot and video quality.

**Communication:** The initial `chat_panel` is a first-wave UI-03 client module for server-wide general and one-to-one private messages. Chat is a server-validated unicast event flow; in particular, `private_chat_event` cannot be globally replicated. The `chat_panel` now passes compiled Linux UE 5.8.3 packaged-client and dedicated-server three-client routing/isolation tests ([evidence](STAGE0C_CHAT_EVIDENCE.md)); additional visual/input refinement and the owner's personal approval remain open. See [CHAT.md](CHAT.md).

**Binding product exclusions:** do not import the original wanted-star, heat/wanted-level or police-pursuit/chase system and do not create any HUD/widget from those mechanics. The previous generated Talent system and Talent UI are also excluded. Narrative institutions and ordinary non-pursuit characters are separate questions; they are not excuses to introduce the excluded systems.

## 2. Ontological levels: what a UI object is

These are **semantic concepts**, not Unreal UCLASS names, built Widget Blueprints or save tables. The machine-readable companion contains each type and its relations.

| Level | Types | Responsibility |
| --- | --- | --- |
| **Session and context** | `interface_session`, `session_state`, `presentation_mode` | Who is using the interface, whether the client is connected, and what activity currently matters |
| **Composition** | `ui_surface`, `module_definition`, `module_instance`, `widget` | Where elements are presented and which reusable controls compose them |
| **Spatial layout** | `layout_region`, `layout_profile`, `layout_constraint`, `visibility_policy` | Anchors, safe areas, priority, collision, scale and optional future customization |
| **Information** | `semantic_signal`, `view_model`, `command_intent`, `authoritative_receipt` | Typed data and command/receipt lifecycle; prevent UI from forging gameplay progress |
| **Feedback** | `feedback_event`, `notification_policy` | Deduplicated event/priority/confirmation and error delivery |
| **HUD semantics** | `player_frame`, `subject_frame`, `party_frame`, `activity_indicator`, `context_prompt`, `objective_tracker`, `location_banner`, `navigation_aid`, `notification_feed`, `action_palette` | The minimal decision-relevant information projected over the world |
| **Management screens** | `inventory_view`, `skills_view`, `journal_view`, `character_view`, `dialogue_view`, `social_view`, `settings_view`, `modal_dialog` | Deliberate, information-rich workflows instead of permanent HUD clutter |
| **Interaction** | `input_action`, `focus_scope` | Input device, key glyphs, navigation, game/UI ownership and focus recovery |
| **Design and accessibility** | `presentation_token`, `localized_string`, `accessible_semantics`, `player_preference` | Visual system, culture-aware text, readable semantics and player settings |
| **Verification** | `capture_case`, `design_fixture`, `performance_budget`, `capability_gate` | Screenshots, controlled preview state, profiling and truthfully gated UI features |

### 2.1 Definition, instance and state

A `module_definition` is an immutable registered concept, such as `inventory_panel`, independent of whether it is currently visible. A `module_instance` is its actual client-side presentation on a particular viewport. A `ui_surface` organizes modules by purpose and input authority (root HUD, world overlay, full-screen management screen, modal). A `widget` is a small reusable control (e.g., progress ring, item tile, status label, input glyph, focusable menu button).

A `presentation_mode` selects a view of available capabilities; it **does not create or change gameplay state**. The `session_state` represents real joining/recovering phases; `ready` cannot be guessed because a loading animation ended. These are separate from the game world's persistent `StateRecord` and `PlayerCharacter`.

### 2.2 Signals and authority

A `semantic_signal` has a type, owner/visibility scope, provenance, freshness, fallback and change identity. A `view_model` consumes these signals to render meaningful values, formatted and localized for the particular player. A `command_intent` is a request. An `authoritative_receipt` is a distinct accepted/rejected outcome. One intent may fail; no UI animation can turn that failure into currency, XP, mission progress or an item.

**Authority classes:**

| Class | Examples | Display rule |
| --- | --- | --- |
| `authoritative_replicated` | Player condition, inventory, mission pin, skill XP, vehicle occupancy, party state | May represent live gameplay only after permission and freshness checks |
| `authoritative_receipt` | One accepted/rejected action, award or transaction | One deduped notification per operation/event ID |
| `client_observed` | Connecting, download progress, timeout, local frame timing | Clearly local state; never claims server admission before confirmation |
| `client_local` | Key glyph, UI scale, layout placement, contrast, focus | Legitimate local control; no gameplay grants |
| `authored_verified` | Period-correct place identity and approved source label | Do not mislabel modern geography as authenticated 1990s history |
| `localized` | Display copy, numbers, date/time formatting | Culture-aware presentation, no authoritative mutation |
| `test_fixture_only` | Demonstration items or simulated HUD values | Explicit SAMPLE DATA indicator and isolation from actual network commands |

This classification makes the UI architecture testable before the complete gameplay ontology is implemented.

## 3. Primary HUD modules

The compact default exploration layout should use **well-designed low-density modules**, with higher-information screens deliberately opened by the player.

| Module (catalogue ID) | UI meaning | When it appears | Stage |
| --- | --- | --- | --- |
| `player_condition` | Vitality/stamina, clear readable state | Small default condition area when supported | UI-03 |
| `location_banner` | District/street/landmark and temporal context | Short transition, then fade | UI-03 |
| `interaction_prompt` | One eligible verb, input glyph and subject | While a reachable target/verb exists | UI-03 |
| `subject_context` | NPC/contact nameplate or interacted object | Near a selected/meaningful subject only | UI-03 |
| `pinned_objective` | Single current actionable mission step | Only if pinned and available | UI-03 |
| `notification_feed` | Server-confirmed rewards, events and failures | Transient or acknowledgeable according to importance | UI-03 |
| `activity_status` and `action_palette` | Work/fishing/trading/photography task progress and limited available tools | Only during a supported activity | UI-05 |
| `vehicle_status` | Vehicle identity/speed/condition when available | Only inside an authorized operated vehicle | UI-05 |
| `party_roster` | Compact small-group list, relevant status | Only in an active group | UI-05 |
| `navigation_aid` | Optional local orientation or mission bearing | Only when a period-plausible aid is activated | UI-05 |
| `connection_status` | State, errors, recovery, latency if supported | During network transitions or problems | UI-02 |
| `status_alert` | Real non-excluded gameplay hazard requiring response | Only when a verified supporting mechanic exists | UI-05 |

Do **not** fill the default HUD with a full ability/action bar, permanent minimap, quest list, unreadable number grids, a radial control menu or a combat target frame without a corresponding real gameplay need. Optional compact action palettes should expose **urban verbs and tools**, such as inspect, talk, fish, photograph, drive, use, trade or perform work. A full navigation map can exist on demand later; a contemporary automatic GPS overlay is not assumed in a 1990s setting.

HUD priority follows **critical → immediate → contextual → optional → decorative**, subject to a finite area budget and non-overlap rules. A critical refusal must take priority over a celebratory XP animation. World-space nameplates require relevance/occlusion tests and bounded density; rendering every pedestrian's name by default is both visually noisy and expensive.

## 4. Screens and information hierarchy

**Character:** portrait/identity as authorized, condition, equipment if supported, learned competencies and social summary. It must not invent unimplemented stats to fill an attractive panel.

**Skills:** scannable registered skills, icon, title, current level/XP, progress and meaningful next unlock when accepted by the gameplay design. The original skills are retained semantically; 0–1990 and progression curve remain reviewable rather than assumed production rules. No Talent panel.

**Inventory:** grouping by gameplay meaning (equipment, useful items, evidence), coherent item cards, counts, selected-item detail, contextual available actions and money if available. An unavailable action is disabled with an explanation, never a clickable fake implementation.

**Journal:** active/completed opportunities, one pinned step, prior objective history, clues, witness/evidence connections and geographic context where authorized. Evidence may be uncertain or contested; the UI must distinguish possession, observation and interpretation.

**Dialogue/Contact:** identity, attributed text, deliberate response choices, meaningful result and input focus behavior. If choices carry consequences, show confirmation/feedback only after authoritative processing.

**Social/Party:** only authorized party and messaging data. Small cooperative groups are the immediate use case; raid-scale frames are not part of the default early HUD. Private skills/money/mission progress remain private unless explicitly authorized.

**Settings/Layout:** native resolution and UI rendering sharpness, independent UI scale, readable fonts, input glyphs and focus, contrast/opacity, motion reduction and saved preferences. Add optional layout presets and module editing only after default behavior is accepted; do not make players repair the default interface.

Each screen has `empty`, `loading`, `ready`, `stale`, `disabled`, `error` and `unauthorized` presentation rules as applicable. These are intentionally visually designed, not generic empty strings.

## 5. Presentation modes and composition rules

The primary modes are **connection**, **exploration**, **focused interaction**, **investigation**, **activity**, **driving**, **party**, **management** and **system modal**. They may overlap, but the compositional precedence must be explicit.

A proposed precedence when multiple surfaces compete:

```text
Critical connection/refusal modal
         > blocking confirmation / deliberate dialogue
         > active management screen
         > activity / driving context
         > focused interaction and party context
         > default exploration HUD
         > decorative transitions
```

The modal owns keyboard/controller focus until acknowledged/cancelled. A nonmodal toast never steals focus. Opening a management screen must not corrupt input mode; closing it restores the prior focus target or gameplay input. A visual mode switch must preserve server-owned activity continuity.

**Sample transition:**

```text
exploration → proximity/context → "Fish — Drava bank"
→ client sends interaction intent
→ server validates and starts activity
→ activity HUD shows wait/strike state
→ server resolves catch/rejection
→ receipt + replicated item/XP/mission state
→ deduplicated feedback appears
→ return to quiet exploration, on-demand Inventory/Journal available
```

Here the UI can *preview* a cast in a labeled fixture for design review, but it can never fabricate a successful catch in real gameplay.

## 6. Layout ontology and customization

A `layout_region` is a responsive semantic container, not a fixed physical pixel rectangle. Suggested starting regions: `top_left_status`, `top_center_location`, `top_right_objective`, `lower_center_context`, `lower_left_condition`, `right_notification_stack`, `world_overlay`, `center_modal` and `full_screen_management`. Actual final anchors must be optimized against the 3D view and measured on target resolutions.

A `layout_constraint` includes anchors/safe-area padding, collision priority, minimum readable size, maximum usable width, content wrapping, aspect behavior, accessibility scale and device-specific input reach. Critical elements cannot be hidden by an optional preset. Responsive collapse prefers **reducing decorative content or moving contextual detail into a panel**, never silently dropping a critical instruction.

A `layout_profile` records per-player opt-in module visibility and placement and must include a version and recovery/default reset. Profiles envisioned after the core default is proven: exploration, detailed activity, driving and small-party play. Do not promise full grid/snap editing in the first UI prototype. The model should still support future move/resize/opacity/grid/snap and import/export **without** requiring a new identity model or a rewrite of HUD state production. A layout file is user configuration data, not gameplay state.

**Visual parameters** remain design tokens, not random hard-coded values: typography (including diacritics), color roles, icon family, border and surface families, spacing scale, emphasis levels, motion, focus outline, hover/pressed/disabled state. A token change should restyle all relevant panels consistently.

## 7. Feedback, information density and accessibility

**Notification kinds:** interaction, progress/XP, skill advancement, objective/mission, item/economy, social/reputation, connection/system warning, activity state and optional group event. A `feedback_event` includes a stable ID, priority, localized text key, optional icon/audio cue, producer and dedupe/aggregation semantics. High-priority warnings persist until acknowledged or resolved; low-value repeated changes can coalesce. No color-only meaning or concealed server rejection.

**Information density:** at-a-glance modules should stay concise; information-rich screens should use selection, tabs, hierarchy and readable density instead of dozens of labels on top of 3D rendering. Keep the world center legible; in high-intensity situations prioritize action/safety over promotions or social notifications.

**Accessibility from the foundation:** keyboard/mouse and controller paths; focus outlines and sane directional order; explicit names/descriptions/roles on active controls; scalable user text/geometry; optional opaque/high-contrast panels; reduced-motion settings; subtitles/dialogue readability; notification timing; audio cue optionality; localization via `FText` and source-aware string keys; correctly rendered Slovene diacritics. Do not claim certified screen-reader support until actual engine/platform verification.

**Initial review matrix:** 1280×720, 1366×768, 1920×1080, 2560×1440, 3440×1440 and 3840×2160 at UI scales 80%, 100%, 125%, 150% and 200% (where implemented), long strings, realistic values, empty/unknown/error states, keyboard/controller modality, contrast variants and connection loss. The goal is **no clipped critical controls, no overlapping important messages, readable text, consistent focus and no wrong authoritative values**. Exact physical font and contrast targets require testing on the selected hardware/display matrix, not arbitrary acceptance by screenshots alone.

## 8. Unreal Engine boundaries and event flow

```text
Server authoritative domain state (MarburgumIIServer + GameMode)
             |
       replicated state / outcome receipt
             v
MarburgumII (GameState / PlayerState) + shared semantic contracts
             |
   typed signal / client freshness and privacy check
             v
MarburgumIIClient view models / presentation-mode resolver
             |
    module registry + visibility / layout + token theme
             |
       UMG widgets / focus / feedback / HUD rendering
```

The server **never** includes UI widgets, font assets, UI themes, input-glyph caches or local layout files. The client must not grant rewards or infer success from animation. The engine-independent catalogue provides **design-time semantics**, not C++ classes or an automatic runtime binding. We prefer **Unreal UMG plus event-driven client-owned adapters** for Stage 0C/1. Advanced plugin-based UI frameworks should be evaluated separately against UE **5.8.3** and should not block first HUD delivery. In particular, avoid unmeasured per-frame binding, expensive construction of inactive screen hierarchies and unnecessary hard references to large world assets.

## 9. Quality and acceptance invariants

1. A professional *default* HUD exists before customizable modules become a substitute for design.
2. Every HUD module has an explicit purpose, visibility rule, data producer and fallback state.
3. Every consequential action is a **request**, and only server-confirmed results become real rewards or mission state.
4. A UI fixture is always visibly labeled and cannot trigger real gameplay mutations or be cited as a functioning MMO screenshot.
5. Critical prompts, refusals and interaction choices are readable at validated scale/resolution extremes and cannot be obscured by decorative content.
6. Client presentation never leaks server-side or other players' private data.
7. Screen transitions retain focus/input correctness and survive reconnect/error/empty states.
8. Fonts/icons/animations are authored or licensed, consistent, localizable and constrained by performance.
9. No excluded wanted/pursuit/legacy talent surface appears in the UI catalogue or implemented game.
10. Real screenshots and a short unedited client gameplay capture with known build SHA are mandatory before a public-facing Stage 1 showcase is called accepted.

## 10. Documented references and evidence boundary

Internal continuity: [original UI roadmap](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/USER_INTERFACE_ROADMAP.md), [original player-experience roadmap](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/PLAYER_EXPERIENCE_PLATFORM_QUALITY_ROADMAP.md), [current UI design](UI_HUD_DESIGN.md), [network architecture](NETWORK_ARCHITECTURE.md) and [original parity register](LEGACY_PARITY.md). Review the original source for behavior and screen meaning; recreate the design without bringing in unreviewed binary art or UI code.

Engine/accessibility engineering references:

- [Epic: UMG optimization guidance](https://dev.epicgames.com/documentation/unreal-engine/optimization-guidelines-for-umg-in-unreal-engine)
- [Epic: User interfaces and HUDs](https://dev.epicgames.com/documentation/unreal-engine/user-interfaces-and-huds-in-unreal-engine)
- [Epic: Common UI framework](https://dev.epicgames.com/documentation/unreal-engine/common-ui-plugin-for-advanced-user-interfaces-in-unreal-engine) — evaluate rather than mandate for the initial milestone.
- [Epic: Text localization](https://dev.epicgames.com/documentation/unreal-engine/text-localization-in-unreal-engine)
- [Microsoft: Game accessibility guidelines](https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/101)

**No part of this ontology proves that a functioning HUD, complete data signal, accessibility feature or build currently exists.** The [roadmap](UI_HUD_ROADMAP.md) and individual acceptance gates determine implementation status.
