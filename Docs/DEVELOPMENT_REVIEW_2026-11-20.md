# Development review — overnight 19–20 November 2026

**Repository:** [MarburgumII](https://github.com/simon-gros/MarburgumII) · **Source reference:** `bed74712591a108e19479194a0b188cdc472d95e` (20 November 2026, 01:36:51 UTC) · **Previous documentation baseline:** `417bcf3b4bd0b902e1d958cb79a1ce1ceb0a7531` (ADR-071, 19 November 2026, 21:05 UTC) · **Scope:** 51 commits / 158 changed paths in this comparison. Later README and documentation commits only clarify the evidence.

**Review method:** GitHub history/changed files, implementation and decision notes, current canonical documentation, [QA audit](QA_AUDIT_2026-11-19.md), [visual audit](VISUAL_QUALITY_BACKLOG.md) and recorded [AI test evidence](AI_GAMEPLAY.md). This is a **source-and-documentation review**, not an additional Unreal build or playtest.

**Subsequent evidence update:** This overnight 01:36 UTC review predates ADR-079–083 and is retained as historical. The [evening 24-hour review](DEVELOPMENT_REVIEW_2026-11-20_EVENING.md) documents corrected **editor** opposite-bank density (28/28), later clean staged doors, story/Photography/localization, server-bound tickets and packaged AES-256-GCM, measured GFX-00 baseline, and restored source/archive CI. No newly packaged ADR-079 crowd matrix or personal owner approval is claimed.

## Overview

The strongest changes are a broad quality-assurance correction series, the ADR-072 nine-window world inspection, and ADR-073–075 changes to server-owned street populations and bridge walk-graph loading. Evidence now includes an editor bridge traversal and genuinely packaged Linux multi-client crowds. **The latest staged daytime population matrix is not fully passing**, so this milestone is not owner-approved.

| Workstream | What changed | Verified scope | Open requirement |
| --- | --- | --- | --- |
| QA, authority, persistence | Safer RPCs, verified HTTPS, bounded account/session work, persistent world-store writes, packaged executable/asset stamping, missing cook assets fixed | QA report: **34/34** automation on QA source, Rust suites and **13/14** staged smokes on its earlier package; rebased editor smokes also pass | Newest full-system packaged regression, ticket binding to intended server (AR1), production operations and owner sign-off |
| UI/HUD | Escape/resume/leave/quit, scrollable management panels, text wrapping, longer story notification visibility, chat focus and scaled prompt | Source and scoped QA build/smokes | Settings; 150%+/5:4 creator overlap; screenshot/focus/controller/localization/contrast matrix; owner approval |
| Environment | ADR-072 static geometry/texture/placement survey, 42-view daytime captures, angler/bridge-start and sign placement/Slovene glyph fixes | Editor survey and [visual captures](Captures/visual-audit/) | Trees, street furniture, heritage details, mission props, 1990s accuracy, latest packaged visual/performance gate |
| Street AI and bridges | ADR-073 local per-viewer district budgets and pursuit progress; ADR-074 nearby shared crowd and grounded bridgeheads; ADR-075 false paving/bridge-sweep rejection repair with a rejected-edge audit | Stari most editor crossing **283 m each direction**; loaded largest graph component **5,926 → 11,719**, retained bridge edges **185/185**, no new wet edges | Remaining genuine blockers/isolated 14 m graph piece, rendered chase/incident tests and fresh integrated owner experience |
| Multi-client crowd | Settle/constrained/timeout matrix, 2–3 real clients, current build stamps and null-RHI performance telemetry | Clean Development package on `c967c46`: night pair **2**, shared shopping street **18**, distinct districts and reconnection settled, cap **48** | **Opposite-bank staged 11/28 after 90 s; daytime script exit 1** |

## Recorded runtime results

**Bridge route (editor):** An automated pedestrian crossed Stari most south to north for **283 m** and returned north to south on the 38-node route. Both legs arrived without being lost or wet, with an offscreen client recording the outward trip. The smoke is `Saved/BridgeCross/20261119T233511Z`. This is one controlled route, not proof that all NPCs can cross every bridge under load.

**Strict multi-client matrix:** The earlier short-window budget checks did not prove population settlement. The revised `73b6ed6` driver keeps each scenario running until population settles, is diagnosed as constrained, or reaches its limit. Editor daytime tested at `Saved/StreetMulti/20261120T001658Z`: commercial **18/18**, separate commercial/houses **18+8**, shared shopping **18** rather than 36, rejoined players **18**, three old-town clients **48**; **opposite banks 14/28** timed out, giving a nonzero script exit.

A **clean Linux Development package** of `c967c46` was produced at 01:31 UTC on 20 November, with executable SHA-256 digests recorded in [AI evidence](AI_GAMEPLAY.md). Staged night (`Saved/StreetMulti/20261120T013134Z`) settled at **2**. Staged daytime (`Saved/StreetMulti/20261120T013156Z`) reproduced the settled shared/district/cap scenes and an **opposite-bank timeout at 11/28**. Of **4,242** spawn tries, **3,364** found no eligible pavement node; no collision rejects explain the shortage. The daytime smoke **exited 1**. This remains a real failure, not a PASS with reservations. Later documentation commits do not retroactively retest these binaries.

**Performance (limited test):** With 46–48 pedestrians and three null-RHI clients, measured server output was **15.2–15.8 KB/s**, busiest client **6.2–6.8 KB/s**, and the street step averaged **0.87 ms**. No broad performance distribution, consumer-GPU benchmark, World Partition assessment or 250-player qualification follows from these observations.

## Outstanding defects and program risks

**P0 population:** Opposite-bank crowd filling is slow/deficient where many spawn probes find no pavement node. Investigate viewer/zone locality, candidate graph and spawn-band geometry before altering budgets. Preserve the strict test and passing district/shared/cap cases.

**P0 integrated build:** The earlier QA package achieved 13/14 smoke passes and later rebased editor tests passed. The new `c967c46` package validated part of the street matrix, but a **complete current-head packaged account, mission, driving, combat, incidents and UI regression** has not been established. Account tickets remain redeemable at a server other than their intended one (AR1 design proposal); engine join-URL ticket logging (AR13) remains a privacy/security risk. No user-facing release should depend on those incomplete protections.

**P0 interface:** QA added practical navigation and fit fixes, while Settings, creator preview overlap under large UI scales, Slovene/localization and controller/accessibility testing remain unclosed. Existing screenshots do not certify the new Escape menu and scrolling states; the owner has not approved the current UI.

**P1 art:** ADR-072 identified missing trees/shrubs, street furniture, landmarks, mission props and the need to measure geometry load. Original species and approved world data still control environmental placement. **Microsoft TRELLIS is the selected image-to-3D pipeline under ADR-071; Hunyuan3D-2.1 is superseded.** Generated props need individual rights, optimization and owner approval.

## Updated development priorities

1. Reproduce and fix the **opposite-bank 28-person** crowd target, add a focused negative/control test, and rerun both editor and clean packaged day/night matrices with explicit stable-settlement acceptance.
2. Complete the fresh matched-code/asset Client/Server smoke suite (login/admission, shop, missions/fishing/restart, driving/passenger >150 m, combat, street places/incidents, weather/daylight, reconnect) and record stamps/hashes, output paths, p95/p99 or other measured budgets.
3. Decide and implement safe **per-server admission-ticket audience binding** with the appropriate threat-model/owner approval; resolve sensitive ticket URLs and add native UE CI or a documented equivalent recurring check.
4. Finish Settings and the UI resolution/focus/controller/localization/contrast gallery; render real screenshots of QA changes and obtain the owner's personal in-game approval.
5. Follow [visual-quality backlog](VISUAL_QUALITY_BACKLOG.md) to add reviewed trees and street furniture, credible mission actors/props and landmark detail without bypassing period, provenance, performance or map rights gates.

## Documentation treatment

The [README](../README.md), [main roadmap](ROADMAP.md), [AI status](AI_GAMEPLAY.md), [UI roadmap](UI_HUD_ROADMAP.md), [development guide](DEVELOPMENT.md), [architecture](ARCHITECTURE.md), [network](NETWORK_ARCHITECTURE.md), [legacy parity](LEGACY_PARITY.md), [QA](QA_AUDIT_2026-11-19.md), [visual backlog](VISUAL_QUALITY_BACKLOG.md), [playtest freshness](PLAYTEST_FRESHNESS.md) and relevant semantic/user experience documents should cite this review for current status. Earlier dated reviews, ADRs and smoke captures remain historical and should **not** be rewritten to claim they ran later code. No runtime or asset modification was performed by this documentation review.

**Evidence levels remain distinct:** source implemented ≠ editor-verified ≠ fully packaged-pass ≠ personally owner-approved. The 20 November packaged daytime test is a documented failure; no new Unreal runtime test was run for this report.
