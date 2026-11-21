# LRP-01/02 evidence — Linux, 21 November 2026

**Result:** the four attributes (Might, Endurance, Eye, Poise; Wind read only as a legacy alias), store schema 7 and
the eight equipment slots with server-decided equip and unequip ([ADR-085](DECISIONS.md)) are **implemented on branch
`feat/lrp-attributes-equipment`, awaiting owner playtest** (ADR-008). The server rules and store pass every automated
test, and the sheet, shop, mission and lent smokes **pass against a Development package staged from `0a0c15b`**, every
client rendering offscreen. Nothing here is owner-approved.

## Environment

| Item | Value |
| --- | --- |
| Branch | `feat/lrp-attributes-equipment` |
| Commits | `42216d2` (feature), `4f812c6` and `0a0c15b` (equipment ids in catalogue case, refusals included), `b0f7a5f` (merge of main `97cd12b`: every smoke client offscreen), plus documentation |
| Engine | Unreal Engine **5.8.3** source build, engine-bundled clang |
| Host / GPU | CachyOS Linux 6.18.60 x86_64; NVIDIA GeForce RTX 5080, driver 620.73.04 (after a reboot) |
| Package | Development Client and Server staged from `0a0c15b`, `dirty=0` (`Saved/StagedBuilds/BUILD_STAMP`) |

## Automated tests

| Check | Result |
| --- | --- |
| `Automation RunTests Marburgum` (editor, NullRHI) at `0a0c15b` | **55/55 pass**, exit 0, including the catalogue-case checks in `MarburgumEquipmentRulesTest` |
| `cargo test` in `tools/mb2` | **69/69 pass**, including `sheet_stores_pass_and_fail` |

A `Marburgum.Server.WorldStoreCondition` failure (`endurance is written`) in a run at 00:25 UTC came from a binary built
before that test was corrected; the rebuilt binary passes it.

## Sheet smoke (`scripts/sheet-smoke-linux.sh`, port 27785)

| Run | Phase 1 (legacy store, SHEET + PEER clients) | Phase 2 (restart, RESTORED client) |
| --- | --- | --- |
| Staged, `42216d2` (`Saved/SheetSmoke/20261121T013236Z`) | Clients PASS, server migration PASS; **store check FAIL**: `equipment_a={"left_hand":"Camera",…}`, server logged `slot=Head item=Camera` | Client PASS; store check FAIL (same ids) |
| Editor, `4f812c6` before the refusal fix (`…/20261121T013830Z`) | Clients PASS, server migration PASS, **store check PASS** (ids in catalogue case); decision list differed only in `-:-:unknown_slot` for the expected `-:camera:unknown_slot` | **Not run**: the client exited at start with `VK_ERROR_INITIALIZATION_FAILED, 0 devices` |
| Editor, `49cb474` after a reboot (`…/20261121T014739Z`) | **PASS** | **PASS** |
| Staged, `49cb474` (`…/20261121T015449Z`) | Clients and store PASS; decision list FAIL: a refusal logged `item=Camera` (packaged FName spelling) | PASS |
| **Staged, `0a0c15b`** (`…/20261121T020632Z`) | **PASS** | **PASS** |

**The case bug.** Ids cross the wire as `FName`, which returns the spelling the engine registered first (`Camera`,
`Head`), and the catalogue lookups compare case-insensitively, so `DecideEquip` stored the requested spelling.
`4f812c6` compares, stores and logs only the catalogue's ids; `0a0c15b` looks the item up before any refusal, so a
refusal also reports it in catalogue case (the packaged engine registers `Camera` capitalised where the editor does not).

**Driver mismatch (resolved).** `nvidia-utils` was upgraded to 620.73.04 at 01:38:51 UTC during an editor run while the
620.70.06 kernel module stayed loaded, so no new process could create a Vulkan device until the host was rebooted.

## Other staged smokes at `0a0c15b`

The smokes whose store assertions moved to schema 7, run one after another against the same package, each on its own
port (27786–27788), every client with `-RenderOffScreen` (SDL `dummy` video driver in the client logs):

| Smoke | Result |
| --- | --- |
| `shop-smoke-linux.sh staged` | **PASS** (`Saved/ShopSmoke/20261121T020725Z`) |
| `mission-smoke-linux.sh staged` | **PASS** (`Saved/MissionSmoke/20261121T020759Z`) |
| `lent-smoke-linux.sh staged` | **PASS** (`Saved/LentSmoke/20261121T021404Z`) |

The captures show the sheet and Inventory without overlap or clipping. The smoke's development character has no
model, so the identity card shows its placeholder; the 3D body on the sheet is not covered by these captures.

**Latent, not changed here:** `MarburgumRetailRules.cpp` also stores the requested item spelling after a
case-insensitive lookup. It is safe only while no shop item id collides with an engine-registered `FName`.

## Open

- A capture of the sheet with a named character's 3D body.
- Owner playtest of the Character sheet (C) and Inventory (I).
- Native-speaker review of the Slovene attribute and slot names.
