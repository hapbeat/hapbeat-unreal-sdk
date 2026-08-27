# Hapbeat Unreal Engine SDK — context for AI coding agents

Single self-contained reference so an AI coding agent can use this SDK
correctly from one file. Unreal plugin name: `HapbeatSDK`. Public class:
`UHapbeatSubsystem`.

- last-verified-against: commit `a7599ea` (v1 — full L1 protocol, streaming,
  EventMap, triggers, editor tooling, samples; UE **5.4 compile-verified**
  across the runtime, editor, and samples modules).
- Source of truth is the code: public API in
  `Source/HapbeatSDK/Public/HapbeatSubsystem.h`; EventMap model in
  `HapbeatEventEntry.h` + `HapbeatEventMap.h`; streaming in `HapbeatClip.h` +
  `HapbeatStreamPlayback.h`; triggers in `HapbeatTriggerComponent.h` (+
  `HapbeatCollisionTriggerComponent.h`, `HapbeatSequenceComponent.h`,
  `HapbeatParameterBinding.h`); targeting in `HapbeatTargetLibrary.h`;
  settings in `HapbeatConfig.h`; wire format in `HapbeatProtocol.h`. If this
  file disagrees with the code, the code wins.
- Canonical docs: https://devtools.hapbeat.com/docs/sdk-integration/
- Event id and wire format are defined by **hapbeat-contracts**
  (`specs/message-format.md`, `specs/device-addressing.md`); follow it, do not
  redefine here.

## What it is

