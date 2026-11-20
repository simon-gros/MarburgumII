> **Marburgum II migration:** only catalogue/index/manifest metadata is versioned here. No image, map, audio or video binary has been copied. Local copies require explicit source-rights review.

# Art-reference media library

This directory is the production-facing material layer behind `../CATALOG.csv`.

```text
media/
  licensed/
    MANIFEST.csv
    files/
      1990s/
      supporting-pre1990/
      review-needed/
  reference-only/
    INDEX.md
```

## Licensed

`licensed/` contains a deliberately small redistribution-cleared seed set and a reproducible downloader. **1990s material has priority.** Older public-domain photographs are retained only when they are useful for persistent geometry, architectural relationships or long-lived urban features and must not be mistaken for 1990s state evidence.

## Reference-only

Most of the archive remains reference-only because photographs, newspapers, broadcasts, plans and museum/archive material often remain copyrighted or carry non-commercial/uncertain terms. `reference-only/INDEX.md` makes those sources browsable without copying protected media into Git.

## Local research copies

For higher-resolution working material that cannot be redistributed, use:

```text
~/marburgum-data/art-reference/
  photographs/
  video/
  audio/
  documents/
  maps/
  contact_sheets/
```

Name local files from their `ref_id`, keep provenance intact, and never interpret public web access as permission to ship the media with Marburgum.
