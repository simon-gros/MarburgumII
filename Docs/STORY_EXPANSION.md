# Story, NPC and progression expansion (ADR-081)

**Status:** Chapter IV and the people and Photography pass (ADR-081) are on `main`; **Chapter VI *The Negative* and the Frame 37 side story (ADR-084) are on branch `feat/story-chapter-vi`**. Both **await the owner's in-game playtest and approval** (ADR-008). Nothing here is accepted release content. Test results are in [§8](#8-tests-and-evidence) and [§10.7](#107-tests-and-evidence-chapter-vi); what is not done is in [§9](#9-not-done-limits-and-blockers) and [§10.8](#108-not-done-limits-and-provisional-parts-chapter-vi).

**Status (20 November evening):** Chapter IV *Voices on Tape*, outcome-dependent Maja branch, Nika/Brane side stories, server-judged Photography and animated/fishing NPC presentation are implemented. `9e11f2c` ran **44/44** editor automation plus staged story/eight original mission/shop/combat/Lent/interaction smoke PASS; later `9b88369` staged runs recorded English/Slovene content and NPC evening post behavior. These are scoped packaged tests, **not owner approval or a single all-feature latest-head release**. The specialisations in §7 remain a proposal and may not revive generated talent trees. [Current review](DEVELOPMENT_REVIEW_2026-11-20_EVENING.md).
**Sources:** original [`MAIN_STORYLINE.md`](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/MAIN_STORYLINE.md), [`NARRATIVE_BIBLE.md`](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/NARRATIVE_BIBLE.md), [`SIDE_STORY_BIBLE.md`](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/SIDE_STORY_BIBLE.md), [`SKILLING_ROADMAP.md`](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/SKILLING_ROADMAP.md) and `project/data/{missions,npcs,interactions,skills}.json`, compared with `Data/Gameplay/gameplay.v0.1.json`, the [parity register](LEGACY_PARITY.md) and the [roadmap](ROADMAP.md).

This pass adds one main-story chapter, its branch, two side missions, Photography as a working skill, recognisable animated people in place of every geometric mission contact, and a visible fishing activity for the angler and the player. It is deliberately small: each piece works end to end on the server, persists and is tested, rather than a wider set of half-wired content.

**Research follow-ups, not present features:** a player Photography album with permitted historical “then and now” references, a simple cassette/payphone interaction in the Chapter IV setting and a noncombat courier/repair task could extend the existing mission/skill loop. See the [proposal roadmap](GAMEPLAY_RESEARCH_ROADMAP_2026-11-20.md). Each requires rights, server-authoritative persistence, content localization and personal owner review.


## 1. Migration matrix

Legend: **Implemented** (in the catalogue and runtime, editor/packaged-tested where §8 says so), **Partial**, **Missing**, **Blocked** (needs an engine feature, a district or a decision first), **Excluded** (owner decision).

### 1.1 Main story: *The City in the Ledger*

