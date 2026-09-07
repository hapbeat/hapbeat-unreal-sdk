---
sidebar:
  order: 2
  label: Blueprint ノード
---

# Blueprint ノード一覧

Hapbeat SDK は、Blueprint から Event Map の entry を再生するための関数、Actor に追加して使う component、接続・宛先・診断のための補助 API を公開しています。このページは、どの入口を選ぶかを先に決められるように、ゲーム実装で使う順に整理したリファレンスです。

## 最初に選ぶ入口

通常は、次のいずれかを使います。

| 作りたいもの | 最初に使うもの | 理由 |
| --- | --- | --- |
| ボタン、UI、Animation Notify、独自の gameplay event で一度だけ鳴らす | **Play Hapbeat Event** | Event Graph から直接呼べる最短経路 |
| 衝突または overlap で鳴らす | **Hapbeat Collision Trigger** component | UE の Hit / Begin Overlap を component が監視する |
| 掴む→保持→離す | **Hapbeat Sequence** component | start、loop、stop を 1 component で管理する |
| スライダー・ノブで連続音の強さや左右を変える | **Hapbeat Parameter Binding** component | 入力値を stream の gain / pan に変換する |
| ノブや slider の目盛りを越えたときに一発ずつ鳴らす | **Hapbeat Tick Emitter** component | 移動量に応じて tick 数を決める |
| 接続・宛先・直接送信を独自管理する | **Hapbeat Subsystem** | 上の高水準 API では足りない場合だけ使う |

`Hapbeat Trigger Component` は C++ 用の基底 class であり、意図的に **Add Component には表示されません**。Blueprint から任意のイベントを発火する用途には `Play Hapbeat Event` を使います。Collision / Sequence / Tick Emitter は検出ロジックを持つため、Add Component から追加できます。

## 1. Event Map を直接再生する

Event Graph の空白を右クリックして `Play Hapbeat Event` を検索します。`Map` と `Entry` に Event Map Data Asset とその entry を指定します。

| ノード | 入力・出力 | 用途 |
| --- | --- | --- |
| **Play Hapbeat Event** | `Map`、`Entry`、任意で `Gain Multiplier` / `Pan` / `Delay Seconds`。StreamClip entry の場合は `Hapbeat Stream Playback` を返す | 単発 Command、単発 Clip、loop Clip の開始 |
| **Stop Hapbeat Event** | `Map`、`Entry` | entry を停止する |

`Gain Multiplier` は Event Map の Gain に掛ける値です。`Pan` は Event Map の Pan に加算され、`Delay Seconds` はこの呼び出しだけに加える遅延です。いずれもノードの詳細ピンから表示します。

```text
On Component Begin Overlap
  → Play Hapbeat Event
      Map: DA_HapbeatEventMap
      Entry: pickup
```

ゲーム側に C++ ラッパーを作る必要はありません。Showcase に追加する単発 event の BP 例も、この入口を使います。

## 2. Collision / Sequence component

Actor を開き、Components の **Add** で `Hapbeat` を検索して追加します。どの component でも、Details の `Event Map` と `Entry Id` を先に設定します。Event Map が設定されると、Entry Id は entry 名を選べるドロップダウンになります。

### Hapbeat Collision Trigger

`Hapbeat Collision Trigger` は owner の Primitive Component に結び付き、Hit または Begin Overlap を検出して再生します。グラフ配線は不要です。

| 設定 | 意味 |
| --- | --- |
| `Trigger Event` | `Hit` または `Begin Overlap` |
| `Gain Mode` | 固定値、または衝突速度で gain を変える `Velocity Scaled` |
| `Tag Filter` | 特定タグの相手だけに限定する |
| `Velocity Threshold` / `Max Velocity` / `Velocity Curve` | 速度を gain に変換する範囲と曲線 |
| `bEnterOnly` | 継続接触を 1 回の接触として扱う。Unity の OnCollisionEnter 相当が必要な場合は on |

Hit を使う場合、衝突する Primitive Component で **Simulation Generates Hit Events** を on にします。Overlap を使う場合は **Generate Overlap Events** を on にします。

### Hapbeat Sequence

`Hapbeat Sequence` は、開始の one-shot、保持中の loop、終了の one-shot を 1 component にまとめます。

