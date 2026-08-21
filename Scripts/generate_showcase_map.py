# Copyright (c) 2026 Hapbeat. MIT License.
"""
Generates the Showcase map: /HapbeatSDK/HapbeatSamples/Showcase/Maps/Showcase.

Run it in a full editor and commit the .umap file it writes:

    UnrealEditor.exe <YourProject>.uproject ^
        -ExecCmds="py <this file>" -unattended -nosplash

NOT `-run=pythonscript`, which is how the other two scripts in this folder are
run. A commandlet brings up no editor selection set and no viewport, and
EditorActorSubsystem.spawn_actor_from_class dies inside EditorFramework with an
access violation the moment it tries to select what it just spawned. The
-ExecCmds route runs the same script from UEngine::TickDeferredCommands, i.e.
after the editor has finished initialising, where all of that exists. It still
needs no human at the keyboard: -unattended -nosplash gets you the same
scriptable run, just with an editor behind it.

The script deliberately does NOT call unreal.SystemLibrary.quit_editor() at the
end, so the editor stays open after it finishes -- close it yourself. A quit
call would be a booby trap: this file is also runnable from the editor's own
Python console, and there it would shut the editor down under a user who only
meant to regenerate a map.

Run it after import_showcase_assets.py (the floor takes its material from
there) and after generate_sample_assets.py (the zones take their Event Map from
there). It touches only the map package, so those two never have to re-run
because of this one.

What the map is for: opening it and pressing Play is the whole setup. It holds
a floor to stand on, one directional light + sky, a PlayerStart, the single
AHapbeatShowcaseActor that owns the five zones, and -- the part that cannot be
done from an actor -- World Settings' GameMode Override pointing at
AHapbeatShowcaseGameMode, without which Play hands you the engine's flying
DefaultPawn instead of the first-person character. UE counterpart of Unity's
Samples~/Showcase/Scenes/Showcase.unity.

Re-running is the normal case. Every actor this script spawns is tagged
HapbeatShowcaseGenerated and a re-run deletes exactly those before spawning
again, so the map package (and any reference to it) survives while its contents
are rebuilt -- and anything you added to the map by hand is left alone.
"""

import unreal

PLUGIN_CONTENT = '/HapbeatSDK/HapbeatSamples'
SHOWCASE = PLUGIN_CONTENT + '/Showcase'
MAP_PKG = SHOWCASE + '/Maps'
MAP_NAME = 'Showcase'
MAP_PATH = MAP_PKG + '/' + MAP_NAME

FLOOR_MATERIAL = SHOWCASE + '/Materials/MI_Floor'
CUBE_MESH = '/Engine/BasicShapes/Cube'

SHOWCASE_ACTOR_CLASS = '/Script/HapbeatSDKSamples.HapbeatShowcaseActor'
SHOWCASE_GAME_MODE_CLASS = '/Script/HapbeatSDKSamples.HapbeatShowcaseGameMode'

# Marks what this script owns, so a re-run can clear its own actors and only
# its own. Anything you drop into the map by hand has no tag and survives.
GENERATED_TAG = 'HapbeatShowcaseGenerated'

# 60 m square, 20 cm thick. The zones build outward from the map origin (Z1's
# lane is the longest run) and the player is teleported between them, so the
# floor only has to be bigger than the largest zone -- there is no level
# geometry beyond it. Cube is 100 cm authored, hence the scale numbers.
FLOOR_SCALE = (60.0, 60.0, 0.2)
# Sunk by half its thickness so the walking surface is exactly Z = 0, which is
# what every zone's GetPlayerSpawnRelative() assumes.
FLOOR_LOCATION = (0.0, 0.0, -10.0)

# Unity's Showcase has one Directional Light plus ambient. Pitch -50 puts the
# sun high enough to light the zones' top faces without flattening them.
SUN_ROTATION = (0.0, -50.0, 30.0)  # (roll, pitch, yaw)

# Z1..Z5 all teleport the player themselves; this only decides where you stand
# for the frame before the initial zone applies its own pose. Matches Z1's
# 2.5 m back, lifted clear of the floor so the capsule does not spawn half
# buried.
PLAYER_START_LOCATION = (-250.0, 0.0, 100.0)


