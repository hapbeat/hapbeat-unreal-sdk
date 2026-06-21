# Hapbeat Unreal Engine SDK — context for AI coding agents

Single self-contained reference so an AI coding agent can use this SDK correctly
from one file. Unreal plugin name: `HapbeatSDK`. Public type: `UHapbeatSubsystem`.

- last-verified-against: plugin VersionName `0.1.0` (`HapbeatSDK.uplugin`, beta).
- Source of truth is the code: the public API in
  `Source/HapbeatSDK/Public/HapbeatSubsystem.h`, the wire format in
  `Source/HapbeatSDK/Public/HapbeatProtocol.h`, module deps in
  `Source/HapbeatSDK/HapbeatSDK.Build.cs`. If this file disagrees, the code wins.
- Canonical docs: https://devtools.hapbeat.com/docs/sdk-integration/

## What it is

A thin **level-1** runtime plugin to drive Hapbeat haptic devices from Unreal
Engine 5 over Wi-Fi UDP broadcast. Exposes a `UGameInstanceSubsystem` that is
callable from both C++ and Blueprint. No cloud; works on the LAN. Uses only the
engine's `Sockets` / `Networking` modules (no external deps).

It does **not** read kits, resample audio, stream clip WAVs, discover devices,
or provide EventMap-style tuning assets. The SDK sends the *instruction*; the
waveform lives in the kit deployed to the device. Discovery, Blueprint Trigger
components, and an EventMap editor are planned level-2/3 features.

## Core model: fire vs editing, linked by event id

- **Fire side** (your UE code): *when/where* to play — `Play` / `Stop` / etc.
- **Editing side** (the kit on the device): *what/how* — which waveform, default
  intensity, loop. Authored in [Hapbeat Studio](https://devtools.hapbeat.com) and
  flashed to the device.
- They are linked only by **event id** (e.g. `"impact.hit"`). Keep waveform
  choices out of game code; put them in the kit. At level-1 the only per-call
  tuning is `Gain` and `Target`.

The event id and wire format are defined by **hapbeat-contracts**
(`specs/message-format.md`) — follow it, do not redefine here.

## Install

1. Copy this repo into your project as `YourProject/Plugins/HapbeatSDK/` (so
   `Plugins/HapbeatSDK/HapbeatSDK.uplugin` exists).
2. Regenerate project files and build (the plugin compiles with your project).
3. **Edit → Plugins → Hardware → Hapbeat SDK** → Enabled.
4. To `#include "HapbeatSubsystem.h"` from C++, add `HapbeatSDK` to your module's
   `PublicDependencyModuleNames` (in your own `*.Build.cs`).

The device must be on the same Wi-Fi/LAN with a **kit deployed** via Hapbeat
Studio.

## Quick start

### C++

```cpp
#include "HapbeatSubsystem.h"

if (UHapbeatSubsystem* Hb = GetGameInstance()->GetSubsystem<UHapbeatSubsystem>())
{
    Hb->Connect(7700, TEXT("MyGame"));   // call once, e.g. BeginPlay
    Hb->Play(TEXT("impact.hit"), 0.5f);  // Gain 0..1
}
```

### Blueprint

```
Get Game Instance → Get Subsystem (Hapbeat Subsystem)
  → Connect (Port 7700, App Name "MyGame")
  → Play (Event Id "impact.hit", Gain 0.5)
```

## Public API (`UHapbeatSubsystem`)

All are `UFUNCTION(BlueprintCallable, Category = "Hapbeat")`. Verbatim C++
signatures:

```cpp
void Connect(int32 InPort = 7700, const FString& InAppName = TEXT(""));
void Play(const FString& EventId, float Gain = 1.0f, const FString& Target = TEXT(""));
void Stop(const FString& EventId, const FString& Target = TEXT(""));
void StopAll(const FString& Target = TEXT(""));
void Ping();
```

- `Connect` — opens the reusable broadcasting UDP socket bound to
  `255.255.255.255:InPort`. `InAppName` is truncated to 16 chars and shown on the
  device OLED; if non-empty, `Connect` sends a CONNECT_STATUS(connected=true).
- `Play` — sends PLAY. `Gain` is clamped to `0..1` on send. `Target` `""` =
  broadcast.
- `Stop` / `StopAll` — stop one event / everything.
- `Ping` — sends PING with Unix-epoch microseconds (keep-alive / probe).
- There is no explicit `Disconnect`. The subsystem's `Deinitialize` (engine
  shutdown / GameInstance teardown) sends CONNECT_STATUS(connected=false) when an
  app name was set, then closes and destroys the socket.

Note: if you call `Play`/`Stop`/etc. before `Connect`, the subsystem lazily
connects with the last `Port`/`AppName` (defaults `7700` / `""`).

## Targeting (device-addressing)

`Target` is a device address string:

```cpp
Hb->Play(TEXT("impact.hit"), 0.6f, TEXT("player_1/chest")); // one device
Hb->Play(TEXT("impact.hit"), 0.6f, TEXT("*/chest"));        // all chest devices
Hb->Play(TEXT("impact.hit"));                                // "" = broadcast (all)
```

Devices self-filter by group/target. Syntax (`player_1/chest`, `*/chest`,
`group_<N>` suffix, `""` = broadcast) is defined by hapbeat-contracts
device-addressing.

## Communication model

- Wi-Fi UDP **broadcast** to `255.255.255.255`; no ACK ("late is worse than
  dropped"). The device command port is UDP **7700**.
- Wire format: 8-byte little-endian header (magic `0x4842` "HB", version `0x01`,
  cmd, seq, length) + payload; see `HapbeatProtocol.h` and contracts
  `message-format.md`. Commands implemented: PLAY `0x01`, STOP `0x02`,
  STOP_ALL `0x03`, PING `0x10`, CONNECT_STATUS `0x20`.
- Group selection is fixed to `0` at level-1 (TODO L2). Device config (TCP 7701)
  is not the SDK's job — that's hapbeat-helper / Studio.

## Common patterns / gotchas

- **Subsystem lifetime** is the GameInstance — it persists across level loads.
  `Connect` once; do not re-create per-actor sockets.
- **Set `AppName`** so the device OLED shows who is connected and so the
  disconnect notice fires on shutdown.
- **Nothing buzzes but the device is on the LAN** => the event id is not in the
  kit deployed to the device (the #1 cause), or `Target` doesn't match (try `""`).
- **Gain is absolute** `0..1` (clamped on send), not a multiplier.
- **Multi-homed PC**: broadcast may exit the wrong NIC; ensure the Hapbeat LAN's
  adapter has the route.
- **Build to verify**: the plugin is authored against the UE5 API and marked
  `IsBetaVersion`; compile it in your Unreal project to confirm.

## More detail

When this single file is not enough, an agent can fetch:

- **Concepts** (shared by every SDK): event id <-> kit https://devtools.hapbeat.com/docs/concepts/event-id-and-kit/ - command vs clip https://devtools.hapbeat.com/docs/concepts/fire-vs-clip/ - targeting https://devtools.hapbeat.com/docs/concepts/group-player-addressing/
- Human docs: https://devtools.hapbeat.com/docs/sdk-integration/ - Portal: https://devtools.hapbeat.com/
