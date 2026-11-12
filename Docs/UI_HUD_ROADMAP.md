# UI/HUD development roadmap — MarburgumII

**Revision:** 0.3.0 — 11 November 2026; adds ADR-040–045 ([latest analysis](DEVELOPMENT_UPDATE_2026-11-11.md)), following [prior audit](DAILY_CHANGE_REVIEW_2026-11-10.md)  
**Scope:** P0-to-production user interface program for the new **Unreal Engine 5.8.3** client.  
**Normative concepts:** [UI/HUD ontology](UI_HUD_ONTOLOGY.md) and [machine-readable module registry](../Data/Ontology/ui_hud.v0.1.json).  
**Visual contract:** [UI_HUD_DESIGN.md](UI_HUD_DESIGN.md).  
**Network dependency:** [NET-001](https://github.com/simon-gros/MarburgumII/issues/2), a genuine two-client authoritative session.  
**Umbrella implementation task:** [UI-001](https://github.com/simon-gros/MarburgumII/issues/3).  
**Status:** Implemented Linux UI slices include UMG gallery/session shell, general/private chat, server receipts, item/NPC/vehicle prompts, location, mission objective, condition including swim/down, vehicle speed/REV, and first Character/Inventory/Journal shells. **ADR-041 adds real combat event notifications and animation**, and **ADR-044 adds a visible, server-driven ambient pedestrian population with incident reactions**; these are gameplay features, **not proof of a completed combat HUD, social UI, NPC-nameplate system or UI-05 population overlay**. The nine-window map (ADR-045) expands camera/visibility/layout testing. UI-02A authenticated first launch is implemented for development (ADR-055, ADR-057: account screens, a full-height 3D creator and ticket admission), awaiting the packaged screen matrix and owner sign-off; Skills and Settings remain unimplemented. UI-07 current-package, keyboard/controller/input conflict, accessibility, multi-client state hygiene and owner sign-off remain open. [Combat evidence](STAGE1_COMBAT_EVIDENCE.md), [street evidence](STAGE1_STREET_EVIDENCE.md).

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
| **UI-02A: Verified accounts and first character** | Stage 0F–1 public entry | Email-confirmed registration, real username/password login, mandatory First/Last/model creation and server-approved world entry | UI-01, UI-02, account service, authoritative character persistence | FL-01–FL-12; secure account admission and owner-approved full-screen captures |
| **UI-03: Live modular exploration HUD and basic communications** | Stage 0C–1 | Location, condition, context prompt, objective, subject cue, notifications and general/private chat | UI-01, UI-02, NET-001 for live status, CHAT-001 for messaging | Real general/private recipient routing, graphical display and no privacy leaks |
| **UI-04: Information-rich management screens** | Stage 1 | Visual Character, Skills, Inventory, Journal and Settings with unified navigation | UI-01 and signal/view-model contracts | Nonempty/empty/error/stale state matrix; screenshot-backed UI review |
| **UI-05: Context and cooperative overlays** | Stage 1 driving prototype; Stage 2–3 expansion | Existing seat/speed/REV/exit context; fishing/work/trade/dialogue/party follow real gameplay | Real authoritative corresponding system | Seat/refusal/reconnect focus, contextual clearing and later group privacy |
| **UI-06: Layout personalization** | After Stage 1 baseline, Stage 2–3 | Presets, local persistence, per-module sizing/position/visibility, edit mode and reset | Accepted default UI and focus/safe-zone constraints | Drag/move/scale/snap/restore tests; essential alerts always visible |
| **UI-07: Fidelity, accessibility, performance and capture** | Cross-cutting from UI-01 | Continuous polish and verifiable public-ready client UX | Each accepted slice | Screenshot/input/accessibility/performance gates per supported target |

**Important:** UI-05 breadth and UI-06 extensive customization do not delay Stage 1, but their underlying *module registry, layout policy, tokenized style and state boundaries* must be designed in UI-01/03 so future work does not require rebuilding the interface.

### Updated phase and work-package ledger (11 November 2026)

| Phase | Proven first runtime slice | Still required |
| --- | --- | --- |
| UI-00 | Ontology, stable module registry and exclusions | Current vehicle/swim vocabulary owner review |
| UI-01 | Design tokens, actual UMG primitives, SAMPLE DATA gallery | Polish and full scale/contrast matrix |
| UI-02 | Join/failure/retry/recovery shell | Keyboard/controller focus, logout and session edge cases |
| UI-02A | Login/character lifecycle **implemented for development** (ADR-055, ADR-057); packaged matrix and owner sign-off open | FL-01–FL-12, verified backend, full-screen creator/model preview, server admission |
| UI-03 | Chat, receipt notifications, prompt, location, mission, condition/swim/down, car status | Integrated consistent layout and privacy/focus/connection-state review |
| UI-04 | First Character, Inventory and Journal | Skills, Settings and actual production identity, loading/disabled/error states |
| UI-05 | First vehicle/seat/driver context plus combat receipt/event feed, live pedestrian activity in-world | Contextual guard/strike feedback, readable incidents, future real fishing/trade/dialogue/group UI; no permanent combat wall |
| UI-06 | Profile architecture only | Noneditable presets after accepted default, editor later |
| UI-07 | Selected authentic Linux captures | Current SHA/asset seal, device/scale/accessibility/owner approval |

**UI-P0A — Current-build integrated visual test.** Repackage the actual default `/Game/World/lent` with the latest façade/roof, protagonist, swim animations, vehicle content and dedicated server. Check display sizes 1280×720, 1366×768, 1920×1080, 2560×1440, 3440×1440, 3840×2160 and UI scales 80/100/125/150/200%. Distinguish gallery SAMPLE DATA from live server-backed captures, record SHA, cooked content hashes and frame-time impact.

**UI-P0B — Input arbitration and truthful status priority.** Focus priority: auth/system modal > managed panel > chat text > nearby E interaction > movement/driving. Typing or opening a panel must never simultaneously move/drive/exit/use an item; E must choose vehicle-door context before NPC/item appropriately. Clear state on logout, server loss, seat change, death/recovery and travel. Condition priority is DROWNED/recovery > underwater/held breath > swimming > winded > running. Upright treading is an animation state, not an invented inventory/gameplay stat.

**UI-P0C — First launch (blocking public entry).** Create polished verified-email, login/password/recovery, First name / Last name / approved 3D model creator and loading/admission screens backed by the trusted account service. Public game must deny unauthorized travel and dev `-MB2Character` bypass. Validate Slovene diacritics, long names, model freshness/rights, duplicate creates, server restart, returning user and two authenticated clients. Owner personally playtests first and subsequent entry.

**UI-P1 — Complete live presentation.** Character/Inventory/Journal must show real server-owned data and honest unavailable fields, Skills and Settings need real first pass, pinned objective must match saved mission, vehicle card only while seated, swimming feedback coherent with condition and camera. Add proper response to denied seat, unavailable fish, failed store commit, private recipient disappearing and reconnect.

**Proposed acceptance IDs (not tests already implemented):** `UI-REAL-01` crate→receipt→inventory; `UI-REAL-02` mission→objective→journal→restart; `UI-COND-01` run→swim→down→recovery; `UI-VEH-01` driver/passenger/brake/reverse/exit; `UI-FOCUS-01` chat/panel/vehicle conflicts; `UI-PRIV-01` three clients and private data; `UI-RES-01` six resolutions/five scales; `UI-FL-01` new/returning-account admission. Every major module requires owner visual and in-game approval.

## 3. UI-00 — Ontology and legacy behavior inventory

**Implement now as documentation and testable definitions.**

1. Register every canonical module in `Data/Ontology/ui_hud.v0.1.json` with ID, mode, stage, signals, source, visibility and priority.
2. Map original UI to new components. Preserve *information content* and interaction behavior while re-authoring every visual asset and Unreal implementation.
3. Confirm excluded mechanics never enter the module catalogue.
4. Define a data privacy/authority table: gameplay state server-owned; input/device/layout client-local; historical place labels reviewed; fixture data isolated.
5. Record unresolved product decisions for later art/UX approval; do not fill gaps with generic gameplay assumptions.

**Exit gate:** machine-readable source validates referential integrity and the visual ontology links to actual phases and acceptance. **Current:** catalogue and several actual UMG widgets exist. The older no-live-UI statement was the creation-time snapshot; design approval and personal visual acceptance remain separate.

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

## 5A. UI-02A — Verified login and mandatory first-character creation (Stage 0F → Stage 1)

**Product and semantic authority:** [FIRST_LAUNCH_ONTOLOGY.md](FIRST_LAUNCH_ONTOLOGY.md) and [first_launch.v0.1.json](../Data/Ontology/first_launch.v0.1.json). This is a **new implementation workstream**, not a declaration that UI-02's existing connection shell supports accounts. UI-02 remains valid for network/dev smoke tests; **UI-02A is a blocker for a player-facing Stage 1 entry**. Its first visible screen is a branded **Username / Password / Sign in** interface with Create account. Registration asks for email verification, then provisions username/password securely; the newly authenticated player then sees a **First name / Last name / character model** creator *before* gameplay. Returning players go directly to their saved character and Enter World.

- **UI-02A-1 — Registration and credentials:** dedicated title/login, register-email, verification pending, credential setup, forgot/reset password, authenticating and refused/retry states. An email link/code must be verified by the account service; no plaintext password in email.
- **UI-02A-2 — Character creation:** full-screen, accessible, visually polished 3D model preview, First name and Last name fields, model selector from a real approved catalogue, validated errors, confirmation/pending/retry and safe cancellation. Keep account username distinct from character display name.
- **UI-02A-3 — Durable authorization:** only server-committed character ID and authenticated ownership enable Enter World; direct travel, stale session, development character key and spoofed client model/name cannot bypass the shipping world gate. Preserve the existing explicit test-key pathway for development smoke tests only.
- **UI-02A-4 — Usability and owner validation:** model asset freshness, full input/focus, 720p–4K and ultrawide snapshots, Slovene diacritics, errors/timeouts, logout/reconnect, returning accounts, and two independent authenticated clients. Owner must personally test first-time and returning flows and approve the exact build.

**Exit gate:** FL-01–FL-12 from the first-launch ontology; pass verified-email, denied unverified access, unique credentials, character create/replay/restart, resumed character, no unauthenticated spawn, visual/input matrix and fresh cooked asset manifests. Current implementation status: **design and roadmap only; no production auth or character creation code**.

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

**Early chat integration:** Build the [`chat_panel`](CHAT.md) with always-available server-wide general messages and private player-to-player conversation. General messages may reach all clients on the logical server; private ones must reach **only sender and chosen recipient**, never other clients' replicated state or notifications. Real UE 5.8.3 packaged Linux clients and dedicated-server **three-client isolation test have passed** ([evidence](STAGE0C_CHAT_EVIDENCE.md)); stale target/focus, visual refinement and personal owner approval remain open. Keep server routing independent of UMG, and give the chat its own focus/opacity/unread-message policies so it does not obscure the interaction HUD.

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

### UI-05A — Unarmed combat and living-street feedback (ADR-041/044; implemented mechanics, incomplete UI)

**Current factual baseline:** F = jab, R = committed swing, G = hold guard; the server selects targets and resolves stamina, cooldown, stagger, blocked blows, down/recovery and NPC reactions. The existing notification feed displays fight results; pedestrians already flee/watch/respond to incidents. No fully designed combat status or population-management panel has been accepted.

**Next UI tasks:**
- Show **only the immediately relevant** jab/heavy/guard context (when appropriate), cooldown/refusal/blocked feedback, condition/stamina and down/recovery. Do not create a permanent fantasy skill bar, automatic lock-on, universal enemy nameplates or fictional reputation/police warnings.
- Distinguish the state **authoritative outcome vs predicted attack input**. Deduplicate repeated move/request-count events and make hit, miss, too-soon, insufficient stamina, out of reach, blocked, down and recovered visually and textually distinct. Do not publish per-NPC private data or invent PvP.
- Establish focus arbitration for **F/R/G, Enter/chat, E/door/interaction, WASD/Shift and Character/Inventory/Journal**, including keyboard/controller translations and when a vehicle seat, water/down state, modal, chat edit or menu disables action.
- Prevent combat toasts, vehicle speed, condition warnings and mission objective from colliding. Urban public captures should show **human street behavior** without a wall of overlaid names. No wanted stars, police heat/pursuit or imported GTA sound/icon assets.
- Add explicit scenarios for off-screen crowds, watching/fleeing NPC reactions, passenger versus driver prompts, incident spread near two connected clients, culling and re-appearance, reconnect and down-body cleanup. The **street smoke currently uses one editor client, not packaged multiplayer**; UI screenshots cannot upgrade that claim.
- On the **1,536 m square** validate location banners across all window boundaries; distinguish authoritative **district labels** from implementation-only map-window IDs. Verify no incorrect local labels or stale prompts as the streamed/not-streamed scene changes. Current one-level world has **no streaming**; don't fake loading/zone status.

**Suggested not-yet-implemented acceptance cases:** `UI-COMBAT-01` jab/guard/heavy receipts and visual priority; `UI-COMBAT-02` down/recovery; `UI-STREET-01` fighting incident reactions with observer; `UI-STREET-02` car dodge/run-over/remote visibility; `UI-MAP-01` nine-window location/occlusion/crossing matrix; `UI-FOCUS-02` Enter/F/R/G/E/drive input safety; `UI-IK-01` rendered stroke, tread, vehicle door and fight feet after the full animation graph on an actually fresh package.

## 8. UI-05 — Contextual activity and multiplayer modes

Only when a real corresponding system exists, expand these overlays in order of user value. The first driving context and combat notification feed already have live inputs:

1. **Fishing/work/interactions:** contextual task status/progress/eligible tools, results, accessibility cues, animation of meaningful events.
2. **Dialogue/investigation:** contact identity, speech text, deliberate responses, evidence and journal links; decision confirmation via authoritative receipt.
3. **Trading and economy:** item detail, explicit currency, price/quantity preview, guarded buy/sell confirmation, receipt/error without double spending.
4. **Driving — first runtime slice implemented (ADR-039):** the real `vehicle_status` card shows replicated vehicle, seat, speed in km/h and REV; `interaction_prompt` chooses DRIVE / GET OUT. Verify driver/passenger/refused seat transitions, context/hide/reset on exit and disconnect, modal/chat input focus, and absent unsupported ownership/damage UI. Route and navigation enhancements remain future scope.
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
| HUD states | Exploration, area/window transition, item/NPC/car prompt, objective/receipt/error, running/winded, swim/tread/underwater/down/recovery, car driver/passenger/reverse/exit, jab/heavy/guard/refusal/down and incident reactions; no fictional police/wanted or unsupported group state |
| Panels | Character, Skills, Inventory, Journal, Settings, connection shell, login, email verification, credential setup and first-character creator |
| Values | Zero, unknown, large valid integers, long Slovene/English titles, invalid item/actions, unauthorized target |
| Input | Keyboard/mouse, controller-only, focus recovery, device switching |
| Continuity | Connected/joining/timeout/disconnect/reconnect, refusal, world exit, stale private target, seat/prompt/condition reset, public auth expiry once UI-02A exists |

At the **first public Stage 1 showcase**, require (a) a clear 1440p or 4K real-client screenshot, (b) a short gameplay capture showing **server-confirmed interaction and feedback** with visible polished HUD, and (c) a capture manifest specifying game/engine SHA, resolution/scale, input, connection truth, licensed assets, and whether any UI fixtures were used. Do not say an activity/panel is implemented when the footage is of a fixture.

### Performance

Measure real UI overhead on the development GPU and a lower-tier representative platform when available. Record UMG/Slate CPU time, widget invalidation, synchronous first-open loads, input-to-feedback latency, GPU composition and VRAM growth. Set **numeric budgets from measured baseline and target platform policy**, not invented numbers. Prevent per-frame rebuilding/binding of large text/inventory/skill trees. No server-side UMG assets or UI threads.

## 11. Completion rules

Each phase has three independent flags: `design_approved`, `runtime_implemented`, `evidence_accepted`. A design document only closes the first. A native build may close implementation if the feature runs; screenshots, input tests, network verification and manual review close acceptance. Mark deferred systems honestly. Keep high-value UI work ahead of a flood of additional NPC/world systems.

**The desired product sequence:** original semantics + ontology → excellent coherent visual foundation → real server-connected HUD → readable management panels → one beautiful, honest shareable district preview → context breadth → optional UI personalization and expansion.

References: [ontology](UI_HUD_ONTOLOGY.md), [visual contract](UI_HUD_DESIGN.md), [MMORPG-first network architecture](NETWORK_ARCHITECTURE.md), [top-level roadmap](ROADMAP.md), [parity register](LEGACY_PARITY.md).
