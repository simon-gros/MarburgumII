# MarburgumII — Early graphical UI & HUD design contract

**Status:** Approved design direction, **not implemented** in MarburgumII.  
**Priority:** P0 presentation architecture; begin alongside Stage 0B networking and deliver a polished first playable presentation in Stage 1.  
**Engine:** Unreal Engine **5.8.3**; client presentation belongs to `MarburgumIIClient`.  
**Ontology and development ownership:** [UI/HUD semantic ontology](UI_HUD_ONTOLOGY.md), [machine-readable component registry](../Data/Ontology/ui_hud.v0.1.json), and [phased acceptance roadmap](UI_HUD_ROADMAP.md). Stable module IDs, signal authority, contexts and layout policies are controlled there. This file controls visual intent and identity; neither file proves runtime implementation.  
**Source of parity:** [Original Marburgum UI roadmap](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/USER_INTERFACE_ROADMAP.md) and the original Unreal `MarburgumGameHudWidget.cpp`, `MarburgumHudModel.cpp`, `MarburgumContextPrompt.cpp`. **These sources are references, not imported code or assets.**

## 1. Product goal

The first shareable MarburgumII gameplay captures should look like an intentional, visually coherent **1990s Central European urban MMORPG**, not an engineering test level covered by debug strings. Create an attractive, professional interface at the **beginning** of the rebuild. Preserve the original Marburgum's **interface semantics and identifiable surfaces**, while redrawing and reimplementing the entire presentation through native Unreal Engine 5.8.3 UI assets and C++. Treat UI design as a **first-class vertical-slice feature**, not late-stage polish. A functioning multiplayer game and a visually coherent HUD are parallel prerequisites for the first public-facing gameplay demonstration.

The city itself remains the visual focus. The normal exploration state must have very little permanent chrome; context and short-lived feedback expose the systems without hiding the street.

**Explicit exclusions (owner decision, 2026-11-08):** do **not** reimplement the original wanted-star indicators, wanted/heat levels, police pursuit, pursuit escalation, roadblock/chase mechanics or a police-chase HUD. These elements are **intentionally removed from MarburgumII**. This decision does not by itself determine whether police officers or public institutions can appear as ordinary NPCs or in authored stories; neither should be used to smuggle pursuit systems back into the game. Also do **not** restore the original generated Talent/TalentTree screens.

## 2. Preserve the original experience — redesign the graphics

| Original Marburgum interface concept | MarburgumII graphical redesign and intended behavior | Default display |
| --- | --- | --- |
| Location / administrative area banner | Elegant, short-lived district/street or landmark title card with restrained transition and period-appropriate visual identity; show only evidence-backed 1990s names | On entering a new named place |
| Nearby interaction prompt | Clear icon, current input glyph, action verb and target name. Examples: `[E] Talk — Riverside angler`, `[E] Inspect — Notice board`, `[E] Fish — Drava bank` | Relevant targets only |
| One pinned mission objective | Compact mission title + current actionable line, optionally small distance or direction when justified | Optional while pinned |
| Vitality and stamina | Designed dual bars with legible labels/icons and understated gradients; no permanent combat overlay | Compact; available in character context |
| Notifications | Distinct icon/text treatment for interaction, objective, XP/skill, item, money, reputation and connection events; aggregation and appropriate persistence | Transient, important errors acknowledged |
| Compact vehicle speed/name | Clear graphical speed display and vehicle identity, readable during motion without blocking the driving view | Only when driving |
| Character sheet | Visual identity and legitimate server-owned stats/condition/equipment slots, with honest empty/unsupported states | On demand |
| Skills | Scannable skill icons, name, level, XP/progress, meaningful milestone; dense but not text-clumped | On demand |
| Inventory | Purposeful slot/list-grid hybrid to test, item silhouettes, quantity, selection details, available actions and evidence role | On demand |
| Mission Journal and evidence | Structured mission timeline, active/completed state, current objective, gathered evidence and pin/unpin, with understated archival graphic motifs | On demand |
| Settings and pause | Consistent menu, interface scale, audio/display/control settings, resume and logout; explicit online disconnect behavior | On demand |
| Login / connect / reconnect | A branded shell with state, loading feedback, actionable error, retry and cancel controls | On joining, loading or recovering |

**Not inherited:** the original Talents screen, wanted stars, police pursuit indicators, or indiscriminately duplicated debug/status text. **Abilities**, if later approved, must be implemented only for genuinely supported actions and must not resurrect the old talent subsystem.

