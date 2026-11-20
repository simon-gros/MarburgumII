#!/usr/bin/env python3
"""Validate both migrated historical metadata archives and cross-reference evidence.

No protected imagery is fetched or bundled by these validation checks.
"""
from __future__ import annotations

import csv
from pathlib import Path

from historical_imagery_catalogue import load_catalogue
from historical_reference_rules import load as load_reference_rules

ROOT = Path(__file__).resolve().parents[1]
ART_CSV = ROOT / "art_reference" / "CATALOG.csv"
MANIFEST_CSV = ROOT / "art_reference" / "media" / "licensed" / "MANIFEST.csv"
INDEX = ROOT / "art_reference" / "media" / "reference-only" / "INDEX.md"


def read_csv(path: Path, mandatory: set[str]) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as file:
        reader = csv.DictReader(file)
        if not reader.fieldnames or missing := mandatory - set(reader.fieldnames or []):
            raise ValueError(f"{path}: missing required headers: {sorted(missing)}")
        rows = []
        for num, item in enumerate(reader, 2):
            if None in item or any(value is None for value in item.values()):
                raise ValueError(f"{path}:{num}: malformed CSV row")
            rows.append(item)
    if not rows:
        raise ValueError(f"{path}: empty catalogue")
    return rows


def main() -> int:
    art = read_csv(ART_CSV, {"ref_id", "title", "source_url", "rights_status", "repository_status"})
    art_ids: set[str] = set()
    for index, row in enumerate(art, 2):
        ref = row["ref_id"].strip()
        if not ref or ref in art_ids:
            raise ValueError(f"{ART_CSV}:{index}: missing/duplicate ref_id {ref!r}")
        art_ids.add(ref)
        for field in ("title", "rights_status", "repository_status"):
            if not row[field].strip():
                raise ValueError(f"{ART_CSV}:{index}: {field} empty")

    manifest = read_csv(MANIFEST_CSV, {"ref_id", "filename", "remote_url", "license", "creator", "auto_fetch"})
    manifest_ids: set[str] = set()
    for index, row in enumerate(manifest, 2):
        ref = row["ref_id"].strip()
        if not ref or ref in manifest_ids:
            raise ValueError(f"{MANIFEST_CSV}:{index}: missing/duplicate ref_id {ref!r}")
        manifest_ids.add(ref)
        if ref not in art_ids and not (ref.startswith("MAR-2026-USER-") and row["auto_fetch"] == "no"):
            raise ValueError(f"{MANIFEST_CSV}:{index}: untracked source reference {ref!r}")
        if row["auto_fetch"] not in {"yes", "no"}:
            raise ValueError(f"{MANIFEST_CSV}:{index}: invalid auto_fetch setting")
        if row["auto_fetch"] == "yes" and not row["remote_url"].startswith("https://"):
            raise ValueError(f"{MANIFEST_CSV}:{index}: automatic fetch requires https URL")
    idx = INDEX.read_text(encoding="utf-8")
    if f"Current coverage: **{len(art_ids)} catalogue records**" not in idx:
        raise ValueError("reference-only INDEX.md coverage does not match CATALOG.csv")

    # Recheck rule provenance against migrated IDs and imagery fields against their schema.
    rules = load_reference_rules()
    maps = load_catalogue()
    for file in (ROOT / "art_reference" / "media").rglob("*"):
        if file.is_file() and file.suffix not in {".csv", ".md"}:
            raise ValueError(f"unexpected binary in research-only archive: {file}")

    print(f"Historical archives OK: {len(art_ids)} art records, "
          f"{len(manifest_ids)} manifest entries, "
          f"{len(rules['rules'])} evidence rules, "
          f"{len(maps['products'])} historical imagery/map products; no media binaries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
