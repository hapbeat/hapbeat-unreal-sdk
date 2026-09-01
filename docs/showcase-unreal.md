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

`Z1_Bowling` を選び、Details の **Hapbeat > Bowling > Pin Hit** を開きます。ここだけで pin-hit の触覚を編集できます。

- **Event Map**: pin が参照する Event Map。既定は `EM_Showcase`。
- **Pin Hit Event**: 6 本すべての pin が発火する entry。`Event Map` を選ぶと、その entry 名から選択できます。

`Pin Hit Event` は Actor に保存される編集可能な参照です。`EM_Showcase` 側で `z1_pin_hit` の Clip、Gain、Target を変えることも、別の entry を `Pin Hit Event` に選び直すこともできます。

### C++ 実装を確認する

`Z1_Bowling` の右クリックメニューには、このプロジェクトで使える C++ を開く項目はありません。

上部の **Tools → Open Visual Studio**（表示名は設定済み IDE に応じて変わります）でプロジェクト全体を開きます。開いた IDE の Solution Explorer で `Plugins/HapbeatSDK/Source/HapbeatSDKSamples/Private` を開くと、対応する `.cpp` 実装を確認できます。

**Open Visual Studio** が Tools にない場合は、エクスプローラーで `.uproject` と同じフォルダの `.sln` を開きます。

- `Source/HapbeatSDKSamples/Public/HapbeatShowcaseZ1BowlingActor.h` — Details に出る `Event Map`、`Pin Hit Event` と pin slot の定義
- `Source/HapbeatSDKSamples/Private/HapbeatShowcaseZ1BowlingActor.cpp` — ball launch、pin の生成、hit を entry 発火へ結ぶ処理

`Public` / `Private` はエンジンの表示可否ではなく、他の Unreal module から include できるヘッダか、module 内部の実装かを分けるフォルダです。どちらも SDK のソースとして確認・変更できます。Content Browser の **C++ Classes** は主に `Public` のクラスをたどる入口なので、`.cpp` は IDE 側で開きます。

Showcase の入力・物理などの挙動は [Showcase の動作](./showcase-behavior.md) を参照してください。

## Z2 Swing Door — Blueprint からイベントを発火する

`BP_Z2_Door` は、ドアの開閉と **Play Hapbeat Event** を同じ Event Graph で接続する Blueprint 完結の例です。

### SDK の接続を最短で確認する

1. World Outliner で `Z2_Door` を選び、Details の **Edit Blueprint** をクリックします。Content Browser から開く場合は `Plugins/HapbeatSDK/Content/HapbeatSamples/Showcase/BP_Z2_Door` をダブルクリックします。
2. 開いた Blueprint Editor の左上 **Components** パネルが component tree です。`DoorHinge`、`DoorLeafMesh`、`DoorHandleMesh` を確認します。
3. 左の **My Blueprint > Graphs > EventGraph** を開きます。
4. `F`、`G`、`L` の Input Key node から、Branch と **Play Hapbeat Event** をたどります。

```text
F Pressed
  → bDoorMoving / bDoorLocked / bDoorOpen の Branch
  → z2_door_open または z2_door_close または z2_door_rattle
  → DoorOpen / DoorClose / DoorRattle

G Pressed
  → bDoorMoving / bDoorLocked / bDoorOpen の Branch
  → z2_door_slam または z2_door_rattle
  → DoorSlam / DoorRattle

L Pressed
  → bDoorMoving / bDoorOpen / bDoorLocked の Branch
  → z2_door_lock または z2_door_unlock

DoorOpen / DoorClose / DoorSlam: Update
  → Lerp (Rotator)
  → Set Relative Rotation (Target: DoorHinge)
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
