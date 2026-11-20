# GFX-00 — rendering configuration and visual baseline of /Game/World/lent

**20 November 2026 · branch `feat/gfx-00-baseline` · [Graphics roadmap](GRAPHICS_ROADMAP.md) GFX-00 · [ADR-078](DECISIONS.md) (approval open).**
**State: tooling implemented; packaged-and-measured on one high-end GPU, with the frame-time tails of five scenes contended by another session (see [Measurements](#measurements)). Not owner-approved. Nothing was enabled, disabled or retuned. No setting in this report is a tested recommendation.**

**ADR-082 follow-up:** GFX-00 audit and staged benchmark evidence now exist, but recorded GPU contention limits the accuracy of some tails. **Hardware ray-traced Lumen has been selected as the next separately cooked experiment**, not enabled in the current graphics configuration. The Linux target needs Vulkan SM6/RT and a complete shader/cook qualification alongside a known non-RT fallback. Do not change global lighting settings or claim improved frame rate from this baseline alone. [Graphics plan](GRAPHICS_ROADMAP.md), [latest review](DEVELOPMENT_REVIEW_2026-11-20_EVENING.md).

## What was built

| Piece | What it does |
| --- | --- |
| `MarburgumRenderAudit` (client, non-Shipping) | At the first survey view writes `render_config.json`: RHI, shader platform, adapter, driver, ray-tracing support, VRAM, engine version and build configuration, the scalability levels, 61 console variables that choose GI, reflections, shadows, anti-aliasing, Nanite, sky/fog/water, exposure and post-processing, and an inventory of the loaded level (lights by kind/mobility/shadowing, each directional light, sky light, height fog, atmosphere, clouds, every post-process volume's overrides, static meshes with LOD0 triangles, Nanite, LOD count, draw distance, materials). Read-only. |
| `UMarburgumViewShotSubsystem` | Samples every frame of each view: frame, game-thread, render-thread, RHI-thread and GPU time as mean and p50/p95/p99, plus process VRAM (the RHI's memory-budget query, this process only), texture streaming/non-streaming memory and resident memory, in `stats.tsv` (the original eight columns first). A `player` view keeps the player's camera and the HUD. `-MB2ViewShotSettle` sets the seconds per view. |
| `mb2 gfx-views` | Four cameras the survey lacked: low over the Drava towards the Lent frontage and towards Stari most, the north-bank quay looking upstream, and above the Lent roofs. |
| `scripts/gfx-baseline-linux.sh` | The 42-view survey + the four + the HUD view at morning 07:00, noon, dusk 21:00, night 23:30 (clear), noon overcast, rain noon, rain night, and the HUD view at the Hotel Lent street door (ADR-076). Records source SHA and dirtiness, engine version or the staged build stamp and a SHA-256 of the cooked containers, GPU/driver/VRAM and any other Unreal process at the start. |
| `mb2 gfx-baseline` | `baseline.csv` (scene × view) and `baseline.json` (provenance, render configuration, per-scene medians and worst views, configuration drift between scenes, `evidence_class` editor vs packaged). |

## Renderer configuration as it runs (read back, not assumed)

There is no `[/Script/Engine.RendererSettings]` block in `Config/DefaultEngine.ini`: the project runs on engine defaults plus what the level and the weather presentation set at run time. Values from `render_config.json` of the packaged client; the editor run and all eight packaged scenes read the same console variables and scalability, only the lights' run-time intensities change with time and weather:

| Area | Running value | Consequence |
| --- | --- | --- |
| Global illumination | `r.DynamicGlobalIlluminationMethod=0` — **none**; no baked lighting either (every light is movable) | Shadowed sides and undersides receive only the 128² real-time sky light: the Stari most underside reads black, shaded façades flat brown. The largest single lighting gap. |
| Reflections | `r.ReflectionMethod=2` — screen-space (SSR quality 3) | The river reflects only what is on screen; off-screen sky and banks fall back to the sky light. Visible as orange speckle on the Drava. |
| Shadows | `r.Shadow.Virtual.Enable=0` — cascaded shadow maps, one shadowed sun, 4 cascades to 400 m, max 2048² | No contact detail at distance; 96 lamp lights cast no shadow. |
| Anti-aliasing | `r.AntiAliasingMethod=4` (TSR), `r.ScreenPercentage=0` (engine default), history 200 % | TSR is the cross-vendor default the roadmap requires. |
| Geometry | **0 of 1,531 unique meshes Nanite**; all single-LOD; all 1,615 components without draw distance; **9.93 M LOD0 triangles** always loaded | VQ-028 confirmed at run time. Nanite is enabled in the project (`r.Nanite=1`), only the meshes do not use it. |
| Distance fields / ray tracing | `r.GenerateMeshDistanceFields=0`, `r.RayTracing=0`; RHI reports no ray-tracing support (it was not enabled) | **Lumen cannot be A/B-tested by a console switch:** software Lumen needs mesh distance fields built, hardware Lumen needs the ray-tracing project setting and Vulkan RT on Linux. Both are project-setting changes with a cook. |
| Exposure | Auto exposure on, `ExtendDefaultLuminanceRange=0` (legacy range); sun 7.9 lux at noon; level volume bias 0.4, weather grade bias 0.36 and min 0.03 at priority 10 | Lighting is in legacy, non-physical units. A physically calibrated sun/sky (GFX-03) is a coupled change of exposure range, every light, emissive windows and the weather grade, not a single setting. |
| Sky, fog, clouds | Sky atmosphere, volumetric clouds, one height fog (density 0.004, **volumetric fog off** on the component although `r.VolumetricFog=1`) | Volumetric fog is available but unused. |
| Lights | 3 directional (sun + the weather presentation's moon/lightning, hidden at noon), 48 spot + 48 point, all movable, unshadowed | The 1,479 street lamps are not each a light; a pool of 96 lights carries the night. |
| Materials | 97 unique materials on placed meshes, no translucent sections | Glass is opaque. |
| Water | `r.Water.SingleLayer=1` available; the Drava is an ordinary material | Single Layer Water is untested (GFX-04). |
| Optional NVIDIA | DLSS/Frame Generation/Reflex/DLSS 5 cvars absent (plugins not loaded on Linux) | As ADR-029/030 intend. |

Not audited in this run: MegaLights. The audit asked for `r.MegaLights.Enable`, which 5.8.3 does not have; it now reads `r.MegaLights.EnableForProject` and `r.MegaLights.Allowed` (after `0ebe6bb`, so not in these numbers).

## The five largest visible deficiencies

Ranked by how much of a normal third-person walk they affect. Each is registered in [VISUAL_QUALITY_BACKLOG.md](VISUAL_QUALITY_BACKLOG.md); this report changes no status.

1. **No vegetation at street level** (VQ-022): none of the 27 sampled street views shows a tree or shrub; the only green is the landscape. Biggest gap between the slice and a lived-in Lent.
2. **No indirect light** (this report): with GI off and a small sky light, every shaded surface is flat; the bridge underside and courtyards go near-black, which also makes materials look plastic (GFX-03 before GFX-02 tuning).
3. **Façades are one pattern, ground floors blank** (VQ-014, VQ-010): every street view repeats the same plaster, socle and window; no shopfronts, no landmarks.
4. **Streets are empty** (VQ-023, VQ-006, VQ-026): almost no benches, shelters, bins, railings or parked cars; mission people drawn as primitives.
5. **The Drava and its banks** (VQ-021, VQ-024, VQ-030): one green, a noisy SSR reflection, no quay wall or railing, bare bridge decks.

Also seen: the "Obreška pot" street-name lettering hangs tilted in mid-air across a corner at the default arrival (registered as VQ-031, needs verification); the existing `angler_close` survey camera now looks into a wall.

## Prerequisites before GFX-01–04 can be measured

- A **free GPU** for the measuring run (another session's smoke at 92 % invalidated the editor numbers here) and the **packaged** client/server from one clean commit (the staged run below).
- **Lumen uses hardware ray tracing** (owner decision, [ADR-082](DECISIONS.md)). It needs the Linux target moved to `SF_VULKAN_SM6` (the shipped `VULKAN_SM5` platform has no ray tracing in 5.8.3) and `r.RayTracing=True`, then a full shader recompile and cook. It cannot be toggled at run time in the shipped configuration.
- **Virtual shadow maps** are only sensible with Nanite geometry; pilot them together with Nanite on the shells and paving (VQ-028), set in the generators.
- **Exposure calibration** (extended luminance range) before any material roughness/albedo tuning, or GFX-02 will be tuned against the wrong exposure.
- The **content passes** (vegetation, furniture, quay) are art-rights work (VA-01/05/08, ADR-071) and do not depend on renderer changes.

## Reversible A/B plan for GFX-01–04

Every experiment is a command-line or ini override on the same packaged build and the same `gfx-baseline` scenes, so A and B differ in one thing and the A side is the baseline above. Nothing is written to `DefaultEngine.ini` until the owner accepts a B.

| Step | B side | Switch | Measured on | Rollback |
| --- | --- | --- | --- | --- |
| GFX-03a | Volumetric fog on the existing height fog | weather presentation flag / `-ini:` override of the fog component default | dusk, night, rain night | remove the override |
| GFX-03b | Extended luminance range + calibrated sun/sky | `-ini:Engine:[SystemSettings]:r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange=1` plus light values from one data table | all eight scenes | remove the override |
| GFX-03c | Hardware-ray-traced Lumen GI + reflections (ADR-082) | separate cook targeting `SF_VULKAN_SM6` with `r.RayTracing=True`, then `r.DynamicGlobalIlluminationMethod=1`, `r.ReflectionMethod=1` at run time | all eight scenes; GPU p95/p99, VRAM; a GPU without Vulkan RT falls back | `=0`/`=2` at run time; the cook is a branch |
| GFX-02 | Material instances (paving, plaster, roof, glass) | asset branch | noon, rain noon, overcast | asset branch |
| GFX-04 | Single Layer Water on the Drava | asset branch | river cameras, four times/weather | asset branch; swim/fish smokes unchanged |
| GFX-01 | Vegetation, furniture, quay along the showcase route | asset branch | street, quay, roof cameras | asset branch; crowd/door smokes unchanged |
| GFX-06 | Nanite on shells and paving, then VSM | generator flag, separate cook | all scenes; VRAM | generator flag off |

## Measurements

**Evidence class: packaged.** Development Client and dedicated Server packaged from `0ebe6bb` (clean tree, `build_fresh: yes`): this branch's tooling commit on `main` `735dce7`, before the branch was rebased (now `0c74b02`; the rebase brought in main's later commits, which these numbers do not include), cooked content SHA-256 `d3b46340…bf24baedb` (`cooked-content.sha256` lists every container). UE 5.8.3, Vulkan SM5, RTX 5080 16 GB, NVIDIA 620.70, Linux 6.18 (CachyOS), 1920×1080 offscreen, TSR at the default screen percentage, scalability Epic (3) everywhere, frame rate **uncapped** (`t.MaxFPS=0`, no VSync), 4 s per view, one client and one server on the same host. Runs: `Saved/GfxBaseline/gfx00-staged` (all eight scenes, 330 shots) and `gfx00-staged-rerun` (scenes 1–5 again, with a monitor). Not committed (≈ 600 MB); [selected captures](Captures/gfx00-2026-11-20/).

**The host was shared, and it shows in the tails, not the medians.** Another session's mission smokes (editor server and rendering client) ran during scenes 1–5 of both runs: the monitor saw another Unreal process in 13–41 of 41 samples of every re-run scene, and in both runs the RHI's VRAM budget for this client was about **12.0 GB in scenes 1–5 against 14.0 GB in scenes 6–8**, so another process held about 2 GB then. Per-scene **median GPU time agrees between the two runs within 0.1 ms**, so medians are usable. The **p99 of scenes 1–5 is not**: 11–18 ms spikes appear on varied views in contended scenes only, while the same 46 cameras in the uncontended rain scenes never exceed 3.7 ms. `gfx-baseline-linux.sh` now records this contention per scene (`contention` in `baseline.json`).

| Scene (Maribor time) | Contended | Frame median | Game median / p95 max | Render median / p95 max | GPU p50 median / max | GPU p99 max | VRAM max |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 morning 07:00, clear | yes | 3.2–3.6 ms | 0.9 / — | 1.9–2.0 / — | **2.84** / — | — | 1.45 GB |
| 2 noon 12:00, clear | yes | 3.1–3.3 | 0.7–1.1 / — | 1.7–2.1 / — | **2.67–2.69** / — | — | 1.41 GB |
| 3 dusk 21:00, clear | yes | 2.8–4.1 | 0.8–1.3 / — | 1.9–2.7 / — | **2.06–2.11** / — | — | 1.44 GB |
| 4 night 23:30, clear | yes | 2.9–3.8 | 0.8–1.3 / — | 1.9–2.7 / — | **2.05–2.16** / — | — | 1.44 GB |
| 5 noon, overcast | yes | 3.5–3.6 | 1.1–1.2 / — | 2.1–2.4 / — | **2.87–2.92** / — | — | 1.41 GB |
| 6 noon, rain | no¹ | 3.04 | 1.06 / 3.29 | 2.06 / 4.28 | **2.62** / 3.09 | **3.66** | 1.54 GB |
| 7 night, rain | no¹ | 2.58 | 0.68 / 1.81 | 1.63 / 3.75 | **2.08** / 2.41 | **3.40** | 1.54 GB |
| 8 Hotel Lent door, noon (player camera only) | no¹ | 3.04 | 2.45 / 3.22 | 1.86 / 2.44 | **2.62** | **3.65** | 1.38 GB |

Ranges are the two runs; "—" is a contended value that is not reported. ¹ No other Unreal process at the scene's start and a 14 GB VRAM budget; the first run had no monitor yet, so "no" is inferred, not sampled. Medians are over the 46 fixed views per scene (the HUD view excluded). Resident memory of the client 3.8–4.0 GB in every scene. RHI-thread time 0.4–0.65 ms median.

What this does and does not show:

- **The current Lent is cheap on this card:** about 2–3 ms of GPU and 1–2.5 ms of render thread per 1080p frame, about 1.4–1.5 GB of VRAM, with GI off, SSR, CSM and 9.9 M non-Nanite triangles. The most expensive views are the Drava toward Stari most and the aerials over Lent (3.0–3.1 ms). The earlier editor survey's "Pobrežje views at 7–8 ms" do not reproduce in the package (2.6–2.8 ms).
- **Rain adds about 0.13 GB of VRAM** and no measurable GPU median; night is cheaper than day (fewer sunlit shadow cascades, darker scene).
- **It is not a frame-budget qualification.** One high-end GPU, uncapped, one client, no crowd load beyond the default, no driving, swimming or door opening during sampling, and contended tails for five scenes. The roadmap's 1080p/60 Balanced target on a declared mid-range card is **unmeasured**: there is no such card on this host.
- **Headroom is the useful number for GFX-03:** a 16.7 ms budget leaves about 13 ms of GPU on this card for Lumen, volumetric fog and VSM experiments. Their cost has to be measured on the same scenes, uncontended.

**Known defect in the tooling:** in the packaged client the `player_hud` view is shot **without the HUD** (it shows the player's camera only), while the editor run of the same code includes it. The packaged UI capture smoke (`ui-capture-linux.sh staged`) does include the HUD through the same engine call, so the difference is in the view tool; not yet found. Until then HUD readability evidence comes from the UI capture smoke.

**To finish GFX-00:** re-run scenes 1–5 when no other session renders on this GPU (the monitor will show it), and run the same scenes on a declared mid-range GPU when one is available.