A runtime plugin driving Hapbeat haptic devices from Unreal Engine 5 over
Wi-Fi UDP broadcast — callable from C++ and Blueprint. No cloud; works on the
LAN. The device self-filters by target/group; UDP has no ACK ("late is worse
than dropped"). It does NOT author or flash Kits (that's Hapbeat Studio), is
not a Bluetooth transport, and streams raw PCM16 WAV bytes directly (no
engine audio decode).

## Install & connect

1. Copy this repo into your project as `YourProject/Plugins/HapbeatSDK/` (so
   `Plugins/HapbeatSDK/HapbeatSDK.uplugin` exists).
2. Regenerate project files and build. **The project must be (or become) a
   C++ project** — a Source-only plugin compiles alongside your own module;
   a Blueprint-only project needs a `Source/` folder added first (Unreal
   offers this automatically the first time you add C++ code).
3. **Edit → Plugins → Hardware → Hapbeat SDK** → Enabled.
4. `#include "HapbeatSubsystem.h"` from your own module after adding
   `HapbeatSDK` to its `PublicDependencyModuleNames`.

No manual `Connect()` is needed in the common case: `UHapbeatSubsystem`
auto-connects in `Initialize()` (reads `UHapbeatConfig` for port/app
name/ping interval) — matches the Unity SDK's `Awake()` auto-connect. This
only fires in PIE / a packaged game; `GameInstanceSubsystem`s don't exist in
the bare editor.

## Make it vibrate (minimal)

```cpp
// C++
#include "HapbeatSubsystem.h"
if (UHapbeatSubsystem* Hb = GetGameInstance()->GetSubsystem<UHapbeatSubsystem>())
{
    Hb->Play(TEXT("sample-kit.sine_100hz"), 0.5f);  // Gain 0..1
    Hb->Stop(TEXT("sample-kit.sine_100hz"));
    Hb->StopAll();
}
```
```
// Blueprint
Get Game Instance → Get Subsystem (Hapbeat Subsystem) → Play (Event Id "sample-kit.sine_100hz", Gain 0.5)
```

`sample-kit.sine_100hz` is the standard cross-SDK verification event
(hapbeat-contracts DEC-040) and must be present in the **kit deployed to the
device** via Hapbeat Studio.

## Core model: fire vs tuning, linked by event id

- **Fire side** (when/where): a `Play()` call or a trigger component's
  `Fire()`.
- **Tuning side** (what/how strong): a `UHapbeatEventMap` (`UDataAsset`) entry
  + the Studio-authored Kit's `manifest.json` `parameters.intensity`. Create
  via Content Browser → *Miscellaneous → Data Asset → Hapbeat Event Map*;
  edit entries in the **native Details panel** (no custom window — add /
  remove / reorder / duplicate / multi-edit are free).
- Triggers reference an entry by its **stable `FGuid` `Id`** (auto-assigned
  on add/duplicate), never by list index — reordering never breaks wiring.
  Look one up with `UHapbeatEventMap::FindById(FGuid, FHapbeatEventEntry&) const`.

`FHapbeatEventEntry` fields (`HapbeatEventEntry.h`): `Id` (`FGuid`,
read-only), `Mode` (`EHapticMode::Command` | `EHapticMode::StreamClip`),
`DisplayName`, `Category` + `EventName` (`GetEventId()` = `Category.EventName`,
or just `EventName` if `Category` is empty), `Gain` (0..2), `Target`, `bLoop`
(StreamClip only), `DelayOffsetSeconds` (declared, **not wired to runtime
timing yet** — see Gotchas), `Notes`, `StreamClip`
(`TSoftObjectPtr<UHapbeatClip>`, StreamClip mode only),
`CachedManifestIntensity` (baked by *Refresh Intensities*, `-1` = unresolved).

```cpp
FString GetEventId() const;       // "" if EventName empty; EventName alone if Category empty
float   GetEffectiveGain() const;  // CachedManifestIntensity < 0 ? Gain : Gain * CachedManifestIntensity
static TArray<FString> StandardPositions(); // pos_neck, pos_chest, pos_abd, pos_l_arm, pos_r_arm, ...
```

The device is a pure executor — it plays `req.gain` verbatim and never reads
the manifest; `GetEffectiveGain()` is the SDK's pre-multiplication before the
wire.

## Public runtime API (`UHapbeatSubsystem`, verbatim)

All `UFUNCTION(BlueprintCallable/BlueprintPure, Category = "Hapbeat"...)`.

```cpp
void Connect(int32 InPort = 7700, const FString& InAppName = TEXT(""));
void Play(const FString& EventId, float Gain = 1.0f, const FString& Target = TEXT(""));
void Stop(const FString& EventId, const FString& Target = TEXT(""));
void StopAll(const FString& Target = TEXT(""));
void Ping();

// Real-time clip streaming (single active session, REPLACE semantics — see Gotchas).
UHapbeatStreamPlayback* StreamClip(UHapbeatClip* Clip, float BaselineGain = 1.0f, float InitialGain = 1.0f,
    const FString& Target = TEXT(""), bool bLoop = false);
void StopStream();

// Global address override (one build, many HMDs, each pinned 1:1 to its own Hapbeat).
static constexpr int32 AddressOverrideDisabled = -1;
void  SetAddressOverride(int32 Player, int32 InGroup, bool bPersist = false);
void  ClearPersistedAddressOverride();
int32 GetOverridePlayer() const;  // AddressOverrideDisabled if that axis isn't overridden
int32 GetOverrideGroup() const;   // AddressOverrideDisabled if that axis isn't overridden

// Liveness (BlueprintPure).
bool  IsConnected() const;          // socket open — NOT device presence
int32 GetAliveDeviceCount() const;  // devices PONGed within max(5s, PingInterval*3)
bool  IsAlive() const;              // GetAliveDeviceCount() > 0
bool  IsStreaming() const;
UHapbeatStreamPlayback* GetActivePlayback() const;

// Delegates (BlueprintAssignable, dynamic multicast).
FHapbeatOnConnected    OnConnected;    // fires only on liveness 0 -> positive (not socket-open)
FHapbeatOnDisconnected OnDisconnected; // fires only on liveness positive -> 0
FHapbeatOnError        OnError;        // (FString Message) — device ERROR (0xFF) packet
FHapbeatOnPong         OnPong;         // (FString Endpoint, int64 RttUs, FString DeviceName, FString Address, FString Firmware)
```

- `Play`/`Stop`/`StopAll` resolve `Target` through
  `UHapbeatTargetLibrary::ResolveTarget` (applies the address override) before
  sending; `Play`'s `Gain` is clamped `0..1`.
- `StreamClip` pre-multiplies every PCM sample by the handle's live
  `Gain`/`Pan`, so `STREAM_BEGIN` always carries `gain = 1.0` (the device must
  not re-apply gain). Returns `nullptr` (+ warning) for a null/empty clip.
- **Liveness**: UDP is connectionless, so `IsConnected()` only means the
  socket is open (stays `true` with every device off). `IsAlive()` /
  `GetAliveDeviceCount()` / `OnConnected`/`OnDisconnected` track real device
  presence from PONG replies.
- No explicit `Disconnect()`: `Deinitialize()` sends `CONNECT_STATUS(false)`
  (if `AppName` was set) and closes the socket.

## Trigger components + ParameterBinding

Add via *Add Component → Hapbeat…*. All reference an `EventMap` +
`EntryId`/`FGuid` (the Editor module gives these a friendly dropdown — see
below).

- **`UHapbeatTriggerComponent`** (base) — `Fire()`,
  `FireWithGain(float GainOverride)`, `FireScaled(float Velocity, float
  MinVelocity = 0, float MaxVelocity = 10)`, `FireWithCurve(float Value,
  UCurveFloat* Curve)`, `Stop()`, `SetGainMultiplier(float)` (pushes live to
  an active StreamClip playback — a plain field write to `GainMultiplier`
  does NOT), `SetStreamPan(float)`, `GetActivePlayback()` (this trigger's own
  handle). Also `bTriggerEnabled`, `Cooldown` (seconds, unscaled real time),
  `GainMultiplier` (0..2), `bVerboseLog`. Gain composition: Command →
  `wireGain = entry.GetEffectiveGain() * GainMultiplier * callMultiplier`;
  StreamClip → `baseline = entry.GetEffectiveGain()`, `initialModulator =
  GainMultiplier * callMultiplier`.
- **`UHapbeatCollisionTriggerComponent : UHapbeatTriggerComponent`** — binds
  the owner primitive's `OnComponentHit` or `OnComponentBeginOverlap`
  (`TriggerEvent`); `GainMode` `Fixed` | `VelocityScaled` (impact speed cm/s →
  `VelocityCurve`, gated by `VelocityThreshold`/`MaxVelocity`); `TagFilter`
  (`NAME_None` = any actor).
- **`UHapbeatSequenceComponent : UHapbeatTriggerComponent`** — 3-phase
  grab/hold/release: `Fire()` fires `StartEntryId` one-shot then starts the
  inherited `EntryId` loop; `Stop()` stops the loop then fires `StopEntryId`
  one-shot after `StopShotDelay` seconds (default `0.05`, isolates it from the
  loop's device-side ring-flush burst).
- **`UHapbeatParameterBinding : UActorComponent`** — per-tick continuous
  modulation of the subsystem's *single* active StreamClip playback:
  `SourceProperty` (`LocalPositionX/Y/Z`, `VelocityMagnitude`,
  `AngularVelocityMagnitude`, `PositionDeltaMagnitude` — frame-delta, for
  kinematic/grabbed actors — or `External`) → normalized
  `[InputMin,InputMax]` → `CurveType` (`Linear`/`EaseIn`/`EaseOut`/
  `Exponential`/`Custom`) → `[OutputMin,OutputMax]` → `OutputParameter`
  (`StreamGain` → `ApplyGainModulation`, `StreamPan` → `SetPan`).
  `SetValue(float)` pushes an `External` value (route a UMG slider's
  `OnValueChanged` here); `EvaluateNow()` runs one read-modulate-write
  immediately — `UHapbeatTriggerComponent` already calls this for every
  attached binding right after a StreamClip start, to avoid a ~100 ms
  unmodulated burst.

## Streaming: `UHapbeatClip` + `UHapbeatStreamPlayback`

- `UHapbeatClip : UDataAsset` — `SampleRate`, `NumChannels`, raw interleaved
  little-endian PCM16 bytes (`Pcm16`), `NumSamples()`, `NumFrames()`,
  `DurationSeconds()`. Built via a **WAV header parse only, no engine audio
  decode**:
  ```cpp
  static bool ParseWav(const TArray<uint8>& WavBytes, int32& OutSampleRate, int32& OutChannels,
      TArray<uint8>& OutPcm16, FString& OutError);
  static UHapbeatClip* CreateFromWavBytes(UObject* Outer, const TArray<uint8>& WavBytes);
  ```
  Requires RIFF/WAVE, PCM format, 16-bit samples. Load bytes yourself (e.g.
  `FFileHelper::LoadFileToArray`) — no asset-import step; returns `nullptr`
  (+ warning) on a bad file.
- `UHapbeatStreamPlayback : UObject` — the handle `StreamClip` returns.
  `BaselineGain` (frozen at start = `entry.GetEffectiveGain()`),
  `ApplyGainModulation(float Modulator)` (`Gain = clamp(BaselineGain *
  Modulator, 0, 2)`), `SetPan(float)` (`[-1,1]`), `Stop()`, `GetGain()`,
  `GetPan()`, `IsStopped()`, `IsActive()`, `GetStereoChannelGains(float& OutL,
  float& OutR)`. Pan is **linear balance, not equal-power** (`pan=0 → L=R=1.0`
  passthrough) — Hapbeat's L/R actuators sit on the body and don't binaurally
  sum, so equal-power's `sqrt(1/2)` would silently attenuate every centered
  stereo clip ~3 dB. Game-thread only, no atomics (writer and reader both run
  on the game thread).

## Editor tooling (`HapbeatSDKEditor` module)

Select a `UHapbeatEventMap` asset — the Details panel adds:

- **Refresh Intensities** — scans every `*-manifest.json` under `Content/`
  (schema 2.0.0: `events`/`stream_events`, matched by `(eventId, mode)`) and
  bakes `parameters.intensity` into each entry's `CachedManifestIntensity`.
  Run after every Kit (re)deploy in Studio.
- **Test Entry** dropdown + **Test Play / Stop / Stop All / Ping** — sanity-
  checks a Command-mode entry against a real device without entering PIE, via
  a small editor-only socket independent of the runtime subsystem. **Test
  Play is disabled for StreamClip entries** (no local clip on the device) —
  use PIE + the runtime `StreamClip` API instead.

Select a trigger component with an `EventMap` assigned and its raw
`EntryId`/`StartEntryId`/`StopEntryId` `FGuid` fields become a dropdown of
that map's entries (by `DisplayName`, falling back to event id, falling back
to a short GUID). No `EventMap` assigned → plain `FGuid` row.

## Targeting + address override

`Target` follows `[player_{N}/] {pos_X} [/group_{M}]` (empty = broadcast).
`UHapbeatTargetLibrary` (Blueprint-callable statics):

```cpp
static FString BuildTarget(int32 Player = -1, const FString& Position = TEXT(""), int32 Group = -1);
static void    ParseTarget(const FString& Target, int32& OutPlayer, FString& OutPosition, int32& OutGroup);
static FString ResolveTarget(const FString& Target, int32 OverridePlayer, int32 OverrideGroup);
```

For one build deployed 1:1 to many HMDs (each paired with its own Hapbeat),
use the subsystem's **global address override** instead of editing every
EventMap entry's `Target`: `SetAddressOverride(Player, Group, bPersist)`
forces the player/group segment on **every** outgoing
`Play`/`Stop`/`StopAll`/`StreamClip`, via
`ResolveTarget`. Pass `AddressOverrideDisabled` (`-1`) for an axis to leave it
alone (not "rewrite to -1"). `bPersist = true` saves to the platform's
GameUserSettings ini (`[HapbeatSDK]` section), restored in `Initialize()`
before auto-connect. `ClearPersistedAddressOverride()` removes the saved keys
and reverts both axes to disabled. Values outside `1..99` normalize to
disabled.

## Gotchas

- Nothing buzzes but a device is on the LAN => the event id is not in the
  **kit deployed to the device** (Command mode's #1 cause — StreamClip needs
  no deploy), or `Target` doesn't match (try `""`).
- `IsConnected()` only means the socket is open; use `IsAlive()` /
  `GetAliveDeviceCount()` / `OnPong` to know a device actually answered.
- **Single active stream, REPLACE semantics** — a new `StreamClip()` first
  stops any currently-streaming session (`STREAM_END`) then starts the new
  one; no multi-source mixing in v1.
- **This is a Source-only plugin.** A pure Blueprint project needs a
  `Source/` folder added (Unreal will offer) and a regenerate/build before it
  can be enabled — it cannot run from Blueprint-only binaries alone.
- Streaming WAVs must be 16-bit PCM RIFF/WAVE; Studio Kit clips are 16 kHz —
  match that rate for parity with the device's Command-mode playback.
- `CachedManifestIntensity` is baked, not live — re-run **Refresh Intensities**
  after every Kit re-deploy, or effective gain silently uses a stale value
  (or plain `Gain` if never baked, `-1`).
- `UHapbeatConfig::HapticDelaySeconds`, `FHapbeatEventEntry::DelayOffsetSeconds`,
  and `DiscoveryTimeoutMs` are declared (Unity config parity) but **not wired
  to any runtime behavior** in v1 — every fire goes out immediately and there
  is no discovery thread.
- Multi-homed PC: UDP broadcast may exit the wrong NIC — ensure the Hapbeat
  LAN's adapter has the route.
- `UHapbeatConfig::Group` is reserved (Unity parity) and **not** consumed by
  the runtime; the device's OLED group display tracks `SetAddressOverride`
  exclusively, and routing-group filtering is the trailing `/group_{N}`
  target segment.

## More detail

When this single file is not enough, an agent can fetch:

- **Samples in this plugin:** `Source/HapbeatSDKSamples/` — `AHapbeatBasicExampleActor`
  (drop one actor in an empty level; Space/R/F/S/C keys) and 5 Showcase zone
  actors (bowling, door, fishing, stream console, charge shot), all
  C++-authored (no binary map/Blueprint assets to import).
- **Concepts** (shared by every SDK): event id <-> kit https://devtools.hapbeat.com/docs/concepts/event-id-and-kit/ - command vs clip https://devtools.hapbeat.com/docs/concepts/fire-vs-clip/ - targeting https://devtools.hapbeat.com/docs/concepts/group-player-addressing/
- Human docs: https://devtools.hapbeat.com/docs/sdk-integration/ - Portal: https://devtools.hapbeat.com/
