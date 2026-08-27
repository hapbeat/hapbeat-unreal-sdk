# Hapbeat Unreal Engine SDK

Drive [Hapbeat](https://hapbeat.com) haptic devices from Unreal Engine 5 over
Wi-Fi UDP. A runtime plugin with a Blueprint-callable subsystem — usable from
both C++ and Blueprint — plus native-Details-panel authoring for event tuning,
trigger components, and real-time clip streaming.

> **📚 Docs**: <https://devtools.hapbeat.com/docs/sdk-integration/>

Two orthogonal pieces, linked only by event id (the same design as the Hapbeat
Unity SDK):

- **Trigger** (the origin) — a collision, a grab/hold sequence, a Blueprint
  call; *what* fires a haptic.
- **EventMap** (the tuning) — a `UHapbeatEventMap` data asset mapping event id
  → default gain/params; *how strong*.

## Requirements

- Unreal Engine 5.3+ (compile-verified on **5.4**). Stable UE5 APIs only, no
  engine version pinned in `HapbeatSDK.uplugin`.
- A **C++ toolchain for your project**: the plugin ships native Runtime /
  Samples / Editor modules, so even a Blueprint-only project needs to
  regenerate project files and build once after adding it (Unreal creates the
  missing `Source/` scaffolding for you).
- A Hapbeat device on the same Wi-Fi/LAN. Command-mode events need a **kit
  deployed** via [Hapbeat Studio](https://devtools.hapbeat.com) first;
  StreamClip events need no kit on the device (raw PCM16 goes over the wire
  directly).
- Marked `IsBetaVersion: true` in the `.uplugin` — build it in your own
  project to confirm; there is no headless UE build in this repo.

## Install

1. Copy this repo into your project's `Plugins/` folder as
   `Plugins/HapbeatSDK/` (so `Plugins/HapbeatSDK/HapbeatSDK.uplugin` exists).
2. Regenerate project files and build (the plugin compiles with your project).
3. **Edit → Plugins → Hardware → Hapbeat SDK** → Enabled (if not already).
4. C++ only: add `HapbeatSDK` to your module's `PublicDependencyModuleNames`
   to `#include "HapbeatSubsystem.h"`.

Connection / behavior settings (port, app name, ping interval, streaming
send-ahead, haptic delay, logging) live in **Project Settings → Plugins →
Hapbeat** (`UHapbeatConfig`, a `UDeveloperSettings`). `HapticDelaySeconds` and
`FHapbeatEventEntry::DelayOffsetSeconds` are reserved fields — v1 does not yet
defer sends by them; every `Fire()` / `Play()` goes out immediately (tracked
for a future release).

## Quick start

The subsystem **auto-connects** during `Initialize()` — before any actor's
`BeginPlay` — using **Project Settings → Plugins → Hapbeat**'s `Port` /
`AppName` (falling back to your project's name if `AppName` is empty), so a
device on the LAN already sees a `CONNECT_STATUS` by the time your level
starts. Calling `Connect()` yourself (as below) is optional — it's safe to
call again and lets you override the port/app name at runtime.

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

```cpp
void Connect(int32 InPort = 7700, const FString& InAppName = TEXT(""));
void Play(const FString& EventId, float Gain = 1.0f, const FString& Target = TEXT(""));
void Stop(const FString& EventId, const FString& Target = TEXT(""));
void StopAll(const FString& Target = TEXT(""));
void Ping();

UHapbeatStreamPlayback* StreamClip(UHapbeatClip* Clip, float BaselineGain = 1.0f,
    float InitialGain = 1.0f, const FString& Target = TEXT(""), bool bLoop = false);
void StopStream();
void StopStreamWithFlush(const FString& Target = TEXT(""));

void SetAddressOverride(int32 Player, int32 InGroup, bool bPersist = false);
void ClearPersistedAddressOverride();
int32 GetOverridePlayer() const;   // BlueprintPure
int32 GetOverrideGroup() const;    // BlueprintPure

bool IsConnected() const;          // socket open (UDP: not device presence)
int32 GetAliveDeviceCount() const;
bool IsAlive() const;
bool IsStreaming() const;
UHapbeatStreamPlayback* GetActivePlayback() const;
```

| Function | Purpose |
|---|---|
| `Connect(Port, AppName)` | open the UDP broadcast socket; `AppName` (≤16 chars) shows on the device OLED |
| `Play(EventId, Gain, Target)` | play an event (`Gain` 0..1) |
| `Stop(EventId, Target)` / `StopAll(Target)` | stop one / everything |
| `Ping()` | probe |
| `StreamClip(Clip, BaselineGain, InitialGain, Target, bLoop)` | stream a PCM16 clip; returns a playback handle for real-time gain/pan |
| `StopStream()` / `StopStreamWithFlush(Target)` | end the active stream (with-flush also forces an immediate device ring-buffer flush) |
| `SetAddressOverride(Player, Group, bPersist)` / `ClearPersistedAddressOverride()` | force player/group on every outgoing send (see [Global address override](#global-address-override)) |
| `IsAlive()` / `GetAliveDeviceCount()` | device presence (from PONGs) |

Delegates (`BlueprintAssignable`): `OnConnected` / `OnDisconnected` (fire only
on a liveness 0↔positive transition, not socket-open), `OnError(Message)`,
`OnPong(Endpoint, RttUs, DeviceName, Address, Firmware)` (once per responsive
device per keep-alive ping). `Target` is a device address string (`""` =
broadcast, `player_1/pos_chest`, `*/pos_chest`, …) — see
[Targeting](#targeting).

## EventMap — the tuning side

`UHapbeatEventMap` is a plain `UDataAsset` — create it via **Content Browser →
Add → Miscellaneous → Data Asset → Hapbeat Event Map**. Its only field is
`TArray<FHapbeatEventEntry> Entries`, edited entirely in the **native Details
panel** (add / remove / reorder / duplicate / multi-edit, all for free — no
custom window). `FindById(FGuid, FHapbeatEventEntry&) const` looks one up.

Each `FHapbeatEventEntry`:

| Field | Notes |
|---|---|
| `Id` | stable `FGuid`, auto-assigned; triggers reference this, never a list index |
| `Mode` | `EHapticMode::Command` (device plays its local clip) or `StreamClip` (SDK streams PCM16 over UDP) |
| `DisplayName`, `Category`, `EventName` | `Category` = kit name, `EventName` = clip file name; `GetEventId()` = `Category.EventName` |
| `Gain` | 0..2, author gain |
| `Target` | device address filter, `""` = broadcast |
| `bLoop` | StreamClip only — re-stream continuously until stopped |
| `DelayOffsetSeconds` | reserved (see [Requirements](#requirements)) |
| `Notes` | designer notes, not sent |
| `StreamClip` | `TSoftObjectPtr<UHapbeatClip>`, StreamClip mode only |
| `CachedManifestIntensity` | baked by **Refresh Intensities** (below); `-1` = unresolved |

`GetEffectiveGain()` = `Gain × CachedManifestIntensity` when the cache is
resolved (`>= 0`), else plain `Gain` — this is what actually goes on the wire.

The `HapbeatSDKEditor` module adds a small toolbar above `Entries`:

- **Refresh Intensities** — scans every `*-manifest.json` under your
  project's `Content/` (recursive), resolves each entry's `(event id, mode)`
  against the kit manifest's `events`/`stream_events` bucket (schema 2.0.0)
  authored in Hapbeat Studio, and bakes the result into
  `CachedManifestIntensity`. The device never reads the manifest — this is
  how the SDK gets `entry.gain × manifest.intensity` without shipping the
  manifest at runtime.
- **Entry picker + Test Play / Stop / Stop All / Ping** — pick an entry by
  `DisplayName` (falling back to event id, then a short guid) and fire it at
  a real device from an editor-only UDP sender, without entering PIE.
  StreamClip entries can't be meaningfully test-played as a Command PLAY (the
  device has no local clip for them) — the button is disabled with an
  explanatory tooltip; exercise StreamClip in PIE instead.

## Trigger components — the fire side

All extend `UHapbeatTriggerComponent : UActorComponent`:

```cpp
UPROPERTY() TObjectPtr<UHapbeatEventMap> EventMap;
UPROPERTY() FGuid EntryId;            // picked via a dropdown once EventMap is assigned
UPROPERTY() bool bTriggerEnabled = true;
UPROPERTY() float Cooldown = 0.0f;    // seconds, unscaled real time
UPROPERTY() float GainMultiplier = 1.0f; // [0, 2], per-trigger scale on top of the entry gain

virtual void Fire();
void FireWithGain(float GainOverride);
void FireScaled(float Velocity, float MinVelocity = 0.0f, float MaxVelocity = 10.0f);
void FireWithCurve(float Value, UCurveFloat* Curve);
virtual void Stop();
void SetGainMultiplier(float NewMultiplier); // pushes live to an active StreamClip playback
void SetStreamPan(float NewPan);             // imperative pan set on an active playback
UHapbeatStreamPlayback* GetActivePlayback() const;
```

Gain composition (both modes read the entry, `GainMultiplier`, and a per-call
`Multiplier` — 1 for `Fire()`, the override for `FireWithGain`, the
normalized velocity for `FireScaled`, the curve sample for `FireWithCurve`):

- **Command** → `wireGain = entry.GetEffectiveGain() × GainMultiplier × Multiplier`, sent via `Play`.
- **StreamClip** → `baseline = entry.GetEffectiveGain()`; `initialModulator = GainMultiplier × Multiplier`; `StreamClip(clip, baseline, initialModulator, target, entry.bLoop)`.

> Assigning `GainMultiplier` directly (e.g. a Blueprint "Set" node) does
> **not** push to an already-playing stream — call `SetGainMultiplier()` for
> live modulation, since `UPROPERTY`s have no setter hook.

Two subclasses cover the common cases:

- **`UHapbeatCollisionTriggerComponent`** — binds `OnComponentHit` or
  `OnComponentBeginOverlap` (`TriggerEvent`) on the owner's primitive.
  `GainMode::Fixed` fires at the entry's gain as-is; `VelocityScaled` maps
  normalized impact speed (`(speed - VelocityThreshold) / (MaxVelocity -
  VelocityThreshold)`, clamped `[0,1]`) through `VelocityCurve` (identity if
  no keys) into the fire multiplier. Optional `TagFilter` (Actor Tags,
  `NAME_None` = any).
- **`UHapbeatSequenceComponent`** — 3-phase grab/hold/release: `Fire()` plays
  the `StartEntryId` one-shot then starts the (looping) inherited `EntryId`
  StreamClip; `Stop()` stops the loop, then — after `StopShotDelay` seconds
  (default `0.05`, so its own PLAY/STREAM_BEGIN doesn't collide with the
  loop's ring-flush burst) — fires the `StopEntryId` one-shot.

## Real-time clip streaming (StreamClip)

`UHapbeatClip : UDataAsset` wraps raw interleaved little-endian PCM16 bytes +
`SampleRate` / `NumChannels` — parsed from a 16 kHz PCM16 `.wav` **with no
engine audio decode** (version-robust across UE releases; the kit format is
already 16 kHz PCM16):

```cpp
static bool ParseWav(const TArray<uint8>& WavBytes, int32& OutSampleRate,
    int32& OutChannels, TArray<uint8>& OutPcm16, FString& OutError);
static UHapbeatClip* CreateFromWavBytes(UObject* Outer, const TArray<uint8>& WavBytes);
```

**v1 limitation:** neither is `BlueprintCallable`, and there is no in-editor
`.wav`-import factory yet — building a `UHapbeatClip` is currently a **C++**
step (load bytes with `FFileHelper::LoadFileToArray`, then
`CreateFromWavBytes`). Once you hold the pointer, everything downstream —
assigning it to `FHapbeatEventEntry::StreamClip`, calling `Fire()`/`Stop()` on
a trigger, modulating gain/pan — is fully Blueprint-usable. See the samples
for the pattern every Showcase zone uses.

`StreamClip()` returns a `UHapbeatStreamPlayback` handle (**game-thread
only**, plain floats, no atomics):

```cpp
UPROPERTY(BlueprintReadOnly) float BaselineGain; // frozen at stream start
void ApplyGainModulation(float Modulator); // Gain = clamp(BaselineGain x Modulator, 0, 2)
void SetPan(float NewPan);                 // [-1, 1]; ignored for mono
void Stop();
float GetGain() const;
float GetPan() const;
bool IsStopped() const;
bool IsActive() const;
```

Pan uses **linear** balance (`pan=0` → `L=R=1.0`), not equal-power — Hapbeat's
left/right actuators are physically separate on the body and don't binaurally
sum, so equal-power's `sqrt(1/2)` would silently attenuate every centered
stereo clip ~3 dB versus mono. Gain/pan are pre-multiplied into the PCM on the
SDK side before every `STREAM_DATA` packet — `STREAM_BEGIN` always carries
`gain=1.0`; the device never re-applies gain.

`StreamClip()` registers an independent logical source with the subsystem's
StreamHub. Sources addressed to the same PONG-confirmed endpoint are mixed
into one 16 kHz stereo PCM16 wire session; different endpoints get separate
exact-unicast sessions. With no matching endpoint the returned playback is
`Deferred(NoResolvedEndpoint)` and sends no STREAM packet. `Stop()` affects
only that source; the last source leaves its endpoint session after a 300 ms
linger.

`UHapbeatParameterBinding : UActorComponent` drives a playback continuously
each tick: `Source → normalize([InputMin,InputMax]) → curve → lerp([OutputMin,
OutputMax]) → write`.

```cpp
UPROPERTY() EHapbeatBindingSource SourceProperty; // LocalPositionX/Y/Z, VelocityMagnitude,
                                                   // AngularVelocityMagnitude, PositionDeltaMagnitude, External
UPROPERTY() float InputMin = 0.0f, InputMax = 1.0f;
UPROPERTY() EHapbeatBindingCurve CurveType;       // Linear, EaseIn, EaseOut, Exponential, Custom
UPROPERTY() TObjectPtr<UCurveFloat> CustomCurve;
UPROPERTY() EHapbeatBindingOutput OutputParameter; // StreamGain or StreamPan
UPROPERTY() float OutputMin = 0.0f, OutputMax = 1.0f;

void SetValue(float Value);   // push a value when SourceProperty == External (e.g. a UMG slider)
float EvaluateNow();          // pre-seed the first chunk right after StreamClip() starts
```

`External` is the primary UE path for anything not physics-driven (route a
UMG slider's `OnValueChanged` to `SetValue`). The binding writes to the
subsystem's **single** `GetActivePlayback()` — v1 has one stream, so there is
no per-event-id scoping to configure. Call `EvaluateNow()` right after
starting a stream so the first ~100 ms isn't sent at un-modulated full
baseline.

## Global address override

For "one identical build deployed to many HMDs, each pinned 1:1 to its own
Hapbeat":

```cpp
void SetAddressOverride(int32 Player, int32 InGroup, bool bPersist = false);
void ClearPersistedAddressOverride();
int32 GetOverridePlayer() const; // AddressOverrideDisabled (-1) if that axis isn't overridden
int32 GetOverrideGroup() const;
static constexpr int32 AddressOverrideDisabled = -1;
```

Values outside `1..99` normalize to `AddressOverrideDisabled`. The override
is applied at exactly 4 send boundaries — `Play` / `Stop` / `StopAll` /
`StreamClip` (+ `StopStreamWithFlush`) — via
`UHapbeatTargetLibrary::ResolveTarget`, which rewrites only the `player_` /
`group_` segments of the outgoing target string **without touching any
EventMap/trigger-authored target**. With `bPersist = true` the values survive
relaunch (`GameUserSettings` ini, section `"HapbeatSDK"`, loaded in
`Initialize()` before auto-connect). The device OLED's group indicator
(`CONNECT_STATUS`) tracks the override group exclusively — `UHapbeatConfig::Group`
is a reserved field, not consumed by routing.

## Targeting

`UHapbeatTargetLibrary` (Blueprint-callable statics) builds / parses / rewrites
target strings, per hapbeat-contracts `device-addressing.md §2`:

```cpp
static FString BuildTarget(int32 Player = -1, const FString& Position = TEXT(""), int32 Group = -1);
static void ParseTarget(const FString& Target, int32& OutPlayer, FString& OutPosition, int32& OutGroup);
static FString ResolveTarget(const FString& Target, int32 OverridePlayer, int32 OverrideGroup);
```

```cpp
Hb->Play(TEXT("sample-kit.sine_100hz"), 0.6f, TEXT("player_1/pos_chest")); // one device
Hb->Play(TEXT("sample-kit.sine_100hz"), 0.6f, TEXT("*/pos_chest"));        // all chest devices
Hb->Play(TEXT("sample-kit.sine_100hz"));                                   // "" = broadcast (all)
```

## Samples

Ship as C++-authored actors (no binary maps/Blueprints to generate headless);
visuals default to engine primitive meshes (`/Engine/BasicShapes/*`) so they
run with zero import. Both sample kits ship raw under `Content/HapbeatSamples/`
— **deploy them to a device with Hapbeat Studio** for their Command-mode
events to produce haptics (StreamClip events need no kit installed).

**BasicExample** — drop `AHapbeatBasicExampleActor` into an empty level, hit
Play (`Content/HapbeatSamples/BasicExample/Kit/basic-exam-kit/`):

| Key | Action |
|---|---|
| Space | stream one-shot (`sine_100hz_1s.wav`, gain `1 × 0.5`) |
| R | stream loop (same clip) |
| F | command play (`basic-exam-kit.sine_200hz_1s`, gain `1 × 0.5`) |
| S | stop everything (`StopStream()` + `StopAll()`) |
| C | ping |

**Showcase** — 5 independent zone actors reusing `showcase-kit`
(`Content/HapbeatSamples/Showcase/Kit/showcase-kit/`), each demonstrating a
different authoring pattern:

| Zone | Actor | Key(s) | Pattern |
|---|---|---|---|
| Z1 Bowling Lane | `AHapbeatShowcaseZ1BowlingActor` | B (launch ball) | 6 pins, each its own `UHapbeatCollisionTriggerComponent` (Hit + VelocityScaled) — the zone issues zero imperative Hapbeat calls |
| Z2 Swing Door | `AHapbeatShowcaseZ2DoorActor` | F / G / L | 6 trigger components fired imperatively from a Tick-driven door state machine (open/close/rattle = StreamClip, slam/lock/unlock = Command) |
| Z3 Fishing | `AHapbeatShowcaseZ3FishingActor` | H (hook/release) | `UHapbeatSequenceComponent` (start/loop/release) + `UHapbeatParameterBinding` (`VelocityMagnitude → StreamGain`), both on the shark actor |
| Z4 Stream Console | `AHapbeatShowcaseZ4StreamConsoleActor` | T / U,J / N,M | live StreamClip gain+pan modulation via `External`-source `UHapbeatParameterBinding` |
| Z5 Target Range | `AHapbeatShowcaseZ5ChargeShotActor` | hold V | entirely imperative charge-loop + light/heavy shot — the counterpart to Z1's pure-component style |

Z4's tick one-shot and its looping StreamClip share the single v1 stream
session (REPLACE semantics) — firing the tick while the loop plays stops the
loop; press T again to restart it.

## Status

Level-2/3: fire from C++/Blueprint, `UHapbeatEventMap` data asset (native
Details-panel authoring, manifest-intensity baking, Test Play), collision /
sequence trigger components, real-time clip streaming with gain/pan
(`UHapbeatParameterBinding`), global address override, PONG-based device
liveness + delegates, 2 sample projects (BasicExample + 5-zone Showcase).
Compile-verified on UE 5.4 — build it in your own project to confirm on
other engine versions.

## License

MIT © Hapbeat
