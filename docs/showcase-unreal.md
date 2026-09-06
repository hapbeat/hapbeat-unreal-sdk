---
sidebar:
  order: 3
  label: Showcase 配線
---

# Showcase の触覚配線

Showcase は、ゲーム内の出来事を Hapbeat Event Map の entry へ接続する 5 つのサンプルです。各 zone Actor の Details と `EM_Showcase` を開くと、再生する触覚とその設定を確認・変更できます。

| Zone | 実装方法 | 確認できる触覚配線 |
| --- | --- | --- |
| Z1 Bowling | C++ Actor + collision trigger | pin の衝突から `z1_pin_hit` を発火する配線 |
| Z2 Swing Door | Blueprint Event Graph | ドアの Timeline と `Play Hapbeat Event` を同じ操作分岐から始める配線 |
| Z3 Fishing | C++ Actor + Sequence / Parameter Binding | hook の開始・loop・解除と、魚の速度を loop Gain へ送る配線 |
| Z4 Stream Console | Blueprint Event Graph + Widget Blueprint | stream の開始・停止、UI の Gain / Pan、tick、Address Override の配線 |
| Z5 Target Range | C++ Actor + collision trigger | charge・shot の直接 SDK 呼び出しと、target hit の light / heavy 分岐 |

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

`Z1_Bowling` を選び、Details の **Hapbeat > Bowling > Pin Hit** を開きます。ここだけで pin-hit の触覚を編集できます。

- **Event Map**: pin が参照する Event Map。既定は `EM_Showcase`。
- **Pin Hit Event**: 6 本すべての pin が発火する entry。`Event Map` を選ぶと、その entry 名から選択できます。

`Pin Hit Event` は Actor に保存される編集可能な参照です。`EM_Showcase` 側で `z1_pin_hit` の Clip、Gain、Target を変えることも、別の entry を `Pin Hit Event` に選び直すこともできます。

### C++ 実装を確認する

**Tools → Open Visual Studio** を選び、次の SDK ファイルを開きます。

```text
Plugins
└ HapbeatSDK
  └ Source
    └ HapbeatSDKSamples
      ├ Public
      │ └ HapbeatShowcaseZ1BowlingActor.h
      └ Private
        └ HapbeatShowcaseZ1BowlingActor.cpp
```

最初に `Public/HapbeatShowcaseZ1BowlingActor.h` を開き、次に同名の `Private/HapbeatShowcaseZ1BowlingActor.cpp` を開きます。

- **Event Map**
  - **Header:** `EventMapOverride`
  - **Implementation:** `AHapbeatShowcaseZ1BowlingActor::BuildEventMap`
  - 使用する Event Map を `EventMap` に解決します。
- **Pin Hit Event**
  - **Header:** `PinHitEvent`
  - **Implementation:** `AHapbeatShowcaseZ1BowlingActor::BuildEventMap`
  - 選んだ entry の ID を `PinHitEntryId` に解決します。
- **pin の配置**
  - **Header:** `PinSlots`
  - **Implementation:** `AHapbeatShowcaseZ1BowlingActor::AHapbeatShowcaseZ1BowlingActor`
  - 6 個の `UChildActorComponent` を作成します。
- **pin への触覚配線**
  - **Header:** `AHapbeatShowcaseZ1PinActor::HitTrigger`
  - **Implementation:** `AHapbeatShowcaseZ1BowlingActor::SetUpPins`
  - 各 pin の `HitTrigger->EventMap` と `HitTrigger->EntryId` に、解決済みの map と entry を代入します。

`SetUpPins` 内の次の 2 行が、Details の選択を実際の pin 衝突 trigger へ接続する箇所です。

```cpp
Pin->HitTrigger->EventMap = EventMap;
Pin->HitTrigger->EntryId = PinHitEntryId;
```

衝突条件そのものは、同じ `.cpp` の `AHapbeatShowcaseZ1PinActor::AHapbeatShowcaseZ1PinActor` で `HitTrigger` に設定されています。

Showcase の入力・物理などの挙動は [Showcase の動作](./showcase-behavior.md) を参照してください。

## Z2 Swing Door — Blueprint からイベントを発火する

`BP_Z2_Door` は、ドアの開閉と **Play Hapbeat Event** を同じ Event Graph で接続する Blueprint 完結の例です。Graph 内では各動作を `DoorOpen | z2_door_open -> Play Hapbeat Event` のように色付きの枠で分けています。

### SDK の接続を最短で確認する

1. World Outliner で `Z2_Door` を選び、Details の **Edit Blueprint** をクリックします。Content Browser から開く場合は `Plugins/HapbeatSDK/Content/HapbeatSamples/Showcase/BP_Z2_Door` をダブルクリックします。
2. 開いた Blueprint Editor の左上 **Components** パネルが component tree です。`DoorHinge`、`DoorLeafMesh`、`DoorHandleMesh` を確認します。
3. 左の **My Blueprint > Graphs > EventGraph** を開きます。
4. `F`、`G`、`L` の Input Key node から **Switch on Door State** と **Play Hapbeat Event** をたどります。

