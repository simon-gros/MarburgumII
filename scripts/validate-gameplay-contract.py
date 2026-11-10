#!/usr/bin/env python3
"""Source-level consistency check for the Gate 1 and Lent gameplay slices (no Unreal build required).

Verifies that the gameplay catalogue, the shared C++ contract and the map generators agree (items,
areas, interactables, NPCs, missions), that the authoritative domain logic and store live only in the
ServerOnly module, that custody, missions and location are replicated to their owner only, and that
every refusal reason and effect has a wire name. Passing this check says nothing about runtime
behaviour; scripts/interaction-smoke-linux.sh and scripts/lent-smoke-linux.sh are the runtime tests.
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
lent_script = read("scripts/editor/create_lent_map.py")


def block(name):
    """The initializer of `inline constexpr <type> <name>[] = { ... };` in the contract."""
    match = re.search(r"%s\[\] = \{(.*?)\n    \};" % re.escape(name), contract, re.S)
    expect(match is not None, f"contract has no {name}[] registry")
    return match.group(1) if match else ""


def pairs(text):
    return dict(re.findall(r'\{\s*TEXT\("([a-z0-9_]+)"\),\s*TEXT\("([^"]+)"\)\s*\}', text))


# Items: the C++ registry and the catalogue list the same ids and English names.
cpp_items = pairs(block("Items"))
json_items = {item["id"]: item["name"]["en"] for item in catalogue["items"]}
expect(cpp_items == json_items, f"item registry mismatch: C++ {cpp_items} vs catalogue {json_items}")
for item in catalogue["items"]:
    expect(re.fullmatch(r"[a-z0-9_]+", item["id"]) is not None, f"item id not snake_case: {item['id']}")
    expect("provenance" in item, f"item without provenance: {item['id']}")

# Areas: the C++ registry, the catalogue and the Lent map agree.
cpp_areas = pairs(block("Areas"))
json_areas = {area["id"]: area["name"]["en"] for area in catalogue["areas"]["entries"]}
expect(cpp_areas == json_areas, f"area registry mismatch: C++ {cpp_areas} vs catalogue {json_areas}")
expect(f'OutsideAreaName = TEXT("{catalogue["areas"]["outside"]["name"]["en"]}")' in contract, "outside area name differs")
for area in catalogue["areas"]["entries"]:
    expect("provenance" in area, f"area without provenance: {area['id']}")
    cx, cy, cz = area["center_m"]
    sx, sy, sz = area["size_m"]
    expect(f'ue({cx}, {cy}, {cz}), label="Area_{area["id"]}"' in lent_script, f"area {area['id']}: centre differs from create_lent_map.py")
    expect(f'set_box_extent(unreal.Vector({sx} * 50.0, {sz} * 50.0, {sy} * 50.0))' in lent_script, f"area {area['id']}: size differs from create_lent_map.py")

# Interactables: ids are shared constants, placed by the map scripts with the catalogue's values.
for constant, ident in re.findall(r'inline constexpr const TCHAR\* (\w+(?:CrateId|AnglerId)) = TEXT\("([a-z0-9_]+)"\);', contract):
    expect(any(entry["id"] == ident for entry in catalogue["interactables"]), f"{constant}={ident} missing from catalogue")
for entry in catalogue["interactables"]:
    if entry["kind"] == "npc":
        x, y, z = entry["position_m"]
        expect(f'ue({x}, {y}, {z}), unreal.Rotator' in lent_script and f'"interactable_id", "{entry["id"]}"' in lent_script,
               f"{entry['id']}: not placed at its catalogue position by create_lent_map.py")
        expect(f'"display_name", unreal.Text("{entry["display_name"]["en"]}")' in lent_script, f"{entry['id']}: display name differs")
        expect(f'"interaction_range", {float(entry["range_cm"])}' in lent_script, f"{entry['id']}: range differs")
        expect(entry["dialogue"]["en"] in lent_script, f"{entry['id']}: dialogue differs")
        expect(any(m["id"] == entry.get("offers_mission") for m in catalogue["missions"]), f"{entry['id']}: offers an unknown mission")
        expect("provenance" in entry, f"{entry['id']}: npc without provenance")
        continue
    expect(entry["kind"] == "test_fixture", f"{entry['id']}: item sources are labeled test fixtures so far")
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

# Missions: registry entries match the catalogue's names, offer targets, start items and registered stages.
for mission in catalogue["missions"]:
    expect(re.search(r'\{ TEXT\("%s"\), TEXT\("%s"\), TEXT\("%s"\), TEXT\("%s"\),' % (
        mission["id"], re.escape(mission["name"]["en"]), mission["category"], mission["offer_target"]), contract) is not None,
        f"mission {mission['id']}: name/category/offer target differ from the contract")
    expect(mission["summary"]["en"] in contract, f"mission {mission['id']}: summary differs")
    for start in mission["start_items"]:
        expect(start["id"] in json_items, f"mission {mission['id']}: unknown start item {start['id']}")
        expect(f'TEXT("{start["id"]}"), {start["quantity"]}, ' in contract, f"mission {mission['id']}: start item differs")
    for stage in mission["registered_stages"]:
        expect(f'TEXT("{stage["id"]}"),' in contract and stage["objective_text"]["en"] in contract,
               f"mission {mission['id']}: stage {stage['id']} differs")
        expect(f'TEXT("{stage["complete_target"]}"), TEXT("{stage["action"]["en"]}")' in contract,
               f"mission {mission['id']}: stage {stage['id']} target/action differ")
        for item in stage["objective"].get("items", []):
            expect(item in json_items, f"mission {mission['id']}: stage {stage['id']} needs unregistered item {item}")
    expect("provenance" in mission, f"mission {mission['id']} without provenance")

# Authority boundary: the reducer and store are ServerOnly; nothing else includes them.
for name in ("MarburgumWorldAuthority", "MarburgumWorldStore", "MarburgumMissionRules"):
    owners = sorted(str(p.relative_to(ROOT)) for p in (ROOT / "Source").rglob(f"{name}.*"))
    expect(owners and all(p.startswith("Source/MarburgumIIServer/") for p in owners), f"{name} outside the server module: {owners}")
    for source in (ROOT / "Source").rglob("*.[ch]*"):
        rel = str(source.relative_to(ROOT))
        if not rel.startswith("Source/MarburgumIIServer/") and f'"{name}.h"' in source.read_text(encoding="utf-8"):
            problems.append(f"{rel} includes {name}.h")
service = read("Source/MarburgumII/Public/MarburgumAuthorityService.h")
expect("UCLASS(Abstract)" in service, "core authority service must stay abstract")

# Privacy: custody, mission progress and location replicate to their owner only.
player_state = read("Source/MarburgumII/Private/MarburgumPlayerState.cpp")
for prop in ("Inventory", "bInventoryKnown", "bHasCharacter", "Missions", "bMissionsKnown", "AreaId", "bLocationKnown"):
    expect(re.search(r"DOREPLIFETIME_CONDITION\(AMarburgumPlayerState, %s, COND_OwnerOnly\)" % prop, player_state) is not None,
           f"{prop} is not replicated COND_OwnerOnly")

# Every refusal reason has a stable wire/log name.
types = read("Source/MarburgumII/Public/MarburgumInteractionTypes.h")
enum_body = re.search(r"enum class EMarburgumInteractionReason : uint8\s*\{(.*?)\};", types, re.S).group(1)
reasons = re.findall(r"^\s*(\w+),?", enum_body, re.M)
names = read("Source/MarburgumII/Private/MarburgumAuthorityService.cpp")
for reason in reasons:
    expect(f"EMarburgumInteractionReason::{reason}:" in names, f"reason {reason} has no ToString case")
effect_body = re.search(r"enum class EMarburgumInteractionEffect : uint8\s*\{(.*?)\};", types, re.S).group(1)
for effect in re.findall(r"^\s*(\w+),?", effect_body, re.M):
    expect(f"EMarburgumInteractionEffect::{effect}:" in names, f"effect {effect} has no ToString case")

# Excluded systems never enter the gameplay slice.
for path in ["Data/Gameplay/gameplay.v0.1.json", "Source/MarburgumIIShared/Public/MarburgumGameplayContract.h",
             "scripts/editor/create_lent_map.py"]:
    text = read(path).lower()
    for banned in ("talent", "wanted", "pursuit"):
        expect(banned not in text, f"{path} mentions excluded system '{banned}'")

if problems:
    print("Gameplay contract check FAILED:")
    for problem in problems:
        print("  - " + problem)
    sys.exit(1)
print(f"Gameplay contract check passed: {len(json_items)} item(s), {len(json_areas)} area(s), "
      f"{len(catalogue['interactables'])} interactable(s), {len(catalogue['missions'])} mission(s), "
      f"{len(reasons)} refusal reasons, server-only authority and owner-only custody, progress and location.")
