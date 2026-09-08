---
sidebar:
  order: 3
  label: Showcase 配線
---

# Showcase の触覚配線

Showcase は、ゲーム内の出来事を Hapbeat Event Map の entry へ接続する 5 つのサンプルです。各 zone Actor の Details と `EM_Showcase` を開くと、再生する触覚とその設定を確認・変更できます。

<p class="hb-notation"><span class="hb-location">エディタ内の場所</span><span class="hb-field">Details の編集項目</span><span class="hb-bp-node">Blueprint ノード</span><code>C++・asset・entry ID</code></p>

| Zone | 実装方法 | 確認できる触覚配線 |
| --- | --- | --- |
| Z1 Bowling | C++ Actor + collision trigger | pin の衝突から `z1_pin_hit` を発火する配線 |
| Z2 Swing Door | Blueprint Event Graph | ドアの Timeline と `Play Event (Hapbeat)` を同じ操作分岐から始める配線 |
| Z3 Fishing | C++ Actor + Sequence / Parameter Binding | hook の開始・loop・解除と、魚の速度を loop Gain へ送る配線 |
| Z4 Stream Console | Blueprint Event Graph + Widget Blueprint | stream の開始・停止、UI の Gain / Pan、tick、Address Override の配線 |
| Z5 Target Range | C++ Actor + collision trigger | charge・shot の直接 SDK 呼び出しと、target hit の light / heavy 分岐 |

## 最初に見る場所

1. <span class="hb-location">Content Browser > Settings</span> で <span class="hb-field">Show Plugin Content</span> を有効にします。
2. <span class="hb-location">Plugins/HapbeatSDK/Content/HapbeatSamples/Showcase/Maps/Showcase</span> を開きます。
3. <span class="hb-location">World Outliner</span> で対象の zone Actor を選びます。
4. <span class="hb-location">Content Browser > HapbeatSamples/Showcase/EM_Showcase</span> を開き、entry の <span class="hb-field">Clip</span>、<span class="hb-field">Gain</span>、<span class="hb-field">Target</span>、<span class="hb-field">Loop</span> を編集します。

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

<span class="hb-field">Pin Hit Event</span> は、6 本の pin が衝突したときに再生する Event Map entry です。

### 編集する場所を開く

1. <span class="hb-location">Showcase map > World Outliner</span> で `Z1_Bowling` を選びます。
2. <span class="hb-location">Details > Hapbeat > Bowling > Pin Hit</span> を開きます。

:::tip[手を動かして試す]
- **操作 A — 強さ:** <span class="hb-location">`EM_Showcase` の `z1_pin_hit` entry</span> にある <span class="hb-field">Gain</span> を変更します。
  - 例: 現在値を半分にする。
  - **確認できること:** ball と pin の衝突は同じまま、触覚の強さだけが変わります。
- **操作 B — entry:** <span class="hb-field">Pin Hit Event</span> を変更します。
  - 例: `z1_pin_hit` から `z2_door_slam` に切り替える。
  - **確認できること:** 同じ pin 衝突が、選んだ entry の触覚を発火します。
:::

### SDK の接続を確認する（C++）

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

`BuildEventMap` が Details の `EventMapOverride` と `PinHitEvent` を解決し、`SetUpPins` がその map と entry を各 pin の `HitTrigger` へ渡します。

`SetUpPins` 内の次の 2 行が、Details の選択を実際の pin 衝突 trigger へ接続する箇所です。

```cpp
Pin->HitTrigger->EventMap = EventMap;
Pin->HitTrigger->EntryId = PinHitEntryId;
```

## Z2 Swing Door — Blueprint からイベントを発火する

`BP_Z2_Door` は、ドア操作の各分岐から <span class="hb-bp-node">Play Event (Hapbeat)</span> を発火する Blueprint 完結の例です。

### 編集箇所を開く（Blueprint）

1. <span class="hb-location">Showcase map > World Outliner</span> で `Z2_Door` を選び、<span class="hb-location">Details</span> の <span class="hb-field">Edit Blueprint</span> をクリックします。
2. <span class="hb-location">My Blueprint > Graphs > EventGraph</span> を開きます。

