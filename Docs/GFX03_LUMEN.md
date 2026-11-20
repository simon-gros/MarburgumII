# GFX-03 — Lumen with hardware ray tracing and mesh distance fields

**20 November 2026 · branch `feat/gfx-03-lumen-rt` · [Graphics roadmap](GRAPHICS_ROADMAP.md) GFX-03 · [ADR-082](DECISIONS.md) · baseline: [GFX00_BASELINE.md](GFX00_BASELINE.md).**
**State: implemented, packaged and measured on one high-end GPU (RTX 5080), uncontended; merged to `main` on the owner's instruction (21 November). No owner playtest yet.**

## What changed

`Config/DefaultEngine.ini` only; no code, no content.

| Setting | Value | Why |
| --- | --- | --- |
| Linux `TargetedRHIs` | `SF_VULKAN_SM5` **and** `SF_VULKAN_SM6` | `VULKAN_SM5` has no ray tracing in UE 5.8.3. The RHI tries SM6 first and falls back to SM5 on a GPU that cannot run it (`LinuxDynamicRHI.cpp`); `-sm5` forces the fallback. |
| `r.RayTracing` | True | Hardware ray tracing (ADR-082). |
| `r.SkinCache.CompileShaders` | True | Animated characters enter the ray-traced scene. |
| `r.GenerateMeshDistanceFields` | True | Software Lumen where hardware tracing is unavailable (owner, same day). |
| `r.DynamicGlobalIlluminationMethod`, `r.ReflectionMethod` | 1, 1 (Lumen) | Lumen GI and reflections are the default. `=0`, `=2` at run time is the GFX-00 renderer. |
| `r.Lumen.HardwareRayTracing` | True | Hardware tracing where available. |
| `r.RayTracing.ResidentGeometryMemoryPoolSizeInMB` | 1536 | The always-loaded level keeps all 287 MiB of ray-tracing geometry resident; with the default 400 MiB pool Development builds printed a red warning on screen. Same VRAM with 1536 (3,078 vs 3,073 MB). |

## Measurements

Packaged Development client and server from `fd581be` (this branch before the pool setting and the rebase onto `main`; the pool setting was checked by a run-time override on the same build). RTX 5080 16 GB, NVIDIA 620.70, Vulkan, 1920×1080, TSR, Epic scalability, uncapped. Same `gfx-baseline-linux.sh` scenes as GFX-00. The GPU monitor saw another Unreal process in **1 of about 330 samples** of the B run (night and overcast, one sample each) and in none of the other runs. Evidence in `Saved/GfxBaseline/gfx03-*` (not committed); [captures](Captures/gfx03-2026-11-20/).

Four variants of the **same package**: **A** the GFX-00 renderer by run-time switch; **B** hardware Lumen (the default); **SW** SM6 with hardware tracing off (distance-field Lumen); **SM5** forced SM5, as on a GPU without SM6 (distance-field Lumen, no ray tracing). GFX-00 is the earlier package without any of this, for reference (its night tails were contended, see that report).

GPU time, median over the 46 fixed views of a scene (door: the player camera), and the worst p99:

| Scene | GFX-00 | A (RT build, no Lumen) | **B hardware Lumen** | SW Lumen (SM6) | SM5 fallback |
| --- | --- | --- | --- | --- | --- |
| Noon | 2.69 | 3.68 | **4.26** (p99 ≤ 7.4) | 4.78 | 4.21 |
| Night | 2.16 | 2.94 | **3.79** (p99 ≤ 5.5) | 4.02 | 3.47 |
| Rain, night | 2.08 | 2.97 | **4.13** (p99 ≤ 8.3) | 4.15 | 3.68 |
| Hotel Lent door | 2.62 | 3.89 | **5.36** (p99 ≤ 6.7) | 5.28 | 4.45 |
| VRAM (max) | 1.4–1.5 GB | 2.1–2.2 GB | **3.1 GB** | 3.1 GB | 2.7–2.9 GB |
| Client resident memory | 3.8–4.0 GB | 5.3–5.7 GB | **5.6–5.8 GB** | 5.6–5.8 GB | 4.7–4.9 GB |

B over all eight scenes: GPU median 3.6–4.7 ms, worst p99 10.7 ms (rain noon, `air_magdalena`); frame p99 at most 21 ms (the first, HUD view of overcast, where the scene is still settling).

What this shows:

- **Hardware Lumen costs about 1.5–2.7 ms of GPU per 1080p frame over GFX-00 on this card**, of which about 0.8–1.3 ms is the ray-tracing build itself (A vs GFX-00: SM6, the ray-tracing scene and distance fields even with Lumen off), and about 1.6 GB of VRAM and 1.8 GB of process memory.
- **Software Lumen costs the same as hardware Lumen here** (SW within 0.5 ms of B). Distance fields are therefore a full-quality fallback, not a degraded one, at least on a high-end GPU.
- **The SM5 fallback works:** a client forced to SM5 starts, reports no ray tracing, and renders Lumen from distance fields at 3.5–4.5 ms.
- **Render thread at the door** rose from 1.9 ms (GFX-00) to 5.1 ms (A) and 8.9 ms (B) median. The street scenes stay at 2.8–3.2 ms (B). The streamed interior probably costs ray-tracing scene updates on the render thread; not yet profiled. Under a 16.7 ms frame it is not a limit, but it is the first thing to look at on a slower CPU.
- **Still no frame-budget qualification**: one high-end GPU, one client, no mid-range card on this host.

Visually (A | B in the captures): sunlit plaster bounces warm light into shaded façades, the doorway and the street; shaded walls go from flat grey-brown to lit; night lamp pools are slightly softer. River reflections are still screen-space-like at this water material; GFX-04 owns the water.

## Costs outside the frame

- **Cook:** every shader is compiled for SM5 and SM6, with ray-tracing permutations, and every static mesh gets a distance field. The first cook on this branch took about 25 minutes of shader compile and **peaked at 23.5 GB of memory** (6 compile jobs, shader workers at 25 %). A package run inside the 16 GB exec bound was killed; it ran as a `systemd-run` unit with `MemoryMax=28G`. Every session that packages after this reaches `main` pays the first recompile and needs the same headroom.
- **Package size:** cooked client content 0.69 → 1.5 GB (staged client 2.9 → 3.7 GB).

## Open

- Owner look at A | B in the captures and a real playtest at night and indoors.
- Render-thread cost at the door (above).
- A mid-range GPU, and a GPU without SM6 for a real (not forced) fallback.
- Lumen quality per graphics preset is GFX-07; MegaLights, virtual shadow maps and Nanite are GFX-06.
