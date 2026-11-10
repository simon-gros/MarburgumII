"""Author /Game/Maps/Lent: the deliberate fallback blockout of the Lent riverside (ADR-016).

Run through the editor's Python commandlet (see scripts/create-maps.sh). This is NOT the licensed
Maribor city model (Stage 0D; purchase pending) and not the original game's generated geometry: it is
an independently authored blockout of engine shapes, so a public checkout has a playable district.

Layout follows the original Marburgum coordinates (project/data, metres: x east and z south of the
middle of Stari most, y up), mapped to Unreal centimetres as X = x, Y = z, Z = y. Carried forward:

  areas.json         `lent` box, centre (0, 5, -78), size (420, 12, 54); outside it is "Maribor"
  interactions.json  `lent_angler` at (-127, -58) with its range (4 m) and dialogue line
  npc_visual_profiles.json  the angler's parka colours

Everything else (quay line, railing, bridge deck, building row, lamps, trees) is placeholder massing,
not a survey of the real Lent. Player starts carry PlayerStartTag values the game mode selects with
?MB2Start=: `arrival` (default, on the promenade 30 m east of the angler), `angler` (within reach of
him) and `bridge` (on Stari most, outside the Lent area).
"""

import math

import unreal

MAP_PATH = "/Game/Maps/Lent"
MATERIAL_DIR = "/Game/Blockout/Materials"
BASIC_MATERIAL = "/Engine/BasicShapes/BasicShapeMaterial"
QUAY_Z = -54.0  # the river edge, metres south of the bridge middle (negative = north bank)
STREET_HALF_WIDTH = 7.0  # a street leads north from where Stari most lands

# sRGB hex colours for the blockout palette; converted to linear for the material parameter.
PALETTE = {
    "Paving": "#8a8178",
    "Quay": "#6f675e",
    "Water": "#2c4a55",
    "Riverbed": "#3a3a34",
    "Bridge": "#9a9186",
    "Railing": "#2f3133",
    "Lamp": "#26292b",
    "LampGlass": "#e8dcb8",
    "Trunk": "#4a3b2c",
    "Canopy": "#4f6b3a",
    "Plinth": "#5d564e",
    "Cornice": "#4a4440",
    "Ochre": "#c49a5a",
    "Cream": "#d8cdb2",
    "Grey": "#a39d94",
    "Rose": "#b98a7a",
    "Sage": "#9fa58a",
    "Umber": "#8f6f52",
}

# Building row behind the promenade: (width m, height m, colour), west to east; a 6 m street every third.
BUILDINGS = [
    (34, 14, "Ochre"), (26, 11, "Cream"), (30, 16, "Grey"),
    (22, 12, "Rose"), (36, 15, "Cream"), (24, 10, "Umber"),
    (28, 13, "Sage"), (32, 17, "Ochre"), (20, 11, "Grey"),
    (30, 12, "Cream"), (26, 14, "Rose"), (34, 13, "Umber"),
    (24, 15, "Grey"), (28, 11, "Ochre"),
]


def to_linear(hex_colour):
    channels = [int(hex_colour[i:i + 2], 16) / 255.0 for i in (1, 3, 5)]
    lin = [c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4 for c in channels]
    return unreal.LinearColor(lin[0], lin[1], lin[2], 1.0)


def ue(x, y, z):
    """Original metres (x east, y up, z south) to Unreal centimetres."""
    return unreal.Vector(x * 100.0, z * 100.0, y * 100.0)


def materials():
    basic = unreal.load_asset(BASIC_MATERIAL)
    names = [str(n) for n in unreal.MaterialEditingLibrary.get_vector_parameter_names(basic)]
    if "Color" not in names:
        raise RuntimeError("BasicShapeMaterial has no Color parameter: %s" % names)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    made = {}
    for name, colour in PALETTE.items():
        path = "%s/MI_Blockout_%s" % (MATERIAL_DIR, name)
        mic = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
        if mic is None:
            mic = tools.create_asset("MI_Blockout_%s" % name, MATERIAL_DIR, unreal.MaterialInstanceConstant,
                                     unreal.MaterialInstanceConstantFactoryNew())
        unreal.MaterialEditingLibrary.set_material_instance_parent(mic, basic)
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mic, "Color", to_linear(colour))
        unreal.MaterialEditingLibrary.update_material_instance(mic)
        unreal.EditorAssetLibrary.save_loaded_asset(mic, False)
        made[name] = mic
    return made


