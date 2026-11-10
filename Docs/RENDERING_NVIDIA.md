# Optional NVIDIA render features (ADR-029)

Marburgum II can use DLSS Super Resolution and DLAA, DLSS Frame Generation and Reflex when NVIDIA's Unreal plugins are present. It can also run under NVIDIA Smooth Motion. The ordinary renderer is TSR with no generated frames. It must look right on its own and is what runs when any of these features is unavailable.

## What the original did

Unreal Marburgum (`magna-raedarum-marburgum`, `UnrealMarburgum/`) never ran DLSS in the game. Its uproject enables no NVIDIA plugin. What it had:

- `tools/unreal_dlss.py` (`tools/unreal.sh dlss-probe`). It looked for `DLSS.uplugin`, `StreamlineCore.uplugin` and the community `DLSS5ForUE5.uplugin`, and gated a profiling dimension, `--neural-rendering-mode dlss5forue5-safe|dlss5forue5-guided`, to Windows/D3D12. OpenDLSS-NR stayed research only.
- `tools/unreal_frame_generation.py` (`framegen-probe`) and `tools/unreal.sh smooth-motion-run`. These kept native DLSS Frame Generation apart from NVIDIA Smooth Motion, launched a packaged Linux/Vulkan build with `NVPRESENT_ENABLE_SMOOTH_MOTION=1`, and refused to combine it with native frame generation.
- `docs/DLSS5_NEURAL_RENDERING_ROADMAP.md` and `docs/NVIDIA_SMOOTH_MOTION_ROADMAP.md`. Their rules: TSR stays the reference, every NVIDIA feature is optional, one setting per feature instead of a "DLSS" master toggle, unsupported choices are hidden with a reason, saved settings survive a missing plugin, safe mode forces vendor features off, temporal history is reset on a switch, screenshots and profiles record the active features, and rendering never reaches the server or the protocol.

## What Marburgum II does

Marburgum II carries those rules into the game client rather than a profiling harness:

| Piece | Where |
|---|---|
| Policy: request + capabilities → active features, with a reason for every fallback | `Source/MarburgumIIClient/{Public,Private}/Render/MarburgumRenderPolicy.*` |
| Runtime probe, apply, save, console commands, `MB2RENDER` log line | `Source/MarburgumIIClient/{Public,Private}/Render/MarburgumRenderFeatureSubsystem.*` |
| Plugin, console-variable and support-query names | `Config/DefaultEngine.ini`, `[/Script/MarburgumIIClient.MarburgumRenderFeatureSubsystem]` |
| Policy test | `Marburgum.Render.FeaturePolicy` |
| Host probe (port of `dlss-probe` and `framegen-probe`) | `scripts/mb2.sh render-probe [UE_ROOT]` |
| Smooth Motion launcher (port of `smooth-motion-run`) | `scripts/smooth-motion-linux.sh` |
| DLSS 5 Neural Rendering (unofficial, source only) | `Plugins/DLSS5ForUE5/` (ADR-030) |

### Settings

| Setting | Values | Default |
|---|---|---|
| Upscaler | `auto` (DLSS where supported, TSR elsewhere), `tsr`, `dlss` | `auto` |
| DLSS mode | `dlaa` (100 %), `quality` (66.7 %), `balanced` (58 %), `performance` (50 %), `ultra_performance` (33.3 %) | `quality` |
| Frame generation | `off`, `dlss` | `off` |
| Reflex | `auto` (on where supported), `off`, `on`, `boost` | `auto` |
| Neural rendering (DLSS 5, unofficial) | `off`, `safe` (one pass, scene guides generated but not fed), `guided` (one pass, depth and motion guides fed) | `off` |

Neural rendering is the original project's two DLSS5ForUE5 profiles. It runs only on top of active DLSS, because the plugin evaluates through the NGX core that DLSS loads. It also needs a runtime that this repository does not and will not contain; see [DLSS 5 through DLSS5ForUE5](#dlss-5-through-dlss5forue5-adr-030).

The choice is saved in `GameUserSettings.ini`, `[MarburgumII.Render]`. It is kept while a plugin is missing, so it applies again once the plugin is installed.

