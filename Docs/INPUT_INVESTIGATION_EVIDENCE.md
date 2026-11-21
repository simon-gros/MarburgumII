# The Foto Vidmar investigation with keyboard and mouse or a gamepad — evidence, 21 November 2026

UI-02 input work for the investigation (ADR-086): left-stick movement, right-stick aiming, gamepad actions for
interaction, photography, Journal/Inventory, confirm and back; a clickable, focusable decision card; key hints that follow
the device in use. Two short parts of the existing story are the acceptance cases: photographing the order-book page in
Chapter VI (`raeda_the_negative`, `copy_order_book`) and the Frame 37 deduction at the light table (`nika_frame_37`,
`what_matters`). Per [ADR-008](DECISIONS.md) the status is `implemented_awaiting_owner_playtest`.

## The build

| | |
| --- | --- |
| Source | branch `feat/investigation-gamepad`, commit `f756780` (on `main` `97cd12b`), tree clean (`dirty=0`) |
| Engine | UE 5.8.3, Linux Development Client and dedicated Server, packaged 2026-11-21T02:02:23Z |
| Client | `sha256 e2356460d47ad74f13b6584dd643aaf06224b98b3e620cbfa0835cf6b285945e` |
| Server | `sha256 46f17bed733dc98677a492dd4934bd6010fe46707d14ca1be4d258b4daf632ee` |
| Map | `/Game/World/lent`, game clock Tuesday 16 June 1996, 10:00 |
| GPU | RTX 5080, NVIDIA 620.73.04, Vulkan SM6, every client `-RenderOffScreen` |

## Input method