The original project's position and content conventions are useful, but screenshots and source code do **not** authorize copying unreviewed art or adopting exact visual measurements without a new design review.

## 3. Visual design system

Direction: **restrained, premium documentary/urban identity with subtle 1990s influence**. The interface should feel contemporary in its clarity but connected to period Maribor through understated typography, archival-paper motifs, city signage, industrial materials and editorial hierarchy. No full-screen scanlines or fake CRT UI. Avoid fantasy ornamentation, cyberpunk neon spam, generic military HUD frames, huge MMO ability bars and permanent toolbars.

**Proposed initial tokens for design review, not immutable canon:**

- Background surfaces: near-charcoal/slate with an optional higher-opacity accessibility mode.
- Typography: warm off-white primary, subdued secondary text, clear Slovene/English diacritic support.
- Accent: muted copper/amber and desaturated teal for neutral controls/wayfinding; danger and success are semantically distinct and never encoded by color alone.
- Graphic treatment: consistently authored SVG/vector source or rights-cleared scalable icon assets, subtle 9-slice framing, optical alignment, high-quality AA and sharp text at native display resolution.
- Spacing: a deliberate small set of spacing/radii/icon sizes, shared through `UITheme`/style resources. No per-screen ad-hoc values as a permanent solution.
- Motion: short fades/slides with easing; global reduced-motion path, no gratuitous effects or animation that makes important text unreadable.

A **visual reference board and actual UMG screenshots** should demonstrate login, empty exploration, interaction, mission state, Skills, Inventory, Journal and a network failure. Mockups are useful for art direction but cannot substitute for running gameplay screenshots.

## 4. Layout and states (first playable slice)

### 4.1 Explore

- Top/edge: minimal. A transient named-place banner appears on transition then fades.
- Upper side: only a small one-mission tracker when pinned.
- Lower area: responsive action prompt while looking at an interactable. It should never obscure the target or pedestrian footing.
- Condition: vitality and stamina in a compact, deliberately composed surface; hide irrelevant combat-like cues.
- Notification queue: a single consistent layer with bounded lifetime and aggregation, safe from overlap with mission tracker and subtitles.
- No wanted level, wanted stars, police pursuit or chase overlay.

### 4.2 Inspect / activity / vehicle

- Overlay or drawer for items, NPC interaction, fishing phases and mission results without replacing the entire HUD or duplicating other screens.
- Active fishing state can temporarily show wait/strike/catch cues; results are shown only after an authoritative event.
- Vehicle mode can temporarily show the vehicle name/speed and a relevant exit prompt, with predictable transitions back to exploration.

### 4.3 Deliberate management

- The player can open **Character, Skills, Inventory, Journal**, plus Settings/Menu from a discoverable unified navigation system.
- On-demand surfaces have graphical headers, meaningful selected-item detail and legible separation, not a single scroll of raw IDs.
- Do not fabricate player currency, XP, skill unlocks, mission rewards or inventory items merely to make early footage appealing. Use explicit **DEMO / SAMPLE DATA** markings for non-live visual prototypes; live captures must use real replicated state or honest empty states.
- Support long names, Slovene/English translations, numbers, uncertainty/unknown values and no-data states.

### 4.4 Network continuity

The client UI must distinguish `starting`, `connecting`, `joining`, `loading_world`, `ready`, `reconnecting` and explicit `error`/refusal states as supported by the actual connection code. Do not display “Connected” before verified admission. A network failure must never strand the player at a decorative indefinite spinner. The server does **not** create UMG widgets.

## 5. Technical ownership and data flow

```text
Server-only authority (MarburgumIIServer + authoritative GameMode)
    → replicated GameState / PlayerState / validated action result
    → shared typed semantic UI state / event adapter
    → MarburgumIIClient view model / notification router
    → UMG widgets, input focus and visual theme
```

- `MarburgumIIShared`: stable semantic contract definitions that must be understood on both sides, with **no dependency on UMG/Slate or either side-specific module**.
- `MarburgumII` core: replicated engine state types and appropriate network ownership.
- `MarburgumIIClient`: UMG Widget Blueprints / C++ UserWidgets, menu layers, presentation data/view models, theme, focus, input glyph mapping and screenshot/test hooks.
- `MarburgumIIServer`: authoritative actions and persistence; **never** imports UI widget assets or depends on `MarburgumIIClient`.

Use **event-driven model updates** from replicated state and semantic events; avoid per-frame text bindings, string assembly and frequent reallocation of large widget trees. Explicitly suppress double notifications when replicated state and an action acknowledgement refer to the same event. Design UI models so recorded deterministic *display fixtures* can be fed to widgets in offline tests, **without** providing an alternate client-side path for awarding real gameplay rewards.