- Console (saves the choice): `mb2.Render.Upscaler`, `mb2.Render.DlssMode`, `mb2.Render.FrameGeneration`, `mb2.Render.Reflex`; and `mb2.Render.Status`.
- This launch only (not saved): `-MB2Upscaler=` `-MB2DlssMode=` `-MB2FrameGeneration=` `-MB2Reflex=`. `-MB2SafeRender` forces TSR, no frame generation and no Reflex.

### Rules the policy enforces

- An unavailable feature falls back with a reason, for example `dlss:plugin_not_installed:DLSS` or `dlss:unsupported`. Variables of a feature that is unavailable are never touched.
- DLSS Frame Generation needs Reflex. With frame generation on, Reflex stays on even if the player chose `off`. If Reflex is unavailable, frame generation stays off.
- Native frame generation never runs together with Smooth Motion (`frame_generation:conflict_smooth_motion`).
- A change during play cuts the camera, so no upscaler history from the old configuration ghosts into the new one.
- A higher-priority setter, such as the command line, a device profile or the console, wins. The log line then says `dlss:overridden:<cvar>`.
- The subsystem does not exist on the dedicated server or under `-nullrhi`. No replicated state, RPC or saved world data depends on it.

### Detection

A feature is available only if all of these hold:

1. Its plugin is installed and enabled.
2. The RHI device is an NVIDIA GPU.
3. Its runtime file is present, where one is configured (DLSS5ForUE5 only).
4. Its support query returns true.
5. Its console variables exist.

The support query is a static, parameterless `UFUNCTION` returning bool, called by name through reflection. The game therefore has no headers, link or packaging dependency on the plugins, and each check that fails names itself in the log. The probe runs once engine start-up is complete (`OnFEngineLoopInitComplete`), because NVIDIA's blueprint modules load at PostEngineInit, after the game instance exists. Asked earlier, they would wrongly report every feature unsupported.

**Names verified.** Every plugin name, console variable and query in `DefaultEngine.ini` was checked against the source of NVIDIA's v8.9.0 package: `r.NGX.DLSS.Enable`, `r.Streamline.DLSSG.Enable`, `t.Streamline.Reflex.Enable`/`Mode`, `UDLSSLibrary::IsDLSSSupported`, `UStreamlineLibraryDLSSG::IsDLSSGSupported` and `UStreamlineLibraryReflex::IsReflexSupported`. The package deprecates `EnableDLAA` in favour of DLSS at 100 % screen percentage, which is what the `dlaa` mode sets. On Linux, all three queries are reached and answer `unsupported`.

### The MB2RENDER line

Every resolution, at startup and after a change, logs one line:

```text
MB2RENDER event=resolved rhi=Vulkan (SM5) gpu="NVIDIA GeForce RTX 5080" driver=620.70 nvidia=1 safe_mode=0
  request=upscaler:auto,dlss_mode:quality,frame_generation:off,reflex:auto
  active=upscaler:tsr,dlss_mode:-,screen_pct:engine,frame_generation:off,reflex:off
  available=dlss:unsupported,frame_generation:unsupported,reflex:unsupported,neural_rendering:plugin_disabled:DLSS5ForUE5
  external_frame_generation=none neural_rendering=off/off notes=dlss:unsupported
```

The line above is wrapped here; the log has it on one line. A capture or frame-time profile is attributable only together with this line.

## Platforms

| Platform / RHI | TSR | DLSS SR / DLAA, DLSS FG, Reflex | Smooth Motion |
|---|---|---|---|
| Windows / D3D12 | yes | yes with NVIDIA's package installed (not yet run on Windows here) | NVIDIA App, outside the game |
| Linux / Vulkan | yes | **no**: NVIDIA's DLSS, Streamline and NGX RHI modules are Win64 only; their blueprint modules build and report `unsupported` | `scripts/smooth-motion-linux.sh` |

DLSS 5 through DLSS5ForUE5 is Windows/D3D12 only. On Linux the project does not enable the plugin.

