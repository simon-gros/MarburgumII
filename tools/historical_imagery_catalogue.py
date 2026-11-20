#!/usr/bin/env python3
"""Catalogue of historical orthophoto/map products covering Maribor.

The original Marburgum GEOSPATIAL_DATA_ENRICHMENT_ROADMAP.md section 4.3 asks for every historical raster to
record its date range, georeference accuracy, coverage, resolution, license, known
distortions and whether absence/presence claims are reliable at that scale. The records
live in `Data/History/historical_imagery_catalogue.json` and are metadata only: the rasters
themselves stay outside Git.

Unknown facts must be explicit (`null` or `"unknown"`); this validator rejects a record that
omits a field, so an unverified value cannot be silently absent or fabricated.

    python3 tools/historical_imagery_catalogue.py
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

SCHEMA_VERSION = 1

REPO = Path(__file__).resolve().parents[1]
DEFAULT_CATALOGUE = REPO / "Data/History/historical_imagery_catalogue.json"

PRODUCT_KINDS = ("orthophoto", "cadastral_map", "topographic_map", "aerial_photo", "other")

# Fields that must be present; a value may be null / "unknown" when not verified.
NULLABLE_NUMBER_FIELDS = ("georeference_accuracy_m", "resolution_m")
UNKNOWABLE_TEXT_FIELDS = ("coverage", "license", "known_distortions", "absence_presence_reliability")
REQUIRED_TEXT_FIELDS = ("id", "title", "publisher", "access")


def _is_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def validate_product(product: Any, index: int) -> list[str]:
    """Return the problems with one catalogue record (empty when valid)."""
    where = f"products[{index}]"
    if not isinstance(product, dict):
        return [f"{where}: must be an object"]
    errors: list[str] = []
    for field in REQUIRED_TEXT_FIELDS + UNKNOWABLE_TEXT_FIELDS:
        value = product.get(field)
        if not isinstance(value, str) or not value.strip():
            errors.append(f"{where}.{field}: required non-empty string (use \"unknown\")")
    if product.get("kind") not in PRODUCT_KINDS:
        errors.append(f"{where}.kind: must be one of {PRODUCT_KINDS}")
    for field in NULLABLE_NUMBER_FIELDS:
        if field not in product:
            errors.append(f"{where}.{field}: required (use null when unknown)")
        elif product[field] is not None and (not _is_number(product[field]) or product[field] <= 0):
            errors.append(f"{where}.{field}: must be a positive number or null")
    date_range = product.get("date_range")
    if not isinstance(date_range, dict) or set(date_range) != {"start", "end"}:
        errors.append(f"{where}.date_range: must be an object with start and end (null when unknown)")
    else:
        for key in ("start", "end"):
            if date_range[key] is not None and not isinstance(date_range[key], str):
                errors.append(f"{where}.date_range.{key}: must be a string or null")
        start, end = date_range["start"], date_range["end"]
        if isinstance(start, str) and isinstance(end, str) and start > end:
            errors.append(f"{where}.date_range: start is after end")
    urls = product.get("source_urls")
    if not isinstance(urls, list) or not all(isinstance(u, str) and u for u in urls):
        errors.append(f"{where}.source_urls: must be a list of non-empty strings")
    if not isinstance(product.get("verified"), bool):
        errors.append(f"{where}.verified: must be a boolean")
    return errors


def validate_catalogue(catalogue: Any) -> list[str]:
    """Return every problem in a loaded catalogue (empty when valid)."""
    if not isinstance(catalogue, dict):
        return ["catalogue must be an object"]
    errors: list[str] = []
    if catalogue.get("schema_version") != SCHEMA_VERSION:
        errors.append(f"schema_version must be {SCHEMA_VERSION}")
    products = catalogue.get("products")
    if not isinstance(products, list):
        return errors + ["products must be a list"]
    seen: set[str] = set()
    for index, product in enumerate(products):
        errors.extend(validate_product(product, index))
        pid = product.get("id") if isinstance(product, dict) else None
        if isinstance(pid, str):
            if pid in seen:
                errors.append(f"products[{index}].id: duplicate id {pid!r}")
            seen.add(pid)
    return errors


def load_catalogue(path: Path | None = None) -> dict[str, Any]:
    """Load and validate the catalogue; raise ValueError listing every problem."""
    path = path or DEFAULT_CATALOGUE
    catalogue = json.loads(path.read_text(encoding="utf-8"))
    errors = validate_catalogue(catalogue)
    if errors:
        raise ValueError(f"{path}: " + "; ".join(errors))
    return catalogue


def main() -> int:
    try:
        catalogue = load_catalogue()
    except ValueError as exc:
        print(exc, file=sys.stderr)
        return 1
    print(f"historical imagery catalogue OK: {len(catalogue['products'])} products")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
