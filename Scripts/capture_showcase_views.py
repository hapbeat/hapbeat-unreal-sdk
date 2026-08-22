# Copyright (c) 2026 Hapbeat. MIT License.
"""
Screenshots every Showcase zone, unattended, so a layout change can be checked
without playing through five zones by hand.

    UnrealEditor.exe <YourProject>.uproject ^
        -ExecCmds="py <this file>" -unattended -nosplash

A FULL EDITOR, not -run=pythonscript: this drives Play-In-Editor, which needs the
editor's viewport and tick loop (the same reason generate_showcase_map.py is run
this way).

Output: Saved/Screenshots/WindowsEditor/showcase_z<k>.png, one per zone, plus a
second "<k>b" shot for the zones whose interesting state only exists after
something has been done to them: showcase_z2b (door open), showcase_z4b (the
stream loop running) and showcase_z5b (a light and a heavy projectile parked in
front of the muzzle).

THE SHOTS INCLUDE THE UI. `HighResShot` re-renders the SCENE at an arbitrary
resolution and never composites Slate, so the HUD, Z4's slider panel and Z5's
charge bar -- the whole point of several of these captures -- were missing from
every image. `Shot showui` instead goes through
UGameViewportClient::HandleScreenshotCommand -> FScreenshotRequest with
bShowUI = true, which makes ProcessScreenShots take an FSlateApplication
screenshot of the WINDOW the game viewport lives in (GameViewportClient.cpp).
Two consequences, both accepted: there is no resolution argument (the shot comes
out at whatever the window is), and because this PIE session plays inside the
level viewport rather than a floating window, the image is the whole editor
window with the game -- UI included -- inside it.

HOW IT DRIVES PIE. UE exposes no Python entry point for starting a play session
-- ULevelEditorSubsystem only offers EditorPlaySimulate(), which is
Simulate-In-Editor and spawns no player, so the held rod and blaster would never
appear. The plugin's editor module therefore ships two thin wrappers around
UEditorEngine::RequestPlaySession (see HapbeatSampleCaptureLibrary.h) and this
script calls those.

Everything after that is a state machine on a Slate post-tick callback, because
the editor has to keep ticking between the steps: nothing here can block. The
steps are load map -> start PIE -> wait for a game world -> per zone (switch,
settle, shoot, then optionally act, settle again and shoot again) -> stop PIE. PIE is stopped at the end; the editor is left
running, so close it yourself.
"""

import unreal

MAP_PATH = '/HapbeatSDK/HapbeatSamples/Showcase/Maps/Showcase'

# Zones to capture, and the view pitch (degrees, negative looks down) to use for
# each. Z3 and Z5 hold a rod / blaster in front of the camera that sits below the
# horizon, so those two look down a little to bring it into frame.
ZONES = [
    (1, 0.0),
    (2, 0.0),
    (3, -20.0),
    (4, 0.0),
    (5, -12.0),
]

# Where the shots land. An ABSOLUTE path, spelled out here rather than left to
# the engine: FScreenshotRequest only prefixes a bare name with
# UEngine::GameScreenshotSaveDirectory, and that field is filled in by
# UGameEngine::Init -- which the editor never runs -- so a bare name would be
# written relative to the engine binary instead. FPaths::ScreenShotDir() is the
# same Saved/Screenshots/<Platform>/ the earlier HighResShot output went to; it
# comes back relative to the engine binary, hence the conversion.
SHOT_DIRECTORY = unreal.Paths.convert_relative_path_to_full(
    unreal.Paths.screen_shot_dir()).rstrip('/\\') + '/'

# Seconds to let a zone settle before shooting it: long enough for the switcher's
# deferred player teleport and the zones' deferred rod/blaster mount (both of
# which happen on a Tick after the switch), and for one physics frame to place
# anything that was reset.
SETTLE_SECONDS = 1.0

# The zones that need a second shot, keyed by zone index:
#   'action'  -- called once, with the PIE world, right after the first shot.
#   'settle'  -- seconds to wait before the second shot. The door's is long
#                enough for its 2.0 s opening tween to finish; the projectiles
#                only have to exist.
# Anything not listed here is captured once, as before.
FOLLOW_UP_SETTLE_DEFAULT = 1.0


def act_open_door(world):
    """Z2: swing the door open through the same path the F key takes."""
    for door in unreal.GameplayStatics.get_all_actors_of_class(
            world, unreal.HapbeatShowcaseZ2DoorActor):
        door.debug_set_door_open(True)


def act_toggle_stream(world):
    """Z4: start the stream loop through the same path the space bar takes."""
    for console in unreal.GameplayStatics.get_all_actors_of_class(
            world, unreal.HapbeatShowcaseZ4StreamConsoleActor):
        console.debug_toggle_stream()


def act_spawn_projectiles(world):
    """Z5: park a light and a heavy projectile in front of the muzzle."""
    for zone in unreal.GameplayStatics.get_all_actors_of_class(
            world, unreal.HapbeatShowcaseZ5ChargeShotActor):
        zone.spawn_projectile_preview(False)
        zone.spawn_projectile_preview(True)


FOLLOW_UPS = {
    2: {'action': act_open_door, 'settle': 2.5},
    # Z4's loop is running as soon as the toggle returns; the wait is only there
    # so the panel has repainted. Check the log for "Stream begin ... loop=1" to
    # tell a running loop from a panel that merely looks the same.
    4: {'action': act_toggle_stream, 'settle': 1.0},
    5: {'action': act_spawn_projectiles, 'settle': 1.0},
}

# Give up waiting for PIE rather than spin forever in an unattended run.
PIE_START_TIMEOUT_SECONDS = 30.0