The Linux driver itself has the DLSS runtime (`libnvidia-ngx.so`). The missing piece is the Unreal plugin, not the GPU. Do not build a private NGX/Streamline Vulkan bridge to get around that. A bridge would be an engine-integration project of its own, with redistribution terms to clear, and NVIDIA's plugin may add Linux later.

## The installed NVIDIA package

| | |
|---|---|
| Package | NVIDIA "DLSS 4.5 plugin for UE 5.8", v8.9.0, 2026-10-16 (DLSS `8.9.0-NGX310.9.16`, Streamline `8.9.0-SL2.14.16`) |
| Source | <https://developer.nvidia.com/rtx/dlss>, "Download UE 5.8 DLSS Plugin" |
| Archive | `2026.10.16_UE5.8_DLSS4.5Plugin_v8.9.0.zip`, 1,166,608,575 bytes, SHA-256 `adeeb59b9635794e05252438bf01307bf8a629769e8f0517237363f690bb380c` |
| Licence | NVIDIA RTX SDKs licence (see the PDFs in the archive's `Documentation/`); accepted by the project owner |
| Installed | `Plugins/DLSS`, `StreamlineCore`, `StreamlineDLSSG`, `StreamlineReflex`, `StreamlineNGXCommon` (local, gitignored, never committed) |

The uproject enables DLSS, StreamlineCore, StreamlineDLSSG and StreamlineReflex as **optional** references, so a checkout without the package still opens and builds; the game then reports `plugin_not_installed`. To install on another machine, unpack the same archive's `Plugins/<name>` folders into the project's `Plugins/` and check the SHA-256. Do not commit them.

Still to do on Windows: run a client with `mb2.Render.Status` and confirm `dlss:yes`, then profile TSR against DLSS Quality and DLAA on the Lent route. Accept on frame time, p95/p99 hitches, input latency and artefacts (wet Drava, foliage, thin railings and wires, signage), not on headline FPS.

## DLSS 5 through DLSS5ForUE5 (ADR-030)

`Plugins/DLSS5ForUE5` is the community plugin's v0.6.1 source (<https://github.com/praveenkumarappu/DLSS5ForUE5>, MIT, commit in `UPSTREAM.txt`). It is unofficial and not affiliated with NVIDIA. Its `LICENSE` and `THIRD_PARTY_NOTICES.md` are kept beside it. Only the plugin's own source, shaders and descriptor were copied. Its release scripts, CI and Windows install script were left out.

The plugin does nothing without `Binaries/ThirdParty/Win64/nvngx_dlssnr.dll`. NVIDIA has not released the DLSS 5 SDK publicly. The DLL the plugin's releases used is an unsigned, community-modified NVIDIA binary, and the plugin's own notices say its redistribution is not authorised. **That DLL is not in this repository, is gitignored, and will not be fetched for the project.** Until a runtime that the project is licensed to use is placed at that path on a Windows machine, the game reports `neural_rendering:runtime_missing:nvngx_dlssnr.dll` and renders without it. A future official NVIDIA DLSS 5 package replaces this lane rather than being added beside it.

With a runtime present, `mb2.Render.NeuralRendering safe` and then `guided` (DLSS on) set `r.DLSS5.Enable`, `PassCount 1`, `EnableSceneGuides 1`, `UseDepth 1`, and `FeedSceneGuides`/`UseMotionVectors` 0 or 1. The plugin's own `DLSS5.Status` and `DLSS5.SafeMode` commands still work. If its editor window or crash recovery overrides the setting, the MB2RENDER line says `neural_rendering:overridden`. Like the rest of this document, it is client-local appearance processing. Captures made with it must carry its MB2RENDER line, because it can change visible detail (faces, signage).

## Smooth Motion on Linux

`scripts/smooth-motion-linux.sh [--debug-log <file>] [--graphics-queue] [--loader-debug] [-- client args]` checks that the driver ships `VK_LAYER_NV_present` and starts the staged client on Vulkan with `NVPRESENT_ENABLE_SMOOTH_MOTION=1`. The client records `external_frame_generation=nvidia-smooth-motion(requested-unverified)`. A request is not proof of activation: use the NvPresent log for that. The client's own frame times stay the renderer's numbers, and generated frames are a separate presentation measurement.
