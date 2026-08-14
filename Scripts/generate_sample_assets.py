# Copyright (c) 2026 Hapbeat. MIT License.
"""
Generates the sample assets that ship inside the plugin's Content.

Run headless and commit the .uasset files it writes:

    UnrealEditor-Cmd.exe <YourProject>.uproject ^
        -run=pythonscript -script="<this file>"

Re-running over already-generated assets is supported and is the normal case
(regenerate after editing a sample actor or a Kit manifest, then commit).

Why a script rather than hand-authored assets: the entries have to stay in step
with the sample actors and the Kit manifests, and a generator makes that
relationship explicit and re-runnable. Regenerate after changing either, then
commit the result -- the assets themselves are what ships, so an end user never
runs this.

Entry ORDER matters for BasicExample: AHapbeatBasicExampleActor maps entries to
keys by index ([0] Space, [1] R, [2] F). The Showcase actors look their entries
up by event name instead, so a regenerate (which mints fresh entry GUIDs) does
not disturb them.
"""

import json
import os
import unreal

PLUGIN_CONTENT = '/HapbeatSDK/HapbeatSamples'
BASIC_KIT = 'BasicExample/Kit/basic-exam-kit'
SHOWCASE_KIT = 'Showcase/Kit/showcase-kit'


def plugin_content_dir():
    """Absolute path of the plugin's Content, resolved from the mount point."""
    return unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_plugins_dir() + 'HapbeatSDK/Content/HapbeatSamples/')


def prime_asset_registry():
    """
    Populate the Asset Registry for our content -- a commandlet does not.

    The registry is either searched asynchronously (started at editor/cook
    startup) or "synchronously and on-demand, requiring ScanPathsSynchronous or
    SearchAllAssets" (IAssetRegistry.h). `-run=pythonscript` is the second case,
    and nothing kicks the search off, so the registry answers about an empty
    world: does_asset_exist() reports False for assets plainly on disk, this
    script takes the create path, and AssetTools then discovers the collision
    itself and raises the interactive "replace existing object?" prompt --
    auto-answered *No* under -unattended, returning null. That null was reaching
    ImportWavIntoClip as a null Clip, whose failure read as "import failed".
    """
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        [PLUGIN_CONTENT], True)


def load_or_create(package_path, asset_name, asset_class, factory):
    """
    Reuse the asset if it is already there, rather than delete-and-recreate.

    Deleting would break every reference to it -- the Event Map points at the
    clips, and the sample actor points at the Event Map -- and a delete that the
    editor refuses (because of those references) leaves create_asset returning
    null. Rewriting the existing object keeps the same package and GUIDs, so a
    regenerate is safe to run over a working project.
    """
    full = '{}/{}'.format(package_path, asset_name)
    if unreal.EditorAssetLibrary.does_asset_exist(full):
        asset = unreal.EditorAssetLibrary.load_asset(full)
    else:
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, package_path, asset_class, factory)
    if asset is None:
        raise RuntimeError('could not open or create ' + full)
    return asset


def manifest_intensities(kit_rel):
    """
    (event id, mode) -> parameters.intensity, straight from the Kit manifest.

    Mirrors what the editor's Refresh Intensities does
    (FHapbeatManifestIntensityBaker::ParseBucket): "events" is Command,
    "stream_events" is Stream Clip, keys are already full event ids, and a
    missing intensity defaults to 1.0. Reading the manifest here rather than
    hardcoding a constant is what stops a generated asset from going stale the
    moment somebody presses Refresh on it -- the Showcase Kit alone authors
    eight different intensities between 0.14 and 0.60.

    The filename match is the baker's lenient one (any *.json with "manifest"
    in the name), so a bare manifest.json is found too.
    """
    kit_dir = os.path.join(plugin_content_dir(), kit_rel)
    names = [n for n in os.listdir(kit_dir)
             if n.lower().endswith('.json') and 'manifest' in n.lower()]
    if not names:
        raise RuntimeError('no manifest json in ' + kit_dir)

    table = {}
    for name in sorted(names):
        with open(os.path.join(kit_dir, name), encoding='utf-8') as f:
            root = json.load(f)
        for bucket, mode in (('events', 'COMMAND'), ('stream_events', 'STREAM_CLIP')):
            for event_id, body in (root.get(bucket) or {}).items():
                params = (body or {}).get('parameters') or {}
                table[(event_id, mode)] = float(params.get('intensity', 1.0))
    return table


