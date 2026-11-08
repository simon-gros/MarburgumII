"""Author /Game/Maps/NetSmoke: the empty Stage 0B network test map.

Run through the editor's Python commandlet (see scripts/create-net-smoke-map.sh).
The map deliberately contains no Marburgum content: a floor, a player start and
basic lighting are enough for dedicated-server and client join tests.
"""

import unreal

MAP_PATH = "/Game/Maps/NetSmoke"


def spawn(actor_class, location, rotation=unreal.Rotator(0.0, 0.0, 0.0), label=None):
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actor = actors.spawn_actor_from_class(actor_class, location, rotation)
    if label:
        actor.set_actor_label(label)
    return actor


def main():
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    if world is None:
        raise RuntimeError("Could not create a blank map")

    floor = spawn(unreal.StaticMeshActor, unreal.Vector(0.0, 0.0, 0.0), label="Floor")
    plane = unreal.load_asset("/Engine/BasicShapes/Plane")
    floor.static_mesh_component.set_static_mesh(plane)
    floor.set_actor_scale3d(unreal.Vector(100.0, 100.0, 1.0))

    spawn(unreal.PlayerStart, unreal.Vector(0.0, 0.0, 120.0), label="PlayerStart")
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
