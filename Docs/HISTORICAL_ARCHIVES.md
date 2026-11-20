# Historical evidence and art-reference archives

**Status (20 November 2026): METADATA IMPORTED; in-engine use, historical overrides, and source-rights approval are separate pending gates.**

The reboot inherits two complementary historical research collections from the original [Magna Raedarum: Marburgum](https://github.com/simon-gros/magna-raedarum-marburgum). Both are **reference data**, not game assets. Neither a listed photograph nor an orthophoto is implicitly owned, redistributable, georeferenced, dated precisely, or accepted as correct 1990s Maribor scenery.

## 1. Art Reference Archive: imported

- [`art_reference/CATALOG.csv`](../art_reference/CATALOG.csv): **246 records**: the original 236 imported records preserved, with ten additional source-linked research records on 20 November 2026.
- [`art_reference/SOURCES.md`](../art_reference/SOURCES.md): archival institutions, bibliographic leads and discovery links.
- [`art_reference/RIGHTS_AND_PROVENANCE.md`](../art_reference/RIGHTS_AND_PROVENANCE.md): source rights, derivative-art decisions and creator attribution.
- [`art_reference/media/reference-only/INDEX.md`](../art_reference/media/reference-only/INDEX.md): browsable metadata index of **240 reference-only / collection / review** sources; no copyrighted media copied.
- [`art_reference/media/licensed/MANIFEST.csv`](../art_reference/media/licensed/MANIFEST.csv): **21 source manifest entries**, including **12 supplemental Maribor 2026 user-image records** that are not evidence of 1990s state. The manifest describes potential licensed inputs; **no binaries were migrated**. Six entries are marked `auto_fetch=yes`; all others remain nonautomatic.
- [`Data/History/historical_reference_rules.json`](../Data/History/historical_reference_rules.json): **five** original derived art-direction/world-state rules, all cross-referenced to catalogue IDs. These are authoring/review constraints only, not a runtime enforcement system or approved texture licence.

Source: original repository `main` at `f8ae0827bb64dd3944375ea2dae3862567e5668f` (art catalogue blob `8a00d79afeb3ad578d1fa555a1be144d01773f73`). Original policy and reference notes are preserved, with explicit import notes and corrected Marburgum II file links.

## 2. Historical imagery and maps: imported as leads

- [`Data/History/historical_imagery_catalogue.json`](../Data/History/historical_imagery_catalogue.json): **six metadata-only leads** for GURS 1997–2001 black-and-white orthophotos, 2006 GURS colour orthophotography and digitized historical cadastral plans (eZKN).
- All six records are intentionally `verified: false`. Accuracy, map coverage, source licence, actual sheet dates and usable presence/absence evidence remain unknown where the source says unknown. **No raster imagery, georeferenced overlays or cadastral geometry was imported.**
- Provenance: original repository's [unmerged PR #329](https://github.com/simon-gros/magna-raedarum-marburgum/pull/329), branch `copilot/implement-historical-imagery-catalogue` as inspected on 20 November 2026. Import into Marburgum II does **not** merge the original PR into the original project.

## 20 November 2026 — additional verified archival leads

The Marburgum II follow-up added **10 item-level historical/art research records** (catalogue **246**) and **3 collection-level cartographic leads** (historical imagery catalogue **6**), with rights and unverified fields retained. Priority institutional sources:

- [Mariborska knjižnica / Pokrajinski arhiv Maribor: 1992 Otrok in umetnost opening photograph](https://www.kamra.si/mm-elementi/otvoritev-30-festivala-otrok-in-umetnost/), official [1992 invitation/programme](https://www.kamra.si/mm-elementi/vabilo-s-programom/) and [1995–96 invitation/programme](https://www.kamra.si/mm-elementi/vabilo-s-programom-2/). The photographic source identifies **Dvorana Union** and archival fond; the invitation records are evidence of event and printed design, not comprehensive venue geometry.
- [Pokrajinski arhiv Maribor: Večer and Delo notices on Jeklotehna/Merkur, January 1993](https://www.kamra.si/mm-elementi/clanki-vecer-delo/). They establish dated retail plans, not the completion or appearance of any planned works.
- [Mariborska knjižnica / JSKD Maribor: sixth Odprta Plesna Scena on 21 November 1997](https://www.kamra.si/mm-elementi/plesna-skupina-magra-pred-nastopom-copy-5-copy-copy-copy-copy-copy-copy-2-copy-copy-copy-copy-copy-copy-copy-copy-copy-copy-copy/), recording the II. gimnazija venue and dance programme.
- [Univerzitetna knjižnica Maribor: Bogomir Čerin's 5 April 1992 Orehova vas motorsport image](https://www.kamra.si/mm-elementi/bogo-cerin-dirka/) for regional sport, motorcycles, clothing and crowds. Orehova vas is not central Maribor.
- [Mariborska knjižnica's bibliography](https://www.kamra.si/digitalne-zbirke/viri-in-literatura-48/) identifies 1998 and 1999 architectural research/publications about earlier Maribor built heritage. These are **bibliographic leads** only and cannot establish 1990s facade changes.
- [Official GURS remote-sensing account](https://www.e-prostor.gov.si/podrocja/drzavni-topografski-sistem/daljinsko-zaznavanje/) identifies CAS historical aerial surveys and early 1994–95 DOF5 production; [Arhiv Republike Slovenije's public catalogue](https://podatki.gov.si/dataset/digitalizirano-arhivsko-gradivo-starih-katastrov-si-as-176-si-as-177-si-as-178-si-as-179-si-as-180-s?resource_id=ce4d9292-6adf-46ec-bffd-25bc2ef6fa3e) identifies the **SI AS 177** Franciscan cadastre of Styria (1823–1869). Individual Maribor sheets, scan permissions and modern control-point accuracy remain unverified; the 19th-century maps are not 1990s ground truth.

All item-linked protected media remain reference-only. None of these additions is a new licensed game texture, downloaded raster, georeferenced historical overlay or approved UE world modification.

## Research / asset / world-state firewall

1. **Evidence** — catalogue a source with stable `ref_id`, provenance, location, date confidence, source URL and rights category.
2. **Ruling** — validate time, place, competing evidence and specific permissible inferences; distinguish factual world-state changes from visual inspiration.
3. **Rights** — clear any asset material actually reproduced in game or marketing under the relevant licence. `CC-BY-NC`, unknown, copyrighted and conflicting records are research-only until separately cleared.
4. **Production** — attach `ref_id` and any approved material-source permissions to a model/texture/scene decision. Microsoft TRELLIS production uses appropriately licensed inputs and the separately documented optimization/approval process; archive photographs do not automatically qualify.
5. **Marburgum II implementation** — explicitly author reviewed period-specific changes into the relevant world, facade, prop, signage, street, mission or weather system. No catalogue record is auto-spawned, auto-applied to Unreal or proof that the current 2014/2022/2026-derived geography depicts 1990s Maribor.
6. **Acceptance** — check visible results, source/asset manifests, licence evidence, reproducible cooking and **fresh owner-playtested Client/Server packages** before declaring the feature done.

## Validation and safe use

Run from repository root:

```bash
python3 tools/validate_historical_archives.py
python3 -m unittest discover -s tests/python -p 'test_historical_*.py' -v
```

GitHub Actions [historical-archives.yml](../.github/workflows/historical-archives.yml) validates schema, unique stable IDs, rule citations, imagery metadata, source manifest and absence of committed media binaries for catalogue-related changes.

The optional [reference downloader](../tools/fetch_art_reference.py) only processes explicitly `auto_fetch=yes` manifest records and writes to `~/marburgum-data/art-reference/licensed/` by default, outside Git. Downloading does not approve an image for inclusion in commercial builds. Restricted media and archival rasters are never included automatically.

## Next production actions

- Curate the highest-value evidence for Lent's shopfronts, bridge details, architecture, night lighting, clothing and 1990s signage.
- Verify all six imagery leads' actual coverage, per-sheet dates, georeferencing and usage rights before using them to draw historical corrections.
- Add reviewable explicit evidence-to-world override data with stable IDs, measured coordinates, dating uncertainties and renderer/test evidence.
- Integrate approved historic material into environment generation, interior art and asset-review pipelines **without relaxing** the current visual quality, graphics, performance or personal-playtest gates.

See [visual backlog](VISUAL_QUALITY_BACKLOG.md), [graphics roadmap](GRAPHICS_ROADMAP.md), [legacy parity](LEGACY_PARITY.md) and [development roadmap](ROADMAP.md). Historical catalogues are a durable research foundation; period-correct city recreation is still pending.
