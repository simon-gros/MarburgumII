#!/usr/bin/env python3
"""Source-level consistency check for the Gate 1 interaction slice (no Unreal build required).

Verifies that the gameplay catalogue, the shared C++ contract and the test-map generator agree,
that the authoritative domain logic and store live only in the ServerOnly module, that custody is
replicated to its owner only, and that every refusal reason has a wire name. Passing this check says
nothing about runtime behaviour; scripts/interaction-smoke-linux.sh is the runtime test.
"""

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
problems = []


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def expect(condition, message):
    if not condition:
        problems.append(message)


catalogue = json.loads(read("Data/Gameplay/gameplay.v0.1.json"))
contract = read("Source/MarburgumIIShared/Public/MarburgumGameplayContract.h")
map_script = read("scripts/editor/create_net_smoke_map.py")

# Items: the C++ registry and the catalogue list the same ids and English names.
cpp_items = dict(re.findall(r'\{\s*TEXT\("([a-z0-9_]+)"\),\s*TEXT\("([^"]+)"\)\s*\}', contract))
json_items = {item["id"]: item["name"]["en"] for item in catalogue["items"]}
expect(cpp_items == json_items, f"item registry mismatch: C++ {cpp_items} vs catalogue {json_items}")
for item in catalogue["items"]:
    expect(re.fullmatch(r"[a-z0-9_]+", item["id"]) is not None, f"item id not snake_case: {item['id']}")
    expect("provenance" in item, f"item without provenance: {item['id']}")

# Interactables: ids are shared constants, placed by the map script with the catalogue's values.
for constant, ident in re.findall(r'inline constexpr const TCHAR\* (\w+CrateId) = TEXT\("([a-z0-9_]+)"\);', contract):
    expect(any(entry["id"] == ident for entry in catalogue["interactables"]), f"{constant}={ident} missing from catalogue")
for entry in catalogue["interactables"]:
    expect(entry["kind"] == "test_fixture", f"{entry['id']}: only labeled test fixtures exist so far")
    expect(entry["grants"]["item"] in json_items, f"{entry['id']}: grants unknown item {entry['grants']['item']}")
    placed = re.search(r'crate\("\w+", "%s", "([^"]+)", (\d+),' % re.escape(entry["id"]), map_script)
    expect(placed is not None, f"{entry['id']}: not placed by create_net_smoke_map.py")
    if placed:
        expect(placed.group(1) == entry["display_name"]["en"], f"{entry['id']}: display name differs from map script")
        expect(int(placed.group(2)) == entry["initial_stock"], f"{entry['id']}: initial stock differs from map script")
expect('"granted_item_id", "fishing_rod"' in map_script, "map script grants an unexpected item")
expect('"test_fixture", True' in map_script, "map fixtures are not marked as test fixtures")
expect(str(catalogue["world_store"]["schema_version"]) in re.findall(r"WorldStoreSchemaVersion = (\d+);", contract),
       "world store schema version differs between catalogue and contract")

# Authority boundary: the reducer and store are ServerOnly; nothing else includes them.
for name in ("MarburgumWorldAuthority", "MarburgumWorldStore"):
    owners = sorted(str(p.relative_to(ROOT)) for p in (ROOT / "Source").rglob(f"{name}.*"))
    expect(owners and all(p.startswith("Source/MarburgumIIServer/") for p in owners), f"{name} outside the server module: {owners}")
    for source in (ROOT / "Source").rglob("*.[ch]*"):
        rel = str(source.relative_to(ROOT))
        if not rel.startswith("Source/MarburgumIIServer/") and f'"{name}.h"' in source.read_text(encoding="utf-8"):
            problems.append(f"{rel} includes {name}.h")
service = read("Source/MarburgumII/Public/MarburgumAuthorityService.h")
expect("UCLASS(Abstract)" in service, "core authority service must stay abstract")

# Custody privacy: inventory state replicates to its owner only.
player_state = read("Source/MarburgumII/Private/MarburgumPlayerState.cpp")
for prop in ("Inventory", "bInventoryKnown", "bHasCharacter"):
    expect(re.search(r"DOREPLIFETIME_CONDITION\(AMarburgumPlayerState, %s, COND_OwnerOnly\)" % prop, player_state) is not None,
           f"{prop} is not replicated COND_OwnerOnly")

# Every refusal reason has a stable wire/log name.
types = read("Source/MarburgumII/Public/MarburgumInteractionTypes.h")
enum_body = re.search(r"enum class EMarburgumInteractionReason : uint8\s*\{(.*?)\};", types, re.S).group(1)
reasons = re.findall(r"^\s*(\w+),?", enum_body, re.M)
names = read("Source/MarburgumII/Private/MarburgumAuthorityService.cpp")
for reason in reasons:
    expect(f"EMarburgumInteractionReason::{reason}:" in names, f"reason {reason} has no ToString case")

# Excluded systems never enter the gameplay slice.
for path in ["Data/Gameplay/gameplay.v0.1.json", "Source/MarburgumIIShared/Public/MarburgumGameplayContract.h"]:
    text = read(path).lower()
    for banned in ("talent", "wanted", "pursuit"):
        expect(banned not in text, f"{path} mentions excluded system '{banned}'")

if problems:
    print("Gameplay contract check FAILED:")
    for problem in problems:
        print("  - " + problem)
    sys.exit(1)
print(f"Gameplay contract check passed: {len(json_items)} item(s), {len(catalogue['interactables'])} interactable fixture(s), "
      f"{len(reasons)} refusal reasons, server-only authority and owner-only custody.")
