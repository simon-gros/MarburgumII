"""Author /Game/Maps/FrontEnd: the client's offline front-end map.

Run through the editor's Python commandlet (see scripts/create-maps.sh). The map is empty on
purpose: the connection shell covers the whole frame, and its world-settings game mode
(AMarburgumFrontEndGameMode) owns no gameplay, so a client that is not connected to an
authoritative server never simulates game rules locally.
"""

import unreal

MAP_PATH = "/Game/Maps/FrontEnd"


def main():
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    if world is None:
        raise RuntimeError("Could not create a blank map")

    game_mode = unreal.load_class(None, "/Script/MarburgumII.MarburgumFrontEndGameMode")
    if game_mode is None:
        raise RuntimeError("MarburgumFrontEndGameMode is not compiled into the editor")
    world.get_world_settings().set_editor_property("default_game_mode", game_mode)

    if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH):
        raise RuntimeError("Could not save " + MAP_PATH)
    unreal.log("MB2MAP saved " + MAP_PATH)


main()
