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

### Settings

| Setting | Values | Default |
|---|---|---|
| Upscaler | `auto` (DLSS where supported, TSR elsewhere), `tsr`, `dlss` | `auto` |
| DLSS mode | `dlaa` (100 %), `quality` (66.7 %), `balanced` (58 %), `performance` (50 %), `ultra_performance` (33.3 %) | `quality` |
| Frame generation | `off`, `dlss` | `off` |
| Reflex | `auto` (on where supported), `off`, `on`, `boost` | `auto` |

DLSS 5 Neural Rendering has no setting. NVIDIA publishes no supported UE 5.8 package for it, and the community DLSS5ForUE5 plugin is Windows/D3D12 only and unofficial. It stays out of the game until a supported package exists for UE 5.8.3 on the platforms we ship.

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
3. Its console variables exist.
4. Its support query returns true.

The support query is a static, parameterless `UFUNCTION` returning bool, called by name through reflection. The game therefore has no headers, link or packaging dependency on the plugins, and each check that fails names itself in the log.

**Unverified names.** The plugin names, console variables and query functions in `DefaultEngine.ini` follow NVIDIA's DLSS 4.5 Unreal package. That package has not been installed here. When it is, run `mb2.Render.Status` in a Windows build. Any `cvar_missing:` or `support_query_missing:` reason points to the exact line to correct in `DefaultEngine.ini`; no code change is needed.

### The MB2RENDER line

Every resolution, at startup and after a change, logs one line:

```text
MB2RENDER event=resolved rhi=Vulkan (SM5) gpu="NVIDIA GeForce RTX 5080" driver=620.70 nvidia=1 safe_mode=0
  request=upscaler:auto,dlss_mode:quality,frame_generation:off,reflex:auto
  active=upscaler:tsr,dlss_mode:-,screen_pct:engine,frame_generation:off,reflex:off
  available=dlss:plugin_not_installed:DLSS,frame_generation:plugin_not_installed:StreamlineDLSSG,reflex:plugin_not_installed:StreamlineReflex
  external_frame_generation=none neural_rendering=off notes=dlss:plugin_not_installed:DLSS
```

The line above is wrapped here; the log has it on one line. A capture or frame-time profile is attributable only together with this line.

## Platforms

| Platform / RHI | TSR | DLSS SR / DLAA, DLSS FG, Reflex | Smooth Motion |
|---|---|---|---|
| Windows / D3D12 | yes | once NVIDIA's UE 5.8 DLSS package is installed and its names are verified | NVIDIA App, outside the game |
| Linux / Vulkan | yes | **no**: NVIDIA's UE plugin ships no Linux support, so it resolves to TSR | `scripts/smooth-motion-linux.sh` |

The Linux driver itself has the DLSS runtime (`libnvidia-ngx.so`). The missing piece is the Unreal plugin, not the GPU. Do not build a private NGX/Streamline Vulkan bridge to get around that. A bridge would be an engine-integration project of its own, with redistribution terms to clear, and NVIDIA's plugin may add Linux later.

## Installing the plugin (Windows)

The project owner should do this. It needs an NVIDIA developer account and acceptance of NVIDIA's licence.

1. Download "DLSS 4.5 plugin for UE 5.8" (v8.9.0, 2026-10-16) from <https://developer.nvidia.com/rtx/dlss>.
2. Unpack it into the engine's `Engine/Plugins/Marketplace/` (or the project's `Plugins/`, which `.gitignore` must then exclude) and enable DLSS, StreamlineCore, StreamlineDLSSG and StreamlineReflex for the client.
3. Record the version and archive SHA-256 here. Do not commit NVIDIA binaries.
4. Run `scripts/mb2.sh render-probe "$UE_ROOT"`, then a client with `mb2.Render.Status`, and correct any binding the log names.
5. Profile TSR against DLSS Quality and DLAA on the Lent route. Accept on frame time, p95/p99 hitches, input latency and artefacts (wet Drava, foliage, thin railings and wires, signage), not on headline FPS.

## Smooth Motion on Linux

`scripts/smooth-motion-linux.sh [--debug-log <file>] [--graphics-queue] [--loader-debug] [-- client args]` checks that the driver ships `VK_LAYER_NV_present` and starts the staged client on Vulkan with `NVPRESENT_ENABLE_SMOOTH_MOTION=1`. The client records `external_frame_generation=nvidia-smooth-motion(requested-unverified)`. A request is not proof of activation: use the NvPresent log for that. The client's own frame times stay the renderer's numbers, and generated frames are a separate presentation measurement.