def intensity_of(table, mode_name, category, event_name):
    """
    Fail loudly on a manifest/entry mismatch instead of baking a wrong value.

    An entry the manifest does not carry is exactly what makes Refresh
    Intensities report "unresolved" later, so it is better caught here, while
    the asset is being written, than shipped.
    """
    key = ('{}.{}'.format(category, event_name), mode_name)
    if key not in table:
        raise RuntimeError('manifest has no {} entry for {}'.format(mode_name, key[0]))
    return table[key]


def parse_wav(data):
    """
    Minimal RIFF reader: returns (sample_rate, channels, pcm_bytes).

    The C++ side has its own parser, but it is a plain static (not a UFUNCTION)
    and therefore invisible to Python. The format accepted here is deliberately
    the same narrow one the SDK streams: 16-bit PCM, no extensible headers.
    """
    if data[0:4] != b'RIFF' or data[8:12] != b'WAVE':
        raise RuntimeError('not a RIFF/WAVE file')

    import struct
    pos = 12
    sample_rate = 0
    channels = 0
    bits = 0
    pcm = None
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        size = struct.unpack_from('<I', data, pos + 4)[0]
        body = pos + 8
        if cid == b'fmt ':
            _fmt, channels, sample_rate, _bps, _align, bits = struct.unpack_from('<HHIIHH', data, body)
        elif cid == b'data':
            pcm = data[body:body + size]
        pos = body + size + (size & 1)   # chunks are word-aligned

    if pcm is None or sample_rate == 0 or channels == 0:
        raise RuntimeError('missing fmt/data chunk')
    if bits != 16:
        raise RuntimeError('expected 16-bit PCM, got {}'.format(bits))
    return sample_rate, channels, pcm


def find_wav(kit_rel, name):
    """Stream clips first, then install clips -- a Kit may carry either."""
    base = plugin_content_dir()
    for sub in ('stream-clips', 'install-clips', 'clips'):
        cand = os.path.join(base, kit_rel, sub, name + '.wav')
        if os.path.isfile(cand):
            return os.path.relpath(cand, base).replace(os.sep, '/')
    return None


def make_clip(package, asset_name, wav_rel_path):
    """A UHapbeatClip holding the WAV's PCM verbatim (see the C++ class doc)."""
    wav = os.path.join(plugin_content_dir(), wav_rel_path)
    with open(wav, 'rb') as f:
        data = f.read()
    sample_rate, channels, pcm = parse_wav(data)

    clip = load_or_create(package, asset_name, unreal.HapbeatClip, unreal.HapbeatClipFactory())
    if not unreal.HapbeatEventMapScripting.import_wav_into_clip(clip, wav):
        raise RuntimeError('import failed for ' + wav)
    unreal.EditorAssetLibrary.save_loaded_asset(clip)
    unreal.log('[Hapbeat] {}: {} Hz, {} ch, {} bytes'.format(
        asset_name, sample_rate, channels, len(pcm)))
    return clip


