# Showcase の触覚配線ガイド

Showcase は、Unreal Engine 5（UE5）で Hapbeat の触覚イベントをゲームの出来事へ接続する、5 つの実例です。このページは、まず **Editor 上で「どの Actor / Component が、どの Hapbeat Event Map entry を再生するか」** を確認・調整するための手引きです。

Showcase の触覚ロジックはすべて C++ で実装されており、Blueprint で配線した箇所はありません。各 zone の Details と component tree を起点に確認します。

## 最初に見る場所

1. Content Browser の Settings から **Show Plugin Content** を有効にします。
2. `Plugins/HapbeatSDK/Content/HapbeatSamples/Showcase/Maps/Showcase` を開きます。
3. World Outliner で `Z1_Bowling`、`Z2_Door`、`Z3_Fishing`、`Z4_StreamConsole`、`Z5_ChargeShot` のいずれかを選択します。
4. Details パネルで **Hapbeat** カテゴリの **Event Map Override** を確認します。既定では `EM_Showcase` です。
5. Actor 名の下にある component tree を展開し、`OpenTrigger`、`HookSequence`、`GainBinding` のような Hapbeat component を選択します。
6. Content Browser で `HapbeatSamples/Showcase/EM_Showcase` をダブルクリックします。Event Map Editor で entry の Mode、Gain、Target、Clip、loop を確認します。

Event Map Editor の entry を選んで右側の **Wiring** から **Scan Level** を実行すると、現在開いている level に、選択した entry を設定済みの trigger があれば表示されます。これは配置済み component の確認に使えます。

> **Showcase の注意:** Z1 の pin や Z3 の shark、Z5 の target に対する Event Map と entry ID は、zone が PIE 開始時に子 Actor へ設定します。そのため、PIE 前の Editor World で Scan Level を実行しても、これらの runtime 配線は表示されません。以下の zone ごとの「接続箇所」で確認してください。PIE 中は Outliner を **Play World** に切り替えると、実行中の child Actor と component を確認できます。

## 配線の全体像

```text
ゲーム入力 / 衝突 / 状態遷移 / UI
  → Hapbeat Trigger Component または zone の C++ 関数
  → EM_Showcase の entry（Mode / Gain / Target / Clip / loop）
  → UHapbeatSubsystem::PlayEntry / StopEntry
  → UDP command または StreamClip の endpoint session
```

`EM_Showcase` は、触覚の再生設定を集めた Data Asset です。各 zone Actor の `Event Map Override` には既定でこの asset が設定されています。別の Event Map を指定するとその asset を使います。ここで変えるのは「何をどの条件で送るか」の再生設定です。ゲーム中の出来事そのものは、各 Actor と component が検出します。

| Zone | Editor で選ぶ Actor | 触覚を発火する component / 関数 | `EM_Showcase` entry |
| --- | --- | --- | --- |
| Z1 Bowling | `Z1_Bowling`、PIE 中の pin child Actor | `HitTrigger` (`UHapbeatCollisionTriggerComponent`) | `z1_pin_hit` |
| Z2 Swing Door | `Z2_Door` | `OpenTrigger` ほか 6 個の `UHapbeatTriggerComponent` | `z2_door_open` ほか 5 個 |
| Z3 Fishing | `Z3_Fishing`、PIE 中の `Shark` child Actor | `HookSequence`、`HookVelocityBinding` | `z3_hook_start`、`z3_hook_loop`、`z3_hook_release` |
| Z4 Stream Console | `Z4_StreamConsole` | `LoopTrigger`、`TickTrigger`、`GainBinding`、`PanBinding` | `z4_stream_loop`、`z4_slider_tick` |
| Z5 Target Range | `Z5_ChargeShot`、PIE 中の `Target` child Actor | zone の charge / shot 関数、`LightHitTrigger`、`HeavyHitTrigger` | `z5_charge_*`、`z5_shot_*`、`z5_tar_hit_*` |

