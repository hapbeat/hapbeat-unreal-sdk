---
sidebar:
  order: 3
  label: Showcase 配線
---

# Showcase の触覚配線

Showcase は、ゲーム内の出来事を Hapbeat Event Map の entry へ接続する 5 つのサンプルです。各 zone Actor の Details と `EM_Showcase` を開くと、再生する触覚とその設定を確認・変更できます。

## 最初に見る場所

1. Content Browser の Settings で **Show Plugin Content** を有効にします。
2. `Plugins/HapbeatSDK/Content/HapbeatSamples/Showcase/Maps/Showcase` を開きます。
3. World Outliner で対象の zone Actor を選びます。
4. Content Browser の `HapbeatSamples/Showcase/EM_Showcase` を開き、entry の Clip、Gain、Target、loop を編集します。

```text
ゲーム入力 / 衝突 / 状態遷移 / UI
  → Hapbeat component または zone Actor
  → Event Map entry
  → Hapbeat SDK
```

## Z1 Bowling — pin の衝突

```text
ball が pin に Hit
  → pin の HitTrigger
  → Pin Hit Event
```

`Z1_Bowling` を選び、Details の **Hapbeat > Bowling > Pin Hit** を開きます。

- **Event Map Override**: pin が参照する Event Map。既定は `EM_Showcase`。
- **Pin Hit Event**: 6 本すべての pin が発火する entry。既定は `z1_pin_hit`。`Event Map Override` の entry 名から選択できます。
- **Launch Speed**: ball の発射速度。

`Pin Hit Event` は Actor に保存される編集可能な参照です。`EM_Showcase` 側で `z1_pin_hit` の Clip、Gain、Target を変えることも、別の entry を `Pin Hit Event` に選び直すこともできます。

### C++ 実装を確認する

Content Browser の **C++ Classes > HapbeatSDKSamples > Public** には、クラス宣言の [`HapbeatShowcaseZ1BowlingActor.h`](https://github.com/Hapbeat/hapbeat-unreal-sdk/blob/master/Source/HapbeatSDKSamples/Public/HapbeatShowcaseZ1BowlingActor.h) が表示されます。

`.cpp` ファイルは Unreal のコンテンツではないため、Content Browser には表示されません。IDE またはエクスプローラーで [`Source/HapbeatSDKSamples/Private/HapbeatShowcaseZ1BowlingActor.cpp`](https://github.com/Hapbeat/hapbeat-unreal-sdk/blob/master/Source/HapbeatSDKSamples/Private/HapbeatShowcaseZ1BowlingActor.cpp) を開きます。

- `AHapbeatShowcaseZ1PinActor::AHapbeatShowcaseZ1PinActor()` — pin の `HitTrigger` を作成
- `AHapbeatShowcaseZ1BowlingActor::BuildEventMap()` — `Pin Hit Event` を解決
- `AHapbeatShowcaseZ1BowlingActor::SetUpPins()` — 解決した Event Map と entry を各 pin へ設定
- `UHapbeatCollisionTriggerComponent::BeginPlay()` / `HandleCollision()` — SDK の衝突検出と発火

## Z2 Swing Door — Blueprint からイベントを発火する

`BP_Z2_Door` は、ドアの開閉と **Play Hapbeat Event** を同じ Event Graph で接続する Blueprint 完結の例です。

### SDK の接続を最短で確認する

1. World Outliner で `Z2_Door` を選び、Details の **Edit Blueprint** をクリックします。Content Browser から開く場合は `Plugins/HapbeatSDK/Content/HapbeatSamples/Showcase/BP_Z2_Door` をダブルクリックします。
2. 開いた Blueprint Editor の左上 **Components** パネルが component tree です。`DoorHinge`、`DoorLeafMesh`、`DoorHandleMesh` を確認します。
3. 左の **My Blueprint > Graphs > EventGraph** を開きます。
4. `F` または `G` の Input Key node から、次の一本の実行線をたどります。

```text
F Pressed
  → Play Hapbeat Event (z2_door_open)
  → DoorMotion: Play from Start

G Pressed
  → Play Hapbeat Event (z2_door_close)
  → DoorMotion: Reverse

DoorMotion: Update
  → Lerp (Rotator)
  → Set Relative Rotation (Target: DoorHinge)
```

`DoorMotion` の float track `OpenAlpha` が 0.65 秒で 0 から 1 へ変化し、`DoorHinge` を Yaw 0° から 90° へ回転させます。開閉と触覚の開始点は、F/G の Input Key node で共通です。

この Blueprint には `OpenTrigger` / `CloseTrigger` component はありません。ドアの可動部分は `DoorHinge` です。

| Event | entry |
| --- | --- |
| Open Door | `z2_door_open` |
| Close Door | `z2_door_close` |
| Lock Door | `z2_door_lock` |

各 **Play Hapbeat Event** node の `Map` と `Entry` pin で再生先を確認します。entry の Clip、Gain、Target は `EM_Showcase` で編集します。

## Z3 Fishing — sequence と gain binding

```text
左クリック press / release
  → Shark.HookSequence
  → z3_hook_start / z3_hook_loop / z3_hook_release

Shark の速度
  → HookVelocityBinding
  → loop の Gain
```

`Z3_Fishing` の **Hook Wiring** では、start / loop / release の entry を確認できます。竿、`RodTipMarker`、釣り糸、Shark slot の位置は Details で編集します。

## Z4 Stream Console — loop と runtime parameter

`BP_Z4_StreamConsole` は、Blueprint から stream entry を開始・停止する例です。

```text
F → z4_stream_loop を開始
G → z4_stream_loop を停止
T → z4_slider_tick を発火
```

Component tree の `GainBinding` と `PanBinding` は、再生中 stream の Gain と Pan を更新する設定です。`LoopTrigger` と `TickTrigger` の Details から Event Map entry を確認できます。

## Z5 Target Range — charge / shot と target hit

```text
charge begin / threshold / release
  → z5_charge_* / z5_shot_*

projectile が target に Hit
  → LightHitTrigger または HeavyHitTrigger
  → z5_tar_hit_light / z5_tar_hit_heavy
```

`Z5_ChargeShot` の **Haptic Wiring** で、charge、shot、target hit の entry を確認します。charge 時間、launch speed、target slot は同 Actor の Details で変更できます。触覚の Clip、Gain、Target、loop は Event Map で変更します。

## 変更場所

| 変更したい内容 | 変更場所 |
| --- | --- |
| Clip、Gain、Target、loop | `EM_Showcase` の entry |
| Z1 の pin-hit entry | `Z1_Bowling` > **Pin Hit Event** |
| ball の発射速度、charge 時間 | 各 zone Actor の Details |
| ドア、竿、target の transform | 各 zone Actor の Details |
| 実行時の送信先 | Address Override |

`Target` は送信先を表す論理フィルタです。Address Override は Event Map や Actor の配線を変更しません。

## 関連資料

- [Blueprint ノード一覧](./blueprint-nodes.md)
- [Advanced usage](./advanced.md)
