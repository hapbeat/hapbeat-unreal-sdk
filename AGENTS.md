# Hapbeat Unreal Engine SDK — context for AI coding agents

Single self-contained reference so an AI coding agent can use this SDK
correctly from one file. Unreal plugin name: `HapbeatSDK`. Runtime entry
points: `UHapbeatBlueprintLibrary` (play an Event Map entry) and
`UHapbeatSubsystem` (transport, direct send, streams).

- last-verified-against: commit `0025d69` (branch `tmp`, tracking
  `save/stream-mixer-limiter`): headers under `Source/HapbeatSDK/Public/`,
  `docs/`, and `CHANGELOG.md` `[Unreleased]` agree with this file (docs-only
  update, no engine build run). The API index in `docs/cpp-api.md` is
  generated from the headers (`node Scripts/generate_api_reference.mjs --check`).
- Source of truth is the code: public API in
  `Source/HapbeatSDK/Public/HapbeatBlueprintLibrary.h` and
  `HapbeatSubsystem.h`; Event Map model in `HapbeatEventEntry.h` +
  `HapbeatEventMap.h` + `HapbeatEntryRef.h`; streaming in `HapbeatClip.h` +
  `HapbeatStreamPlayback.h`; triggers in `HapbeatTriggerComponent.h` (+
  `HapbeatCollisionTriggerComponent.h`, `HapbeatSequenceComponent.h`,
  `HapbeatTickEmitterComponent.h`, `HapbeatParameterBinding.h`,
  `HapbeatAnimNotify.h`); targeting in `HapbeatTargetLibrary.h`; settings in
  `HapbeatConfig.h`. If this file disagrees with the code, the code wins.
- Per-topic docs shipped in this repo (read these before guessing):
  `docs/getting-started.md`, `docs/event-map-and-playback.md`,
  `docs/cpp-api.md` (C++ API + declaration index), `docs/blueprint-nodes.md`
  (every Blueprint node), `docs/targeting-and-multi-hmd.md`,
  `docs/editor-menus.md`, `docs/showcase-unreal.md`, `docs/basic-example.md`,
  `docs/vr-config-example.md`, `docs/unreal-build.md`.
- Canonical docs: https://devtools.hapbeat.com/docs/sdk-integration/
- Event id and wire format are defined by **hapbeat-contracts**
  (`specs/message-format.md`, `specs/device-addressing.md`,
  `specs/stream-session-v2.md`); follow it, do not redefine here.

## What it is

