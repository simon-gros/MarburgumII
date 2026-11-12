# Footstep sounds

Source files for footsteps ([ADR-061](../../../Docs/DECISIONS.md)), imported into `/Game/Audio/Footsteps` by
`scripts/create-weather-content.sh audio`.

| Item | Value |
| --- | --- |
| Pack | Kenney **Impact Sounds** 1.0 (created 2019-12-19), the same pack as the punches (`../Combat/README.md`) |
| Author | Kenney (www.kenney.nl) |
| Licence | Creative Commons Zero (CC0 1.0): `KenneyImpactSounds/License.txt` is the pack's own file, unchanged |
| Source | https://kenney.nl/assets/impact-sounds, `kenney_impact-sounds.zip`, SHA-256 `029d734af1582474edf3a694d1b0cebc97c1c152f2f39fa34d4c2bafc5de77f8` |
| Taken | `Audio/footstep_concrete_*`, `footstep_grass_*` and `footstep_snow_*` (15 files); the carpet and wood steps are not used |
| Conversion | Ogg Vorbis, stereo, 44.1 kHz decoded to 16-bit PCM WAV, mono, 44.1 kHz with ffmpeg (`-ac 1 -ar 44100 -acodec pcm_s16le`) |
| Generated | `Generated/footstep_concrete_wet_*`: each `footstep_concrete_*` with a puddle splash, written by `scripts/mb2.sh generate wet-footsteps` (`tools/mb2/src/wet_footsteps.rs`, fixed seeds, so a rerun reproduces the files). Derived from CC0 material and released the same way |

What plays where (`MarburgumSurface.h` `FootstepSetFor`): asphalt, paving, setts, kerbs and unknown ground
`footstep_concrete`, or `footstep_concrete_wet` on a wet street; grass and earth `footstep_grass`; gravel, river stones and
lying snow `footstep_snow`. One of five variants is picked per step, never the same twice in a row.

The original game's footsteps are not used: they were San Andreas DE banks owned by Rockstar (the original's
`docs/PLACEHOLDER_ASSETS.md`).
