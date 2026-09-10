---
kind: reference
sidebar:
  order: 3
  label: C++ API
---

# C++ API

通常は Event Map の entry を再生します。ゲームコードは再生のタイミングと entry を指定し、Clip・Gain・Target・Loop は Data Asset で管理します。

## まず選ぶクラス

- Event Map の entry を再生: `UHapbeatBlueprintLibrary`。
- Actor の再利用可能な再生口: `UHapbeatTriggerComponent`。
- event ID / Clip を直接送信: `UHapbeatSubsystem`。
- 1 回の Stream を調整・停止: `UHapbeatStreamPlayback`。
- ゲーム内の連続値を Gain / Pan に変換: `UHapbeatParameterBinding`。
- Target の組立・検査: `UHapbeatTargetLibrary`。

`UHapbeatEventMap` は触覚 entry を保持する Data Asset、`FHapbeatEntryRef` はその中の 1 entry を指す参照です。`FHapbeatEntryRef` は安定した ID を保持するため、entry の表示名変更や並べ替えで再生先が変わりません。

## 公開クラス一覧

### 再生・接続

- `UHapbeatBlueprintLibrary` — Event Map entry を再生する static library。`PlayHapbeatEvent` / `StopHapbeatEvent` / `FireHapbeatTickFromValue`。
- `UHapbeatSubsystem` — GameInstance ごとの UDP・Command・Stream 実行基盤。`Connect` / `Play` / `Stop` / `StopAll` / `PlayEntry` / `StopEntry` / `StreamClip` / `StopStream` / `Ping`。
- `UHapbeatStreamPlayback` — 1 回の Stream source を操作する handle。`ApplyGainModulation` / `SetPan` / `SetLoop` / `Stop` / `GetStatus`。
- `UHapbeatTargetLibrary` — Target 文字列と Address Override を扱う static library。`BuildTarget` / `ParseTarget` / `ResolveTarget` / `ApplyAddressPlaceholders` / `AddressMatches`。

### Actor component

- `UHapbeatTriggerComponent` — Event Map entry を発火する基本 component。`Fire` / `FireWithGain` / `FireScaled` / `FireWithCurve` / `Stop` / `SetGainMultiplier` / `SetStreamPan`。
- `UHapbeatCollisionTriggerComponent` — owner の Hit / BeginOverlap から発火する Trigger。`TriggerEvent` / `GainMode` / `VelocityThreshold` / `VelocityCurve` / `bEnterOnly`。
- `UHapbeatSequenceComponent` — start shot、loop、stop shot を 1 component で扱う Trigger。継承した `Fire` / `Stop`、`StartEntryId` / `StopEntryId` / `StopShotDelay`。
- `UHapbeatTickEmitterComponent` — 連続値の閾値通過ごとに tick を発火する Trigger。`FireFromValue` / `FireFromVector2D` / `FireNow` / `ResetReference`、`TickMode` / `TickThreshold`。
- `UHapbeatParameterBinding` — 入力値を Stream Gain / Pan に変換して Playback へ書き込む。`SetValue` / `EvaluateNow` / `GetCurrentInput` / `GetCurrentNormalized` / `GetCurrentOutput`。

### Asset・設定・Animation Notify

- `UHapbeatEventMap` — 触覚 entry の Data Asset。`Entries` / `FindById`。
- `FHapbeatEventEntry` — Event Map 内の 1 entry。mode、event ID、Stream Clip、Gain、Pan、Target、Loop、Delay を保持します。
- `FHapbeatEntryRef` — Event Map 内の entry への安定参照。`EntryId` / `IsSet`。
- `UHapbeatClip` — PCM16 Stream Clip の Data Asset。`NumSamples` / `NumFrames` / `DurationSeconds` / `CreateFromWavBytes`。
- `UHapbeatConfig` — Project Settings の SDK 設定。`Port` / `AppName` / `HapticDelaySeconds` / `bCommandUnicast` / `ForcedOverridePlayer` / `ForcedOverrideGroup`。
- `UHapbeatAnimNotify` — Animation の 1 フレームで entry を再生する Notify。`EventMap` / `EntryId` / `GainMultiplier`。
- `UHapbeatAnimNotifyState` — Animation の区間開始で再生し、終了で停止する Notify State。`EventMap` / `EntryId` / `GainMultiplier`。

`FHapbeatProtocol`、`FHapbeatStreamRunnable`、`FHapbeatStreamGainMirror`、`HapbeatNetInterfaces` は SDK 内部の transport 実装です。ゲームコードから直接使う API ではありません。

## Event Map の entry を再生する

`UHapbeatBlueprintLibrary::PlayHapbeatEvent` は Blueprint の `Play Event (Hapbeat)` と同じ経路です。Command entry は device event を送信し、Stream Clip entry は実行時に制御できる `UHapbeatStreamPlayback` を返します。

```cpp
#include "HapbeatBlueprintLibrary.h"
#include "HapbeatEventMap.h"
#include "HapbeatStreamPlayback.h"

UHapbeatStreamPlayback* Playback = UHapbeatBlueprintLibrary::PlayHapbeatEvent(
    this,
    EventMap,
    Entry,
    /* GainMultiplier */ 1.0f,
    /* Pan */ 0.0f,
    /* DelaySeconds */ 0.0f);
```

-   `EventMap`: `UHapbeatEventMap` asset
-   `Entry`: asset 内の `FHapbeatEntryRef`
-   `GainMultiplier`、`Pan`、`DelaySeconds`: その呼び出しだけの補正。asset の設定は変更しません。
-   戻り値: Stream Clip entry のときだけ `UHapbeatStreamPlayback`。Command entry・entry 解決失敗時は `nullptr`。