A runtime plugin driving Hapbeat haptic devices from Unreal Engine 5 over
Wi-Fi UDP — callable from C++ and Blueprint. No cloud; works on the LAN. The
device self-filters by target/group; UDP has no ACK ("late is worse than
dropped"). It does NOT author or flash Kits (that's Hapbeat Studio), is not a
Bluetooth transport, and streams raw PCM16 WAV data directly (no engine audio
decode).

## Install & connect

1. Put the **contents** of this repo at `YourProject/Plugins/HapbeatSDK/` (so
   `Plugins/HapbeatSDK/HapbeatSDK.uplugin` exists — not one folder deeper).
2. Regenerate project files and build **Development Editor / Win64**
   (`docs/unreal-build.md`). **The project must be (or become) a C++
   project** — a Source-only plugin compiles alongside your own module; a
   Blueprint-only project needs a `Source/` folder added first (Unreal offers
   this automatically the first time you add C++ code).
3. **Edit → Plugins → Hapbeat SDK** → Enabled. Settings: **Tools → Hapbeat →
   Hapbeat Settings** (Project Settings → Plugins → Hapbeat).
4. `#include "HapbeatSubsystem.h"` / `"HapbeatBlueprintLibrary.h"` from your
   own module after adding `HapbeatSDK` to its `PublicDependencyModuleNames`.

No manual `Connect()` is needed in the common case: `UHapbeatSubsystem`
auto-connects in `Initialize()` (reads `UHapbeatConfig` for port / app name /
ping interval). This only fires in PIE / a packaged game;
`GameInstanceSubsystem`s don't exist in the bare editor.

## Make it vibrate (minimal)

Normal path — play an Event Map entry (clip, gain, pan, target, loop, delay
all come from the asset):

```cpp
// C++
#include "HapbeatBlueprintLibrary.h"
#include "HapbeatStreamPlayback.h"
UHapbeatStreamPlayback* Playback = UHapbeatBlueprintLibrary::PlayHapbeatEvent(
    this, EventMap, Entry /* FHapbeatEntryRef */,
    /*GainMultiplier*/ 1.0f, /*Pan*/ 0.0f, /*DelaySeconds*/ 0.0f);
// Playback is non-null only for Stream Clip entries.
UHapbeatBlueprintLibrary::StopHapbeatEvent(this, EventMap, Entry);
```
```
// Blueprint
Play Event (Hapbeat)  Map: DA_HapbeatEventMap  Entry: pickup   (detail pins: Gain Multiplier, Pan, Delay Seconds)
```

Direct send of a device-side event id (bypasses the Event Map — use only when
the id/target is built at runtime):

```cpp
#include "HapbeatSubsystem.h"
if (UHapbeatSubsystem* Hb = GetGameInstance()->GetSubsystem<UHapbeatSubsystem>())
{
    Hb->Play(TEXT("sample-kit.sine_100hz"), 0.5f);  // Gain 0..1
    Hb->Stop(TEXT("sample-kit.sine_100hz"));
    Hb->StopAll();
}
```

`sample-kit.sine_100hz` is the standard cross-SDK verification event
(hapbeat-contracts DEC-040) and must be present in the **kit deployed to the
device** via Hapbeat Studio. Stream Clip playback needs no Kit on the device.

## Core model: fire vs tuning, linked by an Event Map entry

- **Fire side** (when/where): `PlayHapbeatEvent` / a trigger component's
  `Fire()` / an Animation Notify.
- **Tuning side** (what/how strong): a `UHapbeatEventMap` (`UDataAsset`)
  entry + the Studio-authored Kit's `manifest.json` `parameters.intensity`.
  Create it from the Content Browser's *Hapbeat* category (`docs/editor-menus.md`);
  double-click it (or **Tools → Hapbeat → Hapbeat Event Map**) to open the
  dedicated Event Map window.
- Callers reference an entry by its **stable `FGuid` `Id`**, wrapped for
  Blueprint pins as `FHapbeatEntryRef` (`EntryId`, `IsSet()`); trigger
  components store it as `EntryId` (`FGuid`). Never use list index —
  reordering never breaks wiring. Look one up with
  `UHapbeatEventMap::FindById(FGuid, FHapbeatEventEntry&) const`.

`FHapbeatEventEntry` fields (`HapbeatEventEntry.h`): `Id` (`FGuid`,
read-only), `Mode` (`EHapticMode::Command` | `EHapticMode::StreamClip`),
`DisplayName`, `Category` + `EventName` (`GetEventId()` = `Category.EventName`,
or just `EventName` if `Category` is empty), `Gain` (0..2), `Pan` (-1..1,
added to the call-site Pan and clamped), `Target`, `bLoop` (StreamClip only),
`DelayOffsetSeconds` (-0.2..0.2, added to the global delay — see Gotchas),
`Notes`, `StreamClip` (`TSoftObjectPtr<UHapbeatClip>`, StreamClip mode only),
`CachedManifestIntensity` (baked by *Refresh Intensities*, `-1` = unresolved).

```cpp
FString GetEventId() const;       // "" if EventName empty; EventName alone if Category empty
float   GetEffectiveGain() const;  // CachedManifestIntensity < 0 ? Gain : Gain * CachedManifestIntensity
static TArray<FString> StandardPositions(); // pos_neck, pos_chest, pos_abd, pos_l_arm, pos_r_arm, ...
```

The device is a pure executor — it plays `req.gain` verbatim and never reads
the manifest; `GetEffectiveGain()` is the SDK's pre-multiplication before the
wire.

## Public runtime API

`UHapbeatBlueprintLibrary` (`HapbeatBlueprintLibrary.h`, static; the same
route as the Blueprint node "Play Event (Hapbeat)"):

```cpp
static UHapbeatStreamPlayback* PlayHapbeatEvent(const UObject* WorldContextObject, UHapbeatEventMap* Map,
    FHapbeatEntryRef Entry, float GainMultiplier = 1.0f, float Pan = 0.0f, float DelaySeconds = 0.0f);
static void StopHapbeatEvent(const UObject* WorldContextObject, UHapbeatEventMap* Map, FHapbeatEntryRef Entry);
static void FireHapbeatTickFromValue(UHapbeatTickEmitterComponent* TickEmitter, float Value);
```

`UHapbeatSubsystem` (`HapbeatSubsystem.h`; all `UFUNCTION(BlueprintCallable/
BlueprintPure, Category = "Hapbeat"...)` except where noted):

```cpp
void Connect(int32 InPort = 7700, const FString& InAppName = TEXT(""));
// Direct send — NOT through the Event Map (no entry Clip/Gain/Target/Delay).
void Play(const FString& EventId, float Gain = 1.0f, const FString& Target = TEXT(""), float Pan = 0.0f);
void Stop(const FString& EventId, const FString& Target = TEXT(""));
void StopAll(const FString& Target = TEXT(""));
void Ping();
void ReacquireStreamOwnership();   // take back a stream another app superseded (see Gotchas)

// Real-time clip streaming — multiple concurrent sources, one handle each.
UHapbeatStreamPlayback* StreamClip(UHapbeatClip* Clip, float BaselineGain = 1.0f, float InitialGain = 1.0f,
    const FString& Target = TEXT(""), bool bLoop = false, float InitialPan = 0.0f);
void StopStream();                 // stops EVERY source in this subsystem

// C++ only (not Blueprint): Event Map entry by GUID; PlayHapbeatEvent lands here.
UHapbeatStreamPlayback* PlayEntry(UHapbeatEventMap* Map, FGuid EntryId, float GainMultiplier = 1.0f,
    bool bForceNonLoop = false, float Pan = 0.0f, float ExtraDelaySeconds = 0.0f);
void StopEntry(UHapbeatEventMap* Map, FGuid EntryId);

// Global address override (one build, many HMDs, each pinned 1:1 to its own Hapbeat).
static constexpr int32 AddressOverrideDisabled = -1;
void  SetAddressOverride(int32 Player, int32 InGroup, bool bPersist = false);
void  ClearPersistedAddressOverride();
int32 GetOverridePlayer() const;  // AddressOverrideDisabled if that axis isn't overridden
int32 GetOverrideGroup() const;   // AddressOverrideDisabled if that axis isn't overridden
static bool TryGetPersistedAddressOverride(int32& OutPlayer, int32& OutGroup);  // C++ only
static void SavePersistedAddressOverride(int32 Player, int32 InGroup);          // C++ only; stored values only
static void RemovePersistedAddressOverride();                                   // C++ only; stored values only
static int32 NormalizeAddressOverride(int32 Value);                             // outside 1..99 -> -1

// Liveness (BlueprintPure).
bool  IsConnected() const;          // socket open — NOT device presence
int32 GetAliveDeviceCount() const;  // devices PONGed within max(5s, PingInterval*3)
bool  IsAlive() const;              // GetAliveDeviceCount() > 0
bool  IsStreaming() const;          // any endpoint stream session running
UHapbeatStreamPlayback* GetActivePlayback() const;  // first active source only

// Delegates (BlueprintAssignable, dynamic multicast).
FHapbeatOnConnected    OnConnected;    // fires only on liveness 0 -> positive (not socket-open)
FHapbeatOnDisconnected OnDisconnected; // fires only on liveness positive -> 0
FHapbeatOnError        OnError;        // (FString Message) — device ERROR (0xFF) packet
FHapbeatOnPong         OnPong;         // (FString Endpoint, int64 RttUs, FString DeviceName, FString Address, FString Firmware)
```

- `Play`/`Stop`/`StopAll`/`StreamClip` and Event Map playback resolve
  `Target` through `UHapbeatTargetLibrary::ResolveTarget` (applies the
  address override) before sending.
- `Subsystem::Play`'s `Gain` is clamped `0..1`; `Pan` for a Command needs
  firmware that supports DEC-055 pan.
- `StreamClip` returns `nullptr` (+ warning) for a null/empty clip. The SDK
  pre-multiplies PCM by the handle's live `Gain`/`Pan`, so `STREAM_BEGIN`
  always carries `gain = 1.0`.
- **Liveness**: UDP is connectionless, so `IsConnected()` only means the
  socket is open (stays `true` with every device off). `IsAlive()` /
  `GetAliveDeviceCount()` / `OnConnected`/`OnDisconnected` track real device
  presence from PONG replies. Call `Ping()` to probe.
- No explicit `Disconnect()`: `Deinitialize()` ends streams, cancels any
  delayed sends, and closes the socket.

## Trigger components, Tick Emitter, ParameterBinding, Anim Notify

Add via *Add Component → Hapbeat…*. All Event-Map-driven components reference
an `EventMap` + `EntryId` (`FGuid`; the Editor module gives these a friendly
entry picker — see below).

- **`UHapbeatTriggerComponent`** (base) — `Fire()`,
  `FireWithGain(float GainOverride)`, `FireScaled(float Velocity, float
  MinVelocity = 0, float MaxVelocity = 10)`, `FireWithCurve(float Value,
  UCurveFloat* Curve)`, `Stop()`, `SetGainMultiplier(float)` (pushes live to
  an active Stream Clip playback — a plain field write to `GainMultiplier`
  does NOT), `SetStreamPan(float)`, `GetActivePlayback()` (this trigger's own
  handle; `nullptr` for Command). Also `bTriggerEnabled`, `Cooldown` (seconds,
  unscaled real time), `GainMultiplier` (0..2), `bVerboseLog`, and the
  `OnFired` delegate (fire *request* passed the enabled/entry/cooldown
  checks — not a device-playback confirmation). Gain composition: Command →
  `wireGain = entry.GetEffectiveGain() * GainMultiplier * callMultiplier`;
  Stream Clip → `baseline = entry.GetEffectiveGain()`, `initialModulator =
  GainMultiplier * callMultiplier`.
- **`UHapbeatCollisionTriggerComponent : UHapbeatTriggerComponent`** — binds
  the owner primitive's `OnComponentHit` or `OnComponentBeginOverlap`
  (`TriggerEvent`: `EHapbeatCollisionEvent::Hit` | `BeginOverlap`); `GainMode`
  `Fixed` | `VelocityScaled` (impact speed cm/s, gated by
  `VelocityThreshold`/`MaxVelocity`, shaped by `VelocityCurve`);
  `TagFilter` (`NAME_None` = any actor); `bEnterOnly`;
  `ContactSeparationSeconds`. Hit needs the primitive's **Simulation
  Generates Hit Events**, overlap needs **Generate Overlap Events**.
- **`UHapbeatSequenceComponent : UHapbeatTriggerComponent`** — 3-phase
  grab/hold/release: `Fire()` fires `StartEntryId` one-shot then starts the
  inherited `EntryId` loop; `Stop()` stops the loop then fires `StopEntryId`
  one-shot after `StopShotDelay` seconds (default `0.05`).
- **`UHapbeatTickEmitterComponent : UHapbeatTriggerComponent`** — fires the
  entry one-shot each time a continuous value crosses `TickThreshold`:
  `FireFromValue(float)`, `FireFromVector2D(FVector2D)`, `FireNow()`,
  `ResetReference()`; `TickMode` (`AbsolutePosition` fixed marks |
  `AccumulatedMotion` summed movement), `Axis` (`X` | `Y` | `Magnitude`),
  `bEmitOnInitialValue`. At most 64 ticks per input; `Cooldown` can drop
  some.
- **`UHapbeatParameterBinding : UActorComponent`** — per-tick continuous
  modulation of a Stream Clip playback (`TargetTrigger`'s own playback):
  `SourceProperty` (`LocalPositionX/Y/Z`, `VelocityMagnitude`,
  `AngularVelocityMagnitude`, `PositionDeltaMagnitude` — frame-delta, for
  kinematic/grabbed actors — or `External`) → normalized
  `[InputMin,InputMax]` → `CurveType` (`Linear`/`EaseIn`/`EaseOut`/
  `Exponential`/`Custom`) → `[OutputMin,OutputMax]` → `OutputParameter`
  (`StreamGain` → `ApplyGainModulation`, `StreamPan` → `SetPan`).
  `SetValue(float)` pushes an `External` value (route a UMG slider's
  `OnValueChanged` here); `EvaluateNow()` runs one read-modulate-write
  immediately — `UHapbeatTriggerComponent` already calls this for every
  attached binding right after a Stream Clip start, to avoid a ~100 ms
  unmodulated burst. Getters: `GetCurrentInput/Normalized/Output`.
- **`UHapbeatAnimNotify` / `UHapbeatAnimNotifyState`** — play an entry at one
  animation frame (notify) or over a span (state: start plays, end stops);
  `EventMap`, `EntryId`, `GainMultiplier`.

## Streaming: `UHapbeatClip` + `UHapbeatStreamPlayback`

- `UHapbeatClip : UDataAsset` — `SampleRate`, `NumChannels`, raw interleaved
  little-endian PCM16 bytes (`Pcm16`), `NumSamples()`, `NumFrames()`,
  `DurationSeconds()`. **Asset creation**: in the Event Map window use
  **Import WAV...** (creates a `UHapbeatClip` beside the Event Map and
  assigns it to the entry), or Content Browser *Hapbeat Clip* and import a WAV
  from its Details (`docs/editor-menus.md`). **Runtime** (WAV header parse
  only, no engine audio decode):
  ```cpp
  static bool ParseWav(const TArray<uint8>& WavBytes, int32& OutSampleRate, int32& OutChannels,
      TArray<uint8>& OutPcm16, FString& OutError);
  static UHapbeatClip* CreateFromWavBytes(UObject* Outer, const TArray<uint8>& WavBytes); // transient
  ```
  Requires RIFF/WAVE, PCM format, 16-bit samples; `nullptr` (+ warning) on a
  bad file.
- Streams are normalized to **16 kHz stereo PCM16** before sending and mixed
  per endpoint (each alive device gets its own `STREAM_BEGIN/END` session;
  sources with different targets play concurrently, sources on one device are
  mixed). A source with no resolved device stays `Deferred
  (NoResolvedEndpoint)` and is never broadcast.
- Mixer: per-source gain/pan ramped per sample from the previous chunk's
  value, a limiter before final saturation (target 0.9 FS; only acts when the
  mixed peak exceeds 32768), clips resampled to 16 kHz by linear interpolation.
- **Stream session v2** (contracts `stream-session-v2.md`): the SDK detects
  per device whether it is v2 and uses lease + generation framing for v2,
  the legacy form otherwise. **Firmware 0.5.0 and later reject v1 streams** —
  this SDK revision is required to stream to them (FIRE / PLAY unchanged).
- `UHapbeatStreamPlayback : UObject` — the handle `StreamClip` /
  `PlayHapbeatEvent` returns, one per source. `BaselineGain` (frozen at start
  = `entry.GetEffectiveGain()`), `ApplyGainModulation(float Modulator)`
  (`Gain = clamp(BaselineGain * Modulator, 0, 2)`), `SetPan(float)`
  (`[-1,1]`), `SetPlaybackRate(float)` / `GetPlaybackRate()` (`0.25`–`4`,
  speed = pitch, resampled on the PC, `1` = as authored), `SetLoop(bool)` /
  `GetLoop()`, `Stop()` (this source only), `GetGain()`, `GetPan()`,
  `IsStopped()`, `IsActive()`, `GetStatus()`
  (`EHapbeatStreamPlaybackStatus`: `Deferred` / `Active` / `Stopped`),
  `GetDeferredReason()` (`None` / `NoResolvedEndpoint`),
  `GetStereoChannelGains(float& OutL, float& OutR)`. Pan is **linear
  balance, not equal-power** (`pan=0 → L=R=1.0` passthrough). Use
  `IsStopped()` for "finished"; `IsActive() == false` can also mean
  `Deferred`. Game-thread only.

## Editor tooling (`HapbeatSDKEditor` module)

Open an Event Map asset (double-click, or **Tools → Hapbeat → Hapbeat Event
Map**) — the dedicated window provides:

- Entry list add / remove / edit, **Import WAV...** for Stream Clip entries,
  the entry's effective gain.
- **Refresh Intensities** — scans every `*-manifest.json` under the project
  and enabled plugins (schema 2.0.0: `events`/`stream_events`, matched by
  `(eventId, mode)`) and bakes `parameters.intensity` into each entry's
  `CachedManifestIntensity`. Run after every Kit (re)deploy in Studio.
- **Test Play / Stop / Stop All / Ping** — send to a real device without
  entering PIE via a small editor-only socket independent of the runtime
  subsystem. Test Play streams Stream Clip entries too, but only in the legacy
  stream form, so it cannot stream to v2 devices (firmware 0.5.0+); use PIE
  there. Test Play resolves the same saved / build-pinned Address Override.

Other **Tools → Hapbeat** items: *Hapbeat Settings*, *Hapbeat Runtime Status*
(this machine's saved Address Override; PIE effective values), *Check for SDK
Updates*, *Turn Off Verbose Log on All Triggers*, *Export Event Map to
Markdown*. Details: `docs/editor-menus.md`.

Selecting a trigger component / Anim Notify with an `EventMap` assigned turns
its `EntryId` into an entry picker (by `DisplayName`, falling back to event
id, falling back to a short GUID). No `EventMap` → plain `FGuid` row.

## Targeting + address override

`Target` follows `[player_{N}/] {pos_X} [/group_{M}]` (empty = broadcast).
`UHapbeatTargetLibrary` (Blueprint-callable statics):

```cpp
static FString BuildTarget(int32 Player = -1, const FString& Position = TEXT(""), int32 Group = -1);
static void    ParseTarget(const FString& Target, int32& OutPlayer, FString& OutPosition, int32& OutGroup);
static FString ResolveTarget(const FString& Target, int32 OverridePlayer, int32 OverrideGroup);
static FString ApplyAddressPlaceholders(const FString& AppName, int32 OverridePlayer, int32 OverrideGroup); // <p> / <g> -> number or '-'
static bool    AddressMatches(const FString& Target, const FString& DeviceAddress);
```

For one build deployed 1:1 to many HMDs (each paired with its own Hapbeat),
use the subsystem's **global address override** instead of editing every
Event Map entry's `Target`: `SetAddressOverride(Player, Group, bPersist)`
forces the player/group segment on **every** outgoing
`Play`/`Stop`/`StopAll`/`StreamClip`/Event Map playback via `ResolveTarget`.
Pass `AddressOverrideDisabled` (`-1`) for an axis to leave it alone (not
"rewrite to -1"). `bPersist = true` saves to the platform's GameUserSettings
ini (`[HapbeatSDK]` section), restored in `Initialize()` before auto-connect.
`ClearPersistedAddressOverride()` removes the saved keys and turns off every
axis not pinned by the build. **`UHapbeatConfig::ForcedOverridePlayer` /
`ForcedOverrideGroup`** pin an axis for the whole build; they win over saved
and runtime values and survive Clear. Values outside `1..99` normalize to
disabled.

## Project settings (`UHapbeatConfig`)

`Port` (7700), `AppName` (OLED label, ≤16 chars, `<p>` / `<g>` placeholders;
empty = project name), `PingInterval` (5 s, 1–60), `StreamSendAheadSeconds`
(0.05, 0.01–0.2), `bCommandUnicast` (true: unicast Play/Stop/StopAll to
PONG-known devices, broadcast fallback when none), `HapticDelaySeconds`
(0–0.5), `ForcedOverridePlayer` / `ForcedOverrideGroup` (-1 = off),
`bEnableLogging` / `bVerboseLogging`. There is no stream-unicast switch
(streams are exact unicast only), no discovery timeout, and no `Group` field.

## Gotchas

- Nothing buzzes but a device is on the LAN => the event id is not in the
  **kit deployed to the device** (Command mode's #1 cause — Stream Clip needs
  no deploy), or `Target` doesn't match (try `""`).
- `IsConnected()` only means the socket is open; use `IsAlive()` /
  `GetAliveDeviceCount()` / `OnPong` to know a device actually answered.
- **Multiple concurrent stream sources**: each `StreamClip` /
  Stream-Clip `PlayHapbeatEvent` adds an independent source (own gain, pan,
  rate, loop, stop); it does not replace others. `StopStream()` and
  `StopHapbeatEvent` / `StopEntry` on a Stream Clip entry stop **all** sources
  in the subsystem — keep the handle and call `Stop()` for one.
- **Delay**: Event Map playback (`PlayHapbeatEvent`, `PlayEntry`, triggers,
  Anim Notify) is deferred by `max(0, HapticDelaySeconds +
  DelayOffsetSeconds + ExtraDelaySeconds)` (`DelaySeconds` pin). A Stream
  Clip entry still returns its handle immediately. `StopEntry` uses global +
  entry delay only. **Direct** `Subsystem::Play` / `StreamClip` are not
  delayed.
- **Stream ownership**: if another app starts streaming to the same device,
  this SDK's stream stays `Deferred` and nothing is sent (one warning in
  `LogHapbeat`). The SDK never takes it back by itself; it reacquires
  automatically when the app returns to the foreground. Otherwise call
  `ReacquireStreamOwnership()` from a user action (retry button).
- Play return value: `nullptr` is normal for Command; it is also returned for
  an unresolved entry / invalid clip, so check before using it.
- **This is a Source-only plugin.** A pure Blueprint project needs a
  `Source/` folder added (Unreal will offer) and a regenerate/build before it
  can be enabled — it cannot run from Blueprint-only binaries alone.
- Studio Kit clips are 16 kHz; matching that rate for stream WAVs avoids a
  resample (other rates are accepted and converted).
- `CachedManifestIntensity` is baked, not live — re-run **Refresh Intensities**
  after every Kit re-deploy, or effective gain silently uses a stale value
  (or plain `Gain` if never baked, `-1`).
- Multi-homed PC: discovery goes to each local subnet's broadcast address and
  locks to the subnet a PONG came from; if no device is found, check that the
  Hapbeat LAN's adapter has a route.

## More detail

- **Samples in this plugin** (`Source/HapbeatSDKSamples/`, content under
  `Content/HapbeatSamples/`; open with **Show Plugin Content**):
  - **BasicExample** — `AHapbeatBasicExampleActor` + `EM_BasicExample`:
    `Space` stream one-shot, `R` stream loop, `F` Command
    `basic-exam-kit.sine_200hz_1s` (needs `basic-exam-kit` deployed),
    `S` stop all, `C` ping. `docs/basic-example.md`.
  - **Showcase** — `EM_Showcase` + five zones (`docs/showcase-unreal.md` is
    the source of truth for Actor names and keys): Z1 Bowling
    (`AHapbeatShowcaseZ1BowlingActor`, collision trigger), Z2 Swing Door
    (`BP_Z2_Door`, Blueprint, `F`/`G`/`L`), Z3 Fishing
    (`AHapbeatShowcaseZ3FishingActor`, Sequence + velocity→Gain binding),
    Z4 Stream Console (`BP_Z4_StreamConsole`, Blueprint, Space + Gain/Pan
    sliders + Tick Emitter), Z5 Target Range
    (`AHapbeatShowcaseZ5ChargeShotActor`, charge/shot + target hit).
  - **VRConfigExample** — Address Override panel + test waveform for VR
    (`AHapbeatVRConfigExampleActor`, `docs/vr-config-example.md`).
  - Samples-module helpers (`UHapbeatAddressOverridePanelComponent`,
    `UHapbeatStatusOverlayComponent`, `UHapbeatEventLoggerComponent`) are for
    development UI, not a product API.
- **Concepts** (shared by every SDK): event id <-> kit https://devtools.hapbeat.com/docs/concepts/event-id-and-kit/ - command vs clip https://devtools.hapbeat.com/docs/concepts/fire-vs-clip/ - targeting https://devtools.hapbeat.com/docs/concepts/group-player-addressing/
- Human docs: https://devtools.hapbeat.com/docs/sdk-integration/ - Portal: https://devtools.hapbeat.com/