## Z1 Bowling — pin の衝突を発火する

左クリックで zone が ball を発射し、ball が pin に Hit すると、各 pin の `HitTrigger` が触覚を再生します。

```text
Ball の OnComponentHit
  → PinActor.HitTrigger
  → z1_pin_hit
```

- `Z1_Bowling` の Details で `Event Map Override`、`Launch Speed`、pin の大きさなどを確認します。
- pin は child Actor です。PIE 中に pin Actor を選択し、component tree の `HitTrigger` を開くと、Hit 判定、速度しきい値、cooldown、`VelocityScaled` gain を確認できます。
- `HitTrigger` の Event Map と entry ID は `Z1_Bowling` が開始時に全 pin へ設定します。PIE 前の pin を直接選んでも設定値が空なのは仕様です。

`z1_pin_hit` の Clip / baseline Gain / Target を変える場合は `EM_Showcase` を編集します。衝突で鳴る最低速度や強さの変化を変える場合は `HitTrigger` の設定を、ball の速さを変える場合は `Z1_Bowling` の `Launch Speed` を変更します。

## Z2 Swing Door — 状態遷移を 6 個の Trigger へ分ける

`Z2_Door` の component tree に、次の `UHapbeatTriggerComponent` が常設されています。各 component を選択すれば Event Map と entry ID を Details で確認できます。

| Component | 発火する entry | 発火する時点 |
| --- | --- | --- |
| `OpenTrigger` | `z2_door_open` | 開く遷移の開始 |
| `CloseTrigger` | `z2_door_close` | 閉じる遷移の開始 |
| `SlamTrigger` | `z2_door_slam` | 強く閉める遷移の開始 |
| `LockTrigger` | `z2_door_lock` | 施錠時 |
| `UnlockTrigger` | `z2_door_unlock` | 解錠時 |
| `RattleTrigger` | `z2_door_rattle` | 施錠中に操作した時 |

`F`、`G`、`L` の入力を zone の state machine が受け、状態が切り替わる瞬間に上の Trigger を `Fire()` します。ドアの見た目の回転は `DoorHinge` component が担当します。開閉方向、回転軸、duration は `Z2_Door` の Details で調整し、触覚の Clip / Gain / Target は Event Map 側で調整します。

## Z3 Fishing — Shark の sequence と速度 binding

Z3 は、触覚 component が `Z3_Fishing` 直下ではなく **Shark child Actor** にあります。PIE を開始してから Outliner を Play World に切り替え、`Z3_Fishing` の `Shark` child Actor を選択してください。

```text
左クリック press / release
  → Z3_Fishing の SetHooked()
  → Shark.HookSequence
      start: z3_hook_start
      loop:  z3_hook_loop
      stop:  z3_hook_release

Shark の速度
  → Shark.HookVelocityBinding
  → 再生中 loop の StreamGain
```

- `HookSequence` は start / loop / release を 1 つの sequence として管理します。
- `HookVelocityBinding` は Shark の速度を読み、loop StreamClip の gain を連続的に変えます。
- `Z3_Fishing` の Details では rod の mount、`RodTipMarker`、line length、Shark slot の配置を調整します。釣り糸の始点は `RodTipMarker` です。
- `HookSequence` と `HookVelocityBinding` の Event Map / entry ID は開始時に zone が Shark へ設定します。したがって PIE 前の `Shark` を選んだ場合は、runtime の接続値が表示されません。

entry の loop 有無、Clip、Target、baseline Gain は Event Map で変更します。魚の動き、line、rod、速度から gain への変換は Z3 Actor / Shark component の Details を変更します。

## Z4 Stream Console — loop と slider を別 component にする

`Z4_StreamConsole` を選び、component tree の次の 4 個を確認します。

```text
Space → LoopTrigger → z4_stream_loop
slider detent → TickTrigger → z4_slider_tick
gain slider → GainBinding (External) → StreamGain
pan slider  → PanBinding  (External) → StreamPan
```