loop を終了するときは、同じ map と entry を渡します。

```cpp
UHapbeatBlueprintLibrary::StopHapbeatEvent(this, EventMap, Entry);
```

## Stream Clip を実行時に調整する

`PlayHapbeatEvent` の戻り値が非 null のとき、その entry は Stream Clip です。再生ごとの handle を保持すると、他の stream を止めずに Gain・Pan・loop を操作できます。

```cpp
if (Playback != nullptr)
{
    Playback->ApplyGainModulation(0.7f);
    Playback->SetPan(-0.25f);
    Playback->Stop();
}
```

この経路は Event Map で定義した baseline Gain・Target・Clip を維持します。連続値から Gain / Pan を更新する場合は、`UHapbeatParameterBinding` component を使います。

## Actor に Trigger を持たせる

同じ Actor が繰り返し同じ entry を再生する場合は、`UHapbeatTriggerComponent` を Actor component として追加します。`EventMap` と `EntryId` を Details で設定すると、C++ 側は component の `Fire()` と `Stop()` だけを呼べます。

```cpp
#include "HapbeatTriggerComponent.h"

HitTrigger->Fire();
HitTrigger->SetGainMultiplier(0.6f); // Stream 再生中なら即時に Gain を更新
HitTrigger->Stop();
```

- `FireWithGain` は entry の Gain に呼び出し時の係数を掛けます。
- `FireScaled` は速度を指定範囲で 0〜1 に正規化して Gain に使います。
- `FireWithCurve` は `UCurveFloat` の値を Gain 係数として使います。
- `GetActivePlayback` は、この Trigger が開始した Stream の `UHapbeatStreamPlayback` を返します。Command entry では `nullptr` です。

衝突から発火する場合は `UHapbeatCollisionTriggerComponent`、開始・loop・終了の 3 段階を扱う場合は `UHapbeatSequenceComponent` を使います。コンポーネントの Details と Blueprint node は[Blueprint ノード](./blueprint-nodes.md)を参照してください。

## event ID を直接送る

event ID や Target を実行時に組み立てる必要がある場合だけ、`UHapbeatSubsystem` を使います。この経路は Event Map を通らないため、entry の Clip・Gain・Target・Loop・Delay は使用しません。

```cpp
#include "HapbeatSubsystem.h"

UHapbeatSubsystem* Hapbeat = GetGameInstance()->GetSubsystem<UHapbeatSubsystem>();
Hapbeat->Play(TEXT("pickup"), 0.8f, TEXT("player_1/pos_chest"));
```

`Play`、`Stop`、`StopAll`、`StreamClip`、`StopStream` は `HapbeatSubsystem.h` で宣言されています。Target の形式と Address Override は[ターゲティング](./targeting-and-multi-hmd.md)を参照してください。

### Subsystem の関数

| 関数 | 用途 |
| --- | --- |
| `Connect(Port, AppName)` | UDP の送信ソケットを開く。通常は SDK 初期化時に自動接続されるため、接続設定を変更したときだけ明示的に使う。 |
| `Play(EventId, Gain, Target, Pan)` | デバイスに配置済みの event ID を直接再生する。Event Map は通らない。 |
| `Stop(EventId, Target)` / `StopAll(Target)` | 直接再生した event を停止する。 |
| `StreamClip(Clip, BaselineGain, InitialGain, Target, bLoop, InitialPan)` | PCM の `UHapbeatClip` を送信し、個別制御用の Playback handle を返す。 |
| `StopStream()` | 実行中のすべての Stream source を停止する。 |
| `Ping()` | PONG を要求して到達可能な Hapbeat を検出する。 |
| `IsConnected()` / `IsAlive()` / `GetAliveDeviceCount()` | ソケット状態、応答のある device の有無・台数を確認する。 |

`IsConnected()` は UDP ソケットを開けたかを示すだけです。実際に到達可能な Hapbeat の確認には `Ping()` の後で `IsAlive()` または `GetAliveDeviceCount()` を使います。

## 宛先を実行時に切り替える

`SetAddressOverride(Player, Group, bPersist)` は、以後の送信先の player / group を上書きします。各軸に `UHapbeatSubsystem::AddressOverrideDisabled`（`-1`）を渡すと、その軸は Event Map または API で指定した Target を維持します。

```cpp
Hapbeat->SetAddressOverride(
    /* Player */ 2,
    /* Group */ UHapbeatSubsystem::AddressOverrideDisabled,
    /* bPersist */ true);
```

- `bPersist: true` はこの端末の次回起動にも保存します。
- `ClearPersistedAddressOverride()` は保存済み値を削除し、固定されていない軸を off にします。Config の `ForcedOverridePlayer` / `ForcedOverrideGroup` は保存値・実行時指定より優先され、Clear でも維持されます。
- `GetOverridePlayer()` / `GetOverrideGroup()` は実行中の値を返します。

### Target library の関数

| 関数 | 効果 |
| --- | --- |
| `BuildTarget(Player, Position, Group)` | player / position / group から Target 文字列を作る。すべて未指定なら空文字（全 device 宛）。 |
| `ParseTarget(Target, OutPlayer, OutPosition, OutGroup)` | Target を player / position / group に分解する。未指定の軸は `-1` または空文字。 |
| `ResolveTarget(Target, OverridePlayer, OverrideGroup)` | Target の player / group だけを override 値で置き換える。 |
| `ApplyAddressPlaceholders(AppName, OverridePlayer, OverrideGroup)` | `AppName` の `<p>` / `<g>` を実効 player / group、または `-` に置き換える。 |
| `AddressMatches(Target, DeviceAddress)` | device Address が Target に一致するかを確認する。 |

