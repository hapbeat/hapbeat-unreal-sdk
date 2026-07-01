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

A **level-2/3** runtime plugin to drive Hapbeat haptic devices from Unreal
Engine 5 over Wi-Fi UDP broadcast. Exposes a `UGameInstanceSubsystem` callable
from C++ and Blueprint. No cloud; works on the LAN. Uses only the engine's
`Sockets` / `Networking` modules (no external deps).

Implemented: fire (`Play`/`Stop`/`StopAll`), real-time **clip streaming** with
gain/pan (`StreamClip` → `UHapbeatStreamPlayback`), **device liveness** from PONG
replies (delegates + `IsAlive`), a **`UHapbeatEventMap`** data asset for default
tuning, and Blueprint **trigger components** (collision / sequence). The SDK
sends the *instruction*; the waveform lives in the kit deployed to the device.

## Core model: Trigger vs EventMap, linked by event id

- **Trigger side** (your UE code / a trigger component): *when/where* to play —
  `Play` / `Stop`, a collision, a sequence.
- **Tuning side** (the `UHapbeatEventMap` + the kit on the device): *what/how* —
  which waveform, default intensity, loop. Authored in
  [Hapbeat Studio](https://devtools.hapbeat.com) and flashed to the device.
- They are linked only by **event id**, formatted `<kit-name>.<file-name>` (e.g.
  `"sample-kit.sine_100hz"`, the standard verification event). Keep waveform
  choices out of game code.

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
    Hb->Play(TEXT("sample-kit.sine_100hz"), 0.5f);  // Gain 0..1
}
```

### Blueprint

```
Get Game Instance → Get Subsystem (Hapbeat Subsystem)
  → Connect (Port 7700, App Name "MyGame")
  → Play (Event Id "sample-kit.sine_100hz", Gain 0.5)
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
// Real-time clip streaming (returns a handle for live gain/pan modulation):
UHapbeatStreamPlayback* StreamClip(UHapbeatClip* Clip, float BaselineGain = 1.0f,
    float InitialGain = 1.0f, const FString& Target = TEXT(""), bool bLoop = false);
void StopStream();
void StopStreamWithFlush(const FString& Target = TEXT(""));
// Liveness (BlueprintPure): true only when a device has answered a PONG.
bool IsConnected() const;           // socket open (UDP: not device presence)
int32 GetAliveDeviceCount() const;
bool IsAlive() const;
bool IsStreaming() const;
```

- `Connect` — opens the reusable broadcasting UDP socket bound to
  `255.255.255.255:InPort`. `InAppName` truncated to 16 chars, shown on the OLED;
  if non-empty, sends CONNECT_STATUS(connected=true). A ticker sends periodic
  PING + CONNECT_STATUS keep-alive.
- `Play` — sends PLAY. `Gain` clamped to `0..1`. `Target` `""` = broadcast.
- `StreamClip` — streams a PCM16 `UHapbeatClip` and returns a
  `UHapbeatStreamPlayback` whose `Gain`/`Pan` you set per frame; the SDK
  pre-multiplies each sample so STREAM_BEGIN carries gain=1.0. Single active
  session, REPLACE semantics. `StopStream` / `StopStreamWithFlush` end it.
- Liveness: UDP is connectionless, so `IsConnected()` == "socket open". Device
  presence is `IsAlive()` / `GetAliveDeviceCount()`, tracked from PONG replies.
- **Delegates** (`BlueprintAssignable`): `OnConnected` / `OnDisconnected` (fire
  on a 0<->positive liveness transition), `OnError` (device ERROR packet),
  `OnPong` (per PONG; endpoint, RTT µs, device name/address/firmware).
- No explicit `Disconnect`: `Deinitialize` sends CONNECT_STATUS(false) when an
  app name was set, then closes the socket.

If you call `Play`/`Stop`/etc. before `Connect`, the subsystem lazily connects
with the last `Port`/`AppName` (defaults `7700` / `""`).

## Trigger components & EventMap

- `UHapbeatCollisionTriggerComponent` — add to an Actor; fires on hit/overlap
  (tag filter, cooldown, optional velocity-scaled gain).
- `UHapbeatSequenceComponent` — grab/hold/release sequences.
- `UHapbeatTriggerComponent` — base component; resolves an event via the EventMap
  and plays it. Call `Fire()` from any Blueprint event.
- `UHapbeatEventMap` (`UDataAsset`) — a list of `FHapbeatEventEntry` (event id +
  default gain/params). Assign to trigger components so the default gain is data,
  not code. `UHapbeatTargetLibrary` has Blueprint helpers for target strings.

## Targeting (device-addressing)

`Target` is a device address string:

```cpp
Hb->Play(TEXT("sample-kit.sine_100hz"), 0.6f, TEXT("player_1/chest")); // one device
Hb->Play(TEXT("sample-kit.sine_100hz"), 0.6f, TEXT("*/chest"));        // all chest devices
Hb->Play(TEXT("sample-kit.sine_100hz"));                                // "" = broadcast (all)
```

Devices self-filter by group/target. Syntax (`player_1/chest`, `*/chest`,
`group_<N>` suffix, `""` = broadcast) is defined by hapbeat-contracts
device-addressing.

## Communication model

- Wi-Fi UDP **broadcast** to `255.255.255.255`; no ACK ("late is worse than
  dropped"). The device command port is UDP **7700**.
- Wire format: 8-byte little-endian header (magic `0x4842` "HB", version `0x01`,
  cmd, seq, length) + payload; see `HapbeatProtocol.h` and contracts
  `message-format.md`. Commands: PLAY `0x01`, STOP `0x02`, STOP_ALL `0x03`,
  PING `0x10`, PONG `0x11` (received), CONNECT_STATUS `0x20`, and the streaming
  set STREAM_BEGIN/DATA/END (`0x30`–`0x32`).
- Device config (TCP 7701) is not the SDK's job — that's hapbeat-helper / Studio.

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