class Builder:
    def __init__(self, mats):
        self.actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        self.mats = mats
        self.meshes = {
            "cube": unreal.load_asset("/Engine/BasicShapes/Cube"),
            "cylinder": unreal.load_asset("/Engine/BasicShapes/Cylinder"),
            "sphere": unreal.load_asset("/Engine/BasicShapes/Sphere"),
        }
        self.count = 0

    def spawn(self, actor_class, location, rotation=unreal.Rotator(0.0, 0.0, 0.0), label=None):
        actor = self.actors.spawn_actor_from_class(actor_class, location, rotation)
        if label:
            actor.set_actor_label(label)
        return actor

    def block(self, label, centre, size, colour, shape="cube", collide=True, folder="Blockout"):
        """A shape centred at `centre` (original metres) with full `size` (x, y, z metres)."""
        actor = self.spawn(unreal.StaticMeshActor, ue(*centre), label=label)
        component = actor.static_mesh_component
        component.set_static_mesh(self.meshes[shape])
        component.set_material(0, self.mats[colour])
        # Basic shapes are 1 m across; Unreal scale order is (X=x, Y=z, Z=y).
        actor.set_actor_scale3d(unreal.Vector(size[0], size[2], size[1]))
        if not collide:
            component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        actor.set_folder_path(folder)
        self.count += 1
        return actor


def start(builder, tag, x, z, facing_x, facing_z, index):
    yaw = math.degrees(math.atan2(facing_z - z, facing_x - x))
    actor = builder.spawn(unreal.PlayerStart, ue(x, 1.1, z), unreal.Rotator(0.0, 0.0, yaw), label="Start_%s_%d" % (tag, index))
    actor.set_editor_property("player_start_tag", tag)
    actor.set_folder_path("Starts")