Target の意味と構成別の使い分けは[ターゲティング](./targeting-and-multi-hmd.md)を参照してください。

## Stream Playback handle

`UHapbeatStreamPlayback` は `PlayHapbeatEvent` または `StreamClip` が返す、**その 1 回の Stream 再生だけ**を操作する handle です。ほかの source を止めずに値を変えられます。

| 関数 | 効果 |
| --- | --- |
| `ApplyGainModulation(Value)` | entry の baseline Gain に `Value` を掛け、0〜2 に制限して即時反映する。 |
| `SetPan(Value)` | Stream の左右バランスを `-1`〜`1` で設定する。 |
| `SetLoop(bLoop)` | loop の有無を切り替える。 |
| `Stop()` | この handle の source だけを停止する。 |
| `IsActive()` / `GetStatus()` / `GetDeferredReason()` | 再生中か、送信先待ちか、停止済みかを確認する。 |

`UHapbeatParameterBinding` を使う場合は、UI やゲーム値を `SetValue()` で渡し、Stream 開始直後には `EvaluateNow()` を 1 回呼びます。入力値は設定した範囲・曲線・出力範囲を通り、Target Trigger の Playback の Gain または Pan に反映されます。

## Project Settings のクラス

`UHapbeatConfig` は `Project Settings → Plugins → Hapbeat` の設定を表す `UDeveloperSettings` です。主な項目は UDP `Port`、デバイス表示用 `AppName`、音との同期に使う `HapticDelaySeconds`、既知 device への unicast を使う `bCommandUnicast`、build 固定の `ForcedOverridePlayer` / `ForcedOverrideGroup` です。通常のゲームコードから値を変更するのではなく、Project Settings または `SetAddressOverride` を使います。

## 再生の契約と失敗時の扱い

- Event Map の識別子は `FGuid`、Command の device event ID は `FString`（`category.eventName`）です。`FHapbeatEntryRef::EntryId` は GUID を保持します。
- `PlayHapbeatEvent` / `PlayEntry` の戻り値は Stream Clip のみです。Command の `nullptr` は正常ですが、entry 未解決・無効 Clip でも `nullptr` になります。戻り値を使う前に有効性を確認してください。
- Stream は送信前に 16 kHz / stereo PCM16 に正規化され、送信先ごとに混合されます。mono の素材も実行中に Pan を変更できます。各 source の戻り値を保持すると、その source だけを調整・停止できます。
- `Deferred` はまだ送信を開始していない状態、`Active` は送信中、`Stopped` は停止済みです。宛先未検出でも handle は保持されます。`IsActive() == false` を停止済みと解釈せず、停止判定には `IsStopped()` を使います。
- `StopEntry` は Stream Clip の場合 **Subsystem 内の全 source** を停止します。個別停止は `UHapbeatStreamPlayback::Stop()`。Command の `StopAll(Target)` は device event の停止で、SDK 側 Stream の終了には `StopStream()` を使います。
- Event Map 経路の遅延は `max(0, HapticDelaySeconds + DelayOffsetSeconds + ExtraDelaySeconds)`。`StopEntry` は global + entry の遅延を使い、呼出単位の ExtraDelay は加えません。直接 `Play` / `StreamClip` は Event Map の遅延経路を通りません。
- UObject / component の操作はゲームスレッドで行います。保持する playback / asset 参照には `UPROPERTY()` を使い、送信ワーカースレッドから UObject を操作しません。
- UDP の発火要求・`OnFired` はデバイスの再生完了通知ではありません。実機への到達確認は PONG と実機試験で行います。

## C++ 専用の補助 API

`UHapbeatSubsystem`（`HapbeatSubsystem.h`）:

```cpp
UHapbeatStreamPlayback* PlayEntry(UHapbeatEventMap* Map, FGuid EntryId,
    float GainMultiplier = 1.0f, bool bForceNonLoop = false,
    float Pan = 0.0f, float ExtraDelaySeconds = 0.0f);
void StopEntry(UHapbeatEventMap* Map, FGuid EntryId);
static bool TryGetPersistedAddressOverride(int32& OutPlayer, int32& OutGroup);
static void SavePersistedAddressOverride(int32 Player, int32 InGroup);
static void RemovePersistedAddressOverride();
static int32 NormalizeAddressOverride(int32 Value);
```

`bForceNonLoop` は Stream Clip を one-shot にします。保存値の TryGet はどちらかの保存キーがあれば true、未設定軸は `-1`。Save / Remove は保存内容だけを変更し、稼働中の Subsystem へは適用しません。即時適用には `SetAddressOverride` / `ClearPersistedAddressOverride` を使います。Normalize は `1..99` 以外を `-1` にします。

`UHapbeatClip`（`HapbeatClip.h`）:

```cpp
static bool ParseWav(const TArray<uint8>& WavBytes, int32& OutSampleRate,
    int32& OutChannels, TArray<uint8>& OutPcm16, FString& OutError);
static UHapbeatClip* CreateFromWavBytes(UObject* Outer, const TArray<uint8>& WavBytes);
```

RIFF/WAVE の PCM16 を読み込みます。ParseWav は失敗時 false と OutError、CreateFromWavBytes は失敗時 nullptr を返します。生成 Clip は transient です。`Pcm16` は little-endian / interleaved の全チャネルデータ、`NumSamples()` は全 sample 数、`NumFrames()` はチャネル数で割った frame 数です。