# ---------------------------------------------------------------------- setup

def prime_asset_registry():
    """
    Make sure the Asset Registry has seen our content before we ask about it.

    Same call as the other two scripts. A full editor does start the registry
    search on its own, but asynchronously, and -ExecCmds fires as soon as
    initialisation is done -- so a scan that has not reached the plugin yet
    would have does_asset_exist() answer about an empty world, sending this
    script down the "create a new level" path over a map plainly on disk and
    failing to load the floor material. Scanning synchronously first removes
    the race; it is a no-op when the search already covered these paths.
    """
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        [PLUGIN_CONTENT], True)


def load_class(class_path):
    """Resolve a native UClass by /Script/ path, with a readable failure."""
    loaded = unreal.load_object(None, class_path)
    if loaded is None:
        raise RuntimeError(
            'could not resolve ' + class_path + ' -- is the HapbeatSDKSamples '
            'module compiled into the project running this script?')
    return loaded


def open_map(level_editor):
    """
    Open the map, creating it on first run.

    Reuse over recreate: the package path is what a user's shortcut, a level
    reference and this repo's docs all point at, so it has to survive a
    regenerate. new_level() is therefore the first-run path only.
    """
    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        if not level_editor.load_level(MAP_PATH):
            raise RuntimeError('could not open existing map ' + MAP_PATH)
        unreal.log('[Hapbeat] opened existing map ' + MAP_PATH)
    else:
        if not level_editor.new_level(MAP_PATH):
            raise RuntimeError('could not create map ' + MAP_PATH)
        unreal.log('[Hapbeat] created map ' + MAP_PATH)


def clear_generated_actors(editor_actors):
    """Delete the previous run's actors, identified by GENERATED_TAG."""
    removed = 0
    for actor in editor_actors.get_all_level_actors():
        if actor is None:
            continue
        if GENERATED_TAG in [str(tag) for tag in actor.get_editor_property('tags')]:
            editor_actors.destroy_actor(actor)
            removed += 1
    if removed:
        unreal.log('[Hapbeat] cleared {} actor(s) from the previous run.'.format(removed))


# --------------------------------------------------------------- world settings

def resolve_world_settings(editor_actors):
    """
    Get the map's AWorldSettings so GameMode Override can be set on it.

    There is no single blessed Python route to it -- UWorld::GetWorldSettings()
    is not a UFUNCTION, and AWorldSettings is not listed in the Scene Outliner,
    so neither the obvious accessor nor the obvious actor scan is guaranteed to
    answer. Three routes are tried in order of directness and the first that
    answers wins; if none does, we stop rather than save a map that quietly
    plays with the wrong pawn. On 5.4 the second route is the one that answers.
    """
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if world is None:
        raise RuntimeError('no editor world -- the map failed to open')

    # 1. An accessor, if this engine build exposes one.
    getter = getattr(world, 'get_world_settings', None)
    if callable(getter):
        settings = getter()
        if settings is not None:
            return settings

    # 2. The actor's deterministic subobject path. UWorld names it "WorldSettings"
    #    when it spawns it, so it is findable inside the world by fixed path.
    settings = unreal.find_object(world, 'PersistentLevel.WorldSettings')
    if settings is not None:
        return settings

    # 3. Last resort: scan the level. Cheap, and correct on any build where
    #    AWorldSettings does come back from the actor iterator.
    for actor in editor_actors.get_all_level_actors():
        if isinstance(actor, unreal.WorldSettings):
            return actor

    raise RuntimeError(
        'could not reach the map\'s World Settings, so GameMode Override was '
        'not set. Open ' + MAP_PATH + ' and set World Settings -> GameMode '
        'Override to "Hapbeat Showcase Game Mode" by hand, or fix '
        'resolve_world_settings() in this script.')


def set_game_mode(editor_actors):
    """
    Point the map at AHapbeatShowcaseGameMode.

    Map-level, not project-level: a plugin must not rewrite its host project's
    settings, and a sample that only works after the user edits Project
    Settings is not a sample you can just press Play on.
    """
    settings = resolve_world_settings(editor_actors)
    settings.set_editor_property('default_game_mode', load_class(SHOWCASE_GAME_MODE_CLASS))
    unreal.log('[Hapbeat] World Settings -> GameMode Override = HapbeatShowcaseGameMode.')


