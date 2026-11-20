> **Marburgum II archival migration (20 November 2026).** This is the metadata/research catalogue inherited from [the original Marburgum](https://github.com/simon-gros/magna-raedarum-marburgum/tree/main/art_reference). It is not an asset import, an in-engine historical correction, or permission to distribute archival media. See [Marburgum II archival integration](../Docs/HISTORICAL_ARCHIVES.md). The catalogue’s original reference IDs and rights fields are preserved.

# Marburgum Art Reference Archive

**20 November 2026 research expansion:** 246 catalogued references (236 inherited unchanged plus ten new source-linked leads). Three additional historical imagery/map acquisition leads bring the separate [Marburgum II imagery catalogue](../Data/History/historical_imagery_catalogue.json) to six. Research metadata only; see [historical archive audit](../Docs/HISTORICAL_ARCHIVES.md).

This directory is the canonical **historical visual-reference library** for *Magna Raedarum: Marburgum*. Its purpose is to collect, index, evaluate and preserve references for what Maribor actually looked, sounded and felt like during **1990–1999**, with particular emphasis on material that can guide terrain reconstruction, architectural modelling, props, vehicles, signage, vegetation, lighting, clothing, interiors and general art direction.

The archive is an **evidence layer**, not a folder of game-ready assets. A photograph may establish the shape of a shopfront, road markings, paving, facade colour, street furniture, parked-car density or the weathering of a retaining wall without granting any right to copy the photograph itself into the game. Every reference therefore carries date, location, source, rights and production-use metadata.

## Canonical production rule

Before a landmark, important street, terrain feature, historically sensitive building, period-specific prop or representative environment kit is finalized, its artist/modeller should check this archive first. The best available 1990s evidence should override generic "retro" assumptions.

Reference priority is:

1. **Dated 1990–1999 primary evidence from Maribor itself** — photographs, video, film, maps, plans, newspaper imagery, advertisements, catalogues, recordings and contemporary documents.
2. **Undated but securely 1990s Maribor material**, with uncertainty recorded.
3. **Late-1980s or early-2000s Maribor evidence** when the feature demonstrably persisted into the target decade.
4. **1990s evidence from nearby north-eastern Slovenia / former Yugoslav and Central-European contexts** for generic objects or material culture when no Maribor-specific source exists.
5. **Modern evidence** only for geometry or features known not to have changed. Modern imagery must never be used to infer a 1990s state without corroboration.

A current orthophoto, LiDAR return or OSM footprint can say where something is now; this archive is where the project records evidence for **what it was in the target decade**.

## Directory contents

- `CATALOG.csv` — machine-readable master index of individual references and collection-level leads.
- `SOURCES.md` — curated directory of archives, libraries, museums, media collections and discovery portals.
- `RIGHTS_AND_PROVENANCE.md` — rules for copyright, attribution, local storage and derivative art.
- `media/README.md` — recommended local binary-media layout. Large or restricted media files are not committed to Git.
- `.gitignore` — prevents accidental publication of locally collected archival binaries.

The actual working media library should normally live outside the Git repository at:

```text
~/marburgum-data/art-reference/
```

The Git repository stores the **catalogue, provenance and research trail**. A binary may be committed only when its licence clearly permits redistribution and doing so is useful; even then, attribution and source metadata must remain in the catalogue.

## Required catalogue fields

Every useful reference should eventually have:

- stable `ref_id`;
- title / short description;
- date or date range, including whether it is exact, approximate or unknown;
- medium (photograph, video, audio, newspaper, map, plan, advertisement, brochure, etc.);
- location, street/district and coordinates where known;
- subject tags;
- intended production uses;
- institution / collection;
- creator or photographer where known;
- source URL and archival identifier/signature where available;
- rights/licence status;
- local-storage status;
- priority;
- notes on what the reference actually proves.

Do not write "1990s" when the source only proves "around 1990". Record uncertainty explicitly.

## Production-use tags

Use one or more of these in `production_uses`:

`terrain`, `roads`, `paving`, `architecture`, `facades`, `roofs`, `bridges`, `street_furniture`, `signage`, `retail`, `industrial`, `rail`, `vehicles`, `vegetation`, `river`, `interiors`, `props`, `characters`, `clothing`, `crowds`, `lighting`, `weather`, `colour_palette`, `audio`, `narrative`.

## Subject coverage targets

The archive should gradually cover the complete playable city rather than only famous landmarks. Priority acquisition subjects are:

- Lent, Stari most, Titov most, Železniški most and the Drava banks;
- Glavni trg, Grajski trg, Slomškov trg, Gosposka, Poštna, Koroška, Partizanska and other central streets;
- Tabor, Magdalena, Studenci, Pobrežje, Melje, Tezno, Nova vas, Borova vas, Radvanje and other residential districts;
- TAM, Metalna, railway works, warehouses, industrial yards and post-industrial transition;
- main railway station, bus station, bus stops, taxis, parking, road markings and vehicle mix;
- Ljudski vrt, Viole/NK Maribor environments, sports halls and public-event spaces;
- kiosks, bakeries, groceries, bars, cafés, tobacconists, markets, shop windows and period advertising;
- apartment blocks, detached houses, courtyards, stairwells, kitchens, offices, workshops and school interiors;
- street lamps, bins, benches, bollards, traffic lights, phone boxes, utility cabinets, hydrants and signage;
- vegetation maturity, embankments, vacant lots, gardens, railway weeds, riverbank growth and seasonal state;
- clothing, hairstyles, bags, umbrellas, bicycles, prams and ordinary pedestrian behaviour;
- night lighting, winter scenes, rain, snow, fog and wet-street response;
- analogue media, radio/television, newspapers, posters, cassettes, VHS, cameras and public notices.

## Planned literature and book bibliography — research backlog

**Pending collection; no book research or title verification has been performed for this note.** Expand the art-reference archive with a dedicated, searchable **literature/bibliography section** for books that help reconstruct Maribor's 1990s history, culture, everyday life, places, people and material environment, and for books that can inform fictional storylines and mission design.

Include both **works published during 1990–1999 that depict or discuss Maribor** and **works published before or after the decade that document, recall or analyze Maribor in the 1990s**. Coverage should include local-history and documentary books, monographs, memoirs, oral histories, cultural and industrial histories, illustrated books, guides, fiction, novels, short stories and other relevant literary accounts. A book does **not** need to be available digitally: catalogue print-only titles and library/archive leads as references rather than implying their contents have been read.

When this collection is researched, record a bibliographic citation (author, title, publisher, publication year, edition/ISBN where available), **period described versus year published**, subject/district, type of source (contemporary documentation, later recollection, scholarship or fiction), library holding or catalog URL (for example COBISS/UKM, once verified), digital availability or physical access, rights, confidence and possible uses for world reconstruction, narrative, dialogue or missions. Distinguish historical evidence from fictional depiction or fallible memory. Link verified individual records to the existing `CATALOG.csv` using stable `ref_id` values, or define a compatible dedicated bibliography index before bulk entry.

**Purpose:** anchor the game's fragmented, place-based storytelling in actual Maribor cultural and material history without assuming every reference belongs to the central Raeda plot or attributing fictional events to real people.

## Reference-to-model workflow

1. **Discover** — find a source or collection and add a catalogue row immediately, even before downloading anything.
2. **Verify** — confirm date, place, photographer/creator, institution and whether the image is genuinely Maribor.
3. **Classify** — tag the physical facts it can support. A crowd photograph may still be excellent evidence for kerbs, lamps, parked cars and facade condition.
4. **Rights-check** — decide whether it is reference-only or may be redistributed / transformed under its licence.
5. **Store locally** — if permitted for research, save a local copy under `~/marburgum-data/art-reference/` using the `ref_id`; never rely on a random filename.
6. **Model from evidence** — use multiple references where possible, especially for dimensions, hidden sides and colour.
7. **Record derivation** — asset notes or commits for historically specific work should mention the relevant `ref_id` values.
8. **Review in engine** — compare screenshots against the reference set at similar viewpoints; correct silhouette, materials, density and period-specific details rather than chasing photographic pixel identity.

## Evidence confidence

For important assets, record one of these in notes or downstream asset metadata:

- **A — direct:** exact place/object, dated within 1990–1999.
- **B — strong:** exact place/object, near-period or securely undated within the decade.
- **C — contextual:** same district / object type / regional culture, useful for generic details.
- **D — speculative:** weak or indirect lead; never sufficient by itself for a historical claim.

Historical confidence is separate from visual quality. A blurred 1991 snapshot may be stronger evidence than a pristine 2025 photograph.

## Initial seeded material

The catalogue is already seeded with high-value 1990s leads: UKM/Kamra's Stari most 1990–91 material; Bogomir Čerin's 1991 Maribor street photography; Danilo Škofič's June 1991 Mlinska/bus-station photograph from Večer documentation; the UKM/Večer Pekre 1991 collection; RTV Slovenia's year-indexed independence archive and Maribor reporting; a December 1990 Metalna workers' strike photograph referenced to the Večer archive; UKM, Pokrajinski arhiv Maribor, MNOM, dLib and Pokrajinski muzej Maribor as collection-level acquisition targets.

These are **starting points**, not a complete archive. The goal is to turn archival discovery into a continuous art-production input.

## Relationship to other project documentation

This archive provides historical evidence to the project's art and modelling rules. When a reference has been reviewed into a concrete production constraint, that downstream decision may be mirrored in `Data/History/historical_reference_rules.json`. Those rules must cite stable archive `ref_id` values, preserve year/location scope, and state both what the evidence authorizes and what it does **not** prove. They are validated back against this catalogue and imported into Unreal's historical-context review assets; they never change the underlying source rights.

This archive does not replace:

- `docs/NINETIES_VISUAL_ART_DIRECTION_ROADMAP.md`;
- `docs/NINETIES_AUTHENTICITY_WORLD_STATE_ROADMAP.md`;
- `docs/3D_MODELING_ASSET_AESTHETICS_ROADMAP.md`;
- `docs/GEOMETRIC_CORRECTION_ROADMAP.md`;
- `docs/MARBURGUM_FICTIONALIZATION_ROADMAP.md`;
- `docs/PLAYABLE_MAP_RECONSTRUCTION_ROADMAP.md`.

Those documents define **what to build and how**. This directory records **what the historical evidence says it should look like**.