`FHapbeatEventEntry::GetEventId()` は Category と EventName を結合し、`GetEffectiveGain()` は保存された manifest intensity を Gain に掛けます（intensity 未解決なら Gain を使用）。`UHapbeatStreamPlayback::GetStereoChannelGains(float& OutL, float& OutR)` は Pan から左右係数を返します。中央は左右とも 1、左端は 1/0、右端は 0/1 の linear balance です。

## 列挙型と値の変換

- `EHapticMode`: `Command` / `StreamClip`。
- `EHapbeatStreamPlaybackStatus`: `Deferred` / `Active` / `Stopped`。
- `EHapbeatStreamDeferredReason`: `None` / `NoResolvedEndpoint`。遅延時間や device エラーコードは含みません。
- `EHapbeatCollisionEvent`: `Hit` / `BeginOverlap`。`EHapbeatGainMode`: `Fixed` / `VelocityScaled`。速度単位は cm/s、正規化は `clamp((speed - VelocityThreshold) / (MaxVelocity - VelocityThreshold), 0, 1)`、曲線未設定は線形です。
- `EHapbeatBindingSource`: `LocalPositionX` / `LocalPositionY` / `LocalPositionZ`（cm）、`VelocityMagnitude` / `PositionDeltaMagnitude`（cm/s）、`AngularVelocityMagnitude`（deg/s）、`External`（SetValue 入力）。
- `EHapbeatBindingCurve`: `Linear` は t、`EaseIn` は t²、`EaseOut` は 1−(1−t)²、`Exponential` は (exp(3t)−1)/(exp(3)−1)、`Custom` は CustomCurve の値（未設定は線形）。t は InputMin..InputMax を 0..1 に制限した入力です。
- `EHapbeatBindingOutput`: `StreamGain` / `StreamPan`。曲線結果を OutputMin..OutputMax に変換します。Gain は baseline × 出力を 0..2 に制限、Pan は出力を −1..1 に制限します。
- `EHapbeatTickAxis`: `X` / `Y`。`EHapbeatTickMode`: `AbsolutePosition` は固定目盛り通過、`AccumulatedMotion` は前回の基準からの移動量。TickThreshold の単位は入力値と同じです。0 は値の変化ごと、1 回の入力で最大 64 tick。Cooldown により発火数が抑えられることがあります。

## ヘッダーの参照先

