# Scripts

Headless generators for the assets that ship inside `Content/`. Run them from a
project that has the plugin enabled, then commit the resulting `.uasset` files —
an end user never runs these. The release scripts are described under
[Releasing](#releasing-prebuilt-zips).

Run them in this order — each consumes what the ones before it wrote. Note the
third one's different invocation:

```
UnrealEditor-Cmd.exe <YourProject>.uproject -run=pythonscript -script="<abs path>/generate_sample_assets.py"
UnrealEditor-Cmd.exe <YourProject>.uproject -run=pythonscript -script="<abs path>/import_showcase_assets.py"
UnrealEditor.exe     <YourProject>.uproject -ExecCmds="py <abs path>/generate_showcase_map.py" -unattended -nosplash
UnrealEditor.exe     <YourProject>.uproject -ExecCmds="py <abs path>/generate_basic_example_map.py" -unattended -nosplash
```

The map generator needs a full editor rather than the `pythonscript`
commandlet: spawning an actor goes through the editor's selection set, which a
commandlet has none of, and `spawn_actor_from_class` crashes there. `-ExecCmds`
runs the script after the editor has initialised, so it works while still
needing no one at the keyboard. It leaves the editor open when it finishes —
close it once the log reports the map was written.

- `generate_sample_assets.py` — builds the haptic assets (Clips, Event Maps) for
  BasicExample and Showcase from the Kit manifests.
- `import_showcase_assets.py` — imports the Showcase art (meshes, textures,
  materials, sounds) from `Content/HapbeatSamples/Showcase/Source/`.
- `generate_showcase_map.py` — builds `Showcase/Maps/Showcase.umap`: floor,
  lighting, PlayerStart, the `Hapbeat Showcase` actor, and the map's GameMode
  Override.
- `generate_basic_example_map.py` — builds `BasicExample/Maps/BasicExample.umap`:
  floor, lighting, PlayerStart, one `Hapbeat Basic Example` actor, and the
  map's GameMode Override. It assigns the shipped `EM_BasicExample` Event Map
  to the actor so its Details panel is immediately editable.
- `disable_precomputed_sample_lighting.py` — updates the three shipped sample
  maps to force fully dynamic lighting and makes their directional lights
  Movable. It does not respawn or reposition any sample actor.

The first two touch disjoint assets, so either can be run alone. The third takes
the floor's material from the art import and the zones' Event Map from the
sample assets, but only writes the map package — changing the map never means
re-running the other two. Both map generators are re-runnable.

## Releasing (prebuilt ZIPs)

`Update-HapbeatSDK.ps1` ships inside the plugin for end users (see the README
Install section). `package-release.ps1` is for maintainers:

1. Bump `VersionName` in `HapbeatSDK.uplugin` and add the `## [x.y.z]`
   section to `CHANGELOG.md`; commit.
2. On a Windows PC with the Epic Launcher engines installed, from a clean tree:

   ```
   powershell -ExecutionPolicy Bypass -File Scripts/package-release.ps1
   ```

   It builds every installed UE 5.3+ engine (`-Engines 5.4,5.6` to pick) with
   `RunUAT BuildPlugin` and writes to `Saved/ReleasePackages/`:
   `HapbeatSDK-<ver>-UE5.x.zip` (plugin contents at the ZIP root: Win64 editor
   DLLs, Source, Content, Config; no Intermediate, no PDBs unless
   `-IncludePdb`), `HapbeatSDK-<ver>-SHA256SUMS.txt`, and `logs/`. The
   packaged `.uplugin` gets `"EngineVersion": "5.x.0"` and `"Installed": true`
   from BuildPlugin. It exits nonzero if any engine fails.
3. Push the `vX.Y.Z` tag. The Release workflow creates the GitHub Release
   (notes from CHANGELOG, plus a source ZIP). Then attach the engine ZIPs and
   the checksum file:

   ```
   gh release upload vX.Y.Z Saved/ReleasePackages/HapbeatSDK-X.Y.Z-UE5.*.zip Saved/ReleasePackages/HapbeatSDK-X.Y.Z-SHA256SUMS.txt
   ```

   `Update-HapbeatSDK.ps1` picks `HapbeatSDK-*-UE<engine>.zip` from the latest
   release and verifies it against the `*-SHA256SUMS.txt` asset, so keep those
   names. Engines without a ZIP fall back to the source install.
