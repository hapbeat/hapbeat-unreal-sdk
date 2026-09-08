---
sidebar:
  order: 3
  label: Showcase 配線
---

# Showcase の触覚配線

Showcase は、ゲーム内の出来事を Hapbeat Event Map の entry へ接続する 5 つのサンプルです。各 zone Actor の Details と `EM_Showcase` を開くと、再生する触覚とその設定を確認・変更できます。

:::note[音と触覚のタイミング]
音声出力の遅延は環境ごとに異なるため、触覚が音より先に感じられることがあります。これは想定内です。`Tools → Hapbeat → Hapbeat Settings > Behavior > Haptic Delay Seconds` を少しずつ上げ、触覚に遅延を加えて合わせます。詳しくは[音と触覚のタイミングを合わせる](./getting-started.md#音と触覚のタイミングを合わせる)を参照してください。
:::

| Zone | 実装方法 | 確認できる触覚配線 |
| --- | --- | --- |
| Z1 Bowling | C++ Actor + collision trigger | pin の衝突から <code class="hb-entry">z1_pin_hit</code> を発火する配線 |
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

### 編集して試す

1. <span class="hb-location">Showcase map > World Outliner</span> で <code class="hb-asset">Z1_Bowling</code> を選びます。
2. <span class="hb-location">Details > Hapbeat > Bowling > Pin Hit</span> を開きます。
3. <code class="hb-asset">EM_Showcase</code> の <code class="hb-entry">z1_pin_hit</code> entry にある <span class="hb-field">Gain</span> を変更します。
   - 例: 現在値を半分にする。
   - **確認できること:** ball と pin の衝突は同じまま、触覚の強さだけが変わります。
4. <span class="hb-field">Pin Hit Event</span> を変更します。
   - 例: <code class="hb-entry">z1_pin_hit</code> から <code class="hb-entry">z2_door_slam</code> に切り替える。
   - **確認できること:** 同じ pin 衝突が、選んだ entry の触覚を発火します。

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

最初に <code class="hb-cpp">Public/HapbeatShowcaseZ1BowlingActor.h</code> を開き、次に同名の <code class="hb-cpp">Private/HapbeatShowcaseZ1BowlingActor.cpp</code> を開きます。

<code class="hb-cpp">BuildEventMap</code> が Details の <code class="hb-cpp">EventMapOverride</code> と <code class="hb-cpp">PinHitEvent</code> を解決し、<code class="hb-cpp">SetUpPins</code> がその map と entry を各 pin の <code class="hb-cpp">HitTrigger</code> へ渡します。

<code class="hb-cpp">SetUpPins</code> 内の次の 2 行が、Details の選択を実際の pin 衝突 trigger へ接続する箇所です。

```cpp
Pin->HitTrigger->EventMap = EventMap;
Pin->HitTrigger->EntryId = PinHitEntryId;
```

## Z2 Swing Door — Blueprint からイベントを発火する

<code class="hb-asset">BP_Z2_Door</code> は、ドア操作の各分岐から <span class="hb-bp-node">Play Event (Hapbeat)</span> を発火する Blueprint 完結の例です。

### 編集して試す（Blueprint）

1. <span class="hb-location">Showcase map > World Outliner</span> で <code class="hb-asset">Z2_Door</code> を選び、<span class="hb-location">Details</span> の <span class="hb-field">Edit Blueprint</span> をクリックします。
2. <span class="hb-location">My Blueprint > Graphs > EventGraph</span> を開きます。
3. <span class="hb-location">DoorSlam lane</span> の <span class="hb-bp-node">Play Event (Hapbeat)</span> にある <span class="hb-field">Entry</span> を変更します。
   - 例: <code class="hb-entry">z2_door_slam</code> から <code class="hb-entry">z2_door_close</code> に切り替える。
   - **確認できること:** `G` の slam Timeline は同じまま、node で選んだ entry の触覚を発火します。

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

<code class="hb-asset">Z3_Fishing</code> の <span class="hb-field">Hook Start / Loop / Release Event</span> は、左クリックの開始・継続・解除で再生する entry です。

### 編集して試す

1. <span class="hb-location">Showcase map > World Outliner</span> で <code class="hb-asset">Z3_Fishing</code> を選びます。
2. <span class="hb-location">Details > Hapbeat > Fishing > Hook</span> を開き、Event Map と 3 つの entry を確認します。
3. <span class="hb-field">Hook Loop Event</span> を変更します。
   - 例: <code class="hb-entry">z3_hook_loop</code> から <code class="hb-entry">z5_charge_loop</code> に切り替える。
   - **確認できること:** <span class="hb-location">Hook Wiring</span> の <span class="hb-field">Loop Entry Name</span> が選択した entry になり、hook 中の loop が切り替わります。

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

<code class="hb-cpp">BuildEventMapAndHaptics</code> が 3 つの選択値を <code class="hb-cpp">HookSequence</code> の start / loop / stop entry へ渡します。<code class="hb-cpp">SetHooked</code> が sequence の <code class="hb-cpp">Fire()</code> / <code class="hb-cpp">Stop()</code> を呼びます。<code class="hb-cpp">HookVelocityBinding</code> は釣られた魚の速度を loop 再生の Gain に反映します。

## Z4 Stream Console — loop と runtime parameter

<code class="hb-asset">BP_Z4_StreamConsole</code> は、Space による loop 再生と slider 値による Gain / Pan 更新を接続する Blueprint の例です。

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

### 編集して試す（Blueprint）

1. <span class="hb-location">Showcase map > World Outliner</span> で <code class="hb-asset">Z4_StreamConsole</code> を選び、<span class="hb-location">Details</span> の <span class="hb-field">Edit Blueprint</span> をクリックします。
2. <span class="hb-location">My Blueprint > Graphs > EventGraph</span> を開きます。
3. <span class="hb-location">Components</span> で `TickEmitter` を選び、<span class="hb-location">Details</span> の <span class="hb-field">Tick Threshold</span> を変更します。
   - 例: `0.1` から `0.2` にする。
   - **確認できること:** Gain / Pan slider を同じ距離だけ動かしたときの tick 回数が半分になります。

### SDK の接続を確認する（Blueprint）

1. `Space Bar` から <span class="hb-bp-node">Switch on Loop State</span> をたどります。Stopped の <span class="hb-bp-node">Fire Trigger (Hapbeat)</span> が <code class="hb-entry">z4_stream_loop</code> を開始し、Running の <span class="hb-bp-node">Stop Trigger (Hapbeat)</span> が同じ loop を停止します。
2. <span class="hb-bp-node">On Gain Slider Changed</span> / <span class="hb-bp-node">On Pan Slider Changed</span> から <span class="hb-bp-node">Set Binding Input (Hapbeat)</span> → <span class="hb-bp-node">Update Stream Parameter (Hapbeat)</span> をたどります。slider の float 値を `GainBinding` / `PanBinding` に入れ、両 binding が `LoopTrigger` の再生中 stream の Gain / Pan を更新します。
3. 続く <span class="hb-bp-node">Fire Tick From Value (Hapbeat)</span> は、slider が <span class="hb-field">Tick Threshold</span> をまたいだときだけ <code class="hb-entry">z4_slider_tick</code> を再生します。

Address Override は、Player / Group に対応する Hapbeat だけへ送るための実行時の送信先指定です。Event Map の entry や再生配線は変えません。詳しくは[複数の HMD に 1 台ずつ Hapbeat を割り当てる](./advanced.md#複数の-hmd-に-1-台ずつ-hapbeat-を割り当てる)を参照してください。

## Z5 Target Range — charge / shot と target hit

```text
charge begin / threshold / release
  → z5_charge_* / z5_shot_*

projectile が target に Hit
  → LightHitTrigger または HeavyHitTrigger
  → z5_tar_hit_light / z5_tar_hit_heavy
```

<code class="hb-asset">Z5_ChargeShot</code> の 6 つの Event プルダウンは、charge、shot、target hit で再生する entry を選びます。

### 編集して試す

1. <span class="hb-location">Showcase map > World Outliner</span> で <code class="hb-asset">Z5_ChargeShot</code> を選びます。
2. <span class="hb-location">Details > Hapbeat > Showcase</span> を開きます。Event Map entry は <span class="hb-location">Haptic Events</span>、charge の調整は <span class="hb-location">Charge</span> にあります。
3. <span class="hb-field">Heavy Threshold</span> を変更します。
   - 例: `0.7` から `0.4` にする。
   - **確認できること:** threshold entry と heavy shot へ切り替わるタイミングが早くなります。
4. <span class="hb-field">Charge Loop Gain Curve</span> の中間を下げます。
   - 例: `ChargeT = 0.5` の Gain を `0.2` にする。
   - **確認できること:** charge 前半の loop は弱く、後半でより急に強くなります。
5. <span class="hb-field">Heavy Shot Event</span> を変更します。
   - 例: <code class="hb-entry">z5_shot_heavy</code> から <code class="hb-entry">z1_pin_hit</code> に切り替える。
   - **確認できること:** heavy charge の release が、選んだ entry を再生します。

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

<code class="hb-cpp">BuildEventMap</code> が Details の <code class="hb-cpp">EventMapOverride</code> と 6 つの <code class="hb-cpp">*Event</code> を entry ID に解決します。<code class="hb-cpp">Tick</code> は <span class="hb-field">Charge Loop Gain Curve</span> を評価し、再生中の charge loop の Gain を更新します。<code class="hb-cpp">HandleChargeBegin</code>、<code class="hb-cpp">Tick</code>、<code class="hb-cpp">FireShotAfterDelay</code> が charge / shot の ID を <code class="hb-cpp">FireOneShotEntry</code> へ渡します。<code class="hb-cpp">SetUpTarget</code> は light / heavy target-hit の ID を各 collision trigger へ渡します。

## 変更場所

| 変更したい内容 | 変更場所 |
| --- | --- |
| Clip、Gain、Target、loop | <code class="hb-asset">EM_Showcase</code> の entry |
| Z1 の pin-hit entry | <code class="hb-asset">Z1_Bowling</code> の <span class="hb-field">Event Map / Pin Hit Event</span> |
| Z5 の charge / shot / target-hit entry | <code class="hb-asset">Z5_ChargeShot</code> の <span class="hb-field">Haptic Events</span> |
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