def build_basic_example():
    pkg = PLUGIN_CONTENT + '/BasicExample'
    intensities = manifest_intensities(BASIC_KIT)
    clip = make_clip(pkg, 'HC_sine_100hz_1s', BASIC_KIT + '/stream-clips/sine_100hz_1s.wav')

    event_map = load_or_create(pkg, 'EM_BasicExample',
                               unreal.HapbeatEventMap, unreal.HapbeatEventMapFactory())

    api = unreal.HapbeatEventMapScripting
    api.clear_entries(event_map)
    # ORDER IS THE CONTRACT: [0] Space, [1] R, [2] F.
    api.add_entry(event_map, unreal.HapticMode.STREAM_CLIP, 'basic-exam-kit', 'sine_100hz_1s',
                  1.0, False, intensity_of(intensities, 'STREAM_CLIP', 'basic-exam-kit', 'sine_100hz_1s'),
                  clip, 'demo_stream_sine_100hz')
    api.add_entry(event_map, unreal.HapticMode.STREAM_CLIP, 'basic-exam-kit', 'sine_100hz_1s',
                  1.0, True, intensity_of(intensities, 'STREAM_CLIP', 'basic-exam-kit', 'sine_100hz_1s'),
                  clip, 'demo_stream_loop_100hz')
    api.add_entry(event_map, unreal.HapticMode.COMMAND, 'basic-exam-kit', 'sine_200hz_1s',
                  1.0, False, intensity_of(intensities, 'COMMAND', 'basic-exam-kit', 'sine_200hz_1s'),
                  None, 'demo_command_sine_200hz')
    unreal.EditorAssetLibrary.save_loaded_asset(event_map)
    unreal.log('[Hapbeat] Generated EM_BasicExample with 3 entries.')


# (mode, event_name, loop). Order is not load-bearing here -- the Showcase actors
# look their entries up by event name -- but it is kept zone by zone for reading.
SHOWCASE_ENTRIES = [
    ('COMMAND',     'z1_pin_hit',       False),
    ('STREAM_CLIP', 'z2_door_open',     False),
    ('STREAM_CLIP', 'z2_door_close',    False),
    ('COMMAND',     'z2_door_slam',     False),
    ('COMMAND',     'z2_door_lock',     False),
    ('COMMAND',     'z2_door_unlock',   False),
    ('STREAM_CLIP', 'z2_door_rattle',   False),
    ('STREAM_CLIP', 'z3_hook_start',    False),
    ('STREAM_CLIP', 'z3_hook_loop',     True),
    ('STREAM_CLIP', 'z3_hook_release',  False),
    ('STREAM_CLIP', 'z4_stream_loop',   True),
    ('STREAM_CLIP', 'z4_slider_tick',   False),
    ('STREAM_CLIP', 'z5_charge_loop',   True),
    ('STREAM_CLIP', 'z5_charge_thd',    False),
    ('STREAM_CLIP', 'z5_shot_light',    False),
    ('STREAM_CLIP', 'z5_shot_heavy',    False),
    ('STREAM_CLIP', 'z5_tar_hit_light', False),
    ('STREAM_CLIP', 'z5_tar_hit_heavy', False),
]


def build_showcase():
    pkg = PLUGIN_CONTENT + '/Showcase'
    intensities = manifest_intensities(SHOWCASE_KIT)
    event_map = load_or_create(pkg, 'EM_Showcase',
                               unreal.HapbeatEventMap, unreal.HapbeatEventMapFactory())
    api = unreal.HapbeatEventMapScripting
    api.clear_entries(event_map)

    for mode_name, event_name, loop in SHOWCASE_ENTRIES:
        clip = None
        if mode_name == 'STREAM_CLIP':
            rel = find_wav(SHOWCASE_KIT, event_name)
            if rel is None:
                raise RuntimeError('no wav for ' + event_name)
            clip = make_clip(pkg, 'HC_' + event_name, rel)
        api.add_entry(event_map, getattr(unreal.HapticMode, mode_name), 'showcase-kit', event_name,
                      1.0, loop, intensity_of(intensities, mode_name, 'showcase-kit', event_name),
                      clip, event_name)

    unreal.EditorAssetLibrary.save_loaded_asset(event_map)
    unreal.log('[Hapbeat] Generated EM_Showcase with {} entries.'.format(len(SHOWCASE_ENTRIES)))


def main():
    prime_asset_registry()
    build_basic_example()
    build_showcase()


main()
