# Hapbeat Unreal Engine SDK

Drive [Hapbeat](https://hapbeat.com) haptic devices from Unreal Engine 5 over
Wi-Fi UDP. A runtime plugin with a Blueprint-callable subsystem — usable from
both C++ and Blueprint.

> **📚 Docs**: <https://devtools.hapbeat.com/docs/sdk-integration/>

Two orthogonal pieces, linked only by event id (the same design as the Hapbeat
Unity SDK):

- **Trigger** (the origin) — a collision, a grab/hold sequence, a Blueprint call;
  *what* fires a haptic.
- **EventMap** (the tuning) — a `UHapbeatEventMap` data asset mapping event id →
  default gain/params; *how strong*.

## Requirements

- Unreal Engine 5.x (uses the `Sockets` / `Networking` modules).

## Install

1. Copy this repo into your project's `Plugins/` folder as `Plugins/HapbeatSDK/`
   (so `Plugins/HapbeatSDK/HapbeatSDK.uplugin` exists).
2. Regenerate project files and build (the plugin compiles with your project).
3. **Edit → Plugins → Hardware → Hapbeat SDK** → Enabled (if not already).

## Quick start

### Blueprint

```
Get Game Instance → Get Subsystem (Hapbeat Subsystem)
  → Connect (Port 7700, App Name "MyGame")
  → Play (Event Id "sample-kit.sine_100hz", Gain 0.5)
```

### C++

```cpp
#include "HapbeatSubsystem.h"

if (UHapbeatSubsystem* Hb = GetGameInstance()->GetSubsystem<UHapbeatSubsystem>())
{
    Hb->Connect(7700, TEXT("MyGame"));
    Hb->Play(TEXT("sample-kit.sine_100hz"), 0.5f);
}
```

The event id follows `<kit-name>.<file-name>` and must be present in the **kit
deployed to the device** via [Hapbeat Studio](https://devtools.hapbeat.com).
`sample-kit.sine_100hz` is the standard verification event. The SDK sends the
*instruction*; the waveform lives in the kit on the device.

## Fire API (`UHapbeatSubsystem`)

| Function | Purpose |
|---|---|
| `Connect(Port, AppName)` | open the UDP broadcast socket |
| `Play(EventId, Gain, Target)` | play an event (Gain 0..1) |
| `Stop(EventId, Target)` / `StopAll(Target)` | stop one / everything |
| `Ping()` | probe |
| `StreamClip(Clip, BaselineGain, InitialGain, Target, bLoop)` | stream a PCM16 clip, returns a playback handle for real-time gain/pan |
| `StopStream()` / `StopStreamWithFlush(Target)` | end the active stream |
| `IsAlive()` / `GetAliveDeviceCount()` | device presence (from PONGs) |

Delegates (BlueprintAssignable): `OnConnected`, `OnDisconnected`, `OnError`,
`OnPong`. `Target` is a device address (`""` = broadcast, `player_1/chest`,
`*/chest`).

## Trigger components (fire without much code)

- **`UHapbeatCollisionTriggerComponent`** — add to an Actor; fires on
  hit/overlap (with tag filtering, cooldown, optional velocity-scaled gain).
- **`UHapbeatSequenceComponent`** — grab/hold/release style sequences.
- **`UHapbeatTriggerComponent`** — base component that resolves an event via the
  EventMap and plays it; call `Fire()` from any Blueprint event.

## EventMap (the tuning)

`UHapbeatEventMap` is a `UDataAsset` (a list of `FHapbeatEventEntry`: event id +
default gain + params). Assign it to trigger components and resolve the default
gain there, so `Play`/triggers stay one-liners you can re-tune without touching
gameplay. `UHapbeatTargetLibrary` provides Blueprint helpers for building
device-address strings.

## Status

Level-2/3: fire from C++/Blueprint, EventMap data asset, collision/sequence
trigger components, real-time clip streaming with gain/pan, PONG-based device
liveness + delegates. Authored against the UE5 API — **compile it in your Unreal
project** to verify (there is no headless UE build here).

## License

MIT © Hapbeat
