# Licensed media

This directory contains the **metadata manifest** for a candidate redistributable reference seed set. **No binary media files were imported into Marburgum II.** The listed rights are provenance claims requiring review before new downloads or use in production.

`MANIFEST.csv` is authoritative. Rows marked `auto_fetch=yes` have sufficiently clear redistribution status and a remote URL for automatic retrieval. Rows marked `auto_fetch=no` are not fetched automatically; they may need rights review or may be local-only user-supplied files with no remote source URL.

Downloaded files live under:

```text
files/
  1990s/                 # direct target-decade material
  2026/                  # user-supplied Maribor photographs, cleared by their creator under CC BY 4.0
  supporting-pre1990/    # older evidence used only for persistent geometry/material culture
  review-needed/         # never auto-fetched
```

Run:

```bash
python tools/fetch_art_reference.py
```

The script downloads only `auto_fetch=yes` rows, follows the catalogue IDs for filenames, and writes SHA-256 checksums. It does **not** turn a reference photograph into a game asset licence. The original creator, source and licence remain attached to every file through this manifest.

For 1991 Večer/Kamra photographs that Wikimedia Commons currently presents under CC BY-SA while the Kamra-derived catalogue records CC BY-NC, the archive takes the conservative approach: preserve the link and metadata, but do not auto-fetch or redistribute until the discrepancy is resolved.