| 設定・ノード | 意味 |
| --- | --- |
| `Start Entry Id` | `Fire` 時に鳴らす開始 one-shot |
| `Entry Id` | `Fire` で始める loop StreamClip |
| `Stop Entry Id` | `Stop` 後に鳴らす終了 one-shot |
| `Stop Shot Delay` | loop 停止と終了 one-shot の間隔 |
| `Fire` / `Stop` | 掴み開始・終了などの Blueprint event から呼ぶ |

開始と終了の input event は BP で決め、3 段階の触覚再生は component に任せます。

## 3. Stream Playback を操作する

`Play Hapbeat Event` が StreamClip entry を開始した場合、戻り値の `Hapbeat Stream Playback` を変数に保存します。再生中のその source だけを操作できます。

| ノード | 用途 |
| --- | --- |
| `Apply Gain Modulation` | 再生中の gain を変える |
| `Set Pan` | 再生中の pan を変える（-1 = 左、+1 = 右） |
| `Set Loop` / `Get Loop` | loop を切り替える・確認する |
| `Stop` | この playback source だけを停止する |
| `Is Active` / `Is Stopped` / `Get Status` / `Get Deferred Reason` | 再生状態と待機理由を確認する |

同時に複数の StreamClip を再生している場合でも、各戻り値を別々に保持すれば個別に制御できます。Z4 の BP 例では、開始時に handle を保存し、UI から gain・pan・loop・stop を操作します。

## 4. 連続値と tick を結ぶ component

### Hapbeat Parameter Binding

`Hapbeat Parameter Binding` は、入力値を 0〜1 に正規化し、curve と output range を通して、再生中 StreamClip の gain または pan に書き込みます。

| 設定・ノード | 意味 |
| --- | --- |
| `Source Property = External` | Blueprint から値を渡す。UMG Slider の `On Value Changed` に使う |
| `Input Min` / `Input Max` | 入力値の範囲 |
| `Curve Type` / `Custom Curve` | 入力値の変換方法 |
| `Output Parameter` | `Stream Gain` または `Stream Pan` |
| `Output Min` / `Output Max` | 出力範囲 |
| `Target Trigger` | 操作対象の StreamClip trigger。複数 stream がある Actor では指定する |
| `Set Value` | `Source Property = External` の値を渡す |
| `Evaluate Now` | Tick を待たず、現在値をすぐ反映する |

loop を開始した直後に `Evaluate Now` を一度呼ぶと、最初の stream chunk にも現在の値が反映されます。

### Hapbeat Tick Emitter

`Hapbeat Tick Emitter` は、slider やノブの移動量が `Tick Threshold` を越えるたびに entry を 1 回発火します。時間ベースの cooldown ではないため、ゆっくり動かせば少なく、速く動かせば多く tick します。

| ノード | 用途 |
| --- | --- |
| `Fire From Value` | 1 次元の slider 値を渡す |
| `Fire From Vector2D` | 2 次元入力の指定 axis を使う |
| `Fire Now` | 目盛り検出を通さず 1 回発火する |
| `Reset Reference` | UI 値をプログラムから飛ばした後、不要な連続 tick を防ぐ |

Z4 では Parameter Binding と Tick Emitter を併用します。前者は連続した stream の変調、後者は操作感を示す one-shot です。

## 5. Trigger component に共通する操作

Collision / Sequence / Tick Emitter は共通して次のノードを持ちます。通常は component の参照を Event Graph へドラッグして呼び出します。

| ノード | 用途 |
| --- | --- |
| `Fire` | 設定済み entry を再生する |
| `Fire With Gain` | 呼び出しごとの倍率を掛けて再生する |
| `Fire Scaled` | 速度などの値を指定範囲で 0〜1 にして再生する |
| `Fire With Curve` | 値を `Curve Float` で倍率へ変換して再生する |
| `Stop` | この component が開始した entry を停止する |
| `Set Gain Multiplier` | 再生中 stream にも gain を即時反映する |
| `Set Stream Pan` | 再生中 stream の pan を即時変更する |
| `Get Active Playback` | component が開始した Stream Playback handle を取得する |
| `On Fired` event | 実際に触覚を送った時だけ SFX / VFX も実行する |

`On Fired` には、触覚と同じ gate を通したい音・光・アニメーションをつなぎます。cooldown、無効状態、entry 未設定で触覚が送られなかった場合には実行されません。

## 6. 接続と直接送信（高度な用途）

`Get Game Instance Subsystem` で `Hapbeat Subsystem` を取得すると、Event Map を介さない低水準 API を利用できます。

