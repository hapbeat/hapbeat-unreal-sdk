# Copyright (c) 2026 Hapbeat. MIT License.
"""
Imports the Showcase sample's art assets from Content/.../Showcase/Source/.

Run headless and commit the .uasset files it writes:

    UnrealEditor-Cmd.exe <YourProject>.uproject ^
        -run=pythonscript -script="<this file>"

Companion to generate_sample_assets.py, which owns the haptic side (Clips and
Event Maps). This one owns meshes, textures, materials and sounds, and the two
do not touch each other's assets -- run either alone.

Re-running is the normal case: every import task sets replace_existing, and the
material graph is rebuilt from scratch each time, so the packages keep their
paths (and therefore every reference into them) while their contents refresh.

The originals under Source/ are committed alongside the .uasset files, so this
is reproducible without the Unity SDK checked out next door.
"""

import os
import unreal

PLUGIN_CONTENT = '/HapbeatSDK/HapbeatSamples'
SHOWCASE = PLUGIN_CONTENT + '/Showcase'
MESH_PKG = SHOWCASE + '/Meshes'
TEX_PKG = SHOWCASE + '/Textures'
MAT_PKG = SHOWCASE + '/Materials'
SND_PKG = SHOWCASE + '/Sounds'

# The master's BaseColor default. Instances that carry no map (flat-shaded
# Quaternius parts) leave the parameter alone and get white * Tint.
WHITE = '/Engine/EngineResources/WhiteSquareTexture'


# ---------------------------------------------------------------- source table

# (asset name, file under Source/Models, import scale, Unity instance scale)
#
# IMPORT SCALE is a units conversion only -- no per-mesh fudge is baked in, so
# the .uasset keeps the model at its authored size and a zone actor sets its own
# component scale. .obj carries no unit declaration: Unity reads a raw unit as
# one metre, UE reads it as one centimetre, hence 100. .fbx does declare its
# unit and both importers honour it (Unity to metres, UE to centimetres), so the
# conversion is already done and the factor is 1.
#
# UNITY SCALE is not applied here. It is the world scale the Unity Showcase
# gives each instance (prefab root * child, resolved through Showcase.unity and
# Prefabs/*.prefab) and is recorded so Phase 2 can set the same value on the UE
# component -- multiply it by the logged bounds to predict the on-screen size.
MESHES = [
    ('SM_BowlingPin',  'bowling_pin.obj',            100.0, (0.51, 0.68, 0.51)),
    ('SM_Door',        'Door.fbx',                     1.0, (1.0, 1.0, 1.0)),
    ('SM_FishingRod',  'FishingRod_Lvl5.obj',        100.0, (0.5, 0.5, 0.5)),
    ('SM_Shark',       'Shark.obj',                  100.0, (0.183, 0.183, 0.183)),
    ('SM_BlasterG',    'blaster-g.fbx',                1.0, (1.0, 1.0, 1.0)),
    ('SM_BulletFoam',  'bullet-foam-tip-thick.fbx',    1.0, (5.0, 5.0, 5.0)),
    ('SM_TargetLarge', 'target-large.fbx',             1.0, (5.2921, 5.2921, 5.2921)),
    ('SM_Missile',     'Missile.obj',                100.0, (0.1, 0.1, 0.1)),
]

# (asset name, file under Source/Textures)
TEXTURES = [
    ('T_LaminateFloor_D',          'T_LaminateFloor_D.png'),
    ('T_OakVeneer_D',              'T_OakVeneer_D.png'),
    ('T_ConcreteWall_D',           'T_ConcreteWall_D.png'),
    ('T_Colormap',                 'T_Colormap.png'),
    ('T_DefaultMaterial_BaseColor', 'T_DefaultMaterial_BaseColor.png'),
]

