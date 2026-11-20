#!/usr/bin/env python3
"""Regression tests for the 20-Nov-2026 historical archive research expansion.

These tests validate catalogue metadata only, never third-party image download rights.
"""
from __future__ import annotations

import csv
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / "art_reference" / "CATALOG.csv"
MAPS = ROOT / "Data" / "History" / "historical_imagery_catalogue.json"
INDEX = ROOT / "art_reference" / "media" / "reference-only" / "INDEX.md"

NEW_ART_IDS = {
    "MAR-1992-UNION-OTROK-OPENING-001",
    "MAR-1992-OTROK-FESTIVAL-PROGRAM-001",
    "MAR-1992-OTROK-KRAMBERGER-001",
    "MAR-1995-96-OTROK-FESTIVAL-PROGRAM-001",
    "MAR-1993-JEKLOTEHNA-MERKUR-PRESS-001",
    "MAR-1997-ODPRTA-PLESNA-SCENA-006",
    "MAR-1992-OREHOVA-VAS-DIRKA-001",
    "MAR-1998-FRIEDRIGER-SCHOOL-STUDY-001",
    "MAR-1999-FRIEDRIGER-UGM-EXHIBITION-001",
    "MAR-1994-CERNIGOJ-MUSEUM-EXHIBITION-001",
}
NEW_MAP_IDS = {
    "gurs-cas-1975-2005-historical-air-photos",
    "gurs-dof5-initial-1994-1995",
    "si-as-177-styrian-franciscan-cadastral-1823-1869",
}


class HistoricalResearchExpansion(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        with ART.open(newline="", encoding="utf-8-sig") as fh:
            cls.art = list(csv.DictReader(fh))
        cls.maps = json.loads(MAPS.read_text(encoding="utf-8"))["products"]

    def test_new_art_records_have_unique_ids_and_review_only_rights(self) -> None:
        by_id = {x["ref_id"]: x for x in self.art}
        self.assertEqual(len(by_id), len(self.art), "duplicate art reference IDs")
        self.assertGreaterEqual(len(self.art), 246)
        self.assertTrue(NEW_ART_IDS <= by_id.keys())
        for ref_id in NEW_ART_IDS:
            record = by_id[ref_id]
            self.assertTrue(record["source_url"].startswith("https://"), ref_id)
            self.assertIn(record["rights_status"], {"CC-BY-NC", "copyrighted-reference-only"}, ref_id)
            self.assertIn(record["repository_status"], {"catalogued-link", "bibliographic-lead", "catalogued-collection"}, ref_id)
            self.assertTrue(record["date_start"] and record["date_end"])
            self.assertTrue(record["notes"].strip())

    def test_new_restricted_items_are_indexed(self) -> None:
        idx = INDEX.read_text(encoding="utf-8")
        self.assertIn("246 catalogue records", idx)
        self.assertIn("240 remain reference-only", idx)
        for ref_id in NEW_ART_IDS:
            self.assertIn(ref_id, idx)

    def test_new_map_leads_do_not_invent_georeferencing(self) -> None:
        by_id = {x["id"]: x for x in self.maps}
        self.assertEqual(len(by_id), len(self.maps))
        self.assertGreaterEqual(len(self.maps), 6)
        self.assertTrue(NEW_MAP_IDS <= by_id.keys())
        for key in NEW_MAP_IDS:
            item = by_id[key]
            self.assertFalse(item["verified"], key)
            self.assertIsNone(item["georeference_accuracy_m"], key)
            self.assertIsNone(item["resolution_m"], key)
            self.assertEqual(item["license"], "unknown", key)
            self.assertTrue(all(url.startswith("https://") for url in item["source_urls"]), key)

    def test_older_cadastre_not_treated_as_nineties_proof(self) -> None:
        item = next(x for x in self.maps if x["id"] == "si-as-177-styrian-franciscan-cadastral-1823-1869")
        self.assertEqual(item["date_range"], {"start": "1823", "end": "1869"})
        self.assertIn("not evidence", item["access"].lower())


if __name__ == "__main__":
    unittest.main()
