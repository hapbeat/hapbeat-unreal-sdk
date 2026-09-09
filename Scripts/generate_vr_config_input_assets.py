# Copyright (c) 2026 Hapbeat. MIT License.
"""Install the OpenXR input assets required by VRConfigExample into a project.

Enhanced Input's default Mapping Context list deliberately accepts only assets
below /Game. Plugin content cannot be registered with OpenXR at XR-session
creation time, so this script creates the sample Input Actions and Mapping
Context below the host project's Content directory and records that context in
DefaultInput.ini.

Run once in a full UE Editor (not a commandlet):

  UnrealEditor.exe <Project>.uproject -ExecCmds="py <absolute path>/generate_vr_config_input_assets.py" -nosplash

Restart the editor after it finishes, then use VR Preview. Existing assets at
the same paths are updated in place and mappings from other plugins are kept.
"""

import os

import unreal


ROOT = '/Game/HapbeatVRConfig/Input'
INTERACT_PATH = ROOT + '/IA_HapbeatVRInteract'
RECENTER_PATH = ROOT + '/IA_HapbeatVRRecenter'
NAVIGATE_PATH = ROOT + '/IA_HapbeatVRNavigate'
CONTEXT_PATH = ROOT + '/IMC_HapbeatVRConfig'


def get_or_create(asset_name, package_path, asset_class):
    path = package_path + '/' + asset_name
    asset = unreal.load_asset(path)
    if asset is not None:
        return asset
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    asset = tools.create_asset(asset_name, package_path, asset_class, unreal.DataAssetFactory())
    if asset is None:
        raise RuntimeError('Could not create asset: ' + path)
    return asset


def make_mapping(action, key_name):
    # FKey has no value-taking Python constructor in UE 5.4. Build the struct
    # through its reflected KeyName property instead.
    key = unreal.Key()
    key.set_editor_property('key_name', key_name)
    mapping = unreal.EnhancedActionKeyMapping()
    mapping.set_editor_property('action', action)
    mapping.set_editor_property('key', key)
    return mapping


def configure_project_default(context):
    # EnhancedInputDeveloperSettings is not exposed to the UE 5.4 Python API,
    # but its canonical storage is the project Input config. Preserve every
    # existing mapping and append this context exactly once.
    config_path = os.path.join(unreal.Paths.project_config_dir(), 'DefaultInput.ini')
    mapping = ('+DefaultMappingContexts=(InputMappingContext="%s",'
               'Priority=0,bAddImmediately=True,bRegisterWithUserSettings=False)'
               % context.get_path_name())
    section = '[/Script/EnhancedInput.EnhancedInputDeveloperSettings]'

    with open(config_path, 'r', encoding='utf-8') as config_file:
        text = config_file.read()
    # Replace a previous entry from this setup script too. This keeps the
    # operation idempotent if an asset was renamed or an earlier SDK revision
    # wrote an obsolete path.
    source_path = '/Game/HapbeatVRConfig/Input/IMC_HapbeatVRConfig'
    text = '\n'.join(line for line in text.splitlines() if source_path not in line)

    if section in text:
        text = text.rstrip() + '\n' + mapping + '\n'
    else:
        text = text.rstrip() + '\n\n' + section + '\n' + mapping + '\n'
    with open(config_path, 'w', encoding='utf-8', newline='\n') as config_file:
        config_file.write(text)


