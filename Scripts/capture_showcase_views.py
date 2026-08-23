# Copyright (c) 2026 Hapbeat. MIT License.
"""
Screenshots every Showcase zone, unattended, so a layout change can be checked
without playing through five zones by hand.

    UnrealEditor.exe <YourProject>.uproject ^
        -ExecCmds="py <this file>" -unattended -nosplash

A FULL EDITOR, not -run=pythonscript: this drives Play-In-Editor, which needs the
editor's viewport and tick loop (the same reason generate_showcase_map.py is run
this way).

Output: Saved/Screenshots/WindowsEditor/showcase_z<k>.png, one per zone, plus
one or more suffixed shots for the zones whose interesting state only exists
after something has been done to them -- or cannot be seen from the zone's own
spawn point: showcase_z1b (the pin rack, from 1.5 m away), showcase_z2b (door
open), showcase_z3b (the rod, the line and the shark, from 2 m back),
showcase_z4b (the stream loop running), showcase_z5b (a light and a heavy
projectile nose-on), showcase_z5c (the same two from straight above, which is
the shot that shows whether the nose points the way they fly) and showcase_z5d
(the charge bar past its heavy threshold, i.e. in its high colour).

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

# The extra shots, keyed by zone index -- a LIST per zone, taken in order, since
# Z5 wants the projectiles photographed from two angles:
#   'suffix'  -- appended to the zone's name, so 'b' writes showcase_z<k>b.png.
#   'action'  -- called once, with the PIE world, right before the settle.
#   'settle'  -- seconds to wait before shooting. The door's is long enough for
#                its 2.0 s opening tween to finish; the projectiles only have to
#                exist.
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
    """Z5: park a light and a heavy projectile in front of the player, nose-on."""
    for zone in unreal.GameplayStatics.get_all_actors_of_class(
            world, unreal.HapbeatShowcaseZ5ChargeShotActor):
        zone.spawn_projectile_preview(False, 0.0)
        zone.spawn_projectile_preview(True, 0.0)


def act_spawn_projectiles_top_down(world):
    """
    Z5: the same two, seen from directly above.

    Nose-on says nothing about which way a projectile points -- a missile aimed
    at the camera and one aimed away look identical, and turning them broadside
    only trades that ambiguity for another (a roll about the long axis is
    invisible edge-on). From straight above, the projectile is laid out flat
    against the floor: the nose is at one end of the silhouette, the shot
    direction runs UP the image, and both questions are answered in one picture.

    The player is moved 4 m above the zone and pitched almost straight down
    (-89, not -90: at exactly -90 the yaw stops meaning anything and which way
    "up the image" points is undefined). The previews are then placed from that
    same view, so they land in frame.
    """
    place_player_in_zone(world, unreal.HapbeatShowcaseZ5ChargeShotActor,
                         unreal.Vector(150.0, 0.0, 400.0), yaw=0.0, pitch=-89.0)
    for zone in unreal.GameplayStatics.get_all_actors_of_class(
            world, unreal.HapbeatShowcaseZ5ChargeShotActor):
        zone.spawn_projectile_preview(False, 0.0)
        zone.spawn_projectile_preview(True, 0.0)


def act_charge_bar_heavy(world):
    """
    Z5: park the charge bar above the heavy threshold.

    The bar changes colour there, and that colour change is the only on-screen
    feedback for "this shot will be a heavy one" -- so it is worth a picture of
    its own. 0.9 is clear of the 0.7 threshold rather than on top of it.
    """
    for zone in unreal.GameplayStatics.get_all_actors_of_class(
            world, unreal.HapbeatShowcaseZ5ChargeShotActor):
        zone.debug_set_charge_for_capture(0.9)


def act_stand_at_pin_rack(world):
    """
    Z1: stand 1.5 m short of the rack, looking down at it.

    The zone spawn is at the bowler's end, 7 m back, where six 78 cm pins are a
    few pixels tall -- too small to tell an upright rack from an upside-down one,
    which is the thing this shot exists to check.
    """
    place_player_in_zone(world, unreal.HapbeatShowcaseZ1BowlingActor,
                         unreal.Vector(450.0, 0.0, 0.0), yaw=0.0, pitch=-15.0)


def act_stand_back_from_shark(world):
    """
    Z3: stand 2 m back from the zone origin, looking down at the rig.

    The zone's own spawn puts the camera almost on top of the shark, where the
    rod fills the frame and the line disappears off the bottom of it. From here
    the rod tip, the whole line and the shark are all in one shot, which is what
    this capture is for -- the line hanging from the wrong end of the rod is
    exactly the kind of thing it has to be able to show.
    """
    place_player_in_zone(world, unreal.HapbeatShowcaseZ3FishingActor,
                         unreal.Vector(-200.0, 0.0, 0.0), yaw=0.0, pitch=-25.0)


def place_player_in_zone(world, zone_class, zone_relative_offset, yaw, pitch):
    """
    Put the player at an offset from a zone's own origin.

    Zone-relative, not world: the zones sit 30 m apart in the map (zone k at
    Y = (k-1) * 3000), so a world coordinate would only be right for one of them
    and would silently drift if the layout ever changed.
    """
    zones = unreal.GameplayStatics.get_all_actors_of_class(world, zone_class)
    switcher = find_switcher(world)
    if not zones or switcher is None:
        log('could not place the player: zone or switcher missing.')
        return
    origin = zones[0].get_actor_location()
    switcher.debug_place_player(
        unreal.Vector(origin.x + zone_relative_offset.x,
                      origin.y + zone_relative_offset.y,
                      origin.z + zone_relative_offset.z),
        yaw, pitch)


FOLLOW_UPS = {
    1: [{'suffix': 'b', 'action': act_stand_at_pin_rack, 'settle': 1.0}],
    2: [{'suffix': 'b', 'action': act_open_door, 'settle': 2.5}],
    3: [{'suffix': 'b', 'action': act_stand_back_from_shark, 'settle': 1.0}],
    # Z4's loop is running as soon as the toggle returns; the wait is only there
    # so the panel has repainted. Check the log for "Stream begin ... loop=1" to
    # tell a running loop from a panel that merely looks the same.
    4: [{'suffix': 'b', 'action': act_toggle_stream, 'settle': 1.0}],
    5: [{'suffix': 'b', 'action': act_spawn_projectiles, 'settle': 1.0},
        {'suffix': 'c', 'action': act_spawn_projectiles_top_down, 'settle': 1.0},
        # The bar repaints on the next Slate tick; half a second is only so the
        # shot cannot race it.
        {'suffix': 'd', 'action': act_charge_bar_heavy, 'settle': 0.5}],
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
        self.follow_up_cursor = 0
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
        if FOLLOW_UPS.get(zone_index):
            self.follow_up_cursor = 0
            self.enter('follow_up_action')
            return
        self.zone_cursor += 1
        self.enter('next_zone')

    def current_follow_up(self):
        """The follow-up being worked on, or None once the zone's list is done."""
        follow_ups = FOLLOW_UPS.get(ZONES[self.zone_cursor][0], [])
        if self.follow_up_cursor >= len(follow_ups):
            return None
        return follow_ups[self.follow_up_cursor]

    def step_follow_up_action(self):
        zone_index = ZONES[self.zone_cursor][0]
        follow_up = self.current_follow_up()
        if follow_up is None:
            self.zone_cursor += 1
            self.enter('next_zone')
            return
        world = self.editor_subsystem.get_game_world()
        if world is None:
            self.finish('the PIE world went away mid-capture -- stopping.')
            return
        follow_up['action'](world)
        log('zone {}{} action taken; settling for {:.1f}s.'.format(
            zone_index, follow_up['suffix'],
            follow_up.get('settle', FOLLOW_UP_SETTLE_DEFAULT)))
        self.enter('follow_up_settle')

    def step_follow_up_settle(self):
        zone_index = ZONES[self.zone_cursor][0]
        follow_up = self.current_follow_up()
        if follow_up is None:
            self.zone_cursor += 1
            self.enter('next_zone')
            return
        if self.elapsed < follow_up.get('settle', FOLLOW_UP_SETTLE_DEFAULT):
            return
        self.shoot('showcase_z{}{}'.format(zone_index, follow_up['suffix']))
        log('zone {}{} captured.'.format(zone_index, follow_up['suffix']))
        # Straight on to the next follow-up for this zone; step_follow_up_action
        # is what notices the list has run out and moves to the next zone.
        self.follow_up_cursor += 1
        self.enter('follow_up_action')

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
