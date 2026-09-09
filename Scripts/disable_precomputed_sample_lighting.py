# Copyright (c) 2026 Hapbeat. MIT License.
"""Switch the shipped sample maps to fully dynamic lighting without rebuilding them.

Run in a full UE Editor, never as a commandlet:

  UnrealEditor.exe <Project>.uproject -ExecCmds="py <absolute path>/disable_precomputed_sample_lighting.py" -unattended -nosplash

Unlike the map generators, this script does not delete or respawn actors. It
only changes each map's World Settings and its generated directional light, so
it is safe to run after a maintainer has adjusted sample actor placement.
"""

import unreal


SAMPLE_MAPS = [
    ('/HapbeatSDK/HapbeatSamples/BasicExample/Maps/BasicExample', 'BasicExampleSun'),
    ('/HapbeatSDK/HapbeatSamples/Showcase/Maps/Showcase', 'ShowcaseSun'),
    ('/HapbeatSDK/HapbeatSamples/VRConfigExample/Maps/VRConfigExample', 'VRConfigSun'),
]


def set_dynamic_lighting(level_editor, editor_actors, map_path, sun_label):
    if not level_editor.load_level(map_path):
        raise RuntimeError('could not open ' + map_path)

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    settings = world.get_world_settings()
    settings.set_editor_property('force_no_precomputed_lighting', True)

    for actor in editor_actors.get_all_level_actors():
        if actor.get_actor_label() != sun_label:
            continue
        component = actor.get_component_by_class(unreal.DirectionalLightComponent)
        if component is None:
            raise RuntimeError(sun_label + ' has no DirectionalLightComponent')
        component.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
        break
    else:
        raise RuntimeError('could not find ' + sun_label + ' in ' + map_path)

    if not level_editor.save_current_level():
        raise RuntimeError('could not save ' + map_path)
    unreal.log('[Hapbeat] Fully dynamic lighting saved: ' + map_path)


def main():
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor_actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for map_path, sun_label in SAMPLE_MAPS:
        set_dynamic_lighting(level_editor, editor_actors, map_path, sun_label)
    unreal.log('[Hapbeat] All sample maps now force no precomputed lighting.')


main()