Use UMG as the production baseline in UE 5.8.3. Investigate Common UI only when its benefits for unified navigation are demonstrated; do not require an extra framework or novel UI plugin before proving the first slice. Apply DPI scaling and explicit independent interface-scale controls. Use UMG invalidation / Slate Insights based on profiling, not speculative optimization.

**Official engine references:**
- [UE 5.8 UMG best practices](https://dev.epicgames.com/documentation/unreal-engine/umg-best-practices-in-unreal-engine?lang=en-US) (event-driven updates).
- [UE 5.8 DPI scaling](https://dev.epicgames.com/documentation/en-us/unreal-engine/dpi-scaling-in-unreal-engine).
- [UE 5.8 Slate/UMG invalidation](https://dev.epicgames.com/documentation/en-us/unreal-engine/invalidation-in-slate-and-umg-for-unreal-engine).
- [UE 5.8 release notes](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes) (review Common UI input changes before adopting).

## 6. Screenshot and public-showcase acceptance

The UI is **not accepted** because its data model is defined or a static image looks attractive. The first acceptance requires working UE **5.8.3** client UI over a running dedicated-server session, with a **real gameplay screenshot matrix** and a human-reviewable capture index recording engine, client/server build SHAs, state source and resolution.

**Minimum capture states:** title/connect, successful join, idle exploration, entering Lent/place transition, nearby interaction, mission tracker, reward/notification, Character, Skills, Inventory, Journal, Settings and disconnected/reconnecting/error. Only show states actually implemented; clearly label mock/staged content.

**Resolution and scale matrix:** 1280×720, 1920×1080, 2560×1440, 3440×1440 ultrawide and 3840×2160; test at 80%, 100%, 125%, 150%, 200% interface scaling once the setting exists. Include a controller focus path and long Slovene/English text. No clipping, overlapping, illegible numbers, UI blocking of world input, or stretched/blurred typography. Avoid mandatory runtime font sizes that are unreadable on 720p displays.

**Showcase target:** at least one genuine gameplay screenshot at 1440p or 4K and one short unedited gameplay capture showing the interactive HUD and a server-confirmed action. Do **not** advertise a vertical slice as implemented merely from mockups. Assets and fonts must have documented redistribution rights before publication. Distinguish a public-facing *development preview* from a packaged MMO launch.

**Test-quality requirements:** proper keyboard/mouse and controller focus/return, icon+text notifications, consistent color/contrast, selectable opaque/high-contrast mode, reduced-motion support, and no invented or unsafe data displayed by the UI. Record Slate performance only once there is a representative native scene.

## 7. Development ordering and boundaries

**Detailed phase gates:** UI-00 (semantic baseline), UI-01 (visual tokens and gallery), UI-02 (session/focus), UI-03 (live modular HUD), UI-04 (Character/Skills/Inventory/Journal/Settings), UI-05 (activity/vehicle/group modes), UI-06 (presets and optional layout editor), UI-07 (cross-cutting quality and captures) are defined and sequenced by [UI_HUD_ROADMAP.md](UI_HUD_ROADMAP.md). All stage gates distinguish design, running UI and accepted evidence. Build a premium default before letting players rearrange modules.


**UI-001 / Stage 0B parallel:** visual inventory of legacy surfaces → new UI visual tokens/graphical designs → UMG shell, login/connection presentation, basic exploration HUD and state/event interface. Use authoritative demo signal from NET-001 when it becomes available. Early static/UMG previews may be produced before the network test but must not be reported as online gameplay.

**Stage 1 showcase-ready milestone:** complete responsive graphical HUD and a coherent Character/Skills/Inventory/Journal shell; connect a server-confirmed interaction to prompt, notification and relevant screen; capture screenshot matrix and short runtime demo. Finish this early **before** extensive map population, long mission content or large-scale NPC simulation.

**Later expansion:** complex trading ledgers, housing management, full skill roster, photography collections, social operations, public chat, map/navigation breadth and customization require dedicated follow-up tasks. Do not reserve blank panels for every possible system on the default HUD.

**Exclusions are binding:** do not reintroduce the wanted-star/police-chase system, its UI, or the generated talent framework from original Marburgum.

See [NETWORK_ARCHITECTURE.md](NETWORK_ARCHITECTURE.md), [ROADMAP.md](ROADMAP.md), [LEGACY_PARITY.md](LEGACY_PARITY.md) and [DECISIONS.md](DECISIONS.md).