- `LoopTrigger` は `Space` で loop entry を開始 / 停止します。
- `TickTrigger` は slider が detent を通過した時に one-shot を発火します。
- `GainBinding` と `PanBinding` は `Source Property = External` です。UI slider が component に値を渡し、再生中 StreamClip の gain と pan を更新します。

loop / tick の対象 Clip や Target を変える場合は Event Map を開きます。slider の範囲・detent・初期値、binding の curve は `Z4_StreamConsole` と各 binding component の Details で調整します。Address Panel は Event Map を書き換えず、実行中の address override を設定します。

## Z5 Target Range — charge / shot は C++、target hit は Trigger

Z5 は 2 種類の接続を使います。charge / shot は zone Actor の C++ が `PlayEntry()` / `StopEntry()` を直接呼び、target に当たった時だけ child Actor の collision trigger が発火します。

```text
左クリック press → HandleChargeBegin() → z5_charge_loop を開始
charge が threshold を初めて越える → z5_charge_thd
左クリック release → loop を停止 → z5_shot_light または z5_shot_heavy

projectile が Target に Hit
  → Target.LightHitTrigger → z5_tar_hit_light
  → Target.HeavyHitTrigger → z5_tar_hit_heavy
```

- `Z5_ChargeShot` の Details では `Event Map Override`、charge 時間、heavy threshold、launch speed、projectile、Target slot を確認・調整します。
- PIE 中に `Target` child Actor を選択すると、`LightHitTrigger` と `HeavyHitTrigger` の Hit 判定、tag filter、cooldown を確認できます。
- 2 つの target trigger の Event Map と entry ID は `Z5_ChargeShot` が開始時に設定します。
- charge / threshold / shot entry は Trigger component ではないため、Event Map Editor の Scan Level には出ません。上の C++ 直結の経路が正しい確認方法です。

charge loop の Clip / Gain / Target / loop は Event Map で、charge 時間や弾速は zone の Details で、命中判定は target trigger の Details で調整します。

## どこを変更するか

| 変更したい内容 | 変更場所 | 保存対象 |
| --- | --- | --- |
| Clip、Mode、baseline Gain、Target、loop | `EM_Showcase` の該当 entry | Event Map Data Asset |
| 衝突条件、cooldown、速度による gain | `HitTrigger` / `LightHitTrigger` などの Trigger component | Actor / component の設定 |
| ドア・釣り竿・target の位置、回転、scale | zone Actor の Details にある Child Actor slot | Level |
| ball の発射速度、charge 時間、slider 範囲 | 該当 zone Actor の Details | Level |
| 実行時だけの player / group 切替 | Address Panel / `SetAddressOverride()` | 実行時設定 |

`Target` は送信先を表す論理フィルタです。既知 device がある command は該当 endpoint へ unicast され、既知 device が 0 台の場合だけ broadcast へ fallback します。StreamClip は PONG で解決済みの endpoint session に送られます。Address override は Event Map や Trigger の配線を変更しません。

PIE 中の物理結果や runtime に代入された component 値は、PIE を停止すると元の Editor World には保存されません。位置・回転・scale のような Level の調整は PIE を止めて zone Actor の Details で変更し、`Ctrl+S` で保存してください。

## 初期レイアウトを再生成しない

`Scripts/generate_showcase_map.py` は Showcase の初期 layout を作る開発用 script です。通常の位置調整、Event Map の値調整、Trigger の設定変更には使いません。実行すると script が所有する Actor を作り直すため、保存済みの調整を失います。

## 補足資料

- [Unreal Editor での配置・PIE・ライフサイクル](./unity-to-unreal-codex.md) — Unity 経験者が Editor World、PIE World、Actor / Component の違いを把握するための補足です。
- [Advanced usage](./advanced.md) — address override、Target、独自ゲームへ組み込む時の補足です。
