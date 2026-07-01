# Getting Started (Unreal Engine)

Drive a Hapbeat device from Unreal Engine 5.

## Prerequisites

- Unreal Engine 5.x.
- A Hapbeat device on the same Wi-Fi/LAN with a **kit deployed** via
  [Hapbeat Studio](https://devtools.hapbeat.com).

## Install the plugin

1. Copy this repo into `YourProject/Plugins/HapbeatSDK/`.
2. Regenerate project files, then build (the plugin compiles with your project).
3. **Edit → Plugins → Hardware → Hapbeat SDK** → Enabled.

## Blueprint

1. **Get Game Instance → Get Subsystem**, class **Hapbeat Subsystem**.
2. Call **Connect** (Port `7700`, App Name `MyGame`) once, e.g. on BeginPlay.
3. Call **Play** (Event Id `sample-kit.sine_100hz`, Gain `0.5`) wherever you want haptics —
   an OnHit event, an AnimNotify, an input action. Event ids follow
   `<kit-name>.<file-name>`; `sample-kit.sine_100hz` is the standard test event.

## C++

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

Add `HapbeatSDK` to your module's `PublicDependencyModuleNames` to include the
header from C++.

## Targeting

```cpp
Hb->Play(TEXT("sample-kit.sine_100hz"), 0.6f, TEXT("player_1/chest")); // one device
Hb->Play(TEXT("sample-kit.sine_100hz"), 0.6f, TEXT("*/chest"));         // all chest devices
Hb->Play(TEXT("sample-kit.sine_100hz"));                                 // broadcast (all)
```

## The orthogonal design

- **Trigger** = any UE event (OnHit, AnimNotify, input action, gameplay code).
- **Tuning** = the event id + gain you pass.

These stay separable, matching the Hapbeat Unity SDK. The plugin also ships
Blueprint **trigger components** (`UHapbeatCollisionTriggerComponent`,
`UHapbeatSequenceComponent`), a **`UHapbeatEventMap`** data asset for default
gains, real-time **clip streaming** (`StreamClip`), and PONG-based device
liveness — see the README API table.

## Notes

- This plugin is authored against the UE5 API; build it in your project to
  verify. It uses the engine's `Sockets`/`Networking` modules (no external deps).