:::tip[手を動かして試す]
- **操作:** <span class="hb-location">DoorSlam lane</span> の <span class="hb-bp-node">Play Event (Hapbeat)</span> にある <span class="hb-field">Entry</span> を変更します。
  - 例: `z2_door_slam` から `z2_door_close` に切り替える。
  - **確認できること:** `G` の slam Timeline は同じまま、node で選んだ entry の触覚を発火します。
:::

### SDK の接続を確認する（Blueprint）

`F`、`G`、`L` の <span class="hb-bp-node">Input Key</span> から <span class="hb-bp-node">Switch on Door State</span>、対応する動作 lane、<span class="hb-bp-node">Play Event (Hapbeat)</span> をたどります。

```text
F / G / L Pressed
  → Switch on Door State
  → DoorOpen / DoorClose / DoorSlam / DoorRattle / Lock / Unlock
  → 対応する z2_door_* entry の Play Event (Hapbeat)
  → DoorHinge の Timeline
```

各 lane の <span class="hb-bp-node">Play Event (Hapbeat)</span> にある <span class="hb-field">Map</span> と <span class="hb-field">Entry</span> が、ゲーム内操作と Hapbeat 再生を結ぶ箇所です。Timeline は同じ lane で始まるため、ドアの動きと触覚の開始点がそろいます。

## Z3 Fishing — sequence と gain binding

```text
左クリック press / release
  → Shark.HookSequence
  → z3_hook_start / z3_hook_loop / z3_hook_release

Shark の速度
  → HookVelocityBinding
  → loop の Gain
```

`Z3_Fishing` の <span class="hb-field">Hook Start / Loop / Release Event</span> は、左クリックの開始・継続・解除で再生する entry です。

### 編集する場所を開く

1. <span class="hb-location">Showcase map > World Outliner</span> で `Z3_Fishing` を選びます。
2. <span class="hb-location">Details > Hapbeat > Fishing > Hook</span> を開き、Event Map と 3 つの entry を確認します。

:::tip[手を動かして試す]
- **操作:** <span class="hb-field">Hook Loop Event</span> を変更します。
  - 例: `z3_hook_loop` から `z5_charge_loop` に切り替える。
  - **確認できること:** <span class="hb-location">Hook Wiring</span> の <span class="hb-field">Loop Entry Name</span> が選択した entry になり、hook 中の loop が切り替わります。
:::

### SDK の接続を確認する（C++）

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

`BuildEventMapAndHaptics` が 3 つの選択値を `HookSequence` の start / loop / stop entry へ渡します。`SetHooked` が sequence の `Fire()` / `Stop()` を呼びます。`HookVelocityBinding` は釣られた魚の速度を loop 再生の Gain に反映します。

## Z4 Stream Console — loop と runtime parameter

`BP_Z4_StreamConsole` は、Space による loop 再生と slider 値による Gain / Pan 更新を接続する Blueprint の例です。

```text
Space
  → Switch on Loop State
  → Stopped: LoopTrigger.Fire → Loop State = Running
  → Running: LoopTrigger.Stop → Loop State = Stopped

Gain / Pan slider
  → On Gain/Pan Slider Changed
  → BP_Z4_StreamConsole Event Graph
  → Set Binding Input (Hapbeat)
  → Update Stream Parameter (Hapbeat)
  → Fire Tick From Value (Hapbeat)
```

### 編集箇所を開く（Blueprint）

1. <span class="hb-location">Showcase map > World Outliner</span> で `Z4_StreamConsole` を選び、<span class="hb-location">Details</span> の <span class="hb-field">Edit Blueprint</span> をクリックします。
2. <span class="hb-location">My Blueprint > Graphs > EventGraph</span> を開きます。

:::tip[手を動かして試す]
- **操作:** <span class="hb-location">Components</span> で `TickEmitter` を選び、<span class="hb-location">Details</span> の <span class="hb-field">Tick Threshold</span> を変更します。
  - 例: `0.1` から `0.2` にする。
  - **確認できること:** Gain / Pan slider を同じ距離だけ動かしたときの tick 回数が半分になります。
