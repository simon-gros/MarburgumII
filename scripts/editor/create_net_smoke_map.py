"""Author /Game/Maps/NetSmoke: the network and Gate 1 interaction test map.

Run through the editor's Python commandlet (see scripts/create-maps.sh). The map deliberately
contains no Marburgum world content: a floor, player starts, basic lighting and two labeled
development fixtures. The fixtures are supply crates that grant the original catalogue's
`fishing_rod` from a limited server-owned stock (Data/Gameplay/gameplay.v0.1.json):

  dev_supply_crate      3 units, within reach of every player start
  dev_supply_crate_far  1 unit, 25 m away, used to prove the server's range check
"""

import math

import unreal

MAP_PATH = "/Game/Maps/NetSmoke"
CRATE = unreal.Vector(220.0, 0.0, 40.0)
START_RADIUS = 220.0
START_ANGLES = (180.0, 150.0, 210.0, 120.0)  # degrees around the crate; every start faces it


def spawn(actor_class, location, rotation=unreal.Rotator(0.0, 0.0, 0.0), label=None):
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actor = actors.spawn_actor_from_class(actor_class, location, rotation)
    if label:
        actor.set_actor_label(label)
    return actor


def crate(label, interactable_id, display_name, stock, location):
    actor = spawn(unreal.MarburgumInteractable, location, label=label)
    actor.set_editor_property("interactable_id", interactable_id)
    actor.set_editor_property("granted_item_id", "fishing_rod")
    actor.set_editor_property("display_name", unreal.Text(display_name))
    actor.set_editor_property("verb", unreal.Text("Take"))
    actor.set_editor_property("initial_stock", stock)
    actor.set_editor_property("interaction_range", 300.0)
    actor.set_editor_property("test_fixture", True)
    actor.set_actor_scale3d(unreal.Vector(0.8, 0.8, 0.8))
    return actor


def main():
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    if world is None:
        raise RuntimeError("Could not create a blank map")

    floor = spawn(unreal.StaticMeshActor, unreal.Vector(0.0, 0.0, 0.0), label="Floor")
    plane = unreal.load_asset("/Engine/BasicShapes/Plane")
    floor.static_mesh_component.set_static_mesh(plane)
    floor.set_actor_scale3d(unreal.Vector(100.0, 100.0, 1.0))

    for index, angle in enumerate(START_ANGLES):
        radians = math.radians(angle)
        x = CRATE.x + START_RADIUS * math.cos(radians)
        y = CRATE.y + START_RADIUS * math.sin(radians)
        yaw = math.degrees(math.atan2(CRATE.y - y, CRATE.x - x))
        spawn(unreal.PlayerStart, unreal.Vector(x, y, 120.0), unreal.Rotator(0.0, 0.0, yaw), label="PlayerStart%d" % index)

    crate("SupplyCrate", "dev_supply_crate", "Test supply crate", 3, CRATE)
    crate("SupplyCrateFar", "dev_supply_crate_far", "Test supply crate (far)", 1, unreal.Vector(2500.0, 0.0, 40.0))

    # Movable lights need no baked lighting, so packaged builds show no "lighting needs to be rebuilt".
    sun = spawn(unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 1000.0), unreal.Rotator(0.0, -45.0, 30.0), label="Sun")
    sun.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky = spawn(unreal.SkyLight, unreal.Vector(0.0, 0.0, 1000.0), label="SkyLight")
    sky.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    spawn(unreal.SkyAtmosphere, unreal.Vector(0.0, 0.0, 0.0), label="SkyAtmosphere")

    if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH):
        raise RuntimeError("Could not save " + MAP_PATH)
    unreal.log("MB2MAP saved " + MAP_PATH)


main()
