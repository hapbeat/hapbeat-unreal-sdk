# Getting Started (Unreal Engine)

Drive a Hapbeat device from Unreal Engine 5 — Blueprint or C++, plus the
authoring tools for event tuning, trigger components, and real-time clip
streaming.

## Prerequisites

- Unreal Engine 5.3+ (compile-verified on **5.4**).
- A C++ toolchain for your project: the plugin ships native Runtime / Samples
  / Editor modules, so even a Blueprint-only project needs to regenerate
  project files and build once after adding it.
- A Hapbeat device on the same Wi-Fi/LAN. Command-mode events (the ones
  played by event id) need a **kit deployed** via
  [Hapbeat Studio](https://devtools.hapbeat.com) first; StreamClip events need
  no kit on the device.

## Install the plugin

1. Copy this repo into `YourProject/Plugins/HapbeatSDK/`.
2. Regenerate project files, then build (the plugin compiles with your
   project).
3. **Edit → Plugins → Hardware → Hapbeat SDK** → Enabled.
4. C++ only: add `HapbeatSDK` to your module's `PublicDependencyModuleNames`
   (in your own `*.Build.cs`) to `#include "HapbeatSubsystem.h"`.

Connection settings — port, app name, ping interval, streaming send-ahead,
haptic delay, logging — are in **Project Settings → Plugins → Hapbeat**
(`UHapbeatConfig`).

## Quick start: Blueprint

The subsystem **auto-connects** during `Initialize()` — before any actor's
`BeginPlay` — using the Project Settings values above (`AppName` falls back
to your project's name if left empty), so a device on the LAN already sees a
`CONNECT_STATUS` by the time your level starts. Calling **Connect** yourself,
as in step 2 below, is optional: it's safe to call again and lets you
override the port/app name at runtime.

1. **Get Game Instance → Get Subsystem**, class **Hapbeat Subsystem**.
2. Call **Connect** (Port `7700`, App Name `MyGame`) once, e.g. on BeginPlay.
3. Call **Play** (Event Id `sample-kit.sine_100hz`, Gain `0.5`) wherever you
   want haptics — an OnHit event, an input action, a UI button click. Event
   ids follow `<kit-name>.<file-name>`; `sample-kit.sine_100hz` is the
   standard test event.

## Quick start: C++

```cpp
#include "HapbeatSubsystem.h"

void AMyActor::BeginPlay()
{
    Super::BeginPlay();
    if (UHapbeatSubsystem* Hb = GetGameInstance()->GetSubsystem<UHapbeatSubsystem>())
    {
        Hb->Connect(7700, TEXT("MyGame"));
    }
}

void AMyActor::OnHit()
{
    if (UHapbeatSubsystem* Hb = GetGameInstance()->GetSubsystem<UHapbeatSubsystem>())
    {
        Hb->Play(TEXT("sample-kit.sine_100hz"), 0.8f);
    }
}
```

If you call `Play`/`Stop`/etc. before `Connect`, the subsystem lazily connects
with the last `Port`/`AppName` (defaults `7700` / `""`).

## Authoring an EventMap

`UHapbeatEventMap` is the tuning side: it maps a stable event id to a default
gain, mode, and target — so gameplay code / trigger components stay one-liners
you can re-tune without touching them again.

1. **Content Browser → Add → Miscellaneous → Data Asset → Hapbeat Event
   Map.** Name it whatever you like (e.g. `DA_HapbeatEvents`).
2. Open it. Under **Entries**, click `+` to add an entry:
   - **Mode** — `Command` (device plays its own installed clip) or
     `StreamClip` (SDK streams a `UHapbeatClip` over UDP — see
     [Streaming a clip](#streaming-a-clip--modulating-it-live) below).
   - **Display Name** — a human label, e.g. "Landing Impact".
   - **Category** / **Event Name** — the two halves of the event id
     (`Category.EventName`, matching the kit deployed to the device).
   - **Gain** — 0..2 author gain.
   - **Target** — device address filter, empty = broadcast.
3. Click **Refresh Intensities** (button above the Entries array). This
   scans every `*-manifest.json` under your project's `Content/` folder,
   matches each entry's `(event id, mode)` against the kit manifest's
   authored `parameters.intensity` (set in Hapbeat Studio), and bakes it into
   the entry's read-only `Cached Manifest Intensity` field. The device is a
   pure executor and never reads the manifest itself — this baked value is
   what makes `Gain × intensity` land on the wire without shipping the
   manifest at runtime. An entry with no manifest match keeps `-1` (falls
   back to plain `Gain`).
4. Use the entry picker + **Test Play / Stop / Stop All / Ping** row to fire
   the selected entry at a real device straight from the editor, without
   entering PIE. (StreamClip entries are disabled here — the device has no
   local clip to test-play; exercise those in PIE.)

Add / remove / reorder / duplicate / multi-select-edit entries with the
native array UI — there is no separate EventMap window to open.

## Wiring trigger components

Add a `UHapbeatTriggerComponent` (or a subclass) to any Actor, assign the
**Event Map** and pick an **Entry** from the dropdown (only appears once an
EventMap is assigned — otherwise you see the raw entry-id field), then wire
`Fire()` / `Stop()` to whatever should trigger it.

**Collision** (`UHapbeatCollisionTriggerComponent`):

1. Add the component to an actor that has a collider.
2. Set **Trigger Event** to `Hit` (needs "Simulation Generates Hit Events" on
   the primitive) or `Begin Overlap` (needs "Generate Overlap Events").
3. Set **Gain Mode** to `Fixed` (fire at the entry's gain as-is) or
   `Velocity Scaled` (map impact speed through **Velocity Curve**, gated by
   **Velocity Threshold** / **Max Velocity**).
4. Optionally set **Tag Filter** so only actors with a matching Actor Tag
   trigger it.

No `Fire()` call needed — it fires itself from the physics event.

**Sequence** (`UHapbeatSequenceComponent`, for grab/hold/release):

1. Add the component, assign **Event Map**.
2. Set the inherited **Entry Id** to the *loop* entry (a looping StreamClip —
   the sustained "held" feedback).
3. Set **Start Entry Id** / **Stop Entry Id** to the impact/release one-shots.
4. Wire your grab-begin event to `Fire()` (plays Start, then starts the
   loop) and grab-end to `Stop()` (stops the loop, then — after
   **Stop Shot Delay** seconds, default `0.05` — plays Stop).

**Anything else** — call `Fire()` / `FireWithGain(gain)` /
`FireScaled(velocity, min, max)` / `FireWithCurve(value, curve)` /
`Stop()` from any Blueprint event (UI button `OnClick`, an Animation
Notify, `Player Input` action bindings, …) on a plain
`UHapbeatTriggerComponent`.

## Streaming a clip + modulating it live

StreamClip sends raw PCM16 audio to the device instead of an event id — no
kit needs to be installed for it to produce haptics, and you can modulate its
gain/pan continuously while it plays.

**Building the clip is currently a C++ step** (`UHapbeatClip::CreateFromWavBytes`
is not `BlueprintCallable`, and there is no in-editor `.wav` import yet):

```cpp
#include "HapbeatClip.h"
#include "Misc/FileHelper.h"

TArray<uint8> WavBytes;
if (FFileHelper::LoadFileToArray(WavBytes, *WavPath))
{
    UHapbeatClip* Clip = UHapbeatClip::CreateFromWavBytes(GetTransientPackage(), WavBytes);
    // Keep Clip alive with a UPROPERTY on your actor/component — an
    // FHapbeatEventEntry only holds it via a TSoftObjectPtr, which is not a
    // GC-strong reference.
}
```

Once you have a `UHapbeatClip*`, either:

- assign it to an `FHapbeatEventEntry`'s **Stream Clip** field (Mode =
  `StreamClip`) and drive it through a trigger component's `Fire()`/`Stop()`
  as usual, or
- call the subsystem directly:

```cpp
UHapbeatStreamPlayback* Playback = Hb->StreamClip(Clip, /*BaselineGain=*/1.0f,
    /*InitialGain=*/1.0f, /*Target=*/TEXT(""), /*bLoop=*/true);
```

`Playback` is your live handle: `ApplyGainModulation(modulator)` sets
`Gain = clamp(BaselineGain × modulator, 0, 2)`, `SetPan(pan)` sets stereo
balance (`-1`..`+1`, linear balance — not equal-power, since Hapbeat's L/R
actuators don't binaurally sum), `Stop()` ends it. There is a **single active
stream session** in v1 — starting a new one replaces whatever was playing.

To modulate continuously instead of one-shot calls, add a
`UHapbeatParameterBinding` component to the same actor:

1. **Source Property** — pick what to read each tick (`Local Position X/Y/Z`,
   `Velocity Magnitude`, `Angular Velocity Magnitude`,
   `Position Delta Magnitude` for kinematic/code-moved bodies), or
   **External** to push a value yourself (e.g. a UMG slider's
   `OnValueChanged` → `SetValue(Value)`).
2. Set **Input Min** / **Input Max** (the source's expected range) and a
   **Curve Type** (`Linear` / `Ease In` / `Ease Out` / `Exponential` /
   `Custom` — the last reads **Custom Curve**, a `UCurveFloat`).
3. Set **Output Parameter** to `Stream Gain` or `Stream Pan`, and
   **Output Min** / **Output Max**.
4. Call `EvaluateNow()` right after your `StreamClip()` call so the first
   ~100 ms of audio isn't sent at full un-modulated baseline while waiting
   for the first `Tick`.

The binding writes to the subsystem's single active playback
(`GetActivePlayback()`) — attach one component per parameter (one for
`Stream Gain`, another for `Stream Pan`, if you need both at once).

## Global address override

For deploying **one identical build to many HMDs**, each pinned 1:1 to its own
Hapbeat device, without touching any EventMap/trigger target string:

```cpp
Hb->SetAddressOverride(/*Player=*/3, /*InGroup=*/-1, /*bPersist=*/true);
```

This forces `player_3` onto every outgoing `Play`/`Stop`/`StopAll`/
`StreamClip`/`StopStreamWithFlush` target — pass `-1` for an axis to leave it
alone. With `bPersist = true` the value survives the next launch (saved to
the platform's `GameUserSettings` ini). `GetOverridePlayer()` /
`GetOverrideGroup()` read the current values back;
`ClearPersistedAddressOverride()` reverts and forgets them. Values outside
`1..99` normalize to disabled (`UHapbeatSubsystem::AddressOverrideDisabled`,
`-1`).

## Targeting

```cpp
Hb->Play(TEXT("sample-kit.sine_100hz"), 0.6f, TEXT("player_1/pos_chest")); // one device
Hb->Play(TEXT("sample-kit.sine_100hz"), 0.6f, TEXT("*/pos_chest"));         // all chest devices
Hb->Play(TEXT("sample-kit.sine_100hz"));                                    // broadcast (all)
```

`UHapbeatTargetLibrary::BuildTarget` / `ParseTarget` are Blueprint-callable
helpers for composing / decomposing these strings.

## Samples

Both ship as C++-authored actors with engine-primitive visuals (no imported
meshes, no binary Blueprint assets) — drop the actor(s) into any level and
hit Play. **Deploy the sample's kit to your device via Hapbeat Studio first**
so its Command-mode events produce haptics (StreamClip events work with no
kit installed):

- **BasicExample** — place a single `AHapbeatBasicExampleActor` in an empty
  level. Space = stream one-shot, R = stream loop, F = command play, S = stop
  everything, C = ping. Kit: `Content/HapbeatSamples/BasicExample/Kit/basic-exam-kit/`.
- **Showcase** — place any of the 5 zone actors
  (`AHapbeatShowcaseZ1BowlingActor` … `Z5ChargeShotActor`), each independent
  and demonstrating a different authoring pattern (pure collision-trigger
  wiring, imperative state-machine fires, sequence + parameter binding,
  live stream modulation, fully imperative charge/fire). See the README's
  [Samples](../README.md#samples) table for the exact key bindings per zone.
  Kit: `Content/HapbeatSamples/Showcase/Kit/showcase-kit/`.

## The orthogonal design

- **Trigger** = any UE event (OnHit, input action, UI click, gameplay code).
- **Tuning** = the EventMap entry (or the event id + gain you pass directly).

These stay separable, matching the Hapbeat Unity SDK.

## Notes

- The plugin is authored against the UE5 API and marked `IsBetaVersion` in
  `HapbeatSDK.uplugin` — build it in your project to verify. It uses only the
  engine's `Sockets`/`Networking` modules (no external deps).
- `UHapbeatConfig::HapticDelaySeconds` and
  `FHapbeatEventEntry::DelayOffsetSeconds` are reserved fields — v1 does not
  defer sends by them yet; every fire goes out immediately.
- StreamClip is a single-session, REPLACE-semantics feature in v1 — starting
  a new stream stops whatever was already playing.