# (instance name, BaseColor texture or None, Tint RGBA, Roughness)
#
# Tint and Roughness come from the Unity materials this replaces: Tint is the
# Standard shader's _Color (already linear in the .mat), Roughness is
# 1 - _Smoothness. Sources: Samples~/Showcase/Materials/*.mat,
# Models/Z3_Fishing/*.mat, Models/Z5_ChargeShot/Materials/colormap.mat.
MATERIAL_INSTANCES = [
    ('MI_BowlingLane',     'T_LaminateFloor_D',           (0.7804, 0.6196, 0.3765, 1.0), 0.5),
    ('MI_Floor',           'T_OakVeneer_D',               (1.0, 1.0, 1.0, 1.0),          1.0),
    ('MI_Wall',            'T_ConcreteWall_D',            (1.0, 1.0, 1.0, 1.0),          1.0),
    ('MI_Colormap',        'T_Colormap',                  (1.0, 1.0, 1.0, 1.0),          1.0),
    ('MI_DefaultMaterial', 'T_DefaultMaterial_BaseColor', (1.0, 1.0, 1.0, 1.0),          1.0),
    ('MI_Shark_Main',      None,                          (0.4766, 0.6588, 0.9321, 1.0), 1.0),
    ('MI_Shark_Dark',      None,                          (0.3434, 0.3434, 0.3434, 1.0), 1.0),
    ('MI_Shark_Light',     None,                          (0.5069, 0.5069, 0.5069, 1.0), 1.0),
    ('MI_Shark_Eyes',      None,                          (0.0859, 0.0859, 0.0859, 1.0), 1.0),
    ('MI_TargetBase',      'T_Colormap',                  (1.0, 0.8941, 0.7686, 1.0),    1.0),
    ('MI_TargetLight',     'T_Colormap',                  (1.0, 0.9365, 0.0, 1.0),       1.0),
    ('MI_TargetHeavy',     'T_Colormap',                  (1.0, 0.0, 0.0995, 1.0),       1.0),
    ('MI_BowlingBall',     None,                          (0.1, 0.1, 0.15, 1.0),         0.3),
]


# ---------------------------------------------------------------------- helpers

def prime_asset_registry():
    """
    Populate the Asset Registry for our content -- a commandlet does not.

    Same reason as generate_sample_assets.py: under -run=pythonscript nothing
    kicks off the registry search, so does_asset_exist() answers about an empty
    world and a re-import walks into the interactive "replace existing object?"
    prompt, which -unattended answers No.
    """
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        [PLUGIN_CONTENT], True)


def source_dir(kind):
    """Absolute path of Source/<kind>, resolved from the plugin mount point."""
    return unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_plugins_dir()
        + 'HapbeatSDK/Content/HapbeatSamples/Showcase/Source/' + kind + '/')


def run_import(filename, package_path, asset_name, factory, options=None):
    """
    One AssetImportTask through AssetTools, with the factory pinned.

    Pinning matters beyond picking a code path: AssetTools only reaches for
    Interchange when no factory was named --
    AssetTools.cpp:3431 `IsInterchangeImportEnabled() && (SpecifiedFactory ==
    nullptr)` -- so naming one is what keeps the legacy importer, and with it
    the FbxImportUI options below, in charge.
    """
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', filename)
    task.set_editor_property('destination_path', package_path)
    task.set_editor_property('destination_name', asset_name)
    task.set_editor_property('factory', factory)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('automated', True)
    task.set_editor_property('save', True)
    if options is not None:
        task.set_editor_property('options', options)

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    paths = list(task.get_editor_property('imported_object_paths') or [])
    if not paths:
        raise RuntimeError('import produced nothing: ' + filename)
    if len(paths) > 1:
        unreal.log_warning('[Hapbeat] {} produced {} objects: {}'.format(
            asset_name, len(paths), ', '.join(paths)))
    return unreal.EditorAssetLibrary.load_asset(paths[0])


def load_or_create(package_path, asset_name, asset_class, factory):
    """Reuse the package if it exists, so references into it survive a re-run."""
    full = '{}/{}'.format(package_path, asset_name)
    if unreal.EditorAssetLibrary.does_asset_exist(full):
        asset = unreal.EditorAssetLibrary.load_asset(full)
    else:
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, package_path, asset_class, factory)
    if asset is None:
        raise RuntimeError('could not open or create ' + full)
    return asset


# ---------------------------------------------------------------------- meshes