- [HapbeatBlueprintLibrary.h](https://github.com/hapbeat/hapbeat-unreal-sdk/blob/master/Source/HapbeatSDK/Public/HapbeatBlueprintLibrary.h)
- [HapbeatSubsystem.h](https://github.com/hapbeat/hapbeat-unreal-sdk/blob/master/Source/HapbeatSDK/Public/HapbeatSubsystem.h)
- [HapbeatStreamPlayback.h](https://github.com/hapbeat/hapbeat-unreal-sdk/blob/master/Source/HapbeatSDK/Public/HapbeatStreamPlayback.h)
- [HapbeatTriggerComponent.h](https://github.com/hapbeat/hapbeat-unreal-sdk/blob/master/Source/HapbeatSDK/Public/HapbeatTriggerComponent.h)
- [HapbeatParameterBinding.h](https://github.com/hapbeat/hapbeat-unreal-sdk/blob/master/Source/HapbeatSDK/Public/HapbeatParameterBinding.h)
- [HapbeatTargetLibrary.h](https://github.com/hapbeat/hapbeat-unreal-sdk/blob/master/Source/HapbeatSDK/Public/HapbeatTargetLibrary.h)
- [HapbeatConfig.h](https://github.com/hapbeat/hapbeat-unreal-sdk/blob/master/Source/HapbeatSDK/Public/HapbeatConfig.h)

<!-- api-index:start -->
## 公開プロパティ・関数の宣言索引

ゲーム実装から利用する反映対象の宣言（`UFUNCTION` と編集・Blueprint 公開 `UPROPERTY`）をヘッダーから列挙しています。継承したメンバーは基底クラスを参照してください。ネットワーク送信スレッド・パケット組立などの内部 API と Unreal のライフサイクル override は対象外です。通常の C++ 専用 API は前節で説明します。

### UHapbeatAnimNotify

ヘッダー: `Source/HapbeatSDK/Public/HapbeatAnimNotify.h`

- `EventMap`（プロパティ）

  ```cpp
  TObjectPtr<UHapbeatEventMap> EventMap;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `EntryId`（プロパティ）

  ```cpp
  FGuid EntryId;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `GainMultiplier`（プロパティ）

  ```cpp
  float GainMultiplier = 1.0f;
  ```

  Editor 範囲: UIMin=0.0, UIMax=2.0, ClampMin=0.0, ClampMax=2.0。

  Blueprint 読み書き可。Editor で設定可能。

### UHapbeatAnimNotifyState

ヘッダー: `Source/HapbeatSDK/Public/HapbeatAnimNotify.h`

- `EventMap`（プロパティ）

  ```cpp
  TObjectPtr<UHapbeatEventMap> EventMap;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `EntryId`（プロパティ）

  ```cpp
  FGuid EntryId;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `GainMultiplier`（プロパティ）

  ```cpp
  float GainMultiplier = 1.0f;
  ```

  Editor 範囲: UIMin=0.0, UIMax=2.0, ClampMin=0.0, ClampMax=2.0。

  Blueprint 読み書き可。Editor で設定可能。

### UHapbeatBlueprintLibrary

ヘッダー: `Source/HapbeatSDK/Public/HapbeatBlueprintLibrary.h`

- `PlayHapbeatEvent`（関数）

  ```cpp
  static UHapbeatStreamPlayback* PlayHapbeatEvent(const UObject* WorldContextObject, UHapbeatEventMap* Map, FHapbeatEntryRef Entry, float GainMultiplier = 1.0f, float Pan = 0.0f, float DelaySeconds = 0.0f);
  ```

- `StopHapbeatEvent`（関数）

  ```cpp
  static void StopHapbeatEvent(const UObject* WorldContextObject, UHapbeatEventMap* Map, FHapbeatEntryRef Entry);
  ```

- `FireHapbeatTickFromValue`（関数）

  ```cpp
  static void FireHapbeatTickFromValue(UHapbeatTickEmitterComponent* TickEmitter, float Value);
  ```

### UHapbeatClip

ヘッダー: `Source/HapbeatSDK/Public/HapbeatClip.h`

- `SampleRate`（プロパティ）

  ```cpp
  int32 SampleRate = 0;
  ```

  Blueprint 読取専用。

- `NumChannels`（プロパティ）

  ```cpp
  int32 NumChannels = 0;
  ```

  Blueprint 読取専用。

- `NumSamples`（関数）

  ```cpp
  int32 NumSamples() const;
  ```

- `NumFrames`（関数）

  ```cpp
  int32 NumFrames() const;
  ```

- `DurationSeconds`（関数）

  ```cpp
  float DurationSeconds() const;
  ```

### UHapbeatCollisionTriggerComponent

ヘッダー: `Source/HapbeatSDK/Public/HapbeatCollisionTriggerComponent.h`

- `TriggerEvent`（プロパティ）

  ```cpp
  EHapbeatCollisionEvent TriggerEvent = EHapbeatCollisionEvent::Hit;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `GainMode`（プロパティ）

  ```cpp
  EHapbeatGainMode GainMode = EHapbeatGainMode::Fixed;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `TagFilter`（プロパティ）

  ```cpp
  FName TagFilter = NAME_None;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `VelocityThreshold`（プロパティ）

  ```cpp
  float VelocityThreshold = 0.0f;
  ```

  Editor 範囲: ClampMin=0.0。

  Blueprint 読み書き可。Editor で設定可能。

- `MaxVelocity`（プロパティ）

  ```cpp
  float MaxVelocity = 10.0f;
  ```

  Editor 範囲: ClampMin=0.0。

  Blueprint 読み書き可。Editor で設定可能。

- `VelocityCurve`（プロパティ）

  ```cpp
  FRuntimeFloatCurve VelocityCurve;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `bEnterOnly`（プロパティ）

  ```cpp
  bool bEnterOnly = true;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `ContactSeparationSeconds`（プロパティ）

  ```cpp
  float ContactSeparationSeconds = 0.2f;
  ```

  Editor 範囲: ClampMin=0.0。

  Blueprint 読み書き可。Editor で設定可能。

### UHapbeatConfig

ヘッダー: `Source/HapbeatSDK/Public/HapbeatConfig.h`

- `Port`（プロパティ）

  ```cpp
  int32 Port = 7700;
  ```

  Editor 範囲: ClampMin=1, ClampMax=65535。

  Editor で設定可能。

- `AppName`（プロパティ）

  ```cpp
  FString AppName;
  ```

  Editor で設定可能。

- `PingInterval`（プロパティ）

  ```cpp
  float PingInterval = 5.0f;
  ```

  Editor 範囲: ClampMin=1.0, ClampMax=60.0。

  Editor で設定可能。

- `StreamSendAheadSeconds`（プロパティ）

  ```cpp
  float StreamSendAheadSeconds = 0.05f;
  ```

  Editor 範囲: ClampMin=0.01, ClampMax=0.2。

  Editor で設定可能。

- `bCommandUnicast`（プロパティ）

  ```cpp
  bool bCommandUnicast = true;
  ```

  Editor で設定可能。

- `HapticDelaySeconds`（プロパティ）

  ```cpp
  float HapticDelaySeconds = 0.0f;
  ```

  Editor 範囲: ClampMin=0.0, ClampMax=0.5。

  Editor で設定可能。

- `ForcedOverridePlayer`（プロパティ）

  ```cpp
  int32 ForcedOverridePlayer = -1;
  ```

  Editor 範囲: ClampMin=-1, ClampMax=99。

  Editor で設定可能。

- `ForcedOverrideGroup`（プロパティ）

  ```cpp
  int32 ForcedOverrideGroup = -1;
  ```

  Editor 範囲: ClampMin=-1, ClampMax=99。

  Editor で設定可能。

- `bEnableLogging`（プロパティ）

  ```cpp
  bool bEnableLogging = true;
  ```

  Editor で設定可能。

- `bVerboseLogging`（プロパティ）

  ```cpp
  bool bVerboseLogging = false;
  ```

  Editor で設定可能。

### FHapbeatEntryRef

ヘッダー: `Source/HapbeatSDK/Public/HapbeatEntryRef.h`

- `EntryId`（プロパティ）

  ```cpp
  FGuid EntryId;
  ```

  Blueprint 読み書き可。Editor で設定可能。

### FHapbeatEventEntry

ヘッダー: `Source/HapbeatSDK/Public/HapbeatEventEntry.h`

- `Id`（プロパティ）

  ```cpp
  FGuid Id;
  ```

  Blueprint 読取専用。

- `Mode`（プロパティ）

  ```cpp
  EHapticMode Mode = EHapticMode::Command;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `DisplayName`（プロパティ）

  ```cpp
  FString DisplayName;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `Category`（プロパティ）

  ```cpp
  FString Category;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `EventName`（プロパティ）

  ```cpp
  FString EventName;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `Gain`（プロパティ）

  ```cpp
  float Gain = 1.0f;
  ```

  Editor 範囲: UIMin=0.0, UIMax=2.0, ClampMin=0.0, ClampMax=2.0。

  Blueprint 読み書き可。Editor で設定可能。

- `Pan`（プロパティ）

  ```cpp
  float Pan = 0.0f;
  ```

  Editor 範囲: UIMin=-1.0, UIMax=1.0, ClampMin=-1.0, ClampMax=1.0。

  Blueprint 読み書き可。Editor で設定可能。

- `Target`（プロパティ）

  ```cpp
  FString Target;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `bLoop`（プロパティ）

  ```cpp
  bool bLoop = false;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `DelayOffsetSeconds`（プロパティ）

  ```cpp
  float DelayOffsetSeconds = 0.0f;
  ```

  Editor 範囲: UIMin=-0.2, UIMax=0.2, ClampMin=-0.2, ClampMax=0.2。

  Blueprint 読み書き可。Editor で設定可能。

- `Notes`（プロパティ）

  ```cpp
  FString Notes;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `StreamClip`（プロパティ）

  ```cpp
  TSoftObjectPtr<UHapbeatClip> StreamClip;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `CachedManifestIntensity`（プロパティ）

  ```cpp
  float CachedManifestIntensity = -1.0f;
  ```

  Blueprint 読取専用。

### UHapbeatEventMap

ヘッダー: `Source/HapbeatSDK/Public/HapbeatEventMap.h`

- `Entries`（プロパティ）

  ```cpp
  TArray<FHapbeatEventEntry> Entries;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `FindById`（関数）

  ```cpp
  bool FindById(FGuid Id, FHapbeatEventEntry& OutEntry) const;
  ```

### UHapbeatParameterBinding

ヘッダー: `Source/HapbeatSDK/Public/HapbeatParameterBinding.h`

- `SourceProperty`（プロパティ）

  ```cpp
  EHapbeatBindingSource SourceProperty = EHapbeatBindingSource::External;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `InputMin`（プロパティ）

  ```cpp
  float InputMin = 0.0f;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `InputMax`（プロパティ）

  ```cpp
  float InputMax = 1.0f;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `CurveType`（プロパティ）

  ```cpp
  EHapbeatBindingCurve CurveType = EHapbeatBindingCurve::Linear;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `CustomCurve`（プロパティ）

  ```cpp
  TObjectPtr<UCurveFloat> CustomCurve = nullptr;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `OutputParameter`（プロパティ）

  ```cpp
  EHapbeatBindingOutput OutputParameter = EHapbeatBindingOutput::StreamGain;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `OutputMin`（プロパティ）

  ```cpp
  float OutputMin = 0.0f;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `OutputMax`（プロパティ）

  ```cpp
  float OutputMax = 1.0f;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `TargetTrigger`（プロパティ）

  ```cpp
  TObjectPtr<UHapbeatTriggerComponent> TargetTrigger = nullptr;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `SetValue`（関数）

  ```cpp
  void SetValue(float Value);
  ```

- `EvaluateNow`（関数）

  ```cpp
  float EvaluateNow();
  ```

- `GetCurrentInput`（関数）

  ```cpp
  float GetCurrentInput() const;
  ```

- `GetCurrentNormalized`（関数）

  ```cpp
  float GetCurrentNormalized() const;
  ```

- `GetCurrentOutput`（関数）

  ```cpp
  float GetCurrentOutput() const;
  ```

### UHapbeatSequenceComponent

ヘッダー: `Source/HapbeatSDK/Public/HapbeatSequenceComponent.h`

- `StartEntryId`（プロパティ）

  ```cpp
  FGuid StartEntryId;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `StopEntryId`（プロパティ）

  ```cpp
  FGuid StopEntryId;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `StopShotDelay`（プロパティ）

  ```cpp
  float StopShotDelay = 0.05f;
  ```

  Editor 範囲: UIMin=0.0, UIMax=0.5, ClampMin=0.0。

  Blueprint 読み書き可。Editor で設定可能。

### UHapbeatStreamPlayback

ヘッダー: `Source/HapbeatSDK/Public/HapbeatStreamPlayback.h`

- `Id`（プロパティ）

  ```cpp
  FGuid Id;
  ```

  Blueprint 読取専用。

- `Status`（プロパティ）

  ```cpp
  EHapbeatStreamPlaybackStatus Status = EHapbeatStreamPlaybackStatus::Deferred;
  ```

  Blueprint 読取専用。

- `DeferredReason`（プロパティ）

  ```cpp
  EHapbeatStreamDeferredReason DeferredReason = EHapbeatStreamDeferredReason::NoResolvedEndpoint;
  ```

  Blueprint 読取専用。

- `BaselineGain`（プロパティ）

  ```cpp
  float BaselineGain = 1.0f;
  ```

  Blueprint 読取専用。

- `ApplyGainModulation`（関数）

  ```cpp
  void ApplyGainModulation(float Modulator);
  ```

- `SetPan`（関数）

  ```cpp
  void SetPan(float NewPan);
  ```

- `SetLoop`（関数）

  ```cpp
  void SetLoop(bool bNewLoop);
  ```

- `GetLoop`（関数）

  ```cpp
  bool GetLoop() const;
  ```

- `Stop`（関数）

  ```cpp
  void Stop();
  ```

- `GetGain`（関数）

  ```cpp
  float GetGain() const;
  ```

- `GetPan`（関数）

  ```cpp
  float GetPan() const;
  ```

- `IsStopped`（関数）

  ```cpp
  bool IsStopped() const;
  ```

- `IsActive`（関数）

  ```cpp
  bool IsActive() const;
  ```

- `GetStatus`（関数）

  ```cpp
  EHapbeatStreamPlaybackStatus GetStatus() const;
  ```

- `GetDeferredReason`（関数）

  ```cpp
  EHapbeatStreamDeferredReason GetDeferredReason() const;
  ```

### UHapbeatSubsystem

ヘッダー: `Source/HapbeatSDK/Public/HapbeatSubsystem.h`

- `Connect`（関数）

  ```cpp
  void Connect(int32 InPort = 7700, const FString& InAppName = TEXT(""));
  ```

- `Play`（関数）

  ```cpp
  void Play(const FString& EventId, float Gain = 1.0f, const FString& Target = TEXT(""), float Pan = 0.0f);
  ```

- `Stop`（関数）

  ```cpp
  void Stop(const FString& EventId, const FString& Target = TEXT(""));
  ```

- `StopAll`（関数）

  ```cpp
  void StopAll(const FString& Target = TEXT(""));
  ```

- `Ping`（関数）

  ```cpp
  void Ping();
  ```

- `StreamClip`（関数）

  ```cpp
  UHapbeatStreamPlayback* StreamClip(UHapbeatClip* Clip, float BaselineGain = 1.0f, float InitialGain = 1.0f, const FString& Target = TEXT(""), bool bLoop = false, float InitialPan = 0.0f);
  ```

- `StopStream`（関数）

  ```cpp
  void StopStream();
  ```

- `SetAddressOverride`（関数）

  ```cpp
  void SetAddressOverride(int32 Player, int32 InGroup, bool bPersist = false);
  ```

- `ClearPersistedAddressOverride`（関数）

  ```cpp
  void ClearPersistedAddressOverride();
  ```

- `GetOverridePlayer`（関数）

  ```cpp
  int32 GetOverridePlayer() const;
  ```

- `GetOverrideGroup`（関数）

  ```cpp
  int32 GetOverrideGroup() const;
  ```

- `IsConnected`（関数）

  ```cpp
  bool IsConnected() const;
  ```

- `GetAliveDeviceCount`（関数）

  ```cpp
  int32 GetAliveDeviceCount() const;
  ```

- `IsAlive`（関数）

  ```cpp
  bool IsAlive() const;
  ```

- `IsStreaming`（関数）

  ```cpp
  bool IsStreaming() const;
  ```

- `GetActivePlayback`（関数）

  ```cpp
  UHapbeatStreamPlayback* GetActivePlayback() const;
  ```

- `OnConnected`（イベント）

  ```cpp
  FHapbeatOnConnected OnConnected;
  ```

  

- `OnDisconnected`（イベント）

  ```cpp
  FHapbeatOnDisconnected OnDisconnected;
  ```

  

- `OnError`（イベント）

  ```cpp
  FHapbeatOnError OnError;
  ```

  

- `OnPong`（イベント）

  ```cpp
  FHapbeatOnPong OnPong;
  ```

  

### UHapbeatTargetLibrary

ヘッダー: `Source/HapbeatSDK/Public/HapbeatTargetLibrary.h`

- `BuildTarget`（関数）

  ```cpp
  static FString BuildTarget(int32 Player = -1, const FString& Position = TEXT(""), int32 Group = -1);
  ```

- `ParseTarget`（関数）

  ```cpp
  static void ParseTarget(const FString& Target, int32& OutPlayer, FString& OutPosition, int32& OutGroup);
  ```

- `ResolveTarget`（関数）

  ```cpp
  static FString ResolveTarget(const FString& Target, int32 OverridePlayer, int32 OverrideGroup);
  ```

- `ApplyAddressPlaceholders`（関数）

  ```cpp
  static FString ApplyAddressPlaceholders(const FString& AppName, int32 OverridePlayer, int32 OverrideGroup);
  ```

- `AddressMatches`（関数）

  ```cpp
  static bool AddressMatches(const FString& Target, const FString& DeviceAddress);
  ```

### UHapbeatTickEmitterComponent

ヘッダー: `Source/HapbeatSDK/Public/HapbeatTickEmitterComponent.h`

- `TickMode`（プロパティ）

  ```cpp
  EHapbeatTickMode TickMode = EHapbeatTickMode::AbsolutePosition;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `TickThreshold`（プロパティ）

  ```cpp
  float TickThreshold = 0.1f;
  ```

  Editor 範囲: ClampMin=0.0。

  Blueprint 読み書き可。Editor で設定可能。

- `Axis`（プロパティ）

  ```cpp
  EHapbeatTickAxis Axis = EHapbeatTickAxis::Y;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `bEmitOnInitialValue`（プロパティ）

  ```cpp
  bool bEmitOnInitialValue = false;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `FireFromValue`（関数）

  ```cpp
  void FireFromValue(float Value);
  ```

- `FireFromVector2D`（関数）

  ```cpp
  void FireFromVector2D(FVector2D Value);
  ```

- `FireNow`（関数）

  ```cpp
  void FireNow();
  ```

- `ResetReference`（関数）

  ```cpp
  void ResetReference();
  ```

### UHapbeatTriggerComponent

ヘッダー: `Source/HapbeatSDK/Public/HapbeatTriggerComponent.h`

- `EventMap`（プロパティ）

  ```cpp
  TObjectPtr<UHapbeatEventMap> EventMap;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `EntryId`（プロパティ）

  ```cpp
  FGuid EntryId;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `bTriggerEnabled`（プロパティ）

  ```cpp
  bool bTriggerEnabled = true;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `Cooldown`（プロパティ）

  ```cpp
  float Cooldown = 0.0f;
  ```

  Editor 範囲: ClampMin=0.0。

  Blueprint 読み書き可。Editor で設定可能。

- `GainMultiplier`（プロパティ）

  ```cpp
  float GainMultiplier = 1.0f;
  ```

  Editor 範囲: UIMin=0.0, UIMax=2.0, ClampMin=0.0, ClampMax=2.0。

  Blueprint 読み書き可。Editor で設定可能。

- `bVerboseLog`（プロパティ）

  ```cpp
  bool bVerboseLog = false;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `OnFired`（イベント）

  ```cpp
  FHapbeatTriggerFired OnFired;
  ```

  

- `Fire`（関数）

  ```cpp
  virtual void Fire();
  ```

- `FireWithGain`（関数）

  ```cpp
  void FireWithGain(float GainOverride);
  ```

- `FireScaled`（関数）

  ```cpp
  void FireScaled(float Velocity, float MinVelocity = 0.0f, float MaxVelocity = 10.0f);
  ```

- `FireWithCurve`（関数）

  ```cpp
  void FireWithCurve(float Value, UCurveFloat* Curve);
  ```

- `Stop`（関数）

  ```cpp
  virtual void Stop();
  ```

- `SetGainMultiplier`（関数）

  ```cpp
  void SetGainMultiplier(float NewMultiplier);
  ```

- `SetStreamPan`（関数）

  ```cpp
  void SetStreamPan(float NewPan);
  ```

- `GetActivePlayback`（関数）

  ```cpp
  UHapbeatStreamPlayback* GetActivePlayback() const;
  ```

### UHapbeatAddressOverridePanelComponent

ヘッダー: `Source/HapbeatSDKSamples/Public/HapbeatAddressOverridePanelComponent.h`

- `bShowOnBeginPlay`（プロパティ）

  ```cpp
  bool bShowOnBeginPlay = false;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `bPersistOnApply`（プロパティ）

  ```cpp
  bool bPersistOnApply = true;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `bShowCloseButton`（プロパティ）

  ```cpp
  bool bShowCloseButton = true;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `bUseVRConfigLayout`（プロパティ）

  ```cpp
  bool bUseVRConfigLayout = false;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `TestEventId`（プロパティ）

  ```cpp
  FString TestEventId;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `ViewportHAlign`（プロパティ）

  ```cpp
  TEnumAsByte<EHorizontalAlignment> ViewportHAlign = HAlign_Center;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `ViewportVAlign`（プロパティ）

  ```cpp
  TEnumAsByte<EVerticalAlignment> ViewportVAlign = VAlign_Center;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `ViewportPadding`（プロパティ）

  ```cpp
  FMargin ViewportPadding = FMargin(0.0f);
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `ViewportSize`（プロパティ）

  ```cpp
  FVector2D ViewportSize = FVector2D::ZeroVector;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `Show`（関数）

  ```cpp
  void Show();
  ```

- `AttachToWidgetComponent`（関数）

  ```cpp
  void AttachToWidgetComponent(UWidgetComponent* Target);
  ```

- `Hide`（関数）

  ```cpp
  void Hide();
  ```

- `Toggle`（関数）

  ```cpp
  void Toggle();
  ```

- `IsShown`（関数）

  ```cpp
  bool IsShown() const;
  ```

- `MoveFocus`（関数）

  ```cpp
  void MoveFocus(int32 Horizontal, int32 Vertical);
  ```

- `ActivateFocused`（関数）

  ```cpp
  void ActivateFocused();
  ```

- `ShowFocusHighlight`（関数）

  ```cpp
  void ShowFocusHighlight();
  ```

### UHapbeatEventLoggerComponent

ヘッダー: `Source/HapbeatSDKSamples/Public/HapbeatEventLoggerComponent.h`

- `Label`（プロパティ）

  ```cpp
  FString Label;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `bIncludeTimestamp`（プロパティ）

  ```cpp
  bool bIncludeTimestamp = true;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `bAlsoDrawOnScreen`（プロパティ）

  ```cpp
  bool bAlsoDrawOnScreen = false;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `LogEvent`（関数）

  ```cpp
  void LogEvent(const FString& Tag);
  ```

- `LogBeginOverlap`（関数）

  ```cpp
  void LogBeginOverlap();
  ```

- `LogEndOverlap`（関数）

  ```cpp
  void LogEndOverlap();
  ```

- `LogHit`（関数）

  ```cpp
  void LogHit();
  ```

- `LogClicked`（関数）

  ```cpp
  void LogClicked();
  ```

- `LogReleased`（関数）

  ```cpp
  void LogReleased();
  ```

- `LogBeginCursorOver`（関数）

  ```cpp
  void LogBeginCursorOver();
  ```

- `LogEndCursorOver`（関数）

  ```cpp
  void LogEndCursorOver();
  ```

- `LogGrabbed`（関数）

  ```cpp
  void LogGrabbed();
  ```

- `LogDropped`（関数）

  ```cpp
  void LogDropped();
  ```

- `LogActivated`（関数）

  ```cpp
  void LogActivated();
  ```

- `LogDeactivated`（関数）

  ```cpp
  void LogDeactivated();
  ```

### UHapbeatStatusOverlayComponent

ヘッダー: `Source/HapbeatSDKSamples/Public/HapbeatStatusOverlayComponent.h`

- `MaxLogLines`（プロパティ）

  ```cpp
  int32 MaxLogLines = 8;
  ```

  Editor 範囲: ClampMin=1。

  Blueprint 読み書き可。Editor で設定可能。

- `bShowOverlay`（プロパティ）

  ```cpp
  bool bShowOverlay = true;
  ```

  Blueprint 読み書き可。Editor で設定可能。

- `Log`（関数）

  ```cpp
  void Log(const FString& Message);
  ```

- `ClearLog`（関数）

  ```cpp
  void ClearLog();
  ```
<!-- api-index:end -->
