# UI/HUD overhaul evidence — Linux, 20 November 2026

**Result:** a design system v2 and a first pass of every player-facing surface are implemented in `MarburgumIIClient`
on `feat/ui-hud-major-overhaul` and run in a real client against an authoritative dedicated server on
`/Game/World/lent`. All of it is **implemented, awaiting owner playtest** (ADR-008): nothing here is owner-approved.
This is editor-build evidence (`UnrealEditor -game` clients and `-server`), **not** packaged qualification.

## Environment

| Item | Value |
| --- | --- |
| Branch | `feat/ui-hud-major-overhaul`, from `origin/main` `9090d35` (ADR-074). The Grok pedestrian-AI branch was not merged or touched. |
| Head verified | `2444aaf` (code), plus this documentation commit |
| Merged | Into `main` on 20 November 2026 with a merge commit (no rebase, so the commit IDs here stay valid), after `main` had moved 29 commits on (ADR-076 interiors, the bridge and crowd tests, the Grok pedestrian fixes, docs). The evidence above is from the branch before that merge. |
| Engine | Unreal Engine **5.8.3** source build, engine-bundled clang |
| Host / GPU | CachyOS Linux 6.18.60 x86_64; NVIDIA GeForce RTX 5080, driver 620.70.06 |
| Rendering | `-RenderOffScreen -windowed -ForceRes`, `FScreenshotRequest` with UI: the composited window, UMG included |
| Server | Editor `-server /Game/World/lent` with a throwaway world store; daytime clock held for captures (`MB2_CAPTURE_CLOCK`) |

## What changed

77 files, +16,369 / −1,131 lines; source changes are confined to `MarburgumIIClient` (no change to `MarburgumII`,
`MarburgumIIShared`, `MarburgumIIServer`, the account service or any RPC). Commits, oldest first:

| Commit | Change |
| --- | --- |
| `8c97a72` | test: a live tour (`MB2UITour`) and the resolution/scale matrix in `ui-capture-linux.sh`, at a fixed daytime clock |
| `e7e1453` | Design system v2: OFL fonts (Fira Sans, Fira Sans Condensed, Noto Serif), contrast-checked tokens and a high-contrast set, 46 vector glyphs, meters, a project DPI curve, client-local settings |
| `aeb1b4a` | Exploration HUD: title-card location banner, key-cap prompt, objective tracker, toasts and subtitles, vitals, vehicle readout |
| `17fb01e` | Soft-edged HUD shades, wrapping banner source line, quieter vitals |
| `47b5d3b` | One management shell for Character, Skills, Inventory, Journal and a working Settings screen; compact chat; redesigned Escape menu |
| `c0e646e` | **QA U9:** one split account/creator layout that cannot overlap; outbox notes only under `-MB2UIDiagnostics` (U14) |
| `495e56e` | Shades from one bilinear alpha mask per shape (no stripes); fully faded widgets are hidden |
| `75ac27a` | Decision and counter cards: numbered key caps, consequence chips, locked options, localisable consequence text |
| `adf66bb` | A vector survey plate of the Drava at Lent behind the title and account screens |
| `e7c1ebd` | **QA U11:** English/Slovene interface language — localisation target, Language setting, culture-invariant key caps |
| `db7530d` | The Slovene translation (657 strings) and compiled `.locres` for both cultures |
| `913f083` | Dates in the interface language, banner clock formatted once a minute, Slovene ICU data staged |
| `2444aaf` | Tests for settings, Slovene and **QA U13** contrast; UI_HUD_DESIGN §3A |

