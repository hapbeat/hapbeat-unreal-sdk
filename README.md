# Hapbeat Unreal Engine SDK

Drive [Hapbeat](https://hapbeat.com) haptic devices from Unreal Engine 5 over
Wi-Fi UDP. A runtime plugin usable from both C++ and Blueprint: an Event Map
data asset for tuning, trigger components, real-time clip streaming, and
address override for multi-HMD setups.

> **📚 Docs**: <https://devtools.hapbeat.com/docs/sdk-integration/unreal-sdk/>
> (the same pages are in [`docs/`](docs/README.md) in this repository)

Two pieces, linked only by event id (the same design as the Hapbeat Unity SDK):

- **Trigger** — a collision, a grab / hold sequence, a Blueprint or C++ call;
  *what* fires a haptic.
- **Event Map** — a `UHapbeatEventMap` data asset; *how strong*, which clip,
  which target.

## Requirements

- Unreal Engine 5.3+ (verified on 5.6.1 and 5.4; with no preference, use 5.6.1,
  which the Hapbeat demos are built with) and a C++ toolchain: the
  plugin ships native modules, so even a Blueprint-only project builds once
  after adding it ([build steps](docs/unreal-build.md)).
- A Hapbeat on the same Wi-Fi / LAN. Command (Fire) events need a Kit deployed
  with [Hapbeat Studio](https://devtools.hapbeat.com); StreamClip events need no
  Kit on the device.
- Streaming to Hapbeat firmware 0.5.0 or later needs this SDK 0.2.0 or later
  (stream session v2, see [CHANGELOG.md](CHANGELOG.md)).

## Install

1. In the project root (the folder with the `.uproject`), run
   `git clone https://github.com/hapbeat/hapbeat-unreal-sdk.git Plugins/HapbeatSDK`
   (for a ZIP, copy its contents into `Plugins/HapbeatSDK/` so
   `Plugins/HapbeatSDK/HapbeatSDK.uplugin` exists). Clone once per project —
   do not share one clone through a symlink / junction (build outputs live in
   the plugin folder). Update with `git pull` and rebuild.
2. Regenerate project files and build the **Development Editor / Win64** target.
3. **Edit → Plugins → Hapbeat SDK** → enable. Settings are in
   **Project Settings → Plugins → Hapbeat** (also **Tools → Hapbeat → Hapbeat Settings**).
4. C++ only: add `HapbeatSDK` to your module's `PublicDependencyModuleNames`.

Then open `Plugins/HapbeatSDK/Content/HapbeatSamples/BasicExample/Maps/BasicExample`
(enable **Show Plugin Content** in the Content Browser) and press Space in PIE.
Details: [Getting Started](docs/getting-started.md).

## Quick start

The `UHapbeatSubsystem` (a `GameInstanceSubsystem`) connects automatically on
start-up, using the port and app name from Project Settings.

Play an Event Map entry (recommended — gain, clip, target and delay come from
the asset):

```cpp
#include "HapbeatBlueprintLibrary.h"

// Entry: an FHapbeatEntryRef (UPROPERTY) picked from MyEventMap in the Details panel
UHapbeatBlueprintLibrary::PlayHapbeatEvent(this, MyEventMap, Entry);
```

Or send an event id directly (bypasses the Event Map):

```cpp
#include "HapbeatSubsystem.h"

if (UHapbeatSubsystem* Hb = GetGameInstance()->GetSubsystem<UHapbeatSubsystem>())
{
    Hb->Play(TEXT("sample-kit.sine_100hz"), /*Gain*/ 0.5f, /*Target*/ TEXT(""), /*Pan*/ 0.0f);
}
```

The event id is `<kit-name>.<file-name>` and must be in the Kit deployed to the
device. In Blueprint both are **Play Event (Hapbeat)**: the library node takes an
Event Map + entry, the Hapbeat Subsystem node takes an event id.

## Documentation

| Page | Content |
|---|---|
| [Getting Started](docs/getting-started.md) | install, enable, first playback with BasicExample |
| [Event Map and playback](docs/event-map-and-playback.md) | create an Event Map, play it from Blueprint / C++ |
| [C++ API](docs/cpp-api.md) | classes, playback contract, full declaration index |
| [Blueprint nodes](docs/blueprint-nodes.md) | every node and component, full declaration index |
| [Editor menus](docs/editor-menus.md) | Hapbeat Settings, Runtime Status, Event Map, asset creation |
| [Targeting and multi-HMD](docs/targeting-and-multi-hmd.md) | target strings and Address Override |
| [BasicExample](docs/basic-example.md) / [Showcase](docs/showcase-unreal.md) / [VR Config Example](docs/vr-config-example.md) | the sample levels in `Content/HapbeatSamples/` |
| [Unity to Unreal](docs/unity-to-unreal-codex.md) | notes for Unity users |

AI coding agents: [AGENTS.md](AGENTS.md) is a self-contained reference.

## License

MIT © Hapbeat
