# Story, NPC and progression expansion (ADR-081)

**Status:** implemented on branch `feat/story-chapter-iv`; **awaiting the owner's in-game playtest and approval** (ADR-008). Nothing here is accepted release content. Test results are in [§8](#8-tests-and-evidence); what is not done is in [§9](#9-not-done-limits-and-blockers).
**Sources:** original [`MAIN_STORYLINE.md`](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/MAIN_STORYLINE.md), [`NARRATIVE_BIBLE.md`](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/NARRATIVE_BIBLE.md), [`SIDE_STORY_BIBLE.md`](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/SIDE_STORY_BIBLE.md), [`SKILLING_ROADMAP.md`](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/SKILLING_ROADMAP.md) and `project/data/{missions,npcs,interactions,skills}.json`, compared with `Data/Gameplay/gameplay.v0.1.json`, the [parity register](LEGACY_PARITY.md) and the [roadmap](ROADMAP.md).

This pass adds one main-story chapter, its branch, two side missions, Photography as a working skill, recognisable animated people in place of every geometric mission contact, and a visible fishing activity for the angler and the player. It is deliberately small: each piece works end to end on the server, persists and is tested, rather than a wider set of half-wired content.

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
| VI — The Negative | `raeda_the_negative` (proposed) | Missing (next) | Now feasible: Nika and Photography exist, Ivana Zupan is defined (Block 13 unplaced), and the Albin variants are outcome gates on `kiosk_67_customer_stopped_coming`. |
| VII — Registered | `raeda_registered` (proposed) | Blocked | Inspector Ana Ribič and the Block 13 storage cage are not in the world; the "open it before a witness" rule needs a witness-presence check. |
| VIII — Static | `raeda_static` (proposed) | Blocked | Rok Kovačič, Radio Mesto and the workers' club basement are not in the world. |
| IX — The Empty Room | `raeda_empty_room` | Blocked | Needs "any three of IV–VIII" gating and the Tabor headquarters basement. |
| X — The False Page | `raeda_false_page` | Blocked | Needs a deduction/journal assembly; two of its categories now have evidence (the canteen tape: place; Brane's spacing error: typography). |
| XI — Who Owns Tomorrow? | `raeda_who_owns_tomorrow` | Blocked | Needs X, party-scoped Reputation and the forum hearing. |

### 1.2 Side stories (original `SIDE_STORY_BIBLE.md` §3)

| Story | Status | Notes |
| --- | --- | --- |
| 3.1 Kiosk 67 | Implemented (ADR-049) | Its outcome is the gate the Chapter VI Albin variants need. |
| 3.2 Last Shift — Keys to Hall Three | Implemented (ADR-049) | |
| 3.3 Under the Paint | Implemented (ADR-049) | |
| **3.10 The Sign Painter — Old Letters** | **Implemented, this pass** (`brane_old_letters`) | Three photographs; the Prehod sign at the Melje yard carries the spacing error (Chapter X typography). |
| 3.5 The Blue Golf, 3.6 Night Bus, 3.9 Closing Time, 3.21 The Spare Room | Missing; NPCs exist | Franc Kos, Lara Mlinar, Milena Ferk, Ivana Zupan and Tine Kolar are catalogued but **unplaced**: Tabor is not built and they have no temporary floor yet. |
| 3.23 Thirty-Six Exposures — Frame 37 | Missing (next) | Natural follow-up to Nika's mission with the photograph objective. |
| 3.4, 3.7, 3.8, 3.11–3.20, 3.22, 3.24–3.42 | Missing | Each needs a district, a person or a system not yet in the world (radio, telephones, computers, housing). |
| Late Delivery (missions roadmap) | Implemented (ADR-049) | |
| **Then and Now — Three Prints** | **New extension, this pass** (`nika_then_and_now`) | Newly authored introduction to Photography, built on Nika's role in the Bible. |

### 1.3 Characters

| Character | Original role | Marburgum II |
| --- | --- | --- |
| Vera Horvat, Petra Kralj, Maja Koren, Marija Sever, Matej (Hall Three), Milan Šolar, Irena Vogrin, Brane, Drago Potočnik, Nika Vidmar | Named contacts | Placed; **now animated mannequins** (this pass) |
| **Tone Vogrin** | Irena's husband; Drago's night driver | **Added and placed** beside Drago's workshop (canonical character, new position) |
| **Andrej Mlakar** | Computer technician | **Added and placed** on the Lent row (canonical character, new position) |
| **Jožica Rozman** | The canon's unnamed "Tabor household" | **New name**; on the Tabor temporary floor |
| Sara Vidic, Ivana Zupan, Tine Kolar, Franc Kos, Milena Ferk, Lara Mlinar | Tabor contacts | Catalogued, unplaced (Tabor not built) |
| Alenka Pahor | Hospital contact | Placed (dialogue only) |
| Albin Rojko | The Kiosk 67 customer (A.R.) | Present as `kiosk_67_customer`, not yet named in the world |
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
| Pinned-objective guidance | `MarburgumPinnedObjective` | "Vera Horvat · 45 m north-east", the photo subject's aim point for photographs, and "on a temporary floor until that district is built" for pad places. |
| NPC presentation contract | `MarburgumNpcContract.h` | Asset paths and the angler's cycle as a pure function of the server clock (unit-tested). |

## 7. Specialisation proposal (not implemented)

The original generated talent trees and Talent UI stay excluded. The original owner did approve three hand-designed relationships on 9 October 2026 (Research → Investigation, Photography → Nature Observation, Athletics with swimming, running and unarmed-fighting specialisations). Proposal for the owner's decision, not built:

- **Proficiencies, not trees:** a short, hand-written list per skill, each unlocked at a fixed level (25, 50, 100, 250…) and each changing one observable thing. Photography 25: *Steady hands*, a wider cone for photo subjects. Photography 100: *Dated frame*, a date board in a print is legible, which Chapter VI would use. Fishing 50: *Patient line*, a longer bite window.
- **Shown in the Skills screen** under the skill, locked or unlocked. No points to spend, no branching UI, nothing generated.
- **Server-owned:** derived from XP on the server, never chosen by the client.

## 8. Tests and evidence

See the ADR-081 entry in [DECISIONS.md](DECISIONS.md) for the commands, revisions and results of this pass. Editor and packaged results are reported separately there.

## 9. Not done, limits and blockers

- **Named residents do not walk schedules yet.** People turn, talk and fish, but stay at their posts so a story-critical contact is always where the objective says. Walking routines on the pedestrian walk graph need named people to become server-moved characters (like the street people), with a reachability rule for mission contacts. That is the next NPC step, not done here.
- **Catalogue text is English in game.** The new content carries Slovene (`sl`) alongside English in the catalogue, but the runtime still shows catalogue text in English, as it already did for all mission text. New interface text (photograph prompt, notifications, guidance) is in the localization gather.
- **No facial animation or camera framing** in conversations; the body turns and gestures.
- **Chapters V–XI** remain blocked as in §1.1.
- **Tabor and Melje are temporary floors**: Jožica Rozman and the Prehod sign stand on the ADR-052 pads, and the guidance line says so.
