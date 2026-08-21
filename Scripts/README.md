# Scripts

Headless generators for the assets that ship inside `Content/`. Run them from a
project that has the plugin enabled, then commit the resulting `.uasset` files —
an end user never runs these.

```
UnrealEditor-Cmd.exe <YourProject>.uproject -run=pythonscript -script="<abs path>/import_showcase_assets.py"
UnrealEditor-Cmd.exe <YourProject>.uproject -run=pythonscript -script="<abs path>/generate_sample_assets.py"
```

- `import_showcase_assets.py` — imports the Showcase art (meshes, textures,
  materials, sounds) from `Content/HapbeatSamples/Showcase/Source/`.
- `generate_sample_assets.py` — builds the haptic assets (Clips, Event Maps) for
  BasicExample and Showcase from the Kit manifests.

The two touch disjoint assets, so either can be run alone; both are re-runnable.