def main():
    interact = get_or_create('IA_HapbeatVRInteract', ROOT, unreal.InputAction)
    interact.set_editor_property('action_description', unreal.Text('Click Hapbeat VR Config panel'))

    recenter = get_or_create('IA_HapbeatVRRecenter', ROOT, unreal.InputAction)
    recenter.set_editor_property('action_description', unreal.Text('Recenter Hapbeat VR Config panel'))

    navigate = get_or_create('IA_HapbeatVRNavigate', ROOT, unreal.InputAction)
    navigate.set_editor_property('action_description', unreal.Text('Move Hapbeat VR Config selection'))
    navigate.set_editor_property('value_type', unreal.InputActionValueType.AXIS2D)

    context = get_or_create('IMC_HapbeatVRConfig', ROOT, unreal.InputMappingContext)
    mappings = []

    # Meta Quest / Touch, Vive, Windows Mixed Reality, and Valve Index. Both
    # hands are symmetric: each stick moves the same panel cursor, matching the
    # Unity VR Config Example. Generic gamepad keys keep desktop testing possible.
    for key in (
        'OculusTouch_Left_Trigger_Click',
        'OculusTouch_Right_Trigger_Click',
        'Vive_Left_Trigger_Click',
        'Vive_Right_Trigger_Click',
        'MixedReality_Left_Trigger_Click',
        'MixedReality_Right_Trigger_Click',
        'ValveIndex_Left_Trigger_Click',
        'ValveIndex_Right_Trigger_Click',
        'Gamepad_LeftTrigger',
        'Gamepad_RightTrigger',
        'OculusTouch_Left_FaceButton1',
        'OculusTouch_Left_FaceButton2',
        'OculusTouch_Right_FaceButton1',
        'OculusTouch_Right_FaceButton2',
    ):
        mappings.append(make_mapping(interact, key))
    for key in (
        'OculusTouch_Left_Thumbstick_Click',
        'OculusTouch_Right_Thumbstick_Click',
        'Vive_Left_Trackpad_Click',
        'Vive_Right_Trackpad_Click',
        'MixedReality_Left_Thumbstick_Click',
        'MixedReality_Right_Thumbstick_Click',
        'ValveIndex_Left_Thumbstick_Click',
        'ValveIndex_Right_Thumbstick_Click',
    ):
        mappings.append(make_mapping(recenter, key))

    # Map actual 2D axes rather than the virtual Up / Down / Left / Right keys.
    # OpenXR reliably exposes the paired axes, whereas several runtimes do not
    # publish those virtual directional keys to Enhanced Input.
    for key in (
        'OculusTouch_Left_Thumbstick_2D',
        'OculusTouch_Right_Thumbstick_2D',
        'Vive_Left_Trackpad_2D',
        'Vive_Right_Trackpad_2D',
        'MixedReality_Left_Thumbstick_2D',
        'MixedReality_Right_Thumbstick_2D',
        'MixedReality_Left_Trackpad_2D',
        'MixedReality_Right_Trackpad_2D',
        'ValveIndex_Left_Thumbstick_2D',
        'ValveIndex_Right_Thumbstick_2D',
        'ValveIndex_Left_Trackpad_2D',
        'ValveIndex_Right_Trackpad_2D',
        'Gamepad_Left2D',
        'Gamepad_Right2D',
    ):
        mappings.append(make_mapping(navigate, key))

    # Assign the complete array once. In UE 5.4 the Python wrapper for the
    # MapKey UFUNCTION returns a reference to an array element; repeatedly
    # discarding that wrapper can leave only the last mapping for an action in
    # the serialized asset. Direct array assignment preserves every hand and
    # every controller profile deterministically.
    context.set_editor_property('mappings', mappings)

    unreal.EditorAssetLibrary.save_loaded_asset(interact)
    unreal.EditorAssetLibrary.save_loaded_asset(recenter)
    unreal.EditorAssetLibrary.save_loaded_asset(navigate)
    unreal.EditorAssetLibrary.save_loaded_asset(context)

    saved_keys = {
        str(mapping.get_editor_property('key').get_editor_property('key_name'))
        for mapping in context.get_editor_property('mappings')
        if mapping.get_editor_property('action') is not None
        and mapping.get_editor_property('action').get_name() == navigate.get_name()
    }
    required_quest_keys = {
        'OculusTouch_Left_Thumbstick_2D',
        'OculusTouch_Right_Thumbstick_2D',
    }
    missing_quest_keys = sorted(required_quest_keys - saved_keys)
    if missing_quest_keys:
        raise RuntimeError(
            'VRConfigExample input asset did not retain required Quest stick mappings: '
            + ', '.join(missing_quest_keys))

    configure_project_default(context)
    unreal.log('[Hapbeat] VRConfigExample input assets installed (%d mappings). Restart the editor before VR Preview.' % len(mappings))


main()
