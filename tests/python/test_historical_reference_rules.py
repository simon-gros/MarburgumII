#!/usr/bin/env python3
"""Tests for archive-derived historical production rules."""

from __future__ import annotations

import copy
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import historical_reference_rules as rules  # noqa: E402


def valid_rule() -> dict:
    return {
        "id": "test-rule",
        "kind": "art_direction",
        "domain": "signage",
        "location": "Maribor",
        "years": {"start": 1990, "end": 1990},
        "confidence": "high",
        "review_required": False,
        "ref_ids": ["MAR-TEST-001"],
        "requirements": ["use the evidenced format"],
        "prohibitions": ["do not over-read the source"],
        "review_targets": ["signage"],
    }


def valid_store() -> dict:
    return {
        "schema_version": 1,
        "game_period": {"start_year": 1990, "end_year": 1999},
        "source_catalog": "art_reference/CATALOG.csv",
        "rules": [valid_rule()],
    }


class HistoricalReferenceRuleContract(unittest.TestCase):
    def test_minimal_valid_rule(self):
        self.assertEqual(rules.validate_store(valid_store(), {"MAR-TEST-001"}), [])

    def test_missing_catalogue_ref_fails(self):
        problems = rules.validate_store(valid_store(), {"MAR-OTHER-001"})
        self.assertTrue(any("not found in CATALOG.csv" in problem for problem in problems), problems)

    def test_year_scope_must_stay_inside_playable_decade(self):
        store = valid_store()
        store["rules"][0]["years"] = {"start": 1989, "end": 1990}
        problems = rules.validate_store(store, {"MAR-TEST-001"})
        self.assertTrue(any("outside 1990-1999" in problem for problem in problems), problems)

    def test_requirements_and_prohibitions_are_not_optional_prose(self):
        store = valid_store()
        store["rules"][0]["requirements"] = []
        store["rules"][0]["prohibitions"] = []
        problems = rules.validate_store(store, {"MAR-TEST-001"})
        self.assertTrue(any("requirements" in problem for problem in problems), problems)
        self.assertTrue(any("prohibitions" in problem for problem in problems), problems)

    def test_review_required_is_boolean(self):
        store = valid_store()
        store["rules"][0]["review_required"] = "yes"
        problems = rules.validate_store(store, {"MAR-TEST-001"})
        self.assertTrue(any("review_required" in problem for problem in problems), problems)

    def test_duplicate_rule_ids_fail(self):
        store = valid_store()
        store["rules"].append(copy.deepcopy(store["rules"][0]))
        problems = rules.validate_store(store, {"MAR-TEST-001"})
        self.assertTrue(any("duplicate id" in problem for problem in problems), problems)

    def test_committed_rules_resolve_against_the_archive(self):
        self.assertTrue(rules.DEFAULT_STORE.exists(), rules.DEFAULT_STORE)
        store = json.loads(rules.DEFAULT_STORE.read_text(encoding="utf-8"))
        catalogue_ids = rules.load_catalog_ids()
        self.assertEqual(rules.validate_store(store, catalogue_ids), [])
        self.assertEqual(len(store["rules"]), 5)


if __name__ == "__main__":
    unittest.main()