# --------------------------------------------------------------------- spawning

def finish(actor, label):
    """Tag it as ours (so a re-run reclaims it) and give it a readable name."""
    actor.set_editor_property('tags', [GENERATED_TAG])
    actor.set_actor_label(label)
    unreal.log('[Hapbeat] placed {}'.format(label))
    return actor


def spawn_floor(editor_actors):
    mesh = unreal.EditorAssetLibrary.load_asset(CUBE_MESH)
    if mesh is None:
        raise RuntimeError('could not load ' + CUBE_MESH)

    actor = editor_actors.spawn_actor_from_object(
        mesh, unreal.Vector(*FLOOR_LOCATION), unreal.Rotator(0.0, 0.0, 0.0))
    actor.set_actor_scale3d(unreal.Vector(*FLOOR_SCALE))

    # Oak veneer, the same board Unity's Showcase floor uses. Missing art is a
    # warning rather than an error: the map is still playable in engine grey,
    # and import_showcase_assets.py can be run afterwards.
    material = unreal.EditorAssetLibrary.load_asset(FLOOR_MATERIAL)
    if material is None:
        unreal.log_warning(
            '[Hapbeat] {} not found -- floor left with the default material. '
            'Run import_showcase_assets.py first.'.format(FLOOR_MATERIAL))
    else:
        actor.static_mesh_component.set_material(0, material)

    return finish(actor, 'ShowcaseFloor')


def spawn_lighting(editor_actors):
    """
    One sun plus sky: the UE equivalent of Unity's single Directional Light
    and its ambient/skybox, and no more than that. Intensities are left at the
    engine defaults so this stays a lighting setup a reader can recognise
    rather than a tuned one they have to reverse-engineer.
    """
    sun = editor_actors.spawn_actor_from_class(
        unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 1000.0),
        unreal.Rotator(*SUN_ROTATION))
    finish(sun, 'ShowcaseSun')

    sky_light = editor_actors.spawn_actor_from_class(
        unreal.SkyLight, unreal.Vector(0.0, 0.0, 1000.0), unreal.Rotator(0.0, 0.0, 0.0))
    # Real-time capture keeps the ambient correct without a lighting build --
    # which a headless generator cannot run, and which a sample should not
    # require. It needs a movable component, hence the mobility first.
    component = sky_light.get_editor_property('light_component')
    component.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
    component.set_editor_property('real_time_capture', True)
    finish(sky_light, 'ShowcaseSkyLight')

    # Gives the sky something to capture; without it the skylight sees black.
    atmosphere = editor_actors.spawn_actor_from_class(
        unreal.SkyAtmosphere, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))
    finish(atmosphere, 'ShowcaseSkyAtmosphere')


def spawn_player_start(editor_actors):
    actor = editor_actors.spawn_actor_from_class(
        unreal.PlayerStart, unreal.Vector(*PLAYER_START_LOCATION),
        unreal.Rotator(0.0, 0.0, 0.0))
    return finish(actor, 'ShowcasePlayerStart')


def spawn_showcase_actor(editor_actors):
    """
    The one actor that matters. It sits at the origin because every zone is
    spawned at its transform and every zone is authored around its own origin.
    """
    actor = editor_actors.spawn_actor_from_class(
        load_class(SHOWCASE_ACTOR_CLASS), unreal.Vector(0.0, 0.0, 0.0),
        unreal.Rotator(0.0, 0.0, 0.0))
    return finish(actor, 'HapbeatShowcase')


def main():
    prime_asset_registry()

    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor_actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    open_map(level_editor)
    # Before spawning anything: this is the one step with no fallback, so fail
    # here rather than after building a map that plays with the wrong pawn.
    set_game_mode(editor_actors)

    clear_generated_actors(editor_actors)
    spawn_floor(editor_actors)
    spawn_lighting(editor_actors)
    spawn_player_start(editor_actors)
    spawn_showcase_actor(editor_actors)

    if not level_editor.save_current_level():
        raise RuntimeError('could not save ' + MAP_PATH)
    unreal.log('[Hapbeat] Showcase map written: ' + MAP_PATH)


main()
