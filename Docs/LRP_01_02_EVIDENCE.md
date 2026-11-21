# LRP-01/02 evidence — Linux, 21 November 2026

**Result:** the four attributes (Might, Endurance, Eye, Poise; Wind read only as a legacy alias), store schema 7 and
the eight equipment slots with server-decided equip and unequip ([ADR-085](DECISIONS.md)) are **implemented on branch
`feat/lrp-attributes-equipment`, awaiting owner playtest** (ADR-008). The server rules and store pass every automated
test. The end-to-end sheet smoke has **not yet passed as a whole**: its first phase passed in editor mode after the fix
below, and its second phase could not start a client because the host's NVIDIA driver changed under the run. Nothing
here is owner-approved, and this is not packaged qualification.

## Environment

| Item | Value |
| --- | --- |
| Branch | `feat/lrp-attributes-equipment` |
| Commits | `42216d2` (feature), `4f812c6` (equipment ids in catalogue case), plus this documentation commit |
| Engine | Unreal Engine **5.8.3** source build, engine-bundled clang |
| Host / GPU | CachyOS Linux 6.18.60 x86_64; NVIDIA GeForce RTX 5080, kernel module 620.70.06 |
| Package | Development Client and Server staged from `42216d2` (`Saved/StagedBuilds/BUILD_STAMP`, 2026-11-21T01:13:35Z); predates `4f812c6` |

## Automated tests

| Check | Result |
| --- | --- |
| `Automation RunTests Marburgum` (editor, NullRHI) at `4f812c6` | **55/55 pass**, exit 0 |
| `Automation RunTests Marburgum.Server` at `4f812c6` | **15/15 pass**, including the new catalogue-case case in `MarburgumEquipmentRulesTest` |
| `cargo test` in `tools/mb2` | **69/69 pass**, including `sheet_stores_pass_and_fail` |

A `Marburgum.Server.WorldStoreCondition` failure (`endurance is written`) in a run at 00:25 UTC came from a binary built
before that test was corrected; the rebuilt binary passes it.

## Sheet smoke (`scripts/sheet-smoke-linux.sh`, port 27785)

| Run | Phase 1 (legacy store, SHEET + PEER clients) | Phase 2 (restart, RESTORED client) |
| --- | --- | --- |
| Staged, `42216d2` (`Saved/SheetSmoke/20261121T013236Z`) | Clients PASS, server migration PASS; **store check FAIL**: `equipment_a={"left_hand":"Camera",…}`, server logged `slot=Head item=Camera` | Client PASS; store check FAIL (same ids) |
| Editor, `4f812c6` before the refusal fix (`…/20261121T013830Z`) | Clients PASS, server migration PASS, **store check PASS** (ids in catalogue case); decision list differed only in `-:-:unknown_slot` for the expected `-:camera:unknown_slot` | **Not run**: the client exited at start with `VK_ERROR_INITIALIZATION_FAILED, 0 devices` |

**The case bug.** Ids cross the wire as `FName`, which returns the spelling the engine registered first (`Camera`,
`Head`), and the catalogue lookups compare case-insensitively, so `DecideEquip` stored the requested spelling.
`4f812c6` compares, stores and logs only the catalogue's ids, and every refusal now reports the requested item (the
`unknown_slot` line above). The editor smoke has not been rerun since the refusal change; the unit tests cover it.

**The blocker.** `nvidia-utils` was upgraded from 620.70.06 to 620.73.04 at 01:38:51 UTC, during the editor run, while
the 620.70.06 kernel module stayed loaded. Since then no new process on the host can create a Vulkan device
(`vulkaninfo`: `vkEnumeratePhysicalDevices failed with ERROR_INITIALIZATION_FAILED`; NVML: driver/library version
mismatch). Every rendering smoke is blocked until the host is rebooted.

**Latent, not changed here:** `MarburgumRetailRules.cpp` also stores the requested item spelling after a
case-insensitive lookup. It is safe only while no shop item id collides with an engine-registered `FName`.

## Open

- After a reboot: rerun the sheet smoke in editor mode, repackage from the final commit and rerun it staged, and run the
  smokes whose store assertions moved to schema 7 (mission, shop and the others that check `schema=7`).
- Owner playtest of the Character sheet (C) and Inventory (I).
- Native-speaker review of the Slovene attribute and slot names.
