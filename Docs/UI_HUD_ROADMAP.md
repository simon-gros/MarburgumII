# UI/HUD development roadmap — MarburgumII

**Version:** 0.1.0 — 8 November 2026  
**Scope:** P0-to-production user interface program for the new **Unreal Engine 5.8.3** client.  
**Normative concepts:** [UI/HUD ontology](UI_HUD_ONTOLOGY.md) and [machine-readable module registry](../Data/Ontology/ui_hud.v0.1.json).  
**Visual contract:** [UI_HUD_DESIGN.md](UI_HUD_DESIGN.md).  
**Network dependency:** [NET-001](https://github.com/simon-gros/MarburgumII/issues/2), a genuine two-client authoritative session.  
**Umbrella implementation task:** [UI-001](https://github.com/simon-gros/MarburgumII/issues/3).  
**Status:** UI-01 (design tokens, primitives, gallery) and UI-02 (session state machine and connection shell) have a first runtime implementation with rendered-client captures on Linux ([evidence](STAGE0C_UI_EVIDENCE.md)); per ADR-008 they are `implemented_awaiting_owner_playtest`, not accepted. UI-02 keyboard/controller focus traversal is not yet verified. UI-03 has its first module: the themed `chat_panel`, with server receipts in the notification feed, qualified by a three-client network test ([evidence](STAGE0C_CHAT_EVIDENCE.md)); the location, prompt, objective and condition modules remain design only.

## 1. Product requirement and planning principle

**A polished, legible, visually coherent HUD is an early deliverable, not a final cosmetic pass.** Its design is modular from the beginning: each component has a stable ID, data producer, state/visibility rules, layout region and accessibility semantics. Start building real UMG components as soon as the editor/client target can compile; develop networking and presentation **in parallel**. Public-facing captures must use actual running client UI and, for gameplay claims, server-confirmed data. Screenshots of mockups are labeled as concepts, never as a completed gameplay slice.

Preserve Marburgum's recognizable experience: transient district names; concise nearby-action prompts; character condition; one pinned objective; progress and mission notifications; purposeful Character, Skills, Inventory and Journal panels; occasional vehicle and activity cues; menu/settings and connection recovery. A modern visual redesign should bring consistent iconography, typography, layering, stable grids and elegant transitions. Urban exploration retains priority over thick GUI chrome.

**Excluded in every phase:** wanted stars, wanted/heat levels, police pursuit/chase gameplay and chase overlays, and generated Talent/TalentTree UI. Do not create code or assets for them. No fantasy/raid-style default ability wall is required by this project's activity structure.

## 2. Dependency graph and delivery stages

```text
               Native Editor/Client builds (NET-001 Gate 0A)
                               |
                    UI-00 — Ontology / audit
                               |
                      UI-01 — Design system
                        /             \
             UI-02 — Connect UX    UI-03 — In-world HUD
                        \             /
                 NET-001 — verified replication
                                |
                    UI-03 live data qualification
                                |
               UI-04 — Character/Skills/Inventory/Journal
                                |
                      First polished Stage 1 demo
                           /              \
             UI-05 — Context modes       UI-06 — Profiles / edit
                           \              /
                  UI-07 — continuing quality gates
```

**UI-07 runs from the first implementation slice and is not a reason to defer all earlier UI until a future final audit.** UI-01 graphical work can begin against controlled design fixtures. Only state backed by an actual live connection may be called implemented MMORPG gameplay.

| Phase | Main milestone | Product outcome | Principal dependencies | Exit evidence |
| --- | --- | --- | --- | --- |
| **UI-00: Semantic baseline and parity** | Stage 0C entry | Approved ontology, naming, signal ownership, modules, visual references and explicit exclusions | Original Marburgum HUD review | Versioned ontology and traceability, no unresolved concept IDs |
| **UI-01: Visual design system and shell** | Stage 0C | Cohesive UMG theme, responsive layout primitives, icon/type system and widget gallery | UE 5.8.3 Editor/Client builds for runtime acceptance | Actual UMG screenshots, contrast/readability review, reusable theme tokens |
| **UI-02: Session, focus and navigation** | Stage 0B–0C | Branded launch/connect/retry/refusal state and keyboard/controller navigation shell | UI-01; actual session semantics for "ready" | Correct connect/reconnect/error states against NET-001 where available |
| **UI-03: Live modular exploration HUD and basic communications** | Stage 0C–1 | Location, condition, context prompt, objective, subject cue, notifications and general/private chat | UI-01, UI-02, NET-001 for live status, CHAT-001 for messaging | Real general/private recipient routing, graphical display and no privacy leaks |
| **UI-04: Information-rich management screens** | Stage 1 | Visual Character, Skills, Inventory, Journal and Settings with unified navigation | UI-01 and signal/view-model contracts | Nonempty/empty/error/stale state matrix; screenshot-backed UI review |
| **UI-05: Context and cooperative overlays** | Stage 2–3 | Fishing/work/trading/dialogue/driving/group presentation as systems become real | Real corresponding authoritative gameplay systems | Correct context entry/exit, privacy, no phantom controls |
| **UI-06: Layout personalization** | After Stage 1 baseline, Stage 2–3 | Presets, local persistence, per-module sizing/position/visibility, edit mode and reset | Accepted default UI and focus/safe-zone constraints | Drag/move/scale/snap/restore tests; essential alerts always visible |
| **UI-07: Fidelity, accessibility, performance and capture** | Cross-cutting from UI-01 | Continuous polish and verifiable public-ready client UX | Each accepted slice | Screenshot/input/accessibility/performance gates per supported target |

**Important:** UI-05 breadth and UI-06 extensive customization do not delay Stage 1, but their underlying *module registry, layout policy, tokenized style and state boundaries* must be designed in UI-01/03 so future work does not require rebuilding the interface.

## 3. UI-00 — Ontology and legacy behavior inventory

**Implement now as documentation and testable definitions.**

1. Register every canonical module in `Data/Ontology/ui_hud.v0.1.json` with ID, mode, stage, signals, source, visibility and priority.
2. Map original UI to new components. Preserve *information content* and interaction behavior while re-authoring every visual asset and Unreal implementation.
3. Confirm excluded mechanics never enter the module catalogue.
4. Define a data privacy/authority table: gameplay state server-owned; input/device/layout client-local; historical place labels reviewed; fixture data isolated.
5. Record unresolved product decisions for later art/UX approval; do not fill gaps with generic gameplay assumptions.

**Exit gate:** machine-readable source validates referential integrity and the visual ontology links to actual phases and acceptance. **Status at roadmap creation: documentation and catalogue delivered; no live UI.**

## 4. UI-01 — High-fidelity graphical foundations

**First coding vertical:** build real client-side UMG widget styles and a small UI component gallery demonstrating actual render quality.

- A unified `UITheme` / token system: semantic colors, typographic styles, icon sizes, margin/radius scale, focus, disabled/hover/pressed, shadows and transparency variants. Use rights-cleared assets and fonts supporting Slovene/English.
- Responsive `UIRoot`/surface layering: game HUD, world-space overlay, notifications, managed screens, system modal, optional screenshot-only diagnostic overlay.
- Viewport-safe region anchors, DPI-aware scaling and an independent UI-scale setting. Design at 1920×1080 but verify 720p, 1440p, ultrawide and 4K.
- Reusable component primitives (status bar, section title, panel, pill/chip, item tile, progress meter, localized label, action glyph, toast, modal, empty/error card).
- Visual state tokens for loading, unknown, disabled, active, selected, focused, success and failure; no color-only meaning.
- **Visual gallery:** show a legitimate test-level screenshot of a running UI layout; if gameplay is not live, the gallery contains a visible `SAMPLE DATA` marker.

**UI-01 acceptance:** actually build/run UE 5.8.3 Client or Editor UMG, capture a baseline and high-DPI screenshot, verify consistent visual styling and no major occlusion/clipping; record the exact build SHA. An attractive image in a design document is not sufficient.

## 5. UI-02 — Session shell, input and reliable navigation

**In parallel with NET-001.** Player-facing online continuity matters from the first MMO test, not after mission content:

- Startup/title/login or guest development session, connecting/handshake, joining, world-loading, ready, reconnecting, refused/full, incompatible version and terminal failure. Display only connection states that the real networking code supports; do not invent account services.
- Progress and timeout, retry/cancel affordance, readable human errors, transition to/from gameplay and loading screen focus.
- Keyboard/mouse and controller navigation: predictable directional order, focused first element, Escape/back, focus restoration and input glyph updates.
- A coherent menu with Character/Skills/Inventory/Journal/Settings routes, disabled if not yet supported.
- No indefinite spinner on failed network connection, no UI falsely declaring authorization.

**Exit gate:** actual connection-loss and reconnect scenarios yield consistent interface and input state using a real server. Preview-only states are acceptable as development artifacts, but they do not satisfy this gate.

## 6. UI-03 — Modular, server-aware exploration HUD

**High-priority visual deliverable before wide-city production.**

The minimal HUD consists of a restrained player-condition module, transient location banner, single contextual interaction prompt, optional one-objective tracker and unified feedback feed. A nearby subject nameplate appears only when relevant, not across an entire populated scene. Define explicit priority and collision rules so a large modal, cinematic, notification and interaction prompt cannot obscure each other.

- Each UI module consumes a **small typed view model** rather than reading unrelated gameplay internals.
- Signals declare their producer, privacy scope and freshness. Value unknown/unauthorized/not yet received renders appropriately.
- A real action produces an `intent → server validation → receipt → state replication → visual change` chain. Client input alone cannot create XP, currency, items or objective completion.
- Reward/event cues deduplicate by event or operation identity, coalesce repetitive progress, and distinguish persistent errors from transient celebration.
- Context states `exploration` and `focused_interaction` must transition cleanly; no ghost prompt when a subject leaves range or the world disconnects.
- The initial default layout must be attractive **without** custom player rearrangement.

**Exit gate:** at least two real clients connected to one dedicated server correctly see appropriate server-owned state changes and do not show fabricated client rewards. Record on-screen and log evidence. This is a substantive integration gate beyond NET-001's minimal replication marker.

**Early chat integration:** Build the [`chat_panel`](CHAT.md) with always-available server-wide general messages and private player-to-player conversation. General messages may reach all clients on the logical server; private ones must reach **only sender and chosen recipient**, never other clients' replicated state or notifications. Source scaffolding exists, but it requires real UE 5.8.3 compilation, dedicated server / three-client isolation test, visual refinement and personal owner approval. Keep server routing independent of UMG, and give the chat its own focus/opacity/unread-message policies so it does not obscure the interaction HUD.

## 7. UI-04 — Core MMORPG information architecture

**Complete a polished Stage 1 preview instead of exposing five separate debug windows.**

| Screen | Essential first-pass content | UI acceptance |
| --- | --- | --- |
| **Character** | Avatar/identity, vital condition, actual supported stats/equipment and contextual status | Clean read-only/empty/no-permission states; purposeful visual composition |
| **Skills** | Registered skills, proficiency/XP, progress indicators and meaningful future unlock when defined | Scannable hierarchy, no generated talents, correct numeric formatting |
| **Inventory** | Item type/icon/quantity, selected-item detail and valid actions, money if supported | No fictitious usable items or duplicated transactions |
| **Journal** | Mission list, pinned current objective, completed history and discovered evidence | Strong navigation and readable archival/investigative design |
| **Settings / Menu** | Display/UI scale, audio, controller/keyboard, contrast/motion, logout/reconnect where supported | Focus correctness and persistence of local preferences |

All panels share one navigation grammar: consistent tab/section positions, return focus, close behavior, item focus/selection, hover details, scroll extent and button-action rules. Avoid large static tables full of inscrutable entity IDs; progressive disclosure should make extensive gameplay information discoverable.

**Stage 1 acceptance:** all five surfaces have actually rendered UMG screenshots with real or **visibly labeled** preview data and correct empty/error states. At least one interaction pathway displays server-confirmed data through the prompt, feedback and a relevant panel. Public demonstration requires the live pathway.

## 8. UI-05 — Contextual activity and multiplayer modes

Only after a real supporting feature exists, expand in order of game priority:

1. **Fishing/work/interactions:** contextual task status/progress/eligible tools, results, accessibility cues, animation of meaningful events.
2. **Dialogue/investigation:** contact identity, speech text, deliberate responses, evidence and journal links; decision confirmation via authoritative receipt.
3. **Trading and economy:** item detail, explicit currency, price/quantity preview, guarded buy/sell confirmation, receipt/error without double spending.
4. **Driving:** vehicle name, readable speed, context exit action and subtle route information if period-plausible.
5. **Small-group play:** compact party frames, joining/leaving, party-relevant notices and optionally authorized communication with privacy controls.

Each activity module must **disappear when not applicable**; optional display is not permission to invent gameplay. Do not design a raid-focused HUD, long toolbar, global NPC nameplates or a police-chase layer as a default.

**Exit gate:** activity-appropriate screen captures, two-client authority/reconnect checks, supported/unsupported capability transitions and performance on a representative district scene.

## 9. UI-06 — Personalizable module layout

Learn from the advantages of configurable modern online interfaces while preserving Marburgum's visual identity:

- Build on **stable module IDs and safe regions**, not serialized Widget Blueprint pointers or fixed-pixel placement.
- Offer cohesive saved local presets (e.g. Exploration, Activity, Driving, Party) with responsive default fallbacks.
- Optional layout-edit mode: show an alignment grid, drag-to-move, keyboard/controller equivalent, snap-to-grid/nearby modules, scale/opacity/visibility where permitted, undo/reset and save/cancel.
- Preserve critical connection/error/refusal cues even if a player hides optional panels. Handle invalid, outdated, off-screen or contradictory saved coordinates via schema migration/reset.
- Do not transfer layout preferences into authoritative server gameplay; storing settings on a player's profile may be a future separate service decision.

**Scheduling:** architecture hooks in UI-01, non-editable presets may be trialed during UI-04, full interactive editing and sharing **after** the Stage 1 default has been visually approved. Do not postpone the entire HUD for a complex editor.

## 10. UI-07 — Quality gates and public capture protocol

### Continuous design-quality checklist

- Visual: coherent theme tokens; typography and icons remain crisp at 100% render UI scaling (independent of world render scaling); no texture stretching, alpha glitches or accidental default-engine styling; efficient animations and no loading hitches.
- Information: critical controls visible, one objective at a time, alerts do not collide, activity modules hide when inapplicable; server values are accurate and privacy-respecting.
- Input: keyboard, mouse, controller, focus restoration, cancellation and tooltips; no hidden layer traps clicks or focus.
- Accessibility: contrast, solid-panel/high-contrast mode, readability, reduced motion, icon+text status, localization/Slovene diacritics, long names/quantities, meaningful assistive metadata; actual screen-reader traversal only when technically supported/tested.
- Technical: client-only rendering modules, shared contracts safe for server, event-based state updates, no expensive polling bindings, lazy load for non-visible complex screens, no unsolicited rendering work in the server binary.
- Provenance: fonts/icons/images/FX licensing reviewed; period and place labels reviewed; data fixture vs live capture provenance explicit.
- Regression: test disconnect/reconnect and unsupported actions; avoid deleted network session state leaving old HUD widgets visible.

### Required matrix

| Category | Required cases |
| --- | --- |
| Screen size | 1280×720, 1366×768, 1920×1080, 2560×1440, 3440×1440, 3840×2160 |
| UI scale | 80%, 100%, 125%, 150%, 200%, where supported |
| HUD states | Empty exploration, location transition, interactable subject, objective pinned/unpinned, notification, critical error, activity/vehicle/group where implemented |
| Panels | Character, Skills, Inventory, Journal, Settings, connection shell |
| Values | Zero, unknown, large valid integers, long Slovene/English titles, invalid item/actions, unauthorized target |
| Input | Keyboard/mouse, controller-only, focus recovery, device switching |
| Continuity | Connected, joining, timeout, disconnect, reconnect, server rejection and gameplay resumption |

At the **first public Stage 1 showcase**, require (a) a clear 1440p or 4K real-client screenshot, (b) a short gameplay capture showing **server-confirmed interaction and feedback** with visible polished HUD, and (c) a capture manifest specifying game/engine SHA, resolution/scale, input, connection truth, licensed assets, and whether any UI fixtures were used. Do not say an activity/panel is implemented when the footage is of a fixture.

### Performance

Measure real UI overhead on the development GPU and a lower-tier representative platform when available. Record UMG/Slate CPU time, widget invalidation, synchronous first-open loads, input-to-feedback latency, GPU composition and VRAM growth. Set **numeric budgets from measured baseline and target platform policy**, not invented numbers. Prevent per-frame rebuilding/binding of large text/inventory/skill trees. No server-side UMG assets or UI threads.

## 11. Completion rules

Each phase has three independent flags: `design_approved`, `runtime_implemented`, `evidence_accepted`. A design document only closes the first. A native build may close implementation if the feature runs; screenshots, input tests, network verification and manual review close acceptance. Mark deferred systems honestly. Keep high-value UI work ahead of a flood of additional NPC/world systems.

**The desired product sequence:** original semantics + ontology → excellent coherent visual foundation → real server-connected HUD → readable management panels → one beautiful, honest shareable district preview → context breadth → optional UI personalization and expansion.

References: [ontology](UI_HUD_ONTOLOGY.md), [visual contract](UI_HUD_DESIGN.md), [MMORPG-first network architecture](NETWORK_ARCHITECTURE.md), [top-level roadmap](ROADMAP.md), [parity register](LEGACY_PARITY.md).
