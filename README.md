# Hapbeat Unreal Engine SDK

Drive [Hapbeat](https://hapbeat.com) haptic devices from Unreal Engine 5 over
Wi-Fi UDP. A runtime plugin with a Blueprint-callable subsystem — usable from
both C++ and Blueprint.

> **📚 Docs**: <https://devtools.hapbeat.com/docs/sdk-integration/>

This is the **level-1** SDK: a script/Blueprint can drive Hapbeat. The fire side
stays orthogonal to event tuning (the same design as the Hapbeat Unity SDK).

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
  → Play (Event Id "impact.hit", Gain 0.5)
```

### C++

```cpp
#include "HapbeatSubsystem.h"

if (UHapbeatSubsystem* Hb = GetGameInstance()->GetSubsystem<UHapbeatSubsystem>())
{
    Hb->Connect(7700, TEXT("MyGame"));
    Hb->Play(TEXT("impact.hit"), 0.5f);
}
```

`"impact.hit"` must be an event id present in the **kit deployed to the device**
via [Hapbeat Studio](https://devtools.hapbeat.com). The SDK sends the
*instruction*; the waveform lives in the kit on the device.

## API (`UHapbeatSubsystem`)

| Function | Purpose |
|---|---|
| `Connect(Port, AppName)` | open the UDP broadcast socket |
| `Play(EventId, Gain, Target)` | play an event (Gain 0..1) |
| `Stop(EventId, Target)` | stop one event |
| `StopAll(Target)` | stop everything |
| `Ping()` | keep-alive / probe |

`Target` is a device address (`""` = broadcast, `player_1/chest`, `*/chest`).

## Status

Level-1 (fire from C++/Blueprint) is implemented as a runtime plugin. It is
authored against the UE5 API; **compile it in your Unreal project** to verify.
Blueprint Trigger components, an EventMap-style editor, and device discovery are
planned level-2/3 features.

## License

MIT © Hapbeat
