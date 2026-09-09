# Copyright (c) 2026 Hapbeat. MIT License.
"""Create the BasicExample map once.

Run in a full UE Editor, never as a commandlet (Actor spawning needs the level
editor):

  UnrealEditor.exe <Project>.uproject -ExecCmds=\"py <absolute path>/generate_basic_example_map.py\" -unattended -nosplash

The script owns only actors tagged ``HapbeatBasicExampleGenerated``. Re-run it
to restore the sample layout; any untagged user actor is left alone.
"""

import unreal

PLUGIN_CONTENT = '/HapbeatSDK/HapbeatSamples'
MAP_PATH = PLUGIN_CONTENT + '/BasicExample/Maps/BasicExample'
GENERATED_TAG = 'HapbeatBasicExampleGenerated'
GAME_MODE = '/Script/HapbeatSDKSamples.HapbeatShowcaseGameMode'
BASIC_ACTOR = '/Script/HapbeatSDKSamples.HapbeatBasicExampleActor'
EVENT_MAP = PLUGIN_CONTENT + '/BasicExample/EM_BasicExample'


def finish(actor, label):
    actor.set_actor_label(label)
    actor.set_editor_property('tags', [GENERATED_TAG])
    return actor


def load_class(path):
    result = unreal.load_object(None, path)
    if result is None:
        raise RuntimeError('Could not load class: ' + path)
    return result


def open_or_create(level_editor):
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([PLUGIN_CONTENT], True)
    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        level_editor.load_level(MAP_PATH)
    else:
        level_editor.new_level(MAP_PATH)


def clear_old(editor_actors):
    for actor in editor_actors.get_all_level_actors():
        if GENERATED_TAG in actor.tags:
            editor_actors.destroy_actor(actor)


def spawn_static_mesh(editor_actors, mesh_path, location, scale, label):
    actor = editor_actors.spawn_actor_from_class(unreal.StaticMeshActor, location, unreal.Rotator())
    component = actor.get_editor_property('static_mesh_component')
    component.set_editor_property('static_mesh', unreal.load_asset(mesh_path))
    actor.set_actor_scale3d(unreal.Vector(*scale))
    return finish(actor, label)


def main():
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor_actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    open_or_create(level_editor)
    clear_old(editor_actors)

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property('default_game_mode', load_class(GAME_MODE))

    # The actor renders its own HUD during play. The room only establishes a
    # clear spawn point and enough visual depth for a first run.
    spawn_static_mesh(editor_actors, '/Engine/BasicShapes/Cube.Cube',
                      unreal.Vector(0.0, 0.0, -10.0), (20.0, 20.0, 0.1), 'BasicExampleFloor')

    start = editor_actors.spawn_actor_from_class(
        unreal.PlayerStart, unreal.Vector(-500.0, 0.0, 100.0), unreal.Rotator(0.0, 0.0, 0.0))
    finish(start, 'BasicExamplePlayerStart')

    example = editor_actors.spawn_actor_from_class(
        load_class(BASIC_ACTOR), unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))
    example.set_editor_property('event_map_override', unreal.load_asset(EVENT_MAP))
    finish(example, 'BasicExample')

    sun = editor_actors.spawn_actor_from_class(
        unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 1000.0), unreal.Rotator(-50.0, 30.0, 0.0))
    sun.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property(
        'mobility', unreal.ComponentMobility.STATIONARY)
    finish(sun, 'BasicExampleSun')

    sky = editor_actors.spawn_actor_from_class(
        unreal.SkyLight, unreal.Vector(0.0, 0.0, 1000.0), unreal.Rotator())
    sky.get_component_by_class(unreal.SkyLightComponent).set_editor_property(
        'mobility', unreal.ComponentMobility.MOVABLE)
    finish(sky, 'BasicExampleSkyLight')

    # A newly-created level is initially /Temp/Untitled, so SaveCurrentLevel
    # has no filename on its first pass. SaveMap establishes the package path;
    # subsequent re-runs use the normal current-level save.
    if not level_editor.save_current_level():
        if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH):
            raise RuntimeError('Could not save ' + MAP_PATH)
    unreal.log('[Hapbeat] BasicExample map generated: ' + MAP_PATH)


main()