def main():
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    if world is None:
        raise RuntimeError("Could not create a blank map")
    b = Builder(materials())

    # Ground: north bank (Lent), river, south bank. Walking surfaces are at y = 0.
    b.block("LentPromenade", (0.0, -1.5, (QUAY_Z - 130.0) / 2.0), (440.0, 3.0, 130.0 + QUAY_Z), "Paving")
    b.block("SouthBank", (0.0, -1.5, 92.5), (440.0, 3.0, 35.0), "Paving")
    b.block("Riverbed", (0.0, -5.5, 10.5), (520.0, 1.0, 129.0), "Riverbed")
    b.block("Drava", (0.0, -1.6, 10.5), (520.0, 0.1, 129.0), "Water", collide=False)
    b.block("QuayFaceNorth", (0.0, -3.0, QUAY_Z + 0.25), (440.0, 6.0, 0.5), "Quay")
    b.block("QuayFaceSouth", (0.0, -3.0, 74.75), (440.0, 6.0, 0.5), "Quay")

    # Stari most: a flush deck with railings, so the bridge is outside the Lent area box.
    b.block("StariMostDeck", (0.0, -0.3, 10.5), (14.0, 0.6, 129.0), "Bridge")
    for side in (-1, 1):
        b.block("StariMostRailing%+d" % side, (side * 6.85, 0.55, 10.5), (0.3, 1.1, 129.0), "Railing")
    for pier_z in (-10.0, 30.0):
        b.block("StariMostPier%d" % int(pier_z), (0.0, -3.4, pier_z), (12.0, 5.0, 4.0), "Bridge")

    # Quay railing along the promenade edge, open where the bridge lands; 1.1 m stops a jump over it.
    for side in (-1, 1):
        b.block("QuayRailing%+d" % side, (side * 111.0, 0.55, QUAY_Z - 0.15), (208.0, 1.1, 0.3), "Railing")

    # Lamps along the quay and plane trees between them.
    for x in range(-195, 196, 30):
        if abs(x) < 12:
            continue
        b.block("LampPost%d" % x, (x, 2.1, QUAY_Z - 1.5), (0.16, 4.2, 0.16), "Lamp", shape="cylinder", folder="Blockout/Lamps")
        b.block("LampGlass%d" % x, (x, 4.35, QUAY_Z - 1.5), (0.45, 0.45, 0.45), "LampGlass", shape="sphere", collide=False, folder="Blockout/Lamps")
    for x in (-180, -150, -90, -60, 45, 75, 135, 165):
        b.block("TreeTrunk%d" % x, (x, 1.75, -71.0), (0.45, 3.5, 0.45), "Trunk", shape="cylinder", folder="Blockout/Trees")
        b.block("TreeCanopy%d" % x, (x, 5.5, -71.0), (6.0, 4.5, 6.0), "Canopy", shape="sphere", collide=False, folder="Blockout/Trees")

    # Building row: facades on the line z = -78, 22 m deep, with a darker plinth and a cornice.
    x = -212.0
    for index, (width, height, colour) in enumerate(BUILDINGS):
        if x < STREET_HALF_WIDTH and x + width > -STREET_HALF_WIDTH:
            x = STREET_HALF_WIDTH  # keep the street that leads off the bridge open
        centre_x = x + width / 2.0
        b.block("Building%02d" % index, (centre_x, height / 2.0, -89.0), (width, height, 22.0), colour, folder="Blockout/Buildings")
        b.block("Plinth%02d" % index, (centre_x, 1.75, -78.1), (width, 3.5, 0.4), "Plinth", folder="Blockout/Buildings")
        b.block("Cornice%02d" % index, (centre_x, height + 0.3, -88.8), (width + 0.6, 0.6, 22.8), "Cornice", folder="Blockout/Buildings")
        x += width + (6.0 if index % 3 == 2 else 0.0)

    # The named area from areas.json; the server derives each player's location from it.
    area = b.spawn(unreal.MarburgumArea, ue(0.0, 5.0, -78.0), label="Area_lent")
    area.set_editor_property("area_id", "lent")
    area.set_editor_property("note", unreal.Text("Maribor  ·  1990s  ·  fallback blockout, not the licensed city model"))
    area.get_component_by_class(unreal.BoxComponent).set_box_extent(unreal.Vector(420.0 * 50.0, 54.0 * 50.0, 12.0 * 50.0))
    area.set_folder_path("Areas")

    # The riverside angler (interactions.json `lent_angler`), facing the Drava.
    angler = b.spawn(unreal.MarburgumNpc, ue(-127.0, 0.0, -58.0), unreal.Rotator(0.0, 0.0, 90.0), label="Npc_lent_angler")
    angler.set_editor_property("interactable_id", "lent_angler")
    angler.set_editor_property("display_name", unreal.Text("Riverside angler"))
    angler.set_editor_property("verb", unreal.Text("Talk"))
    angler.set_editor_property("interaction_range", 400.0)
    angler.set_editor_property("test_fixture", False)
    angler.set_editor_property("dialogue", unreal.Text(
        "The Drava looks calm from here, but the current does the real work. Keep your first casts close to the bank."))
    angler.set_editor_property("primary_color", to_linear("#596455"))
    angler.set_editor_property("secondary_color", to_linear("#353a34"))
    angler.set_editor_property("holds_rod", True)
    angler.set_folder_path("Npcs")

    for index, (sx, sz) in enumerate([(-96.0, -62.0), (-96.0, -65.0), (-93.0, -62.0), (-93.0, -65.0)]):
        start(b, "arrival", sx, sz, sx - 10.0, sz, index)
    for index, (sx, sz) in enumerate([(-124.0, -60.0), (-123.5, -57.5)]):
        start(b, "angler", sx, sz, -127.0, -58.0, index)
    # On the bridge, looking up the promenade towards the angler.
    for index, sx in enumerate((-2.0, 2.0)):
        start(b, "bridge", sx, -40.0, -60.0, -66.0, index)

    # Movable lights need no baked lighting, so packaged builds show no "lighting needs to be rebuilt".
    # Late-afternoon sun from the south-west, so the riverside facades are lit.
    sun = b.spawn(unreal.DirectionalLight, ue(0.0, 30.0, 0.0), unreal.Rotator(0.0, -32.0, -60.0), label="Sun")
    sun.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky = b.spawn(unreal.SkyLight, ue(0.0, 30.0, 0.0), label="SkyLight")
    sky.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    b.spawn(unreal.SkyAtmosphere, ue(0.0, 0.0, 0.0), label="SkyAtmosphere")
    fog = b.spawn(unreal.ExponentialHeightFog, ue(0.0, 0.0, 0.0), label="HeightFog")
    fog.get_component_by_class(unreal.ExponentialHeightFogComponent).set_editor_property("fog_density", 0.01)

    if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH):
        raise RuntimeError("Could not save " + MAP_PATH)
    unreal.log("MB2MAP saved %s blocks=%d" % (MAP_PATH, b.count))


main()