```text
F / G / L Pressed
  → Switch on Door State
  → DoorOpen / DoorClose / DoorSlam / DoorRattle / Lock / Unlock
  → 対応する z2_door_* entry の Play Hapbeat Event
  → DoorHinge の Timeline
```

F は閉じたドアを開き、開いたドアを通常速度で閉じます。ロック中の F は `DoorRattle` を再生します。G は開いたドアだけを 0.117 秒で閉じ、ロック中は同じくラトルを再生します。L は閉じた状態だけで lock / unlock を切り替えます。動作中の入力は受け付けません。

`DoorOpen`、`DoorClose`、`DoorSlam` の float track `OpenAlpha` が `DoorHinge` を閉じた Yaw -90° と開いた Yaw 0° の間で回転させます。各 Timeline と触覚イベントは同じ分岐から開始するため、視覚と触覚の開始点を Event Graph で確認できます。

この Blueprint には `OpenTrigger` / `CloseTrigger` component はありません。ドアの可動部分は `DoorHinge` です。

| Event | entry |
| --- | --- |
| Open Door | `z2_door_open` |
| Close Door | `z2_door_close` |
| Slam Door | `z2_door_slam` |
| Lock Door | `z2_door_lock` |
| Unlock Door | `z2_door_unlock` |
| Locked Rattle | `z2_door_rattle` |

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

`Z3_Fishing` の **Hapbeat > Fishing > Hook** で Event Map を選び、**Hook Start Event**、**Hook Loop Event**、**Hook Release Event** の各プルダウンから entry を割り当てます。**Hook Wiring** は実際に Shark の sequence へ渡された結果を確認する読み取り専用の表示です。竿、`RodTipMarker`、釣り糸、Shark slot の位置は Details で編集します。

### Details で触覚の接続を確認する

`Z3_Fishing` の Details では、**Hapbeat > Fishing > Hook** の Event Map と 3 つの entry を選択します。**Hapbeat > Fishing > Hook Wiring** には実際に解決された start / loop / release の entry 名が表示されます。

### C++ 実装を確認する

**Tools → Open Visual Studio** を選び、次の SDK ファイルを開きます。

```text
Plugins
└ HapbeatSDK
  └ Source
    └ HapbeatSDKSamples
      ├ Public
      │ └ HapbeatShowcaseZ3FishingActor.h
      └ Private
        └ HapbeatShowcaseZ3FishingActor.cpp
```

- **Event Map と3 entryの選択・解決**
  - **Header:** `EventMapOverride`、`Hook*Event`、`ResolvedHook*EntryName`
  - **Implementation:** `AHapbeatShowcaseZ3FishingActor::BuildEventMapAndHaptics`
- **sequence への map / entry の代入**
  - **Header:** `SharkSlot`、`AHapbeatShowcaseZ3SharkActor::HookSequence`
  - **Implementation:** `BuildEventMapAndHaptics`
- **魚の速度から loop Gain を更新**
  - **Header:** `HookVelocityBinding`
  - **Implementation:** `AHapbeatShowcaseZ3SharkActor::AHapbeatShowcaseZ3SharkActor`
- **左クリックで sequence を開始・停止**
  - **Implementation:** `HandleFirePressed`、`HandleFireReleased`、`SetHooked`

`BuildEventMapAndHaptics` の `WiredShark->HookSequence` への `EventMap`、`EntryId`、`StartEntryId`、`StopEntryId` の代入が、Details の entry と sequence をつなぐ箇所です。`SetHooked` が `Fire()` と `Stop()` を呼びます。

## Z4 Stream Console — loop と runtime parameter

`BP_Z4_StreamConsole` と `BP_Z4_StreamConsoleWidget` は、stream の開始・停止、runtime parameter、tick、Address Override を Blueprint で接続する例です。

```text
Space
  → LoopTrigger.Get Active Playback
  → Is Valid（ハンドルなしなら Fire）
  → Is Active
  → Stop または Fire

Gain / Pan slider
  → GainBinding / PanBinding.Set Value
  → Evaluate Now
  → Fire Hapbeat Tick From Value (TickTrigger)
```

### SDK の接続を最短で確認する