No physical gamepad was used. `UMarburgumInputSmokeSubsystem` (client, `-MB2InputSmoke`) builds Slate events
(`FPointerEvent`, `FKeyEvent`, `FAnalogInputEvent` with the platform input device mapper's gamepad id) and hands them to
`FSlateApplication::ProcessMouseMoveEvent / ProcessMouseButtonDownEvent / ProcessKeyDownEvent / ProcessAnalogInputEvent`, the
same entry points the SDL layer feeds, so focus, routing, the viewport's `Gamepad_RightY` negation and the input mappings
all apply. Sticks send one sample per axis per tick and an explicit 0 on release, as SDL does. Aiming at the page is done
by eye: the driver reads the camera and turns the right stick until the server-shared photo cone holds the subject.

The story before each scene is played with the existing RPC drivers (`-nullrhi`, `scripts/negative-smoke-linux.sh`'s
steps); the store the server commits is read between the light-table clients.

## Results (packaged, `scripts/input-smoke-linux.sh staged`)

| Culture | Resolution | Result | Evidence |
| --- | --- | --- | --- |
| English | 1920×1080 | PASS | `Saved/InputSmoke/20261121T020224Z-en-1920x1080` |
| Slovene | 1920×1080 | PASS | `Saved/InputSmoke/20261121T020438Z-sl-1920x1080` |
| English | 1280×720 | PASS | `Saved/InputSmoke/20261121T020653Z-en-1280x720` |
| Slovene | 1280×720 | PASS | `Saved/InputSmoke/20261121T020909Z-sl-1280x720` |

Each run, from server state (English 1920×1080 shown; the others match):

- **Counter.** Left stick walks 57 cm out, stops on release (9 cm back); the mouse turns the view; stick up looks up; the
  right stick frames the page (in frame, 1° off axis, 2.0 m); the prompt reads `RT` on the pad and `P` after a mouse move;
  RT takes the photograph: `applied / stage_completed`, Chapter VI `copy_order_book → ask_drago`, Photography XP 0 → 10,
  the print `photo_foto_vidmar_order_book` in custody. View and Y open the Journal and the Inventory, B closes them.
- **Light table, deduction due:** `frame_37=what_matters`, Photography XP 30, money 120, 25 items, store revision 11.
- **Cancel** (card opened with A and A held: nothing sent; D-pad and a pointer move switch the hints; B, then Escape):
  store unchanged (revision 11), world input back, the stick walks.
- **Mouse, wrong answer** (click on "The man in the foreground", then a double click): one request,
  `rejected / wrong_conclusion`, refusal text shown, store unchanged.
- **Number key, wrong answer** (`2` held): one request, `rejected / wrong_conclusion`, store unchanged.
- **Gamepad, right answer** (D-pad to row 3, A): `applied / stage_completed`, `what_matters → nika_prints` once,
  Photography XP 30 → 40 (exactly +10), store revision 12.
- **Server log:** 3 answers received (`the_man`, `the_light_leak`, `the_place`; the cancels and held keys sent none);
  2 photographs committed (the page by RT, the contact sheet by the RPC driver).

## Regressions (same package)

The staged Chapter VI smokes (`scripts/negative-smoke-linux.sh staged`), one rendering client at a time after the input runs:

| Run | Route | Result | Evidence |
| --- | --- | --- | --- |
| English | Kiosk 67 `object_sold` | PASS, 30 checks, 3 photographs committed | `Saved/NegativeSmoke/20261121T021121Z-object_sold` |
| Slovene (`-culture=sl`) | Kiosk 67 `left_alone` | PASS, 31 checks, 3 photographs committed | `Saved/NegativeSmoke/20261121T021625Z-left_alone` |

## Captures

[Captures/input-investigation](Captures/input-investigation): the counter prompt on keyboard (`P`) and pad (`RT`), the
photograph taken, the card on the pad and after switching to keyboard and mouse, mouse hover on a wrong answer, the refusal
(mouse and number key), pad focus on the right answer and the result; the card and the refusal in both languages at
1280×720 and 1920×1080. All answer rows draw the same states; only focus or hover frames a row.

## Found and fixed on the way

- `FSceneViewport` hands `Gamepad_RightY` to the game negated: the look code negates it again so stick up looks up.
- The test client's stick release had to send an explicit 0 once per axis, as SDL does, or the axis kept its last value.
- The print a photograph hands over is the subject's catalogue `Item` (`photo_foto_vidmar_order_book`), not `photo_<subject>`.
- A fixed 0.2 s window for the after-close walk failed once under a frame hitch: it now pushes until 8 cm or 1 s.
- The smoke first read the world store with Python, which the repository's tooling check forbids. It now uses
  `mb2 store-check snapshot` (`554d153`, tooling and script only, no Unreal source), which prints the same line; the
  packaged English 1920×1080 run was repeated with it on the `f756780` package: PASS (`Saved/InputSmoke/20261121T022515Z-en-1920x1080`).
- First packaged run (before a host reboot for the NVIDIA 620.70 → 620.73 update): every rendered client failed to create
  a Vulkan device; not a game fault, rerun after the reboot.

## Known gaps

- No physical gamepad and no target-platform screen reader were used; caps are text ("A", "RT"), not platform glyphs.
- Driving, combat, chat and the counter UI have no gamepad bindings yet (Settings says so); keys cannot be rebound.
- The Slovene district banner still shows the weather as "Clear" (existing string, not part of this work).
- The pad's wider "View"/"Pogled" cap sits tight against the pinned objective's header (no overlap).

## Merged to `main`

Merged on the owner's instruction (21 November 2026) together with `main`'s LRP-01/02 character sheet (ADR-085), which
took the ADR number this work first used; this work is ADR-086. The merged source builds with `-DisableAdaptiveUnity`,
`mb2 validate all` and the tooling tests pass, localization was regathered (788 entries, none without Slovene), and the
input smoke passes on the merged source in the editor (`Saved/InputSmoke/20261121T025149Z-en-1920x1080`). The packaged
results above are for `f756780`; the merged commit has not been packaged.

## Owner walkthrough (this exact build)

On the playtest host, from `/mnt/data/marburgum-story` with the `f756780` package staged:
`bash Saved/InputEvidence/simon-playtest.sh` (or `… sl` for Slovene) seeds a fresh store for `simon` with Kiosk 67 as
`object_sold`, starts the server on 17777 at 10:00 and opens a 1600×900 window at Nika's; `… stop` ends both.

1. With a pad: left stick to Nika, A to take Chapter VI, A again to show the torn photograph, A at the order book.
2. Right stick until the photograph prompt reads `RT` over the page; move the mouse and see it read `P`; RT takes it.
3. View and Y open the Journal and the Inventory (the print is there), B closes them.
4. Drago (A), Ivana at Block 13 in Tabor (A), the cage (A), then the contact sheet on the light table (RT).
5. Nika offers Frame 37 (A); A at the light table for the loupe; A again opens the card.
6. B closes it, A reopens; Escape closes it; click "The man in the foreground" and read the refusal; press 2 for the
   light leak; D-pad to the third answer and A. Only the last one moves Frame 37 on, with +10 Photography.

Owner playtest: **not yet**.
