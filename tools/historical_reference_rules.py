#!/usr/bin/env python3
"""Validate archive-derived production rules for 1990s Marburgum.

The Art Reference Archive is evidence, not game art. This module defines the small machine-readable
layer that says what selected archive records are allowed to change in production: art-direction
envelopes, year-aware world-state constraints, and explicit prohibitions against over-reading a
source. It deliberately sits beside historical_overrides.json rather than inside it because most
archive evidence does not target one geospatial building/zone.

The gate cross-checks every ref_id against art_reference/CATALOG.csv, so a renamed or mistyped
citation fails before it may be used as a production constraint. No Unreal import is implied.
"""

from __future__ import annotations

import csv
import json
from pathlib import Path
from typing import Any

PERIOD_START_YEAR = 1990
PERIOD_END_YEAR = 1999
CONFIDENCES = ("high", "medium", "low")

SCHEMA_VERSION = 1
REPO = Path(__file__).resolve().parents[1]
DEFAULT_STORE = REPO / "Data/History/historical_reference_rules.json"
DEFAULT_CATALOG = REPO / "art_reference/CATALOG.csv"

KINDS = ("art_direction", "world_state")
REQUIRED_FIELDS = (
    "id",
    "kind",
    "domain",
    "location",
    "years",
    "confidence",
    "review_required",
    "ref_ids",
    "requirements",
    "prohibitions",
    "review_targets",
)


def _is_year(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and 1000 < value < 3000


def _string_list(value: Any) -> bool:
    return (
        isinstance(value, list)
        and bool(value)
        and all(isinstance(item, str) and item.strip() for item in value)
    )


def load_catalog_ids(path: Path = DEFAULT_CATALOG) -> set[str]:
    """Return every stable ref_id in the canonical archive catalogue."""
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        if not reader.fieldnames or "ref_id" not in reader.fieldnames:
            raise ValueError(f"{path}: no ref_id column")
        ids: set[str] = set()
        for line, row in enumerate(reader, start=2):
            ref_id = (row.get("ref_id") or "").strip()
            if not ref_id:
                raise ValueError(f"{path}:{line}: empty ref_id")
            if ref_id in ids:
                raise ValueError(f"{path}:{line}: duplicate ref_id {ref_id!r}")
            ids.add(ref_id)
    return ids


def validate_store(store: Any, catalog_ids: set[str] | None = None) -> list[str]:
    """Return every rule-contract error rather than stopping at the first."""
    if not isinstance(store, dict):
        return ["historical reference rules: top level is not an object"]

    errors: list[str] = []
    if store.get("schema_version") != SCHEMA_VERSION:
        errors.append(
            "historical reference rules: "
            f"schema_version {store.get('schema_version')!r} is not {SCHEMA_VERSION}"
        )

    game_period = store.get("game_period")
    if not isinstance(game_period, dict):
        errors.append("historical reference rules: game_period must be an object")
    else:
        start = game_period.get("start_year")
        end = game_period.get("end_year")
        if start != PERIOD_START_YEAR or end != PERIOD_END_YEAR:
            errors.append(
                "historical reference rules: game_period must be "
                f"{PERIOD_START_YEAR}-{PERIOD_END_YEAR}"
            )

    source_catalog = store.get("source_catalog")
    if not isinstance(source_catalog, str) or not source_catalog.strip():
        errors.append("historical reference rules: source_catalog must be a non-empty string")

    rules = store.get("rules")
    if not isinstance(rules, list):
        return errors + ["historical reference rules: rules must be an array"]
    if not rules:
        errors.append("historical reference rules: rules must not be empty")

    seen_ids: set[str] = set()
    for index, rule in enumerate(rules):
        where = f"rules[{index}]"
        if not isinstance(rule, dict):
            errors.append(f"{where}: not an object")
            continue

        rid = rule.get("id")
        if not isinstance(rid, str) or not rid.strip():
            errors.append(f"{where}: id must be a non-empty string")
        else:
            where = f"rules[{index}] ({rid})"
            if rid in seen_ids:
                errors.append(f"historical reference rules: duplicate id {rid!r}")
            seen_ids.add(rid)

        for field in REQUIRED_FIELDS:
            if field not in rule:
                errors.append(f"{where}: missing required field {field!r}")

        kind = rule.get("kind")
        if kind not in KINDS:
            errors.append(f"{where}: kind {kind!r} not in {KINDS}")

        for field in ("domain", "location"):
            value = rule.get(field)
            if not isinstance(value, str) or not value.strip():
                errors.append(f"{where}: {field} must be a non-empty string")

        confidence = rule.get("confidence")
        if confidence not in CONFIDENCES:
            errors.append(f"{where}: confidence {confidence!r} not in {CONFIDENCES}")

        if not isinstance(rule.get("review_required"), bool):
            errors.append(f"{where}: review_required must be boolean")

        years = rule.get("years")
        if not isinstance(years, dict):
            errors.append(f"{where}: years must be an object")
        else:
            start = years.get("start")
            end = years.get("end")
            if not _is_year(start) or not _is_year(end):
                errors.append(f"{where}: years.start/end must be integer years")
            elif start > end:
                errors.append(f"{where}: years.start {start} is after years.end {end}")
            elif start < PERIOD_START_YEAR or end > PERIOD_END_YEAR:
                errors.append(
                    f"{where}: years {start}-{end} outside "
                    f"{PERIOD_START_YEAR}-{PERIOD_END_YEAR}"
                )

        for field in ("ref_ids", "requirements", "prohibitions", "review_targets"):
            if not _string_list(rule.get(field)):
                errors.append(f"{where}: {field} must be a non-empty string array")

        refs = rule.get("ref_ids")
        if _string_list(refs):
            if len(refs) != len(set(refs)):
                errors.append(f"{where}: ref_ids contains duplicates")
            if catalog_ids is not None:
                for ref_id in refs:
                    if ref_id not in catalog_ids:
                        errors.append(f"{where}: ref_id {ref_id!r} not found in CATALOG.csv")

    return errors


def load(
    path: Path = DEFAULT_STORE,
    catalog_path: Path = DEFAULT_CATALOG,
) -> dict[str, Any]:
    """Load the committed store, failing closed on schema or provenance drift."""
    store = json.loads(path.read_text(encoding="utf-8"))
    ids = load_catalog_ids(catalog_path)
    errors = validate_store(store, ids)
    if errors:
        raise ValueError("invalid historical reference rules:\n  " + "\n  ".join(errors))
    return store


def main() -> int:
    if not DEFAULT_STORE.exists():
        raise SystemExit(f"missing {DEFAULT_STORE}")
    try:
        store = load()
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        raise SystemExit(str(exc)) from exc
    print(
        f"historical reference rules OK: {len(store['rules'])} rule(s), "
        f"{len(load_catalog_ids())} catalogue ref(s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