1. World Outliner で `Z4_StreamConsole` を選び、Details の **Edit Blueprint** をクリックします。Content Browser から開く場合は `Plugins/HapbeatSDK/Content/HapbeatSamples/Showcase/BP_Z4_StreamConsole` を開きます。
2. 開いた Blueprint Editor 左上の **Components** で、`LoopTrigger`、`TickTrigger`、`GainBinding`、`PanBinding`、`AddressPanel` を確認します。`TickTrigger` の class は **Hapbeat Tick Trigger** です。
3. **My Blueprint > Graphs > EventGraph** を開きます。`Space Bar` から `LoopTrigger` の `Fire` / `Stop` を、`On Showcase Zone Activated` から `Create Widget` と `AddressPanel.Show` を確認します。
4. **My Blueprint > Construction Script** を開きます。`GainBinding` と `PanBinding` の `Set Target Trigger` に、この Actor の `LoopTrigger` を接続しています。
5. `BP_Z4_StreamConsoleWidget` を開き、**My Blueprint > Graphs > EventGraph** を開きます。`Handle Gain Value Changed` と `Handle Pan Value Changed` はそれぞれ `Binding.Set Value` → `Evaluate Now` → `Fire Hapbeat Tick From Value` に接続されています。

`LoopTrigger` は `z4_stream_loop`、`TickTrigger` は `z4_slider_tick` を指します。各 trigger の Details で **Event Map** と entry を変更できます。Gain/Pan を切り替えた最初の値では tick の参照をリセットするため、別の slider の値との差による誤発火はありません。`GainBinding` と `PanBinding` の **Source = External** にスライダー値を渡し、Construction Script で接続した `LoopTrigger` の再生だけを調整します。

`AddressPanel` は Zone が表示中に `Show`、非表示時に `Hide` されます。Player / Group の選択はこの component の UI で Apply します。

## Z5 Target Range — charge / shot と target hit

```text
charge begin / threshold / release
  → z5_charge_* / z5_shot_*

projectile が target に Hit
  → LightHitTrigger または HeavyHitTrigger
  → z5_tar_hit_light / z5_tar_hit_heavy
```

`Z5_ChargeShot` の **Haptic Wiring** で、charge、shot、target hit の entry を確認します。charge 時間、launch speed、target slot は同 Actor の Details で変更できます。触覚の Clip、Gain、Target、loop は Event Map で変更します。

### Details で触覚の接続を確認する

`Z5_ChargeShot` の Details では、**Hapbeat > Event Map Override** と **Hapbeat > Showcase > Haptic Wiring** を確認します。後者には charge、shot、target hit の解決済み entry 名が表示されます。

### C++ 実装を確認する

**Tools → Open Visual Studio** を選び、次の SDK ファイルを開きます。

```text
Plugins
└ HapbeatSDK
  └ Source
    └ HapbeatSDKSamples
      ├ Public
      │ └ HapbeatShowcaseZ5ChargeShotActor.h
      └ Private
        └ HapbeatShowcaseZ5ChargeShotActor.cpp
```

- **Event Map と6 entryの解決**
  - **Header:** `EventMapOverride`、`Resolved*EntryName`
  - **Implementation:** `AHapbeatShowcaseZ5ChargeShotActor::BuildEventMap`
- **charge 開始・threshold・release**
  - **Header:** `ChargeLoopEntryId`、`ChargeThresholdEntryId`
  - **Implementation:** `HandleChargeBegin`、`Tick`、`HandleChargeRelease`
- **light / heavy shot の直接再生**
  - **Header:** `ShotLightEntryId`、`ShotHeavyEntryId`
  - **Implementation:** `FireShotAfterDelay`、`FireOneShotEntry`
- **target hit の collision trigger**
  - **Header:** `TargetSlot`、`LightHitTrigger`、`HeavyHitTrigger`
  - **Implementation:** `SetUpTarget`

`SetUpTarget` では `Target->LightHitTrigger` と `Target->HeavyHitTrigger` に Event Map と entry を代入します。`FireOneShotEntry` は charge / shot の entry を SDK の再生 API で直接発火する箇所です。

## 変更場所

| 変更したい内容 | 変更場所 |
| --- | --- |
| Clip、Gain、Target、loop | `EM_Showcase` の entry |
| Z1 の pin-hit entry | `Z1_Bowling` > **Event Map / Pin Hit Event** |
| 実行時の送信先 | Address Override |

`Target` は送信先を表す論理フィルタです。Address Override は Event Map や Actor の配線を変更しません。

## SDK の接続設定と確認

エディタ上部の **Tools → Hapbeat** から次を開けます。

- **Hapbeat Settings** — Port、App Name、Command Unicast、タイミング、ビルド固定の Address Override を編集します。
- **Hapbeat Runtime Status** — この PC に保存する Address Override と、PIE 中の実効 Player / Group、socket 状態を確認します。PIE 外で保存した値は次回の PIE / packaged launch に使われ、PIE 中に保存した値は即時反映されます。

Event Map の **Test Play** は、同じ保存済み Address Override と build-pinned override を解決してから送信します。Test Play はボタンを押した時点で送信されます。

## 関連資料

- [Blueprint ノード一覧](./blueprint-nodes.md)
- [Advanced usage](./advanced.md)
