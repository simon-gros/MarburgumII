#!/usr/bin/env python3
"""Validate the design-time MarburgumII UI/HUD registry; does not compile Unreal."""
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "Data/Ontology/ui_hud.v0.1.json"
ROADMAP = ROOT / "Docs/UI_HUD_ROADMAP.md"
ONTOLOGY = ROOT / "Docs/UI_HUD_ONTOLOGY.md"

def main():
    errors = []
    doc = json.loads(REGISTRY.read_text(encoding="utf-8"))
    required_keys = {"type_definitions", "relation_definitions", "semantic_signals",
                     "module_registry", "presentation_modes", "implementation_stages",
                     "invariants", "excluded_features", "verification"}
    missing = sorted(required_keys - doc.keys())
    errors.extend(f"missing catalogue key: {k}" for k in missing)
    if missing:
        for e in errors: print("ERROR:", e, file=sys.stderr)
        return 1

    if doc.get("engine") != "Unreal Engine 5.8.3":
        errors.append("Engine pin must remain exactly Unreal Engine 5.8.3")
    if doc.get("version") != "0.1.0" or doc.get("status") != "design_only_not_implemented":
        errors.append("Unexpected ontology version or implementation status")
    pattern = re.compile(r"^[a-z][a-z0-9_]*$")
    def index(items, label):
        ids = [x.get("id") for x in items]
        if len(ids) != len(set(ids)):
            errors.append(f"{label} has duplicate IDs")
        for i in ids:
            if not isinstance(i, str) or not pattern.fullmatch(i):
                errors.append(f"{label} has invalid stable ID: {i!r}")
        return set(ids)

    types = index(doc["type_definitions"], "types")
    relations = index(doc["relation_definitions"], "relations")
    signals = index(doc["semantic_signals"], "signals")
    modules = index(doc["module_registry"], "modules")
    modes = index(doc["presentation_modes"], "modes")
    index(doc["invariants"], "invariants")
    for rel in doc["relation_definitions"]:
        if rel.get("source") not in types or rel.get("target") not in types:
            errors.append(f"broken relation: {rel.get('id')}")
        if rel.get("cardinality") not in {"0..1", "1", "0..*", "1..*"}:
            errors.append(f"unsupported cardinality: {rel.get('id')}")
    stages = doc["implementation_stages"]
    if stages != [f"UI-{x:02d}" for x in range(8)]:
        errors.append("UI-00 through UI-07 stages must be in order")
    for x in doc["module_registry"]:
        if x.get("stage") not in stages or x.get("primary_mode") not in modes:
            errors.append(f"invalid module stage/mode: {x.get('id')}")
        if not x.get("required_signals"):
            errors.append(f"module lacks explicit signal contract: {x.get('id')}")
        for s in x.get("required_signals", []):
            if s not in signals:
                errors.append(f"unknown signal {s} in module {x.get('id')}")
    for x in doc["presentation_modes"]:
        if x.get("stage") not in stages:
            errors.append(f"invalid mode stage: {x.get('id')}")
        for m in x.get("default_modules", []):
            if m not in modules:
                errors.append(f"unknown module {m} in mode {x.get('id')}")
    expected_exclusions = {"wanted_stars", "wanted_level_or_heat",
                           "police_pursuit_or_chase", "legacy_talents"}
    recorded = {x.get("mechanic") for x in doc["excluded_features"]}
    if not expected_exclusions.issubset(recorded):
        errors.append("Owner-approved exclusion list has been weakened")
    for m in modules:
        if any(k in m for k in ("wanted", "pursuit", "chase", "talent")):
            errors.append(f"excluded mechanic reintroduced in module: {m}")
    road = ROADMAP.read_text(encoding="utf-8")
    ontology = ONTOLOGY.read_text(encoding="utf-8")
    for stage in stages:
        if stage not in road:
            errors.append(f"Roadmap missing stage {stage}")
    if "Data/Ontology/ui_hud.v0.1.json" not in ontology:
        errors.append("Ontology doc missing catalogue link")
    if errors:
        for e in errors: print("ERROR:", e, file=sys.stderr)
        return 1
    print(f"UI/HUD ontology v{doc['version']} valid: "
          f"{len(types)} types, {len(relations)} relations, "
          f"{len(signals)} signals, {len(modules)} modules, "
          f"{len(modes)} modes, {len(stages)} phases")
    print("NOTE: Source consistency only; not an Unreal build or running UI.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