def log(message):
    unreal.log('[Hapbeat] ' + message)


def find_switcher(world):
    """The AHapbeatShowcaseActor in the PIE world, or None."""
    if world is None:
        return None
    actors = unreal.GameplayStatics.get_all_actors_of_class(
        world, unreal.HapbeatShowcaseActor)
    return actors[0] if actors else None


def find_character(world):
    """The Showcase's own pawn in the PIE world, or None."""
    if world is None:
        return None
    actors = unreal.GameplayStatics.get_all_actors_of_class(
        world, unreal.HapbeatShowcaseCharacter)
    return actors[0] if actors else None


class ShowcaseCapture:
    """
    A tick-driven walk through the zones.

    Written as a state machine rather than as a loop with sleeps because the
    editor only advances -- starts PIE, runs a frame, writes a screenshot --
    while it is ticking, and a Python loop that blocked would stop exactly that.
    """

    def __init__(self):
        self.handle = None
        self.elapsed = 0.0
        self.zone_cursor = 0
        self.state = 'start_pie'
        self.editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

    # ---------------------------------------------------------------- lifecycle

    def run(self):
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def finish(self, message):
        log(message)
        unreal.HapbeatSampleCaptureLibrary.end_play_in_editor()
        if self.handle is not None:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None

    # -------------------------------------------------------------------- steps

    def tick(self, delta_seconds):
        self.elapsed += delta_seconds
        handler = getattr(self, 'step_' + self.state, None)
        if handler is None:
            self.finish('unknown capture state "{}" -- stopping.'.format(self.state))
            return
        handler()

    def enter(self, state):
        self.state = state
        self.elapsed = 0.0

    def step_start_pie(self):
        if not unreal.HapbeatSampleCaptureLibrary.start_play_in_editor():
            self.finish('could not start PIE -- see the log above.')
            return
        log('PIE requested; waiting for the game world.')
        self.enter('wait_for_world')

    def step_wait_for_world(self):
        # The request is queued, so the world appears a tick or two later.
        if self.editor_subsystem.get_game_world() is not None \
                and unreal.HapbeatSampleCaptureLibrary.is_playing_in_editor():
            self.enter('next_zone')
            return
        if self.elapsed > PIE_START_TIMEOUT_SECONDS:
            self.finish('PIE did not start within {:.0f}s -- stopping.'.format(
                PIE_START_TIMEOUT_SECONDS))

    def step_next_zone(self):
        if self.zone_cursor >= len(ZONES):
            self.finish('captured {} zone(s); stopping PIE.'.format(len(ZONES)))
            return

        zone_index, view_pitch = ZONES[self.zone_cursor]
        world = self.editor_subsystem.get_game_world()
        switcher = find_switcher(world)
        if switcher is None:
            self.finish('no Hapbeat Showcase actor in the PIE world -- is the right map open?')
            return

        switcher.set_active_zone(zone_index)
        character = find_character(world)
        if character is not None:
            # After the switch: the switcher teleports the player as part of it,
            # and this only adjusts the pitch it left level.
            character.set_view_pitch_for_capture(view_pitch)
        log('zone {} selected; settling for {:.1f}s.'.format(zone_index, SETTLE_SECONDS))
        self.enter('settle')

    def step_settle(self):
        if self.elapsed < SETTLE_SECONDS:
            return
        zone_index = ZONES[self.zone_cursor][0]
        self.shoot('showcase_z{}'.format(zone_index))
        log('zone {} captured.'.format(zone_index))
        if zone_index in FOLLOW_UPS:
            self.enter('follow_up_action')
            return
        self.zone_cursor += 1
        self.enter('next_zone')

    def step_follow_up_action(self):
        zone_index = ZONES[self.zone_cursor][0]
        follow_up = FOLLOW_UPS[zone_index]
        world = self.editor_subsystem.get_game_world()
        if world is None:
            self.finish('the PIE world went away mid-capture -- stopping.')
            return
        follow_up['action'](world)
        log('zone {} follow-up action taken; settling for {:.1f}s.'.format(
            zone_index, follow_up.get('settle', FOLLOW_UP_SETTLE_DEFAULT)))
        self.enter('follow_up_settle')

    def step_follow_up_settle(self):
        zone_index = ZONES[self.zone_cursor][0]
        follow_up = FOLLOW_UPS[zone_index]
        if self.elapsed < follow_up.get('settle', FOLLOW_UP_SETTLE_DEFAULT):
            return
        self.shoot('showcase_z{}b'.format(zone_index))
        log('zone {} follow-up captured.'.format(zone_index))
        self.zone_cursor += 1
        self.enter('next_zone')

    def shoot(self, filename):
        # The write happens asynchronously, over the next frame or two -- which
        # is fine, the next settle gives it that time.
        #
        # The command is `Shot showui`, not HighResShot, so Slate is composited
        # in (see the module docstring). "-nosuffix" keeps the name exactly as
        # asked instead of appending 00000, so a re-run overwrites the previous
        # image rather than piling up numbered copies; the path is quoted
        # because FParse::Value only tolerates spaces inside quotes.
        unreal.SystemLibrary.execute_console_command(
            self.editor_subsystem.get_game_world(),
            'Shot showui filename="{}{}.png" -nosuffix'.format(SHOT_DIRECTORY, filename))


def main():
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level_editor.load_level(MAP_PATH):
        raise RuntimeError('could not open ' + MAP_PATH
                           + ' -- run generate_showcase_map.py first.')
    log('opened ' + MAP_PATH)
    ShowcaseCapture().run()


main()