The implemented design system and surfaces are described in [UI_HUD_DESIGN §3A](UI_HUD_DESIGN.md#3a-implemented-design-system-v2-featui-hud-major-overhaul-2026-11-20).

### Settings (O, or Escape → Settings)

Client-local, saved in `GameUserSettings.ini` on every change, never sent to a server or account:
interface scale (80–200 %), text size (100/112/125 %), high contrast, reduce motion, HUD opacity, condition display
(when it matters / always), notification time, chat timestamps, **language** (English / Slovenščina, switched live),
master volume, VSync and frame-rate cap. Resolution and DPI scale are shown read-only (no runtime resolution
switching). The Controls section lists the real bindings. Undo restores the values from when the screen opened;
Reset restores defaults. Each change rebuilds the interface in place.

### Input and focus

While a management screen, the Escape menu or a modal is open, input is UI-only, the HUD is hidden and the screen owns
Escape; closing restores game input. The chat composer keeps the QA guard from `4110d63` (typing neither moves, runs
nor brakes; Escape or a click on the world leaves chat); this pass did not add an automated check for it. Escape closes the topmost layer, otherwise opens the menu; Leave and Quit ask for confirmation. The
world is never paused.

## Tests at `2444aaf`

| Check | Result | Evidence |
| --- | --- | --- |
| Editor build (`scripts/build-linux.sh MarburgumIIEditor`) | **Pass** | `/tmp/mb2ui-qa/build.log` (local) |
| Unreal automation `Marburgum.*` | **37/37 pass** (Shared 19, Server 6, Client 5, Gameplay 2, Chat, Editor, Net, Render, Vehicles) | `/tmp/mb2ui-qa/automation-all.log` |
| New: `Marburgum.Client.UISettings` | Pass: clamps, defaults, save/load round trip | |
| New: `Marburgum.Client.UIContrast` | Pass: every text colour on every opaque surface, standard and high contrast, measures 4.9–16 : 1 (≥ 4.5 : 1) | |
| New: `Marburgum.Client.Localization` | Pass: compiled `sl/Game.locres` resolves the interface strings, Slovene calendar names | |
| Existing: `Marburgum.Client.UICaps`, `LocationBanner` | Pass | |

## Live runs (editor client ↔ dedicated server)

| Run | Result | Folder (`Saved/` of the worktree) |
| --- | --- | --- |
| Account flow at 1920×1080: register, wrong code refused, credentials, creator with all three bodies, refused name, enter the world, returning sign-in, forged / replayed / duplicate / missing ticket refused | **PASS** | `AccountSmoke/20261120T014909Z` |
| **U9:** the same flow at **1280×1024 (5:4) and 150 %** — card and 3D stage side by side, no overlap | **PASS** | `AccountSmoke/20261120T015223Z` |
| Live tour in English: arrival, explore, interaction with the angler, Character, Skills, Inventory, Journal, Settings, chat, Escape menu | **PASS** | `UICapture/20261120T014409Z` |
| Live tour and title in **Slovene** (`-culture=sl`) — č, š, ž render, nothing clips | **PASS** | `UICapture/20261120T015521Z-sl` |
| Title at 1280×720, 1920×1080, 2560×1440, 3840×2160; connection to a dead port → timeout → Retry | **PASS** (at `2444aaf`) | `UICapture/20261120T020251Z` |
| Chat smoke (three clients, general + private, isolation, refusals, burst) | **PASS**, 5 messages accepted, no private bodies in logs (at `913f083`) | `ChatSmoke/20261120T015831Z` |

## Before and after

Real rendered output, not mock-ups. *Before* is `origin/main` `9090d35` (the tour driver only, captured in this worktree
or the `marburgum-ui-before` worktree); *after* is the branch. All at 1920×1080 unless named; JPEG quality 84 of the
original PNGs.

| # | Surface | Before | After |
| --- | --- | --- | --- |
| 1 | Title / connect | [before](Captures/ui-overhaul/before/01-title-1920x1080.jpg) | [after](Captures/ui-overhaul/after/01-title-1920x1080.jpg) · [720p](Captures/ui-overhaul/after/01-title-1280x720.jpg) · [4K](Captures/ui-overhaul/after/01-title-3840x2160.jpg) · [Slovene](Captures/ui-overhaul/after/01-title-sl-1920x1080.jpg) |
| 2 | Registration / sign-in | [credentials](Captures/ui-overhaul/before/02-registration-credentials-1920x1080.jpg) | [credentials](Captures/ui-overhaul/after/02-registration-credentials-1920x1080.jpg) · [sign-in 5:4 150 %](Captures/ui-overhaul/after/02-signin-1280x1024-150.jpg) |
| 3 | Character creator | [before](Captures/ui-overhaul/before/03-creator-1920x1080.jpg) | [after](Captures/ui-overhaul/after/03-creator-1920x1080.jpg) · [**5:4 150 % (U9)**](Captures/ui-overhaul/after/03-creator-1280x1024-150.jpg) |
| 4 | Exploration HUD | [before](Captures/ui-overhaul/before/04-explore-arrival-1920x1080.jpg) · [ultrawide](Captures/ui-overhaul/before/04-explore-3440x1440.jpg) | [after](Captures/ui-overhaul/after/04-explore-arrival-1920x1080.jpg) |
| 5 | Interaction | [before](Captures/ui-overhaul/before/05-interaction-1920x1080.jpg) | [after](Captures/ui-overhaul/after/05-interaction-1920x1080.jpg) |
| 6 | Mission / notification | — | [after](Captures/ui-overhaul/after/06-mission-notification-1920x1080.jpg) |
| 7 | Character | [before](Captures/ui-overhaul/before/07-character-1920x1080.jpg) · [5:4 150 %](Captures/ui-overhaul/before/07-character-1280x1024-150.jpg) | [after](Captures/ui-overhaul/after/07-character-1920x1080.jpg) |
| 8 | Skills | [before](Captures/ui-overhaul/before/08-skills-1920x1080.jpg) | [after](Captures/ui-overhaul/after/08-skills-1920x1080.jpg) |
| 9 | Inventory | [before](Captures/ui-overhaul/before/09-inventory-1920x1080.jpg) | [after](Captures/ui-overhaul/after/09-inventory-1920x1080.jpg) |
| 10 | Journal | [before](Captures/ui-overhaul/before/10-journal-1920x1080.jpg) | [after](Captures/ui-overhaul/after/10-journal-1920x1080.jpg) |
| 11 | Chat | [before](Captures/ui-overhaul/before/11-chat-1920x1080.jpg) | [after](Captures/ui-overhaul/after/11-chat-1920x1080.jpg) |
| 12 | Settings | none existed | [after](Captures/ui-overhaul/after/12-settings-1920x1080.jpg) · [Slovene](Captures/ui-overhaul/after/12-settings-sl-1920x1080.jpg) |
| 13 | Escape menu | [before](Captures/ui-overhaul/before/13-escape-menu-1920x1080.jpg) | [after](Captures/ui-overhaul/after/13-escape-menu-1920x1080.jpg) · [Slovene](Captures/ui-overhaul/after/13-escape-menu-sl-1920x1080.jpg) |
| 14 | Connection failure | [before](Captures/ui-overhaul/before/14-connection-timeout-1920x1080.jpg) | [after](Captures/ui-overhaul/after/14-connection-timeout-1920x1080.jpg) |

The component gallery (`scripts/ui-capture-linux.sh editor gallery`) remains the only SAMPLE DATA surface and carries
its badge; none of the captures above use it.

## QA findings

| ID | Status on this branch |
| --- | --- |
| U9 | **Fixed** (`c0e646e`): widths divided at run time; verified at 1280×1024 / 150 % and 1920×1080 |
| U11 | **Fixed for interface text** (`e7c1ebd`, `db7530d`, `913f083`): 657 strings in a localisation target, Slovene compiled and captured live. Gameplay data (NPC, mission, item, weather names) is still English. |
| U13 | **Fixed and tested** (`2444aaf`): every text colour ≥ 4.5 : 1 on opaque surfaces, `Marburgum.Client.UIContrast` |
| U14 | **Fixed**: the account outbox note only under `-MB2UIDiagnostics`; the banner clock is formatted when its minute changes (checked once a second, not every frame); the condition chip rebuilds its text only when the state or the recovery second changes |
| UX1 | **Partly closed**: new captures of the Escape menu, the scrolling screens, and 150 % at 5:4 in the account flow. The in-world 150 % / 200 % matrix was not re-run (below). |
| UX2 | Open: the chat smoke passes, but there is still no automated Escape/click-away check |

## Performance

No frame-time measurement was taken in this pass. The structural changes are the U14 items above, glyphs and shades
drawn by Slate from one shared 64 × 64 mask texture (no per-widget textures), and fully faded widgets collapsed rather
than drawn at zero alpha. A measured HUD/Slate cost (`stat slate`, `stat unit` at 1080p and 4K, before and after) is
still owed.

## Not proven by this evidence

- **Owner playtest:** none. No surface is accepted.
- **The full resolution/scale matrix after the change** (720p–4K, 3440×1440, 80–200 % in the live world) and the
  regression smokes other than chat and account (mission, shop, interaction, condition, vehicle, combat) were **not
  re-run on this branch**: the owner asked to skip the hour-long QA pass. The mission smoke was stopped part-way.
  The *before* matrix exists (`marburgum-ui-before/Saved/UICapture/20261120T002605Z`, 56 shots); the *after* matrix
  is `MB2_CAPTURE_MATRIX` / `ui-capture-linux.sh editor matrix`.
- **Packaged builds:** no Development Client/Server package was cooked from this branch, so the fonts, `.locres`
  files and ICU data are not yet verified in a cooked build.
- **Controller:** focus rings and directional navigation exist in the shell and Settings, but no gamepad was used.
  The decision and counter cards take number keys and mouse only.
- **Not implemented:** key rebinding, separate music/effects volumes, resolution switching from Settings, translated
  gameplay content.
