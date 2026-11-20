# Rights, Provenance and Storage Policy

The art-reference archive exists to improve historical accuracy without creating copyright or provenance problems.

## Reference is not a licence

Being able to view or download an archival photograph does **not** mean it may be redistributed, shipped with the game, used as a texture, printed in marketing, or copied into a decal.

The safe default is:

> **Study the physical facts shown by the source; do not reproduce the source itself unless the licence explicitly permits the intended use.**

A modeller can use a protected photograph to understand the geometry of a 1993 kiosk or the arrangement of bollards on a street. That is different from cropping the photograph into an in-game texture.

## Rights classes

Record one of these (or a more precise licence string) in `CATALOG.csv`:

- `public-domain` — redistribution normally permitted; still preserve provenance.
- `CC-BY` — permitted subject to attribution and licence terms.
- `CC-BY-SA` — attribution/share-alike obligations apply.
- `CC-BY-NC` — useful for research/reference, but **do not assume it can be included in a commercial game or commercial promotional material**.
- `copyrighted-reference-only` — visual research only unless permission is obtained.
- `rights-vary-by-item` — collection page only; inspect each item.
- `unknown` — treat as copyrighted until clarified.

If the source institution gives more specific contractual terms, those terms control.

## Mandatory provenance

For every downloaded/researched item preserve, where available:

- `ref_id`;
- title/description;
- creator/photographer;
- date;
- source institution;
- collection/fonds;
- archival signature, inventory number, URN or COBISS ID;
- stable URL;
- licence / rights holder;
- date accessed;
- local filename;
- checksum for irreplaceable local copies.

Never strip creator/institution data because the image is "only a reference".

## Local binary storage

Large archival binaries should normally live outside Git:

```text
~/marburgum-data/art-reference/
  photographs/
  video/
  audio/
  documents/
  maps/
  contact_sheets/
```

Recommended filename:

```text
<ref_id>__short-description.ext
```

Example:

```text
MAR-1991-MLINSKA-UNDERPASS-001__mlinska-bus-station.jpg
```

The catalogue row is the authoritative link between a local file and its archival source.

## Git policy

Do not commit:

- restricted archival downloads;
- press photographs with ordinary copyright;
- newspaper scans;
- broadcast video/audio;
- personal photographs supplied for reference only;
- high-resolution research copies whose licence does not permit redistribution.

A binary may be committed only when its redistribution rights have been checked and the project benefits from having the file versioned. Where attribution is required, add it to both catalogue metadata and any relevant asset documentation.

## Derivative assets

For every historically specific game asset, distinguish:

1. **evidence source** — the reference that proves form/state/date;
2. **asset source** — any material actually incorporated into the game asset.

A protected photograph may be an evidence source without becoming an asset source.

Do not:

- photo-project copyrighted archival images onto meshes for final game textures;
- copy a protected poster, newspaper page, commercial logo or advertisement pixel-for-pixel into the shipped game without clearance;
- infer permission from low resolution, age, public web access or lack of a visible watermark.

Prefer original re-creation, fictionalised period-equivalent design, public-domain/appropriately licensed source material, or permission from the rights holder.

## CC BY-NC material

Several useful Kamra/UKM/Večer-linked items identify **CC BY-NC** status. Treat these as excellent research references. Because Marburgum may ultimately be distributed commercially, do not build a release dependency on reproducing NC-licensed material.

## Contact and permission trail

When permission is requested from an archive, photographer or rights holder, add the outcome to the catalogue notes and keep the correspondence outside the public repository if it contains personal information. Record the scope precisely: internal reference, public documentation, promotional use, in-game reproduction, modification, commercial distribution, territory and duration can all differ.

## Historical transformation and fictionalisation

The project intentionally fictionalises Maribor into Marburgum after evidence-based reconstruction. Archival references should therefore support:

- plausible geometry;
- period materials;
- object density;
- spatial logic;
- regional visual vocabulary.

They should not force the game to reproduce every protected sign, advertisement or branded surface. Where rights are unclear, preserve the physical role and period character while designing an original fictional equivalent.

## Quality control

Before an archival-derived asset is accepted, reviewers should be able to answer:

- What `ref_id` entries support it?
- Are those references actually 1990s Maribor evidence?
- What is direct observation versus artistic interpolation?
- Was any source image content copied into the asset?
- If yes, what licence/permission permits that use?
- Are required credits tracked?
- Would the asset still be defensible if the original web page disappeared?

If those questions cannot be answered, provenance is incomplete.
