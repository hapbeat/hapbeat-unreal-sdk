# Copyright (c) 2026 Hapbeat. MIT License.
"""Create the VRConfigExample map once.

Run in a full UE Editor, never as a commandlet (Actor spawning needs the level
editor):

  UnrealEditor.exe <Project>.uproject -ExecCmds="py <absolute path>/generate_vr_config_example_map.py" -unattended -nosplash

The script owns only actors tagged ``HapbeatVRConfigExampleGenerated``. Re-run
it to restore the sample layout; any untagged user actor is left alone.
"""

import unreal

PLUGIN_CONTENT = '/HapbeatSDK/HapbeatSamples'
MAP_PATH = PLUGIN_CONTENT + '/VRConfigExample/Maps/VRConfigExample'
GENERATED_TAG = 'HapbeatVRConfigExampleGenerated'
GAME_MODE = '/Script/HapbeatSDKSamples.HapbeatShowcaseGameMode'
VR_CONFIG_ACTOR = '/Script/HapbeatSDKSamples.HapbeatVRConfigExampleActor'


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
    settings = world.get_world_settings()
    settings.set_editor_property('default_game_mode', load_class(GAME_MODE))
    # Keep the shipped sample fully dynamic: users can open it without running
    # a Lightmass build, and this map-local setting does not alter the host
    # project's rendering policy.
    settings.set_editor_property('force_no_precomputed_lighting', True)

    # A minimal neutral room: it provides a floor and visual depth without
    # competing with the panel, which is the thing this sample demonstrates.
    spawn_static_mesh(editor_actors, '/Engine/BasicShapes/Cube.Cube',
                      unreal.Vector(0.0, 0.0, -10.0), (20.0, 20.0, 0.1), 'VRConfigFloor')

    start = editor_actors.spawn_actor_from_class(unreal.PlayerStart,
        unreal.Vector(0.0, 0.0, 100.0), unreal.Rotator(0.0, 0.0, 0.0))
    finish(start, 'VRConfigPlayerStart')

    panel = editor_actors.spawn_actor_from_class(load_class(VR_CONFIG_ACTOR),
        unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))
    finish(panel, 'VRConfigExample')

    sun = editor_actors.spawn_actor_from_class(unreal.DirectionalLight,
        unreal.Vector(0.0, 0.0, 1000.0), unreal.Rotator(-50.0, 30.0, 0.0))
    sun.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property(
        'mobility', unreal.ComponentMobility.MOVABLE)
    finish(sun, 'VRConfigSun')

    sky = editor_actors.spawn_actor_from_class(unreal.SkyLight,
        unreal.Vector(0.0, 0.0, 1000.0), unreal.Rotator())
    sky.get_component_by_class(unreal.SkyLightComponent).set_editor_property(
        'mobility', unreal.ComponentMobility.MOVABLE)
    finish(sky, 'VRConfigSkyLight')

    if not level_editor.save_current_level():
        raise RuntimeError('Could not save ' + MAP_PATH)
    unreal.log('[Hapbeat] VRConfigExample map generated: ' + MAP_PATH)


main()
