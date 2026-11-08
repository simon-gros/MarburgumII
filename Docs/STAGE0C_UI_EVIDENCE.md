# Stage 0C evidence — UI-01 / UI-02 first implementation, Linux, 2026-11-08

**Result:** the UI-01 design system and the UI-02 connection shell **run and render in packaged Linux clients**. A real join to a packaged dedicated server, a connection timeout, and a real server stop/restart with automatic client recovery were captured. Per [ADR-008](DECISIONS.md), this work is `implemented_awaiting_owner_playtest`: it has not been played or approved by the project owner and is not "accepted".

## Environment

| Item | Value |
| --- | --- |
| Source | `ae3868a3c1c3987766688fc8bb5375365a38df66` (clean tree; Editor, Server and Client rebuilt and repackaged from it) |
| Engine | Unreal Engine **5.8.3** source build, engine-bundled clang toolchain |
| Host / GPU | CachyOS Linux 6.18.60 x86_64; NVIDIA GeForce RTX 5080, driver 620.70.06 |
| Rendering | Packaged `MarburgumIIClient` with `-RenderOffScreen -windowed -ForceRes -ResX/-ResY` (real GPU rendering without a display) |
| Screenshot | `FScreenshotRequest` with UI enabled: the composited window, UMG included |

## What was built

- **Session state machine** (`UMarburgumSessionSubsystem`, phases in shared `MarburgumSessionContract.h`): `starting → idle → connecting → joining → loading_world → ready`, plus `reconnecting` and `error` with faults `invalid_address`, `timeout`, `refused`, `contract_mismatch`, `connection_lost`, `travel_failed`. `ready` requires a live server connection, a replicated non-authoritative `AMarburgumGameState` with the client's network contract, and a replicated `PlayerState`. Attempts time out after 20 s; unexpected loss of a ready session triggers up to 3 reconnect attempts with linear backoff, then an error with retry.
- **No-authority front-end** ([ADR-013](DECISIONS.md)): `/Game/Maps/FrontEnd` with `AMarburgumFrontEndGameMode` is the client default map, so an unconnected or disconnected client never runs gameplay rules locally.
- **UI-01 design system:** versioned tokens (`MarburgumUITheme`: colour roles, type scale on engine-bundled Roboto, spacing, radii, motion timings) and UMG primitives (`MarburgumUI::FBuilder`: text roles, surfaces, buttons, pills, meters, text field, key/value field, toast, state card, sample-data badge). A component gallery shows all of them under a visible **SAMPLE DATA · NOT LIVE GAMEPLAY** marker.
- **UI-02 connection shell:** branded front-end with server address, Connect, a step indicator that lights only for phases the session subsystem observed, elapsed time, Retry/Cancel, phase-specific headlines and guidance, and engine failure detail.
- **UI root and HUD groundwork:** one root per world with ordered layers (HUD, notifications, screens, modal, diagnostic); a deduplicated `notification_feed` that announces admission once; an opt-in, labeled diagnostics overlay showing live replicated values.

## Captures (`scripts/ui-capture-linux.sh staged`, run `Saved/UICapture/20261108T224655Z`)

| Case | Result | Committed capture |
| --- | --- | --- |
| Front-end idle at 1280×720, 1920×1080, 2560×1440, 3840×2160 | PASS, 4 separate client processes | [1920×1080](Captures/stage0c/frontend-idle-1920x1080.png), [1280×720](Captures/stage0c/frontend-idle-1280x720.png) |
| Component gallery at 1920×1080 and 2560×1440 | PASS, SAMPLE DATA marker visible | [2560×1440](Captures/stage0c/gallery-sample-data-2560x1440.png) |
| Connect to an unused port | PASS: `connecting`, then `error` / `timeout` after 20 s, Retry offered | [error](Captures/stage0c/error-timeout-1920x1080.png) |
| Join a packaged dedicated server | PASS: `ready`; HUD shows the "Connected" toast; diagnostics show server heartbeat, contract v1, 1 player | [admitted](Captures/stage0c/admitted-packaged-server-1920x1080.jpg) |
| Server stopped while admitted | PASS: engine reports "Host closed the connection"; client returns to the front-end and shows `reconnecting`, attempt 1 of 3 | [reconnecting](Captures/stage0c/reconnecting-after-server-stop-1920x1080.png) |
| Server restarted | PASS: client re-admitted automatically on its first attempt; "Reconnected" toast; heartbeat restarted from the new server process | [re-admitted](Captures/stage0c/readmitted-after-restart-1920x1080.jpg) |

The session log lines (`MB2UI ...`) and server join/leave lines for all cases are in [Captures/stage0c/summary.txt](Captures/stage0c/summary.txt). The two in-world captures are JPEG conversions (quality 88) of the original PNGs; the others are unmodified.

## Regression checks on the same commit

| Check | Result |
| --- | --- |
| Stage 0B packaged network smoke (`net-smoke-linux.sh staged`, `Saved/NetSmoke/20261108T224823Z`) | PASS; server joins 4, refused 3, applied 3, duplicates 4. A disconnected smoke client now returns to the FrontEnd map. |
| Stage 0B editor network smoke (`net-smoke-linux.sh editor`, `Saved/NetSmoke/20261108T224849Z`) | PASS, same totals |
| Side separation (symbol search in `Binaries/Linux/*.sym`) | Server: 0 `UMarburgumConnectionScreen`/`UMarburgumSessionSubsystem` symbols. Client: 0 `FMarburgumIIServerModule` symbols |
| Source checks | `.NET` manifest validator and `validate-ui-ontology.py` pass |

## Defects found and fixed while qualifying

- `UBorder` copies content alignment into its slot only when content is added, so the diagnostics panel filled the whole screen and the toast feed sat on the wrong side. `FBuilder::Align` now sets both.
- `UBorder::SetBrushColor` multiplies the brush tint, so step bars built on a dark brush never lit up. They now use a white brush tinted by the token colour.
- The NetSmoke map's static lights printed "LIGHTING NEEDS TO BE REBUILT" in development clients; the generator now makes them movable.
- Without `-ForceRes`, offscreen clients clamp the requested resolution (888×500 instead of 1920×1080).
- Raw engine failure strings can be a full connection dump; the shell shows the leading sentence and the log keeps the full text.

## Not proven by this evidence

- **Owner playtest:** none yet (ADR-008). No feature here is accepted.
- **Input and focus:** no keyboard-only or controller traversal was tested; captures are scripted, not interactive. Escape-to-cancel and Enter-to-connect are implemented but untested.
- **Intermediate phases:** on loopback the client goes from `connecting` to `ready` within one 10 Hz observation, so `joining` and `loading_world` were never observed or captured. They need a slower link or an artificial delay to verify.
- **Refusal and version mismatch:** the `refused` and `contract_mismatch` paths have no server that triggers them yet (no admission control, single contract version).
- **UI-scale matrix:** `-MB2UIScale` exists but no 80–200% captures were taken; ultrawide 3440×1440 was not captured.
- **Visual design review:** the theme follows UI_HUD_DESIGN §3 proposed tokens; no art-direction review has happened. The typeface is engine-bundled Roboto (Apache-2.0) pending a brand typeface decision.
- **Real networks and Windows:** loopback only; no Windows client.
- **UI-03:** no exploration HUD modules (location, prompt, objective, condition) exist yet.
