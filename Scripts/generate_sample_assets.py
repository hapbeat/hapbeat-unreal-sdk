# Copyright (c) 2026 Hapbeat. MIT License.
"""
Generates the sample assets that ship inside the plugin's Content.

Run once, headless, and commit the .uasset files it writes:

    UnrealEditor-Cmd.exe <YourProject>.uproject ^
        -run=pythonscript -script="<this file>"

Why a script rather than hand-authored assets: the entries have to stay in step
with the sample actors and the Kit manifests, and a generator makes that
relationship explicit and re-runnable. Regenerate after changing either, then
commit the result -- the assets themselves are what ships, so an end user never
runs this.

Entry ORDER matters: AHapbeatBasicExampleActor maps entries to keys by index
([0] Space, [1] R, [2] F).
"""

import os
import unreal

PLUGIN_CONTENT = '/HapbeatSDK/HapbeatSamples'
KIT_REL = 'BasicExample/Kit/basic-exam-kit'

# Authored in the shipped manifest (schema 2.0.0): both the events and
# stream_events entries set parameters.intensity = 0.5.
MANIFEST_INTENSITY = 0.5


def plugin_content_dir():
    """Absolute path of the plugin's Content, resolved from the mount point."""
    return unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_plugins_dir() + 'HapbeatSDK/Content/HapbeatSamples/')


def create_or_replace(package_path, asset_name, asset_class, factory):
    full = '{}/{}'.format(package_path, asset_name)
    if unreal.EditorAssetLibrary.does_asset_exist(full):
        unreal.EditorAssetLibrary.delete_asset(full)
    return unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name, package_path, asset_class, factory)


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


def make_clip(asset_name, wav_rel_path):
    """A UHapbeatClip holding the WAV's PCM verbatim (see the C++ class doc)."""
    wav = os.path.join(plugin_content_dir(), wav_rel_path)
    with open(wav, 'rb') as f:
        data = f.read()
    sample_rate, channels, pcm = parse_wav(data)

    clip = create_or_replace(PLUGIN_CONTENT + '/BasicExample', asset_name,
                             unreal.HapbeatClip, unreal.HapbeatClipFactory())
    if not unreal.HapbeatEventMapScripting.import_wav_into_clip(clip, wav):
        raise RuntimeError('import failed for ' + wav)
    unreal.EditorAssetLibrary.save_loaded_asset(clip)
    unreal.log('[Hapbeat] {}: {} Hz, {} ch, {} bytes'.format(
        asset_name, sample_rate, channels, len(pcm)))
    return clip


def main():
    clip = make_clip('HC_sine_100hz_1s', KIT_REL + '/stream-clips/sine_100hz_1s.wav')

    event_map = create_or_replace(PLUGIN_CONTENT + '/BasicExample', 'EM_BasicExample',
                                  unreal.HapbeatEventMap, unreal.HapbeatEventMapFactory())

    api = unreal.HapbeatEventMapScripting
    api.clear_entries(event_map)
    # ORDER IS THE CONTRACT: [0] Space, [1] R, [2] F.
    api.add_entry(event_map, unreal.HapticMode.STREAM_CLIP, 'basic-exam-kit', 'sine_100hz_1s',
                  1.0, False, MANIFEST_INTENSITY, clip, 'demo_stream_sine_100hz')
    api.add_entry(event_map, unreal.HapticMode.STREAM_CLIP, 'basic-exam-kit', 'sine_100hz_1s',
                  1.0, True, MANIFEST_INTENSITY, clip, 'demo_stream_loop_100hz')
    api.add_entry(event_map, unreal.HapticMode.COMMAND, 'basic-exam-kit', 'sine_200hz_1s',
                  1.0, False, MANIFEST_INTENSITY, None, 'demo_command_sine_200hz')
    unreal.EditorAssetLibrary.save_loaded_asset(event_map)
    unreal.log('[Hapbeat] Generated EM_BasicExample with 3 entries.')


main()
