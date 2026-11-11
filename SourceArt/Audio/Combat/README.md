# Combat impact sounds

Source files for the blows of unarmed fighting ([ADR-041](../../../Docs/DECISIONS.md)), imported into
`/Game/Audio/Combat` by `scripts/create-combat-content.sh audio`.

| Item | Value |
| --- | --- |
| Pack | Kenney **Impact Sounds** 1.0 (created 2019-12-19), 130 sounds |
| Author | Kenney (www.kenney.nl) |
| Licence | Creative Commons Zero (CC0 1.0): `KenneyImpactSounds/License.txt` is the pack's own file, unchanged |
| Source | https://kenney.nl/assets/impact-sounds, `kenney_impact-sounds.zip` fetched 2026-11-11, SHA-256 `029d734af1582474edf3a694d1b0cebc97c1c152f2f39fa34d4c2bafc5de77f8` |
| Taken | the 20 `Audio/impactPunch_*` and `Audio/impactSoft_*` files; the pack's other 110 sounds (glass, metal, wood, plate, …) are not part of the game |
| Conversion | The pack ships Ogg Vorbis, stereo, 44.1 kHz. Unreal imports WAV, so each was decoded to 16-bit PCM WAV, mono, 44.1 kHz with ffmpeg (`-ac 1 -ar 44100 -acodec pcm_s16le`). The samples of every WAV here equal that decode of the pack's file. |

What plays when (`MarburgumCombatContract.h`): a landed jab `impactPunch_medium_*`, a landed committed swing
`impactPunch_heavy_*`, a blow taken on a guard `impactSoft_medium_*`, a body going down `impactSoft_heavy_*`; one of
the five variants is picked per blow. A miss is silent.

The original game's fight sounds are not used: they were GTA III / San Andreas placeholders owned by Rockstar (the
original's `docs/PLACEHOLDER_ASSETS.md`).