def fbx_static_mesh_options(uniform_scale):
    """
    Static-mesh-only import: no materials, no textures, collision generated.

    Materials are skipped deliberately -- the Showcase drives its look from the
    MI_* instances below, and letting the importer mint its own would leave two
    competing sets on the same meshes. bCombineMeshes folds a multi-part file
    into one Static Mesh, which is what the naming table asks for (one SM_Door,
    not SM_Door_1..3).
    """
    options = unreal.FbxImportUI()
    options.set_editor_property('import_mesh', True)
    options.set_editor_property('import_as_skeletal', False)
    options.set_editor_property('import_materials', False)
    options.set_editor_property('import_textures', False)
    options.set_editor_property('import_animations', False)
    options.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_STATIC_MESH)

    mesh_data = options.get_editor_property('static_mesh_import_data')
    mesh_data.set_editor_property('combine_meshes', True)
    mesh_data.set_editor_property('auto_generate_collision', True)
    mesh_data.set_editor_property('generate_lightmap_u_vs', True)
    # ImportUniformScale lives on UFbxAssetImportData, the shared base
    # (FbxAssetImportData.h:27), not on FbxImportUI itself.
    mesh_data.set_editor_property('import_uniform_scale', uniform_scale)
    return options


def import_meshes():
    base = source_dir('Models')
    for asset_name, filename, uniform_scale, unity_scale in MESHES:
        path = os.path.join(base, filename)
        if not os.path.isfile(path):
            raise RuntimeError('missing source mesh ' + path)

        mesh = run_import(path, MESH_PKG, asset_name, unreal.FbxFactory(),
                          fbx_static_mesh_options(uniform_scale))

        # get_bounding_box() is the imported extent in cm (StaticMesh.h:1822).
        # Multiplying by the recorded Unity instance scale predicts the
        # on-screen size a Phase 2 actor gets, which is the number to eyeball
        # against the Unity Showcase.
        box = mesh.get_bounding_box()
        size = (box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z)
        world = tuple(size[i] * unity_scale[i] for i in range(3))
        unreal.log('[Hapbeat] {:<15} scale x{:g}  mesh {:.1f} x {:.1f} x {:.1f} cm'
                   '  -> at Unity scale {:.1f} x {:.1f} x {:.1f} cm'.format(
                       asset_name, uniform_scale, size[0], size[1], size[2],
                       world[0], world[1], world[2]))


# -------------------------------------------------------------------- textures

def import_textures():
    base = source_dir('Textures')
    for asset_name, filename in TEXTURES:
        path = os.path.join(base, filename)
        if not os.path.isfile(path):
            raise RuntimeError('missing source texture ' + path)

        texture = run_import(path, TEX_PKG, asset_name, unreal.TextureFactory())
        # All five are colour maps, so sRGB regardless of what the importer
        # guessed from the file's channel layout.
        texture.set_editor_property('srgb', True)
        unreal.EditorAssetLibrary.save_loaded_asset(texture)
        unreal.log('[Hapbeat] {:<30} {} x {}'.format(
            asset_name, texture.blueprint_get_size_x(), texture.blueprint_get_size_y()))


# ------------------------------------------------------------------- materials

