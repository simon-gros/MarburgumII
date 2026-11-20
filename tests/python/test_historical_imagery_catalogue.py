#!/usr/bin/env python3
"""Tests for the historical imagery/map catalogue schema (roadmap section 4.3)."""

from __future__ import annotations

import copy
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import historical_imagery_catalogue as cat  # noqa: E402


class HistoricalImageryCatalogueTests(unittest.TestCase):
    def test_shipped_catalogue_is_valid(self) -> None:
        self.assertGreaterEqual(len(cat.load_catalogue()["products"]), 1)

    def test_missing_field_is_rejected(self) -> None:
        catalogue = copy.deepcopy(cat.load_catalogue())
        del catalogue["products"][0]["resolution_m"]
        self.assertTrue(any("resolution_m" in e for e in cat.validate_catalogue(catalogue)))

    def test_fabricated_nonpositive_number_is_rejected(self) -> None:
        catalogue = copy.deepcopy(cat.load_catalogue())
        catalogue["products"][0]["georeference_accuracy_m"] = -1
        self.assertTrue(cat.validate_catalogue(catalogue))

    def test_duplicate_id_is_rejected(self) -> None:
        catalogue = copy.deepcopy(cat.load_catalogue())
        catalogue["products"].append(copy.deepcopy(catalogue["products"][0]))
        self.assertTrue(any("duplicate" in e for e in cat.validate_catalogue(catalogue)))


if __name__ == "__main__":
    unittest.main()