| ノード | 用途 |
| --- | --- |
| `Connect` | UDP socket を開く |
| `Play` / `Stop` / `Stop All` | event ID を直接送信・停止する |
| `Ping` | 到達可能 device を確認する |
| `Stream Clip` / `Stop Stream` | Clip を直接 stream する |
| `On Connected` / `On Disconnected` / `On Error` / `On Pong` | 接続状態を BP event として扱う |
| `Is Connected` / `Is Alive` / `Get Alive Device Count` / `Is Streaming` | 状態を UI などに表示する |

通常のゲームイベントには Event Map を使います。`Play` や `Stream Clip` は、event ID や宛先を実行時に動的に組み立てる必要があり、Event Map で管理できない場合に限ります。

## 7. 宛先、運用 UI、診断

これらはゲームの触覚演出ではなく、複数端末運用・検証のための API です。

| 種別 | 主なノード | 用途 |
| --- | --- | --- |
| Target Library | `Build Target`、`Parse Target`、`Resolve Target`、`Apply Address Placeholders`、`Address Matches` | Target 文字列の組み立てと検証 |
| Address override | `Set Address Override`、`Clear Persisted Address Override` | 起動中の player / group を上書きする |
| Address Override Panel | `Show`、`Hide`、`Toggle`、`Attach To Widget Component` | 展示用の切替 panel を出す |
| Status Overlay | `Log`、`Clear Log` | 接続状態と簡易ログを画面表示する |
| Event Logger | `Log Event`、`Log Hit`、`Log Begin Overlap` など | Output Log と画面への発火記録 |

Address override は Event Map や Trigger component の設定を変更しません。送信時に target を解決する値だけを上書きします。詳細は[応用](./advanced.md)を参照してください。

## 8. Data Asset の補助関数

`Hapbeat Event Map` と `Hapbeat Clip` は Blueprint Type の Data Asset です。

| 型 | 関数 | 用途 |
| --- | --- | --- |
| `Hapbeat Event Map` | `Find By Id` | GUID から entry を取得する |
| `Hapbeat Clip` | `Num Samples`、`Num Frames`、`Duration Seconds` | stream asset の長さを確認する |

通常は Event Map Editor と entry picker を使うため、これらを Event Graph で呼ぶ場面は限定的です。

## Showcase との対応

Showcase はすべての node を並べる場所ではなく、実際の gameplay での経路を示す場所です。BP の代表例は次の 2 種類に分けます。

| Zone | BP で示す範囲 |
| --- | --- |
| Z2 Door | `Play Hapbeat Event` による単発 event の発火 |
| Z4 Stream Console | Stream Playback、Parameter Binding、Tick Emitter による連続制御 |

`BP_Z2_Door` と `BP_Z4_StreamConsole` は Showcase map に配置済みの直接 BP 例です。前者は Event Graph の `Play Hapbeat Event`、後者は loop の開始・停止と slider の runtime parameter / tick を示します。Z4 の Components には `LoopTrigger`、Gain/Pan ごとの `Hapbeat Parameter Binding`、1つの `Hapbeat Tick Emitter` が設定されています。Collision / Sequence は Z1 / Z3 の C++ 実装でも component の設定と lifecycle を確認できます。接続・Target・診断はこのページと[応用](./advanced.md)で確認します。Showcase 内の Actor / Component と Event Map の配線は[Showcase の触覚配線ガイド](./showcase-unreal.md)を参照してください。

## 実装の参照先

- [Blueprint Library](https://github.com/hapbeat/hapbeat-unreal-sdk/blob/master/Source/HapbeatSDK/Public/HapbeatBlueprintLibrary.h)
- [Trigger / Collision / Sequence](https://github.com/hapbeat/hapbeat-unreal-sdk/tree/master/Source/HapbeatSDK/Public)
- [Stream Playback](https://github.com/hapbeat/hapbeat-unreal-sdk/blob/master/Source/HapbeatSDK/Public/HapbeatStreamPlayback.h)
- [Parameter Binding](https://github.com/hapbeat/hapbeat-unreal-sdk/blob/master/Source/HapbeatSDK/Public/HapbeatParameterBinding.h)
- [Tick Emitter](https://github.com/hapbeat/hapbeat-unreal-sdk/blob/master/Source/HapbeatSDK/Public/HapbeatTickEmitterComponent.h)
- [Hapbeat Subsystem](https://github.com/hapbeat/hapbeat-unreal-sdk/blob/master/Source/HapbeatSDK/Public/HapbeatSubsystem.h)