| Chapter | Original id | Status in Marburgum II | Notes |
| --- | --- | --- | --- |
| Prologue — Something in the Water | `lent_first_catch` | Implemented (ADR-049) | Unchanged. |
| I — Names That No Longer Match | `raeda_names_no_longer_match` | Implemented (ADR-049) | Its `maja_informed` / `investigation_private` outcome is now **read** by Maja's folder (outcome gate). |
| II — The Last Shift | `raeda_last_shift` | Partial (ADR-052) | Sequential baseline. Hall Three access variants and the scrap-collector confrontation still need cross-mission outcome branching inside a mission (the new gate opens whole missions only). Melje stands on a temporary floor. |
| III — The City for Sale | `raeda_city_for_sale` | Implemented baseline (ADR-052) | Tabor/Pristan contacts on temporary floors. |
| **IV — Voices on Tape** | `raeda_voices_on_tape` | **Implemented, this pass** | Recorder in Tabor → Drago → Tone's taxi mixtape → Andrej's overwritten tape → Vera places the room → the Tone decision. Mid-decade (1993–96) variant. |
| IV (Maja branch) | `raeda_mentor_folder` | **Implemented, this pass** | Separate follow-up, offered only after `maja_informed` (Chapter I). |
| V — Busy Signal | `raeda_busy_signal` (proposed) | Blocked | Needs a Magdalena dispatch office, the floppy route that reads `milan_trusted`, and the fax-log race that reads Chapter VIII. |
| **VI — The Negative** | `raeda_the_negative` | **Implemented, ADR-084** (branch) | Mid-decade (1993–96) variant: the order book, Drago's print, the Block 13 contact sheet, Frame 37, the Glas strip, Albin by Kiosk 67 outcome (all five), the ticket. See [§10](#10-chapter-vi--the-negative-adr-083). The 1990–92 and 1997–99 variants are not built. |
| VII — Registered | `raeda_registered` (proposed) | Blocked | Inspector Ana Ribič and the Block 13 storage cage are not in the world; the "open it before a witness" rule needs a witness-presence check. |
| VIII — Static | `raeda_static` (proposed) | Blocked | Rok Kovačič, Radio Mesto and the workers' club basement are not in the world. |
| IX — The Empty Room | `raeda_empty_room` | Blocked | Needs "any three of IV–VIII" gating and the Tabor headquarters basement. |
| X — The False Page | `raeda_false_page` | Blocked | Needs a deduction/journal assembly; two of its categories now have evidence (the canteen tape: place; Brane's spacing error: typography). |
| XI — Who Owns Tomorrow? | `raeda_who_owns_tomorrow` | Blocked | Needs X, party-scoped Reputation and the forum hearing. |

### 1.2 Side stories (original `SIDE_STORY_BIBLE.md` §3)

| Story | Status | Notes |
| --- | --- | --- |
| 3.1 Kiosk 67 | Implemented (ADR-049) | Its outcome now chooses Albin's route in Chapter VI (ADR-084). |
| 3.2 Last Shift — Keys to Hall Three | Implemented (ADR-049) | |
| 3.3 Under the Paint | Implemented (ADR-049) | |
| **3.10 The Sign Painter — Old Letters** | **Implemented, this pass** (`brane_old_letters`) | Three photographs; the Prehod sign at the Melje yard carries the spacing error (Chapter X typography). |
| 3.5 The Blue Golf, 3.6 Night Bus, 3.9 Closing Time | Missing; NPCs exist | Franc Kos, Lara Mlinar, Milena Ferk and Tine Kolar are catalogued but **unplaced**: Tabor is not built and they have no temporary floor yet. |
| **3.23 Thirty-Six Exposures — Frame 37** | **Implemented, ADR-084** (`nika_frame_37`, branch) | The scrap tin, the loupe, a deduction, the Melje date board found and photographed. Shares the Frame 37 print and the date-board photograph with Chapter VI. |
| 3.21 Block 13 — The Spare Room | Partial (ADR-084) | Ivana Zupan and her storage cage are placed (Tabor temporary floor) for Chapter VI; the household story itself (the student, the telephone) is not built. |
| 3.25 Purple Saturday — The Scarf | Partial (ADR-084) | Boris, an ordinary terrace regular, vouches for you to Albin (`left_alone`); the scarf story, Ljudski vrt and the match-day event are not built. |
| 3.4, 3.7, 3.8, 3.11–3.20, 3.22, 3.24, 3.26–3.42 | Missing | Each needs a district, a person or a system not yet in the world (radio, telephones, computers, housing). |
| Late Delivery (missions roadmap) | Implemented (ADR-049) | |
| **Then and Now — Three Prints** | **New extension, this pass** (`nika_then_and_now`) | Newly authored introduction to Photography, built on Nika's role in the Bible. |

### 1.3 Characters

| Character | Original role | Marburgum II |
| --- | --- | --- |
| Vera Horvat, Petra Kralj, Maja Koren, Marija Sever, Matej (Hall Three), Milan Šolar, Irena Vogrin, Brane, Drago Potočnik, Nika Vidmar | Named contacts | Placed; **now animated mannequins** (this pass) |
| **Tone Vogrin** | Irena's husband; Drago's night driver | **Added and placed** beside Drago's workshop (canonical character, new position) |
| **Andrej Mlakar** | Computer technician | **Added and placed** on the Lent row (canonical character, new position) |
| **Jožica Rozman** | The canon's unnamed "Tabor household" | **New name**; on the Tabor temporary floor |
| **Ivana Zupan** | Stefan Zupan's widow, Block 13 | **Placed (ADR-084)** on the Tabor temporary floor beside the storage cage (provisional; canonical Block 13 is about 170 m south-west) |
| Sara Vidic, Tine Kolar, Franc Kos, Milena Ferk, Lara Mlinar | Tabor contacts | Catalogued, unplaced (Tabor not built) |
| **Ernest Pavlič**, **Boris** | The canon's unnamed collector; an ordinary terrace regular (3.25) | **New names (ADR-084)**, placed on Lent |
| Alenka Pahor | Hospital contact | Placed (dialogue only) |
| Albin Rojko | The Kiosk 67 customer (A.R.) | `kiosk_67_customer`; **named by Chapter VI** (the Glas strip), his testimony depends on the Kiosk 67 outcome (ADR-084) |
| Oskar Lorber, Inspector Ana Ribič, Rok Kovačič | Antagonist, police, radio | Missing (Chapters VII–XI) |
| Stefan Zupan | Died 1994 | Not a person in the world (canon) |

### 1.4 Skills and progression

| Skill (original roster) | Status | Activity |
| --- | --- | --- |
| Fishing | Implemented (ADR-047/048); **now visible** | Server cast/bite/strike/catch; the player and the angler hold a rod and play cast, wait and reel clips (this pass). |
| **Photography** | **Implemented, this pass** | Camera tool, nine photo subjects, server-judged frame, first-photograph XP once per subject, prints, mission photograph stages, a darkroom choice. |
| Trading | Implemented (ADR-060) | Kiosk 67 first purchase; mission choices. Brane's `sell_copies` adds a small Trading award. |
| Reputation | Implemented (reversible) | Mission choices, including Chapter IV and Brane. |
| Housing | Registered, no activity | Not shown as playable. |
| Cooking, Electronics, Music, Chemistry, Logistics, Athletics, Research, Mechanics, Medicine, Occult | Design roster only | Not registered: none has a working activity yet (Logistics is the cheapest next: delivery runs on the Late Delivery pattern). |
| Generated talents / Talent UI | **Excluded** (owner decision) | See [§7](#7-specialisation-proposal-not-implemented) for a proposal that respects it. |

## 2. Chapter IV — *Voices on Tape* (canonical)

Offered by Vera after Chapter III. R-90's expense lines list two C-60 cassettes for the minutes of 22 December 1989.

| Stage | Where | What happens | Evidence |
| --- | --- | --- | --- |
| `find_recorder` | Jožica Rozman, Tabor floor | The office recorder went to her family with the Raeda Servis furniture; its counter stopped at 0412; a Melje driver asked for the cassettes "for music". | `raeda_recorder_counter_note` |
| `ask_drago` | Drago Potočnik, West Lent | "Tone. He records everything onto everything." | — |
| `borrow_cassette` | Tone Vogrin at Drago's cab | A C-60 labelled "Mix — Poletje '91" over an older "Zbor 22.12." | `canteen_cassette_c60` |
| `listen_andrej` | Andrej Mlakar, Lent (cassette held) | Muffled voices, a chair scrape over the sentence that matters, a shunting locomotive, a radio jingle. "A tape tells you where a room was." | `canteen_tape_notes` |
| `place_the_room` | Vera | The council met in a carpeted boardroom; a locomotive puts the recording in the Melje yard canteen. | flag `canteen_tape_place_melje` |
| `tone_question` (choice) | Tone | **Ask where the tape came from** (`tone_questioned`, +10 Reputation; Tone goes home early, the first crack in Irena's household) or **keep his household out of it** (`tone_spared`, +15). | journal `blue_ledger_voices_on_tape`, 60 money |

Irena is not revealed early: the recorder trail points at "a driver from the yard", never at the office typist. The jingle is identified in Chapter VIII, as canon has it.

**Maja branch — *The Borrowed Folder*.** Only a character whose Chapter I ended `maja_informed` is offered it: collect the folder at the Mariborski Glas archive desk, show Maja the note "Prehod — ask Lorber re: Kainach" (spring 1990). A character who kept the investigation private is never offered it, and Maja does not explain a refusal. The decision taken in Chapter I closes or opens the door for good.

## 3. Side missions

**Then and Now — Three Prints** (`nika_then_and_now`, new extension). Nika lends a 35 mm camera and three of her father's prints. Photograph the Water Tower, the Judgement Tower and the Old Bridge (three Photograph stages), bring the photographs back, and decide: leave copies in the Foto Vidmar archive (+30 money, +15 Reputation) or develop a second set yourself in her darkroom (+40 Photography). The camera stays with you.

**The Sign Painter — Old Letters** (`brane_old_letters`, canonical 3.10, after Nika's mission). Photograph Brane's lettering on the Kvik department store and the Velika kavarna in the old town, then go out to the **Melje yard** and photograph the PREHOD sign. Brane sees the error at once: "The space after the R is too wide. August 1990, a rush job." Sets `prehod_spacing_error_documented` for Chapter X; settle by selling him copies (+45 money, +10 Trading) or giving them (+20 Reputation).

## 4. Photography (the skill activity)

- **Camera:** `camera` (tool), lent by Nika. **[P]** takes a photograph. The interaction prompt shows "[P] Photograph — *subject*" with what it shows, or "already documented" when you hold the print.
- **Subjects:** nine, in `photography.subjects` of the gameplay catalogue: Vodni stolp, Sodni stolp, Židovski stolp, the Synagogue, the Minorite church, Stari most (heritage-register positions from `lent_places.json`, ADR-063), Kvik and Velika kavarna lettering, and the Prehod sign at the Melje yard.
- **Server judgement:** the camera must be in custody; from the server's copy of the body's eye and view, the subject's aim point must be within range, inside its cone and in plain view (`UMarburgumPhotographySubsystem`). The client asks the same question only to draw the prompt.
- **Progress:** the first photograph of each subject pays its Photography XP once per character and is remembered in the world store (**schema 6**, `discoveries`). Every photograph is a print (`photo_*`, item kind `photo`); the same subject again while the print is held is refused with nothing changed; a lost print can be retaken without XP. A mission's current Photograph stage completes in the same commit.
- **Feedback:** a shutter flash, a PHOTOGRAPH notification with what the print shows, the XP toast, the print in the Inventory (Photographs) and the Skills screen.

## 5. People instead of shapes (NPC replacement)

**Audit.** Every authored NPC (`AMarburgumNpc`: the angler, all mission contacts, the Kiosk 67 counter's Petra) was a cylinder with a sphere head and a disc cap. Street people and the fight cast were already the tinted template mannequin. Place markers (crates, signs, the debris) are deliberately not people and stay as they are.

**Now.** `AMarburgumNpc` draws the template mannequin (`SKM_Manny_Simple`), tinted from the original `npc_visual_profiles.json` colours like the street people, breathing in the template idle at a per-person phase and with a small per-person height difference. The cylinder stays as the invisible collision stand-in, so nothing about reach or blocking changed.

- **Attention:** an idle person turns (up to 110°) toward the nearest player within 5 m. Every client sees the same pawns, so they agree on whom.
- **Conversation:** when the server admits an interaction with a person it multicasts it; every client sees that person turn to the speaker and talk with their hands (`A_NpcTalk`) for 4.5 s. The words appear as subtitles with the speaker's name (existing feed).
- **The riverside angler:** a period two-piece spinning rod (`SM_FishingRod`: cork grips, reel seat, glass blank, seven guides, fixed-spool reel; built from code, no imported asset) on a new `fishing_rod_r` socket of the mannequin skeleton. He runs a 32.4 s cycle: cast (2.4 s), wait with a line and a float on the Drava (18 s), reel in (6 s), stand (6 s), computed from the server's clock so every client sees the same moment. A client that walks up mid-cast joins the clip at the server's time. He keeps his eyes on the water when spoken to; his mission and dialogue are unchanged.
- **The player** shows the same rod and clips while their line is out: the server sets a public pose byte (`FishingPose`) whenever the owner's fishing status changes, so others see the cast, the wait and the bite. A body seen for the first time mid-wait starts at the wait, as `FISHING_PRESENTATION.md` asked.

**How the hands stay on the rod.** The clips are keyed in the original's convention (`MarburgumGodotClip`), but the left arm is not keyed by eye. `MarburgumNpcContent` brings the right elbow in until the left palm can rest on the reel handle, puts the socket where the right fist closes round the grip, then for every key searches the left arm so the left palm lands on the turning handle (reel) or the rod butt (cast). The fingers close round the grip on the axis the tool finds. The solver's per-key residuals are logged as evidence.

## 6. Engine features added

| Feature | Where | Why |
| --- | --- | --- |
| Outcome gates (`requires_outcomes`) | catalogue, `MarburgumMissionLogic`, `mb2` | Chapter I's decision opens Maja's folder; the same gate serves the Chapter VI Albin variants. Waits while the gated mission is unfinished; closed for good on another outcome. |
| Photograph objective | catalogue, mission logic and rules | A stage completed by photographing a subject, never by talking. |
| `DecidePhotograph`, `HandlePhotograph`, `[P]` | server rules, world authority, player controller | Server-validated photographs in one commit with any stage they complete. |
| World store schema 6 (`discoveries`) | `MarburgumWorldStore` | First-photograph XP paid once per subject per character; schema 1–5 documents load unchanged. |
| Slovene catalogue text at runtime | catalogue `sl` on every string, generated `SloveneText`, `Localized()` at every display site | The game's original missions, people and items in Slovene when the interface is Slovene, switching live from Settings. The Slovene for the pre-existing content is a drafted translation awaiting a native review; `mb2 validate gameplay` fails on any string without one. |
| Day posts | catalogue `day_posts`, `FDayPost`, `AMarburgumNpc` walks, `UpdatePosts` | Seven named people (Petra, Vera, Andrej, Brane, Drago, Tone, Nika) move to an evening post up to 10 m away by the Marburgum clock and stroll there and back with the template walk. The walk replicates once (from, to, start time) and every machine computes the same stroll. Each post is checked at server start for dry, level ground and a clear straight way, tried mirrored, otherwise dropped and logged. Someone spoken to on the way stops for the conversation. |
| Pinned-objective guidance | `MarburgumPinnedObjective` | "Vera Horvat · 45 m north-east", the photo subject's aim point for photographs, and "on a temporary floor until that district is built" for pad places. |
| NPC presentation contract | `MarburgumNpcContract.h` | Asset paths and the angler's cycle as a pure function of the server clock (unit-tested). |

## 7. Specialisation proposal (not implemented)

The original generated talent trees and Talent UI stay excluded. The original owner did approve three hand-designed relationships on 9 October 2026 (Research → Investigation, Photography → Nature Observation, Athletics with swimming, running and unarmed-fighting specialisations). Proposal for the owner's decision, not built:

- **Proficiencies, not trees:** a short, hand-written list per skill, each unlocked at a fixed level (25, 50, 100, 250…) and each changing one observable thing. Photography 25: *Steady hands*, a wider cone for photo subjects. Photography 100: *Dated frame*, a date board in a print is legible, which Chapter VI would use. Fishing 50: *Patient line*, a longer bite window.
- **Shown in the Skills screen** under the skill, locked or unlocked. No points to spend, no branching UI, nothing generated.
- **Server-owned:** derived from XP on the server, never chosen by the client.

## 8. Tests and evidence

All runs on this branch, Linux, UE 5.8.3, `/Game/World/lent`, dedicated server plus clients on ports 27731–27741 (never 7777 or 17777). **Editor** means `UnrealEditor -server` / `-game` processes from the branch's editor build; **staged** means the cooked Linux Client/Server packaged from the stamped commit. Neither is the owner's playtest.

| Check | Mode | Revision | Result |
| --- | --- | --- | --- |
| `mb2 validate all`, `cargo test` (61) | source | `9e11f2c` | Pass (gameplay contract: 54 items, 54 interactables, 12 missions, 30 refusal reasons; scripts and Python-free checks) |
| Full unity build (`-DisableAdaptiveUnity`) | editor build | pre-rebase | Pass (caught and fixed a `PaintTint` name clash adaptive unity hid) |
| `Automation RunTests Marburgum` | editor, null RHI | `9e11f2c` (as packaged) | **44/44 Success**, including `Server.Story.VoicesOnTape`, `.OutcomeGate`, `.Photography`, `Shared.Npc.AnglerCycle`, `Shared.Photography.Catalogue` and `Server.WorldStoreCondition` (schema 6) |
| `scripts/story-smoke-linux.sh editor` | editor | `9e11f2c` | **PASS** — `Saved/StorySmoke/20261120T074555Z`: Chapter IV to the Tone decision, Maja's folder through the outcome gate, two characters at Nika at the same moment, every photograph stage judged by the server, Brane, a side photograph and its refused repeat; server restart; `mb2 store-check story` = 180 money, 70 Reputation, 215 Photography XP, 7 documented subjects, 7 photograph commits. NPC presentation from every STEP: mannequin body, talkers `talk` turned 180° to the player, the angler in a fishing clip with the rod in hand and the line out |
| `scripts/mission-smoke-linux.sh editor` (regression) | editor | pre-rebase (`1bf4719` code) | Every client PASS (all eight original missions, FISH at the moved angler with the new rod/clip expectation, restart CHECK, store check `90 16 95`, fishing 3 casts/2 bites); the run was marked FAIL only by the script's stale `schema 5` log expectation, fixed in `9e11f2c` |
| Rendered NPC captures | editor | pre-rebase | `Saved/VisualSurvey/npc-views-1..3`: found the twisted fingers, the angler 18 m from the water and the 110° talkers; fixed; `npc-views-3` shows the angler on the bank with both fists round the grip and the cast |
| Packaged Linux Client/Server | package | `9e11f2c` | `BUILD SUCCESSFUL`; `build-stamp check`: clean tree, `build_fresh: yes`, Server `1cf34657…`, Client `d675b093…`, packaged 08:08 UTC |
| `story-smoke-linux.sh staged` | staged | `9e11f2c` | **PASS** — `Saved/StorySmoke/20261120T080849Z`: the same sequence and store check (180 / 70 / 215 / 7), NPC presentation lines as in the editor |
| `mission-smoke-linux.sh staged` | staged | `9e11f2c` | **PASS** — `Saved/MissionSmoke/20261120T081154Z`: all eight original missions, fishing at the bank (3 casts, 2 bites, too early / escaped / caught, rod and clip in hand), restart CHECK, store reloaded at schema 6 |
| `shop`, `combat`, `lent`, `interaction` smokes | staged | `9e11f2c` | **PASS** — `Saved/ShopSmoke/20261120T081657Z` (Kiosk 67 shared shelves, far and closed refusals), `CombatSmoke/20261120T081725Z`, `LentSmoke/20261120T081811Z` (movement-time bound), `InteractSmoke/20261120T081839Z` (shared stock contention) |

Captures: [the angler's grip](Captures/story-npc/angler-grip.jpg), [on the bank](Captures/story-npc/angler-bank.jpg), [reeling](Captures/story-npc/angler-reel.jpg), [a photograph and its notification](Captures/story-npc/photograph-water-tower.jpg), [Chapter IV guidance](Captures/story-npc/chapter-iv-guidance.jpg), [Nika's darkroom](Captures/story-npc/nika-darkroom.jpg) (editor renders).

**Second pass (`9b88369`: Slovene catalogue text, evening posts).** Package `build_fresh: yes` (Server `97c198a8…`, Client `67ca6a6a…`). `Automation RunTests Marburgum` 46/46 (adds `Shared.Npc.DayPosts`, `Shared.Gameplay.SloveneText` with the `sl` culture active). Staged **PASS**: `StorySmoke/20261120T150528Z` (English), `StorySmoke/20261120T150836Z` (the whole sequence with `-culture=sl`), `MissionSmoke/20261120T151142Z`, `ShopSmoke/20261120T151647Z`, `CombatSmoke/20261120T151714Z`, `LentSmoke/20261120T151801Z`, `InteractSmoke/20261120T151829Z`. Editor evening renders at 18:30 (`VisualSurvey/npc-evening-1`): Vera at her riverside post with her desk spot empty, Drago beside Tone at the cab rank, Nika facing the street; server log `npc_post` for the four people whose evening had begun and `npc_post_resolved` for all seven posts (Andrej's mirrored). Captures: [Slovene Chapter IV](Captures/story-npc/slovene-chapter-iv.jpg), [Slovene decision](Captures/story-npc/slovene-decision.jpg), [Vera's evening](Captures/story-npc/evening-vera-riverside.jpg), [the cab rank](Captures/story-npc/evening-cab-rank.jpg).

**Known blockers not touched:** the street population code and the interiors were not changed; the opposite-bank crowd matrix and the door smoke were **not re-run** in this pass.

**Not tested here:** the owner's playtest; a staged run of the other subsystems' smokes beyond those listed; controller/gamepad input for [P]; Slovene runtime text for catalogue content.

## 9. Not done, limits and blockers

- **Named residents keep a short day, not a city-wide routine.** Seven people walk to an evening post within 10 m and back; everyone is at their authored post in the working morning, so a contact is always near where the objective points, and the guidance line follows them. Routines across the city on the pedestrian walk graph (home, work, café) would need named people to become server-moved characters like the street people, with a reachability rule for mission contacts; not done.
- **Slovene catalogue text is a draft.** All 470 catalogue strings now show in Slovene with a Slovene interface; the translation of the pre-existing content was written in this pass and needs a native speaker's review. Interface strings added in this pass (photograph prompt, notifications, guidance) are in the localization gather but still need their `Game.po` entries.
- **No facial animation or camera framing** in conversations; the body turns and gestures.
- **Chapters V–XI** remain blocked as in §1.1.
- **Tabor and Melje are temporary floors**: Jožica Rozman and the Prehod sign stand on the ADR-052 pads, and the guidance line says so.

## 10. Chapter VI — *The Negative* (ADR-084)

**Status:** implemented on branch `feat/story-chapter-vi`, **awaiting the owner's playtest** (ADR-008). Default mid-decade (1993–96) variant only.
**Narrative authority:** original `MAIN_STORYLINE.md` Chapter VI, §3 (the pencilled initials, Stefan Zupan behind the camera), §7 (16 February 1990), and `SIDE_STORY_BIBLE.md` 3.1, 3.21, 3.23 and 3.25.

### 10.1 What the player does

Nika Vidmar offers the chapter once **Chapter III**, her own **Then and Now** (the camera) and **Kiosk 67** are done. Chapter V is not implemented and is never required or rewarded; Chapter IV is not required either (IV–VIII are parallel in the canon, and IX needs any three).

| # | Stage | Where | What the player does | What it yields (class) |
| --- | --- | --- | --- | --- |
| 1 | `date_the_print` | Nika Vidmar | Shows her the torn gate photograph from the cashbox (required) | Information: Foto Vidmar stamp, Fotokemika paper, February 1990; the negative went back to the customer |
| 2 | `read_order_book` | Foto Vidmar order book (counter beside Nika) | Inspects February 1990 | Order 0217, *"Vogrin I., 36 posn., 20.2.1990"* |
| 3 | `copy_order_book` | the same page, **[P]** | Photographs the page; the book stays in the lab | `photo_foto_vidmar_order_book` (**copy**) |
| 4 | `ask_drago` | Drago Potočnik | Asks who else kept prints | `drago_wallet_print_account` (**testimony**): frame 36, Stefan took it with Irena's camera |
| 5 | `ask_ivana` | Ivana Zupan (Tabor floor) | Asks about Stefan's photographs | The cage key and a condition: a receipt |
| 6 | `open_cage` | Block 13 storage cage | Searches Stefan's office boxes (the returned registered letter is left sealed, for Chapter VII) | `zupan_contact_sheet` (**original**): 37 frames on a 36 roll |
| 7 | `copy_contact_sheet` | the light table, **[P]**, only with the sheet in hand | Photographs it under even light | `photo_zupan_contact_sheet` (**copy**) |
| 8 | `frame_37_print` | Nika | Needs a readable print of Frame 37; if there is none, Nika offers the side story | Holds `frame_37_print` (**print**) |
| 9 | `chronology` | Vera Horvat | Brings the print, the date-board photograph and Chapter II's serial photographs | `chronology_frame_37` (**inference**): K 07 at Melje on 16.2.1990, two months after the file says the arrangement closed; flag `kainach_after_december` |
| 10 | `glas_full_frame` | Mariborski Glas archive desk | Asks for the photo desk's uncropped print | `torn_strip_print` (**copy**): a seventh man (Kainach's, unnamed) and Albin Rojko |
| 11 | Albin's route | see 10.2 | | `albin_testimony` (**testimony**) |
| 12 | `the_ticket` | Nika | Brings the punched ticket (Prologue), Albin's account, the sheet's copy and the original sheet, which goes into the lab safe in Ivana's name | `contact_sheet_custody_receipt` (**original**), 60 money, journal *Blue Ledger — The Negative*; flags `frame_37_chronology`, `albin_testified` |

### 10.2 Albin Rojko, by Kiosk 67 outcome

Conditional stages (`when`) read the recorded Kiosk 67 outcome; Kiosk 67 is a prerequisite, so the route is fixed when the chapter starts, and `mb2 validate` fails if any Kiosk 67 outcome has no stage.

| Kiosk 67 | Route | Distinct consequence |
| --- | --- | --- |
| `temporary_work` | Albin, on his break from the work at Hall Three, testifies at once | "You found me work when I was hiding. I will not hide from this." |
| `tab_paid`, `debt_erased` | Albin hesitates → Petra Kralj's word → Albin again | "The tab is settled… This is a different debt." Petra: "Tell him I said to stop being proud." |
| `object_sold` | Albin: the object was the machine-hall council stamp → the collector **Ernest Pavlič**: buy it back (−40, Trading +15) or photograph his stock for his price list (Photography +20) → Albin, given the stamp | The stamp goes back to Albin; the route never depends on money |
| `left_alone` | Albin refuses → **Boris**, a terrace regular of the match-day circle, vouches (a faded purple scarf) → Albin | "Boris does not vouch for anybody." |

The match-day gathering itself (Ljudski vrt, `PublicEventState`) is not built: Boris stands by the Kiosk 67 café. This is an explicit dependency, not a silent gap.

### 10.3 Thirty-Six Exposures — Frame 37 (`nika_frame_37`, side story 3.23)

Offered by Nika after *Then and Now*. Nika is clearing her father's scrap tin of fogged roll ends. Under the **loupe** at the light table, a 37th frame with *0217* scratched in its rebate; a **deduction** (the man in the foreground and the light leak are wrong answers, refused with a reply and nothing recorded; *the place, not the person* is right, Photography +10); Nika's print (`frame_37_print`); the player **searches for the place**: a yard with a date board on two posts, east of the centre, photographed from where the photographer stood (`melje_date_board_view` on the Melje floor, Photography +25 once); then the scrap goes into the lab safe (Reputation +15) or the player makes a second print (Photography +30).

**Either order works.** Played first (before Kiosk 67, say), the print and the photograph are information without attribution; Chapter VI's step 8 then completes at once and step 9 has its photograph. Played inside Chapter VI, Nika offers it at step 8 because she has no print to compare. The print, the photograph and the discovery's XP are granted once: unique items and the world store's discoveries ensure it, and the rules test plays both orders.

### 10.4 Evidence, chain of custody and the four kinds of knowing

Every evidence item carries a class shown in the Inventory and the Journal: **Original** (the thing itself; custody matters), **Print** (shows what was in front of the lens), **Copy** (records an original kept elsewhere), **Testimony**, **Information** and **Inference**. The chapter keeps the original storyline's distinctions: Nika's dating and the order book are information; Drago and Albin are testimony; Vera's chronology is explicitly an inference; the contact sheet is an original whose custody is recorded (Block 13 → you → the lab safe, with a receipt); your photographs of it and of the order book are copies. The Journal lists the chapter's **open questions** (who took the photograph, where the roll is, what Frame 37 shows, who is on the strip, whether Albin will speak, why the file says December) and moves each to **Established** with its answer as it is settled; *why December* stays open for Chapter X.

### 10.5 The world

| Place | Look (ADR-084 prop) | Where | Status |
| --- | --- | --- | --- |
| Foto Vidmar order book | Wooden counter, the order book open, earlier years stacked | Beside Nika's original spot on Glavni trg | Provisional: Foto Vidmar has no interior; the lab's counter stands outside |
| Foto Vidmar light table | Steel desk, opal glass glowing, loupe, lamp | Beside Nika | Provisional, as above |
| Block 13 storage cage | Wire-mesh cage, Stefan's boxes, winter tyres, jars, "13 · ZUPAN" | Tabor temporary floor (ADR-052) with Ivana Zupan | Provisional: Tabor and its block cellars are not built |
| Melje date board | Two steel posts, green board, "RAEDA · ODPREMA", empty date slot | Melje temporary floor, between the Kainach trucks and the log shelf | Provisional floor (ADR-052) |
| Kainach trucks | Two lorries from the project's own lorry mesh, rust-red, K 07 and K 11, the Austrian oval | Melje temporary floor | The trucks Chapter II already photographs now look like trucks |
| Glas archive desk | Steel desk, bound volumes, green-shaded lamp | Unchanged position on Lent | |
| Ernest Pavlič, Boris | Tinted template mannequins (ADR-081) | Lent, near the Glas desk and the Kiosk 67 café | New names |

All furniture is built at runtime from engine primitives and blocks the camera and the body; no asset was imported or generated (TRELLIS was not needed). The Melje and Tabor floors are grey pads in an empty sky and are not presented as built districts.

### 10.6 Engine features (narrow, tested on their own)

| Feature | Where | Test |
| --- | --- | --- |
| Conditional stages (`when`) | catalogue, `MarburgumMissionLogic` (`StageApplies`, `FirstStage`, `NextStage`, `PreviousStage`), rules, journal, notifications | `Shared.Mission.ConditionalStages`, `Negative.AlbinBranches` |
| Deductions (`correct: false`, refusal `WrongConclusion`) | catalogue, `DecideChoice`, choice panel (reward chips hidden), LOOK AGAIN notification | `Negative.AlbinBranches`, `Negative.Refusals`, the smoke |
| Choice grant items | `DecideChoice`, receipts | `Negative.AlbinBranches` (object_sold) |
| Unique items | `AddItem` | `Negative.FrameThirtySevenFirst` |
| Evidence class and description on items; mission questions | catalogue, Inventory, Journal | catalogue validator |
| Anchored photo subjects, `requires_item` | `UMarburgumPhotographySubsystem`, `DecidePhotograph`, prompt | `Negative.Refusals`, the smoke |
| Story props | `MarburgumStoryProps`, `AMarburgumInteractable` | rendered captures |
| `[P]` card at a prop whose document a step asks for | `MarburgumInteractionPrompt` | rendered captures |

The world store schema is unchanged (6): conditional stages, deductions and unique items need no new fields, and `Negative.Persistence` round-trips a mid-chapter world through `FMarburgumWorldStore`.

### 10.7 Tests and evidence (Chapter VI)

All runs on branch `feat/story-chapter-vi`, Linux, UE 5.8.3, `/Game/World/lent`, dedicated server plus clients on ports 27751–27760 (never 7777 or 17777). **Source** is repository validation; **editor** means `UnrealEditor -server` / `-game` from the branch's editor build; **staged** means the cooked Linux Client/Server packaged from the stamped commit. None of them is the owner's playtest.

| Check | Mode | Revision | Result |
| --- | --- | --- | --- |
| `mb2 validate all`; `cargo test` | source | `0cd1a2f` | **Pass**: gameplay contract 66 items, 60 interactables, 14 missions, 31 refusal reasons, catalogue current; every Kiosk 67 outcome has a stage; wrong answers change nothing; 66 Rust tests |
| `Automation RunTests Marburgum` | editor, null RHI | `0cd1a2f`; again on the merge with `main` `e9bc6a1` | **52/52 Success**; **53/53** after the merge (main added one), six new: `Server.Story.Negative.{Prerequisites, AlbinBranches, FrameThirtySevenFirst, Refusals, Persistence}`, `Shared.Mission.ConditionalStages` (log `Saved/Ch6Evidence/automation-3.log`) |
| `negative-smoke-linux.sh editor`, `object_sold` | editor | `72f6a9e` | **PASS**, `Saved/NegativeSmoke/20261120T160556Z-object_sold`: 51 authored people and places, 0 skipped, 12 photo starts; two characters started the chapter at the same moment; every step of the chapter and the side story, the light table refusing a sheet not yet in hand, a wrong deduction refused (LOOK AGAIN), the collector paid 40, the ticket, Nika with nothing more; server restart: the chapter refused as done, the date board refused as already photographed; `store-check negative` = 140 money, 65 Photography, 15 Trading, 3 documents; photograph commits 3; the other character still at `read_order_book` |
| `negative-smoke-linux.sh editor`, `left_alone`, `-culture=sl` | editor | `9d780f0` | **PASS**, `Saved/NegativeSmoke/20261120T161515Z-left_alone`: the whole sequence in Slovene; Albin refuses, a second ask changes nothing, Boris vouches, Albin testifies; `store-check negative` = 180 money, 65 Photography, 0 Trading, 3 documents; the scarf kept |
| `negative-smoke-linux.sh editor`, `tab_paid` | editor | `0cd1a2f` | **PASS**, `Saved/NegativeSmoke/20261120T163458Z-tab_paid`: Albin hesitates, asking again at once changes nothing, Petra's word, Albin testifies; `store-check negative` = 180 money, 65 Photography, 0 Trading, 3 documents |
| `negative-smoke-linux.sh editor`, `temporary_work` | editor | `0cd1a2f` | **PASS**, `Saved/NegativeSmoke/20261120T164310Z-temporary_work`: Albin testifies at once; `store-check negative` = 180 money, 65 Photography, 0 Trading, 3 documents |
| Prop survey (`visual-survey-linux.sh editor ch6-props`, 18 fixed cameras round the new furniture) | editor | `64ba57e` | 18/18 views, 0 skipped packages, no asset warnings (`Saved/VisualSurvey/ch6-props`) |
| Packaged Linux Development Client/Server | package | `64ba57e` | `BUILD SUCCESSFUL`, 2,332 packages cooked; `build-stamp check`: clean tree, `build_fresh: yes`, Server `74b95667…`, Client `24893134…`, packaged 16:52 UTC (an earlier attempt failed with exit 25 because an editor smoke in the same worktree held the cached asset registry; the cook was repeated with nothing else running in the worktree) |
| `negative-smoke-linux.sh staged`, `object_sold` | staged | `64ba57e` | **PASS**, `Saved/NegativeSmoke/20261120T165552Z-object_sold`: the whole sequence, restart, `store-check negative` = 140 / 65 / 15 / 3, photograph commits 3; stamp `build_fresh: yes` |
| `negative-smoke-linux.sh staged`, `left_alone`, `-culture=sl` | staged | `64ba57e` | **PASS** on the second run, `Saved/NegativeSmoke/20261120T170342Z-left_alone`: the whole sequence in Slovene, restart, `store-check negative` = 180 / 65 / 0 / 3; stamp `build_fresh: yes`. The first run hit an engine crash, below |
| `street-multiplayer-smoke-linux.sh staged` (day matrix) | staged | `64ba57e` (before the merge of `main`) | **PASS**, `Saved/StreetMulti/20261120T204125Z`: solo, A, B, C apart and back, **D opposite banks 28/28**, E and the three-player cap all settled; the Chapter VI furniture on Glavni trg did not disturb the crowd |
| `door-smoke-linux.sh staged` | staged | `64ba57e` (before the merge of `main`) | **FAIL** (known, not caused by this chapter), `Saved/DoorSmoke/20261120T204619Z`: Hotel Lent passes; in the lagged vine museum the rival's street door is not its use candidate (`rival_street_door_not_the_use_candidate`), so the walker never sees the reversal. The same failure appears in the other session's staged matrix on `main` `44106bc` (`Docs/INTEGRATED_MULTIPLAYER_2026-11-20.md` on `fix/grok-integrated-multiplayer-2026-11-20`), whose commit `849eb74` (door use follows the camera) addresses it; not merged here |
| Owner playtest | — | — | **Not done** |


### 10.8 Not done, limits and provisional parts (Chapter VI)

- **Owner approval: not given.** Nothing in this chapter is accepted until the owner has played it (§10.9).
- **Period variants.** Only the default mid-decade (1993–96) variant exists. 1990–92 (Stefan alive, with the negatives) and 1997–99 (Nika's scan, a copy, and the chain-of-custody question she puts to the player) need a calendar-dependent stage selection the world clock (mirrored to 1996) does not yet call for.
- **Provisional places.** Foto Vidmar has no interior: its counter and light table stand beside Nika on Glavni trg. Block 13 and its cellar are not built: Ivana Zupan and her storage cage stand on the Tabor temporary floor, about 170 m from the canon's Block 13. The Melje date board and the Kainach trucks stand on the Melje temporary floor. All three are reached by development starts (`?MB2Start=mission_<id>`, `photo_<subject>`), not by walking across the city; none is presented as a finished district.
- **Purple Saturday.** Albin's `left_alone` route is meant to find him among the match-day group at Ljudski vrt. Neither the stadium district nor a match-day event (`PublicEventState`) exists; Boris, by the Kiosk 67 café, vouches for you instead. Dependency recorded, route completable.
- **Albin's routine.** The original says Albin resumes his kiosk routine after Kiosk 67; he stands at his authored spot whatever the outcome. His Chapter VI dialogue is what changes. His greeting line and name tag are still the Kiosk 67 ones (*"Kiosk 67 missing customer"*, *"I'm all right…"*): a person has one static line in the catalogue, shown before each step's own text, so after his testimony the greeting reads stale.
- **Props are primitives.** Furniture is built from engine boxes and cylinders (and the project's lorry mesh), deliberately plain; no bespoke or generated models. The engine text font has no Latin Extended-A, so lettering on props avoids č, š and ž.
- **Controller.** Gameplay actions (`[E]`, `[P]`, the number keys of a decision) have **no gamepad bindings** in the project; only menus navigate with a controller. Chapter VI is therefore keyboard and mouse only, like Chapters I–IV. A gameplay controller layer (aiming the camera for `[P]` included) is a separate item on the roadmap.
- **Slovene.** All new catalogue text and the 21 new interface strings are drafted Slovene awaiting a native editorial review (terms checked against the existing translation: *fotografija*, *kontaktna kopija*, *dvorišče v Meljah*, *Modra knjiga*; new: *odtis* for a print, *izvirnik*, *pričevanje*, *sklep*, *garažna številka* for a fleet number).
- **Chapter V** is not implemented and its evidence is never awarded; Chapter VI does not require it.
- **Why Nika waits.** A character who has not finished Kiosk 67 (or Chapter III) is offered only the Frame 37 side story; once that is done Nika answers with the generic "not ready to ask more of you yet". The refusal does not name the missing mission (the receipt carries no prerequisite id); the Journal does not list unoffered missions either.
- **Light table glow.** The table's small warm light throws a visible pool on the paving in daylight (survey `vidmar_*`); cosmetic, left for an art pass.

### 10.9 Owner playtest: how to play Chapter VI

1. **Build.** Package the branch (`scripts/package-linux.sh`) or use the staged build whose `Saved/StagedBuilds/BUILD_STAMP` names the commit under test (`scripts/build-stamp.sh check` must say `build_fresh: yes`).
2. **A world with the prerequisites done**, one character per Kiosk 67 outcome you want to see (or play Prologue to Chapter III, *Then and Now* and Kiosk 67 yourself):
   ```bash
   scripts/seed-chapter-vi-store.sh Saved/Playtest/ch6/world-store.json \
       simon-ch6-work:temporary_work simon-ch6-tab:tab_paid simon-ch6-sold:object_sold simon-ch6-alone:left_alone
   ```
3. **Server** on a free port (never 7777 or 17777):
   ```bash
   Saved/StagedBuilds/LinuxServer/MarburgumIIServer.sh /Game/World/lent -port=27760 \
       -MB2WorldStore="$PWD/Saved/Playtest/ch6/world-store.json" -log
   ```
4. **Client** (windowed; add `-culture=sl` for Slovene):
   ```bash
   Saved/StagedBuilds/LinuxClient/MarburgumIIClient.sh "127.0.0.1:27760?MB2Start=mission_nika_vidmar?MB2Character=simon-ch6-sold" -windowed -ResX=1920 -ResY=1080
   ```
   The remote places are reached by reconnecting with another start: `mission_ivana_zupan` or `mission_block_13_storage_cage` (Tabor floor), `photo_melje_date_board_view` (Melje floor), `mission_vera_horvat`, `mission_mariborski_glas_archive`, `mission_kiosk_67_customer`, `mission_stamp_collector`, `mission_viole_terrace_regular`. Progress is saved by the server at every step, so a reconnect continues where you were.
5. **Play.** `[E]` talks or inspects, `[P]` photographs (the prompt names the subject in the middle of the frame), `[1]`–`[3]` answer a decision, `[J]` Journal, `[I]` Inventory. Follow the pinned objective. Things worth checking:
   - the order book's page and the contact sheet are photographed at the counter and the light table (the `[P]` card appears there); the light table refuses a photograph before the sheet is in hand;
   - at step 8 Nika offers *Thirty-Six Exposures*; at the light table pick a wrong conclusion first: LOOK AGAIN, nothing changes; then *the place, not the person*;
   - at the Melje floor, frame the date board between the trucks;
   - the Journal's OPEN QUESTIONS move to ESTABLISHED as you go, and *why December* stays open; the Inventory cards say original, print, copy, testimony, information or inference;
   - each character meets a different Albin: at once (`temporary_work`), after Petra (`tab_paid`), after the collector (`object_sold`: try buying with less than 40, then work it off), after Boris (`left_alone`);
   - at the end, Nika keeps the contact sheet and gives you a receipt; talking to her again changes nothing; restart the server and reconnect: everything is where you left it.
6. **Record** the commit, the stamp, what you played and your decision (ADR-008).

**Separate defect observed (not caused by this chapter, not fixed here).** In the first staged Slovene run, started while another staged run was rendering on the same GPU, one client crashed about two seconds after launch on the engine's RHI thread: `Assertion failed: AtomicFlags.GetNumRefs(...) == 0` in `FRHIResource::~FRHIResource()` from `FVulkanGraphicsPipelineState::~FVulkanGraphicsPipelineState()` (`RHIResources.cpp:37`), signal 11. Its step never reached the server, so every later step of that run was one stage behind and failed (`Saved/Ch6Evidence/negative-staged-left_alone-sl.run1-client-crash.log`, crash in `Saved/NegativeSmoke/20261120T165612Z-left_alone/client-n6-date.log`). The run was stopped and repeated on its own. The smoke harness retries a client only when Vulkan device creation fails, not after a crash like this one.