:::

### SDK の接続を確認する（Blueprint）

1. `Space Bar` から <span class="hb-bp-node">Switch on Loop State</span> をたどります。Stopped の <span class="hb-bp-node">Fire Trigger (Hapbeat)</span> が `z4_stream_loop` を開始し、Running の <span class="hb-bp-node">Stop Trigger (Hapbeat)</span> が同じ loop を停止します。
2. <span class="hb-bp-node">On Gain Slider Changed</span> / <span class="hb-bp-node">On Pan Slider Changed</span> から <span class="hb-bp-node">Set Binding Input (Hapbeat)</span> → <span class="hb-bp-node">Update Stream Parameter (Hapbeat)</span> をたどります。slider の float 値を `GainBinding` / `PanBinding` に入れ、両 binding が `LoopTrigger` の再生中 stream の Gain / Pan を更新します。
3. 続く <span class="hb-bp-node">Fire Tick From Value (Hapbeat)</span> は、slider が <span class="hb-field">Tick Threshold</span> をまたいだときだけ `z4_slider_tick` を再生します。

Address Override は、Player / Group に対応する Hapbeat だけへ送るための実行時の送信先指定です。Event Map の entry や再生配線は変えません。詳しくは[複数の HMD に 1 台ずつ Hapbeat を割り当てる](./advanced.md#複数の-hmd-に-1-台ずつ-hapbeat-を割り当てる)を参照してください。

## Z5 Target Range — charge / shot と target hit

```text
charge begin / threshold / release
  → z5_charge_* / z5_shot_*

projectile が target に Hit
  → LightHitTrigger または HeavyHitTrigger
  → z5_tar_hit_light / z5_tar_hit_heavy
```

`Z5_ChargeShot` の 6 つの Event プルダウンは、charge、shot、target hit で再生する entry を選びます。

### 編集する場所を開く

1. <span class="hb-location">Showcase map > World Outliner</span> で `Z5_ChargeShot` を選びます。
2. <span class="hb-location">Details > Hapbeat > Showcase > Haptic Events</span> を開きます。

:::tip[手を動かして試す]
- **操作:** <span class="hb-field">Heavy Shot Event</span> を変更します。
  - 例: `z5_shot_heavy` から `z1_pin_hit` に切り替える。
  - **確認できること:** heavy charge の release が、選んだ entry を再生します。
:::

### SDK の接続を確認する（C++）

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

`BuildEventMap` が Details の `EventMapOverride` と 6 つの `*Event` を entry ID に解決します。`HandleChargeBegin`、`Tick`、`FireShotAfterDelay` が charge / shot の ID を `FireOneShotEntry` へ渡します。`SetUpTarget` は light / heavy target-hit の ID を各 collision trigger へ渡します。

## 変更場所

| 変更したい内容 | 変更場所 |
| --- | --- |
| Clip、Gain、Target、loop | `EM_Showcase` の entry |
| Z1 の pin-hit entry | `Z1_Bowling` の <span class="hb-field">Event Map / Pin Hit Event</span> |
| Z5 の charge / shot / target-hit entry | `Z5_ChargeShot` の <span class="hb-field">Haptic Events</span> |
| 実行時の送信先 | Address Override |

`Target` は送信先を表す論理フィルタです。Address Override は Event Map や Actor の配線を変更しません。詳細は[応用](./advanced.md)を参照してください。

## SDK の接続設定と確認

エディタ上部の **Tools → Hapbeat** から次を開けます。

- **Hapbeat Settings** — Port、App Name、Command Unicast、タイミング、ビルド固定の Address Override を編集します。
- **Hapbeat Runtime Status** — この PC に保存する Address Override と、PIE 中の実効 Player / Group、socket 状態を確認します。PIE 外で保存した値は次回の PIE / packaged launch に使われ、PIE 中に保存した値は即時反映されます。

Event Map の **Test Play** は、同じ保存済み Address Override と build-pinned override を解決してから送信します。Test Play はボタンを押した時点で送信されます。

## 関連資料

- [Blueprint ノード一覧](./blueprint-nodes.md)
- [Advanced usage](./advanced.md)