def build_master_material():
    """
    M_ShowcaseBase: BaseColor texture * Tint into Base Color, Roughness scalar.

    Three parameters is the whole surface area the Showcase needs -- every
    Unity material it replaces is a Standard shader with a diffuse map, a
    _Color and a _Smoothness -- and keeping it to that means one master and
    thirteen constant instances instead of thirteen materials to recompile.
    """
    material = load_or_create(MAT_PKG, 'M_ShowcaseBase',
                              unreal.Material, unreal.MaterialFactoryNew())

    api = unreal.MaterialEditingLibrary
    # Rebuild rather than patch: a re-run must not leave last run's nodes
    # dangling in the graph, and the parameter names are what instances bind
    # to, so the package (and the instances' parent link) is what has to
    # survive, not the individual expressions.
    api.delete_all_material_expressions(material)

    base_color = api.create_material_expression(
        material, unreal.MaterialExpressionTextureSampleParameter2D, -700, 0)
    base_color.set_editor_property('parameter_name', 'BaseColor')
    base_color.set_editor_property('texture', unreal.EditorAssetLibrary.load_asset(WHITE))

    tint = api.create_material_expression(
        material, unreal.MaterialExpressionVectorParameter, -700, 250)
    tint.set_editor_property('parameter_name', 'Tint')
    tint.set_editor_property('default_value', unreal.LinearColor(1.0, 1.0, 1.0, 1.0))

    multiply = api.create_material_expression(
        material, unreal.MaterialExpressionMultiply, -350, 100)
    api.connect_material_expressions(base_color, 'RGB', multiply, 'A')
    api.connect_material_expressions(tint, '', multiply, 'B')
    api.connect_material_property(multiply, '', unreal.MaterialProperty.MP_BASE_COLOR)

    roughness = api.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, -350, 400)
    roughness.set_editor_property('parameter_name', 'Roughness')
    roughness.set_editor_property('default_value', 0.8)
    api.connect_material_property(roughness, '', unreal.MaterialProperty.MP_ROUGHNESS)

    api.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    unreal.log('[Hapbeat] M_ShowcaseBase rebuilt (BaseColor / Tint / Roughness).')
    return material


def build_material_instances(master):
    api = unreal.MaterialEditingLibrary
    for asset_name, texture_name, tint, roughness in MATERIAL_INSTANCES:
        instance = load_or_create(MAT_PKG, asset_name, unreal.MaterialInstanceConstant,
                                  unreal.MaterialInstanceConstantFactoryNew())
        api.set_material_instance_parent(instance, master)
        # Drop last run's overrides first, so an instance that no longer names a
        # texture stops carrying the one it used to.
        api.clear_all_material_instance_parameters(instance)

        if texture_name is not None:
            texture = unreal.EditorAssetLibrary.load_asset(
                '{}/{}'.format(TEX_PKG, texture_name))
            if texture is None:
                raise RuntimeError('{} wants missing texture {}'.format(
                    asset_name, texture_name))
            api.set_material_instance_texture_parameter_value(instance, 'BaseColor', texture)

        api.set_material_instance_vector_parameter_value(
            instance, 'Tint', unreal.LinearColor(*tint))
        api.set_material_instance_scalar_parameter_value(instance, 'Roughness', roughness)

        api.update_material_instance(instance)
        unreal.EditorAssetLibrary.save_loaded_asset(instance)
        unreal.log('[Hapbeat] {:<20} tex={:<30} tint=({:.3f}, {:.3f}, {:.3f}) rough={:g}'.format(
            asset_name, texture_name or '-', tint[0], tint[1], tint[2], roughness))


# ---------------------------------------------------------------------- sounds

def import_sounds():
    """
    Source audio stays .ogg.

    USoundFactory registers "ogg" alongside wav (SoundFactory.cpp:153) whenever
    WITH_SNDFILE_IO is on, which AudioEditor.Build.cs sets for Win64, Mac and
    Linux alike (:87, :99, :112) -- so every desktop editor that can run this
    script can read the file, and the import converts to wav on the way in
    (SoundFactory.cpp:212). Keeping the originals costs ~340 KB in the repo
    against ~4 MB for decoded wavs, for the same USoundWave.
    """
    base = source_dir('Audio')
    names = sorted(n for n in os.listdir(base) if n.lower().endswith('.ogg'))
    if not names:
        raise RuntimeError('no .ogg files under ' + base)

    for filename in names:
        stem = os.path.splitext(filename)[0]
        sound = run_import(os.path.join(base, filename), SND_PKG, 'S_' + stem,
                           unreal.SoundFactory())
        unreal.log('[Hapbeat] {:<28} {} Hz, {} ch, {:.2f} s'.format(
            'S_' + stem,
            sound.get_editor_property('imported_sample_rate'),
            sound.get_editor_property('num_channels'),
            sound.get_editor_property('duration')))


def main():
    prime_asset_registry()
    import_meshes()
    import_textures()
    build_material_instances(build_master_material())
    import_sounds()
    unreal.log('[Hapbeat] Showcase art import complete.')


main()
