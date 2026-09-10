---
kind: reference
sidebar:
  order: 4
  label: Blueprint ノード
---

# Blueprint ノード

Hapbeat SDK の node は末尾に **`(Hapbeat)`** が付属します。表記のない `Create Widget`、`Add to Viewport`、`Set Value` などは Unreal Engine 標準 node です。Samples module が提供する運用 UI / ログ node も `(Hapbeat)` を付けますが、製品用の必須 API ではありません。

## 最初に選ぶもの

| 実現したいこと | 使うもの |
| --- | --- |
| 一度だけ entry を再生する | `Play Event (Hapbeat)` |
| Hit / Overlap を監視する | `Hapbeat Collision Trigger` component |
| 開始・loop・終了をまとめる | `Hapbeat Sequence` component |
| slider / ノブで連続再生を変調する | `Hapbeat Parameter Binding` component |
| 操作の目盛りごとに one-shot を鳴らす | `Hapbeat Tick Emitter` component |
| event ID・Clip・宛先を実行時に直接指定する | `Hapbeat Subsystem` |

## 1\. Event Map を再生する node

### Play Event (Hapbeat)

**用途:** Event Map の entry を再生します。通常の gameplay event の入口です。

-   **入力:** `Map`、`Entry`。詳細 pin に `Gain Multiplier`、`Pan`、`Delay Seconds`。
-   **結果:** Command entry は device event を送信し、Stream Clip entry は `Hapbeat Stream Playback` handle を返します。
-   **送信への影響:** entry に保存された Clip / Gain / Target / Loop を使います。`Gain Multiplier` は entry の Gain に乗算、`Pan` は entry の Pan に加算、`Delay Seconds` はプロジェクト設定と entry の Delay Offset に加算されます。

```text
ゲームイベント → Play Event (Hapbeat)
                 Map: DA_HapbeatEventMap
                 Entry: pickup
```

### Stop Event (Hapbeat)

**用途:** Event Map entry を停止します。

-   **入力:** `Map`、`Entry`。
-   **結果:** Command entry は指定 event / Target に STOP を送ります。Stream Clip entry は、この Subsystem の全 Stream source を停止します。1 回分だけ止める場合は再生時の戻り値に `Stop Stream Playback (Hapbeat)` を呼びます。
-   **使う場面:** loop を開始した gameplay state の終了時。

## 2\. Actor に追加する component

Components の **Add** で `Hapbeat` を検索して追加します。`Event Map` を設定すると、`Entry Id` は asset 内の entry を選ぶ picker になります。

### Hapbeat Collision Trigger

**用途:** owner の Primitive Component の Hit または Begin Overlap から entry を発火します。Event Graph の配線は不要です。

-   `Trigger Event`: `Hit` または `Begin Overlap`。
-   `Gain Mode`: 固定 Gain または衝突速度に応じた `Velocity Scaled`。
-   `Tag Filter`: 接触相手を tag で限定。
-   `Velocity Threshold` / `Max Velocity` / `Velocity Curve`: 衝突速度を Gain へ変換。
-   `bEnterOnly`: 継続接触を 1 回の接触として扱います。

Hit を使う場合は、衝突する Primitive Component の **Simulation Generates Hit Events** を有効にします。Overlap を使う場合は **Generate Overlap Events** を有効にします。

### Hapbeat Sequence

**用途:** 開始 one-shot、保持中 loop、終了 one-shot を一つの component として管理します。

-   `Start Entry Id`: `Fire` 時に鳴らす開始 event。
-   `Entry Id`: `Fire` で開始する loop Stream Clip。
-   `Stop Entry Id`: `Stop` 後に鳴らす終了 event。
-   `Stop Shot Delay`: loop の停止と終了 event の間隔。

`Fire Trigger (Hapbeat)` と `Stop Trigger (Hapbeat)` を、掴み開始・終了などのゲームイベントから呼びます。

## 3\. Trigger に共通する node

Collision / Sequence / Tick Emitter は `Hapbeat Trigger Component` を基底にします。component の参照を Event Graph へドラッグして、以下の node を呼びます。

| node | 入力 | 結果・使いどころ |
| --- | --- | --- |
| `Fire Trigger (Hapbeat)` | なし | 設定済み entry をそのまま再生する。 |
| `Fire Trigger With Gain (Hapbeat)` | `Gain Multiplier` | entry の Gain に呼び出しごとの倍率を掛ける。 |
| `Fire Trigger Scaled (Hapbeat)` | 入力値、入力 min / max | 値を 0〜1 へ正規化して Gain にする。衝突速度などに使う。 |
| `Fire Trigger With Curve (Hapbeat)` | 入力値、`Curve Float` | Curve の出力を Gain にする。任意の感度曲線に使う。 |
| `Stop Trigger (Hapbeat)` | なし | この trigger が開始した entry を停止する。 |
| `Set Trigger Gain Multiplier (Hapbeat)` | `Gain Multiplier` | trigger が保持する再生中 Stream Clip の Gain を即時更新する。 |
| `Set Trigger Stream Pan (Hapbeat)` | `Pan` | trigger が保持する再生中 Stream Clip の Pan を即時更新する。 |
| `Get Trigger Playback (Hapbeat)` | なし | component が開始した Stream Playback handle を返す。 |

`On Trigger Fired (Hapbeat)` は有効状態・entry 解決・cooldown の判定を通過した発火要求で発生します。SFX / VFX を同じ判定につなげられますが、デバイスへの到達や再生成功を保証する通知ではありません。

## 4\. Stream Playback node

`Play Event (Hapbeat)` が Stream Clip entry を再生したときの戻り値、または `Play Stream Clip (Hapbeat)` の戻り値を変数に保存します。各 handle は一つの logical stream source を表します。

| node | 入力 | 結果・使いどころ |
| --- | --- | --- |
| `Apply Stream Gain Modulation (Hapbeat)` | `Modulator` | authored baseline Gain に倍率を掛ける。`0` は無音、`1` は authored Gain、最大 `2`。 |
| `Set Stream Pan (Hapbeat)` | `New Pan` | `-1` 左、`0` 中央、`+1` 右へ設定する。mono Clip も送信前に stereo 化されるため、再生中に変更できる。 |
| `Set Stream Loop (Hapbeat)` | `Loop` | 再生中 stream の loop を切り替える。 |
| `Get Stream Loop (Hapbeat)` | なし | 現在の loop 設定を返す。 |
| `Stop Stream Playback (Hapbeat)` | なし | この handle の stream だけを停止する。 |
| `Get Stream Gain (Hapbeat)` | なし | 現在の最終 Gain を返す。 |
| `Get Stream Pan (Hapbeat)` | なし | 現在の Pan を返す。 |
| `Is Stream Playback Active (Hapbeat)` | なし | stream chunk を送信中なら true。 |
| `Is Stream Playback Stopped (Hapbeat)` | なし | 停止要求済み、または one-shot が終了済みなら true。 |
| `Get Stream Playback Status (Hapbeat)` | なし | `Active`、`Deferred`、`Stopped` などの状態を返す。 |
| `Get Stream Deferred Reason (Hapbeat)` | なし | `None` または `NoResolvedEndpoint` を返す。遅延秒数を返すものではない。 |

複数 source を同時再生する場合は、戻り値を source ごとに別変数へ保存します。Z4 は一つの loop handle を保存し、slider でその handle の Gain / Pan を更新します。

## 5\. 連続値と tick

### Hapbeat Parameter Binding

**用途:** 入力値を `0..1` に正規化し、curve と output range を通して、再生中 stream の Gain または Pan を更新します。

-   `Source Property = External`: Blueprint から値を渡す設定。UMG Slider に使います。
-   `Input Min` / `Input Max`: 入力範囲。
-   `Curve Type` / `Custom Curve`: 入力から出力への変換。
-   `Output Parameter`: `Stream Gain` または `Stream Pan`。
-   `Output Min` / `Output Max`: 出力範囲。
-   `Target Trigger`: 反映先の Stream Clip trigger。Actor に stream が複数ある場合は指定します。

| node | 入力 | 結果・使いどころ |
| --- | --- | --- |
| `Set Binding Input (Hapbeat)` | `Value` | External source の現在値を保存する。Slider の `On Value Changed` をここへ接続する。 |
| `Update Stream Parameter (Hapbeat)` | なし | 入力を直ちに読み取り、正規化・curve・出力変換を行って target stream に書く。戻り値は計算結果で、対象 stream がなくても返るため送信成功の判定には使わない。 |
| `Get Binding Input (Hapbeat)` | なし | 最後に読んだ生の入力値を返す。 |
| `Get Binding Normalized Input (Hapbeat)` | なし | input range 適用後の `0..1` 値を返す。 |
| `Get Binding Output (Hapbeat)` | なし | 最後に計算した出力値を返す。実機の状態や送信完了を表す値ではない。 |

### Hapbeat Tick Emitter

**用途:** 入力値が `Tick Threshold` を越えるたびに entry を一回発火します。時間間隔ではなく移動量に基づくため、素早く動かすほど tick が増えます。

-   `Tick Mode`: fixed mark を使う `Absolute Position`、または毎回の移動量を積算する `Accumulated Motion`。
-   `Tick Threshold`: 一回の tick に必要な入力差分。`0.1` なら 0.1 ごと。
-   `Axis`: Vector2D 入力で使う X / Y。
-   `bEmitOnInitialValue`: 最初の入力でも tick を鳴らすか。

| node | 入力 | 結果・使いどころ |
| --- | --- | --- |
| `Fire Tick From Value (Hapbeat)` | `Value` | 1 次元 slider / ノブ値を渡す。 |
| `Fire Tick From Vector2D (Hapbeat)` | `Value` | Vector2D の指定 axis を使う。 |
| `Fire Tick Now (Hapbeat)` | なし | 閾値判定を通さず一回発火する。 |
| `Reset Tick Reference (Hapbeat)` | なし | UI 値をプログラムから飛ばした後、不要な連続 tick を防ぐ。 |

Parameter Binding は連続した stream の値を変え、Tick Emitter は操作感を示す one-shot を鳴らします。両者は同じ slider に併用できます。

## 6\. Hapbeat Subsystem

`Get Game Instance Subsystem` で `Hapbeat Subsystem` を取得します。Event Map では扱えない event ID、Clip、Target を実行時に決める場合にだけ使います。

### 接続・直接再生

| node | 入力 | 結果・使いどころ |
| --- | --- | --- |
| `Connect (Hapbeat)` | `Port`、`App Name` | UDP socket を開き、PONG を受け取れる状態にする。 |
| `Play Event (Hapbeat)` | `Event Id`、`Gain`、`Target`、`Pan` | Kit 内の event ID を直接送信する。Event Map は経由しない。 |
| `Stop Event (Hapbeat)` | `Event Id`、`Target` | 指定 event を停止する。 |
| `Stop All Events (Hapbeat)` | `Target` | Target に一致する device 上の event を停止する。 |
| `Play Stream Clip (Hapbeat)` | `Clip`、baseline / initial Gain、`Target`、`Loop`、initial Pan | Clip を直接 stream し、source handle を返す。 |
| `Stop Streams (Hapbeat)` | なし | この subsystem が持つすべての stream source を停止する。 |

### 接続状態・診断

| node / event | 値 | 使いどころ |
| --- | --- | --- |
| `Ping (Hapbeat)` | なし | 到達可能な device を探索し、PONG を要求する。 |
| `On Connected (Hapbeat)` | なし | responsive device 数が 0 から 1 以上へ変わった時に発生。 |
| `On Disconnected (Hapbeat)` | なし | responsive device 数が 1 以上から 0 になった時に発生。 |
| `On Error (Hapbeat)` | `Message` | device が ERROR packet を返した時に発生。 |
| `On Pong (Hapbeat)` | endpoint、RTT、device name、address、firmware | PONG ごとに発生。device 一覧や RTT 表示に使う。 |
| `Is Connected (Hapbeat)` | bool | UDP socket が開いているか。device がいることは保証しない。 |
| `Get Alive Device Count (Hapbeat)` | int | PONG に応答した device 数。 |
| `Is Device Alive (Hapbeat)` | bool | 少なくとも一台が PONG に応答中か。 |
| `Is Streaming (Hapbeat)` | bool | 少なくとも一つの endpoint stream session が動作中か。 |
| `Get Active Stream Playback (Hapbeat)` | handle | 最初の active source を返す。source 固有の制御には再生時の戻り値を保存する。 |

## 7\. Target を扱う node

Target は device-addressing の論理フィルタです。Event Map entry の `Target` を作る editor 操作が通常ですが、実行時に Target を生成・検証する場合は以下を使います。

### Build Target (Hapbeat)

-   **入力:** `Player`、`Position`、`Group`。
-   **出力:** `player_1/pos_chest/group_2` 形式の Target 文字列。全て未指定なら空文字。
-   **使う場面:** player / position / group を UI や game state から組み立てるとき。

### Parse Target (Hapbeat)

-   **入力:** Target 文字列。
-   **出力:** `Player`、`Position`、`Group`。存在しない数値軸は `-1`、Position は空文字。
-   **使う場面:** 既存 Target を UI の個別フィールドへ戻すとき。

### Resolve Target (Hapbeat)

-   **入力:** authored `Target`、override `Player`、override `Group`。
-   **出力:** 指定された軸だけを置換した Target。
-   **使う場面:** Address Override と同じ規則で、送信前の実効 Target を表示・検証するとき。両 override が `-1` なら Target は変わりません。

### Apply Address Placeholders (Hapbeat)

-   **入力:** `<p>` / `<g>` を含められる app name、override `Player` / `Group`。
-   **出力:** placeholder を実効番号に置換した app name。無効軸は `-`。
-   **使う場面:** 接続時に device OLED へ HMD ごとの番号を表示するとき。

### Does Address Match (Hapbeat)

-   **入力:** 解決済み `Target`、PONG から得た `Device Address`。
-   **出力:** device が Target に一致するか。
-   **使う場面:** 独自の device 一覧・送信先診断 UI。空 Target は全 device に一致します。

## 8\. Address Override node

Address Override は全送信の Target の player / group 軸だけを上書きします。Event Map と Trigger component の設定は変えません。複数 HMD で同一 build を使う運用は[ターゲティング](./targeting-and-multi-hmd.md)を参照してください。

| node | 入力 / 出力 | 結果・使いどころ |
| --- | --- | --- |
| `Set Address Override (Hapbeat)` | `Player`、`Group`、`Persist` | 以後の Command / Stream の実効 Target を上書きする。`-1` はその軸を off にする。`Persist` は次回起動へ保存する。 |
| `Clear Saved Address Override (Hapbeat)` | なし | 保存済み設定を消し、固定されていない player / group を off にする。build の Forced Override は維持する。 |
| `Get Override Player (Hapbeat)` | int | 現在有効な player、または off の `-1` を返す。 |
| `Get Override Group (Hapbeat)` | int | 現在有効な group、または off の `-1` を返す。 |

## 9\. Samples の運用・診断 node

ここは Samples module の補助 component です。製品 UI には必要な機能だけを Unreal 標準 UI と `Hapbeat Subsystem` で実装できます。

### Hapbeat Address Override Panel

| node | 入力 / 出力 | 結果・使いどころ |
| --- | --- | --- |
| `Show Address Panel (Hapbeat)` | なし | Address Override panel を viewport に表示する。 |
| `Attach Address Panel (Hapbeat)` | `Widget Component` | 同じ panel を world-space Widget Component へ表示する。VR 用。 |
| `Hide Address Panel (Hapbeat)` | なし | viewport / world-space の panel を閉じる。 |
| `Toggle Address Panel (Hapbeat)` | なし | viewport panel の表示・非表示を切り替える。 |
| `Is Address Panel Shown (Hapbeat)` | bool | panel が表示中かを返す。 |

Panel の `Apply` は `Set Address Override (Hapbeat)`、`Clear` は `Clear Saved Address Override (Hapbeat)`、`Test` は現在の実効 Target へ sample event を送ります。VR での使い方は[VR Config Example](./vr-config-example.md)を参照してください。

### Hapbeat Status Overlay

| node | 入力 / 出力 | 結果・使いどころ |
| --- | --- | --- |
| `Log Status Message (Hapbeat)` | `Message` | on-screen status log に一行追加する。 |
| `Clear Status Log (Hapbeat)` | なし | status log を消去する。 |

この component は connected / disconnected / PONG / error / stream transition も自動表示します。shipping UI の代替ではなく、開発時の状態確認用です。

### Hapbeat Event Logger

| node | 記録するゲームイベント |
| --- | --- |
| `Log Event (Hapbeat)` | 任意のイベント名 |
| `Log Begin Overlap (Hapbeat)` / `Log End Overlap (Hapbeat)` | overlap の開始 / 終了 |
| `Log Hit (Hapbeat)` | hit |
| `Log Clicked (Hapbeat)` / `Log Released (Hapbeat)` | click / release |
| `Log Begin Cursor Over (Hapbeat)` / `Log End Cursor Over (Hapbeat)` | cursor hover の開始 / 終了 |
| `Log Grabbed (Hapbeat)` / `Log Dropped (Hapbeat)` | grab / drop |
| `Log Activated (Hapbeat)` / `Log Deactivated (Hapbeat)` | activate / deactivate |

いずれも Output Log と sample の表示へ記録するだけで、触覚を送信しません。ゲームイベントが発生しているかを先に切り分けるための node です。

## 10\. Data Asset の補助 node

| asset | node | 結果 |
| --- | --- | --- |
| `Hapbeat Event Map` | `Find Event Entry (Hapbeat)` | GUID から `Hapbeat Event Entry` のコピーを `Out Entry` に返す。戻り値 bool は検索成功。Entry Ref ではなく、コピーの編集は asset に反映されない。 |
| `Hapbeat Clip` | `Get Clip Sample Count (Hapbeat)` | PCM sample 数を返す。 |
| `Hapbeat Clip` | `Get Clip Frame Count (Hapbeat)` | frame 数を返す。 |
| `Hapbeat Clip` | `Get Clip Duration (Hapbeat)` | Clip の秒数を返す。 |

通常は Event Map Editor の entry picker を使うため、これらを Event Graph で呼ぶのは asset を動的に選ぶ場合に限られます。

## 実装の参照先

-   [Blueprint Library](https://github.com/hapbeat/hapbeat-unreal-sdk/blob/master/Source/HapbeatSDK/Public/HapbeatBlueprintLibrary.h)
-   [Trigger / Collision / Sequence](https://github.com/hapbeat/hapbeat-unreal-sdk/tree/master/Source/HapbeatSDK/Public)
-   [Stream Playback](https://github.com/hapbeat/hapbeat-unreal-sdk/blob/master/Source/HapbeatSDK/Public/HapbeatStreamPlayback.h)
-   [Parameter Binding](https://github.com/hapbeat/hapbeat-unreal-sdk/blob/master/Source/HapbeatSDK/Public/HapbeatParameterBinding.h)
-   [Tick Emitter](https://github.com/hapbeat/hapbeat-unreal-sdk/blob/master/Source/HapbeatSDK/Public/HapbeatTickEmitterComponent.h)
-   [Hapbeat Subsystem](https://github.com/hapbeat/hapbeat-unreal-sdk/blob/master/Source/HapbeatSDK/Public/HapbeatSubsystem.h)

<!-- api-index:start -->
## ノードの宣言索引

公開する全 `BlueprintCallable` / `BlueprintPure` 関数です。同名ノードは所有クラスで区別します。引数の型・既定値・戻り値は宣言どおりです。非 const 参照引数は出力ピン、`WorldContextObject` は通常自動入力です。component のメンバー関数はその component を Target に渡します。使いどころと動作上の制約は上の機能別説明を参照してください。

### Play Event (Hapbeat) — UHapbeatBlueprintLibrary.PlayHapbeatEvent

所有: `HapbeatSDK / HapbeatBlueprintLibrary.h`。Callable（実行ピンあり）。

```cpp
static UHapbeatStreamPlayback* PlayHapbeatEvent(const UObject* WorldContextObject, UHapbeatEventMap* Map, FHapbeatEntryRef Entry, float GainMultiplier = 1.0f, float Pan = 0.0f, float DelaySeconds = 0.0f);
```

### Stop Event (Hapbeat) — UHapbeatBlueprintLibrary.StopHapbeatEvent

所有: `HapbeatSDK / HapbeatBlueprintLibrary.h`。Callable（実行ピンあり）。

```cpp
static void StopHapbeatEvent(const UObject* WorldContextObject, UHapbeatEventMap* Map, FHapbeatEntryRef Entry);
```

### Fire Tick From Value (Hapbeat) — UHapbeatBlueprintLibrary.FireHapbeatTickFromValue

所有: `HapbeatSDK / HapbeatBlueprintLibrary.h`。Callable（実行ピンあり）。

```cpp
static void FireHapbeatTickFromValue(UHapbeatTickEmitterComponent* TickEmitter, float Value);
```

### Get Clip Sample Count (Hapbeat) — UHapbeatClip.NumSamples

所有: `HapbeatSDK / HapbeatClip.h`。Pure（実行ピンなし）。

```cpp
int32 NumSamples() const;
```

### Get Clip Frame Count (Hapbeat) — UHapbeatClip.NumFrames

所有: `HapbeatSDK / HapbeatClip.h`。Pure（実行ピンなし）。

```cpp
int32 NumFrames() const;
```

### Get Clip Duration (Hapbeat) — UHapbeatClip.DurationSeconds

所有: `HapbeatSDK / HapbeatClip.h`。Pure（実行ピンなし）。

```cpp
float DurationSeconds() const;
```

### Find Event Entry (Hapbeat) — UHapbeatEventMap.FindById

所有: `HapbeatSDK / HapbeatEventMap.h`。Pure（実行ピンなし）。

```cpp
bool FindById(FGuid Id, FHapbeatEventEntry& OutEntry) const;
```

### Set Binding Input (Hapbeat) — UHapbeatParameterBinding.SetValue

所有: `HapbeatSDK / HapbeatParameterBinding.h`。Callable（実行ピンあり）。

```cpp
void SetValue(float Value);
```

### Update Stream Parameter (Hapbeat) — UHapbeatParameterBinding.EvaluateNow

所有: `HapbeatSDK / HapbeatParameterBinding.h`。Callable（実行ピンあり）。

```cpp
float EvaluateNow();
```

### Get Binding Input (Hapbeat) — UHapbeatParameterBinding.GetCurrentInput

所有: `HapbeatSDK / HapbeatParameterBinding.h`。Pure（実行ピンなし）。

```cpp
float GetCurrentInput() const;
```

### Get Binding Normalized Input (Hapbeat) — UHapbeatParameterBinding.GetCurrentNormalized

所有: `HapbeatSDK / HapbeatParameterBinding.h`。Pure（実行ピンなし）。

```cpp
float GetCurrentNormalized() const;
```

### Get Binding Output (Hapbeat) — UHapbeatParameterBinding.GetCurrentOutput

所有: `HapbeatSDK / HapbeatParameterBinding.h`。Pure（実行ピンなし）。

```cpp
float GetCurrentOutput() const;
```

### Apply Stream Gain Modulation (Hapbeat) — UHapbeatStreamPlayback.ApplyGainModulation

所有: `HapbeatSDK / HapbeatStreamPlayback.h`。Callable（実行ピンあり）。

```cpp
void ApplyGainModulation(float Modulator);
```

### Set Stream Pan (Hapbeat) — UHapbeatStreamPlayback.SetPan

所有: `HapbeatSDK / HapbeatStreamPlayback.h`。Callable（実行ピンあり）。

```cpp
void SetPan(float NewPan);
```

### Set Stream Loop (Hapbeat) — UHapbeatStreamPlayback.SetLoop

所有: `HapbeatSDK / HapbeatStreamPlayback.h`。Callable（実行ピンあり）。

```cpp
void SetLoop(bool bNewLoop);
```

### Get Stream Loop (Hapbeat) — UHapbeatStreamPlayback.GetLoop

所有: `HapbeatSDK / HapbeatStreamPlayback.h`。Pure（実行ピンなし）。

```cpp
bool GetLoop() const;
```

### Stop Stream Playback (Hapbeat) — UHapbeatStreamPlayback.Stop

所有: `HapbeatSDK / HapbeatStreamPlayback.h`。Callable（実行ピンあり）。

```cpp
void Stop();
```

### Get Stream Gain (Hapbeat) — UHapbeatStreamPlayback.GetGain

所有: `HapbeatSDK / HapbeatStreamPlayback.h`。Pure（実行ピンなし）。

```cpp
float GetGain() const;
```

### Get Stream Pan (Hapbeat) — UHapbeatStreamPlayback.GetPan

所有: `HapbeatSDK / HapbeatStreamPlayback.h`。Pure（実行ピンなし）。

```cpp
float GetPan() const;
```

### Is Stream Playback Stopped (Hapbeat) — UHapbeatStreamPlayback.IsStopped

所有: `HapbeatSDK / HapbeatStreamPlayback.h`。Pure（実行ピンなし）。

```cpp
bool IsStopped() const;
```

### Is Stream Playback Active (Hapbeat) — UHapbeatStreamPlayback.IsActive

所有: `HapbeatSDK / HapbeatStreamPlayback.h`。Pure（実行ピンなし）。

```cpp
bool IsActive() const;
```

### Get Stream Playback Status (Hapbeat) — UHapbeatStreamPlayback.GetStatus

所有: `HapbeatSDK / HapbeatStreamPlayback.h`。Pure（実行ピンなし）。

```cpp
EHapbeatStreamPlaybackStatus GetStatus() const;
```

### Get Stream Deferred Reason (Hapbeat) — UHapbeatStreamPlayback.GetDeferredReason

所有: `HapbeatSDK / HapbeatStreamPlayback.h`。Pure（実行ピンなし）。

```cpp
EHapbeatStreamDeferredReason GetDeferredReason() const;
```

### Connect (Hapbeat) — UHapbeatSubsystem.Connect

所有: `HapbeatSDK / HapbeatSubsystem.h`。Callable（実行ピンあり）。

```cpp
void Connect(int32 InPort = 7700, const FString& InAppName = TEXT(""));
```

### Play Event (Hapbeat) — UHapbeatSubsystem.Play

所有: `HapbeatSDK / HapbeatSubsystem.h`。Callable（実行ピンあり）。

```cpp
void Play(const FString& EventId, float Gain = 1.0f, const FString& Target = TEXT(""), float Pan = 0.0f);
```

### Stop Event (Hapbeat) — UHapbeatSubsystem.Stop

所有: `HapbeatSDK / HapbeatSubsystem.h`。Callable（実行ピンあり）。

```cpp
void Stop(const FString& EventId, const FString& Target = TEXT(""));
```

### Stop All Events (Hapbeat) — UHapbeatSubsystem.StopAll

所有: `HapbeatSDK / HapbeatSubsystem.h`。Callable（実行ピンあり）。

```cpp
void StopAll(const FString& Target = TEXT(""));
```

### Ping (Hapbeat) — UHapbeatSubsystem.Ping

所有: `HapbeatSDK / HapbeatSubsystem.h`。Callable（実行ピンあり）。

```cpp
void Ping();
```

### Play Stream Clip (Hapbeat) — UHapbeatSubsystem.StreamClip

所有: `HapbeatSDK / HapbeatSubsystem.h`。Callable（実行ピンあり）。

```cpp
UHapbeatStreamPlayback* StreamClip(UHapbeatClip* Clip, float BaselineGain = 1.0f, float InitialGain = 1.0f, const FString& Target = TEXT(""), bool bLoop = false, float InitialPan = 0.0f);
```

### Stop Streams (Hapbeat) — UHapbeatSubsystem.StopStream

所有: `HapbeatSDK / HapbeatSubsystem.h`。Callable（実行ピンあり）。

```cpp
void StopStream();
```

### Set Address Override (Hapbeat) — UHapbeatSubsystem.SetAddressOverride

所有: `HapbeatSDK / HapbeatSubsystem.h`。Callable（実行ピンあり）。

```cpp
void SetAddressOverride(int32 Player, int32 InGroup, bool bPersist = false);
```

### Clear Saved Address Override (Hapbeat) — UHapbeatSubsystem.ClearPersistedAddressOverride

所有: `HapbeatSDK / HapbeatSubsystem.h`。Callable（実行ピンあり）。

```cpp
void ClearPersistedAddressOverride();
```

### Get Override Player (Hapbeat) — UHapbeatSubsystem.GetOverridePlayer

所有: `HapbeatSDK / HapbeatSubsystem.h`。Pure（実行ピンなし）。

```cpp
int32 GetOverridePlayer() const;
```

### Get Override Group (Hapbeat) — UHapbeatSubsystem.GetOverrideGroup

所有: `HapbeatSDK / HapbeatSubsystem.h`。Pure（実行ピンなし）。

```cpp
int32 GetOverrideGroup() const;
```

### Is Connected (Hapbeat) — UHapbeatSubsystem.IsConnected

所有: `HapbeatSDK / HapbeatSubsystem.h`。Pure（実行ピンなし）。

```cpp
bool IsConnected() const;
```

### Get Alive Device Count (Hapbeat) — UHapbeatSubsystem.GetAliveDeviceCount

所有: `HapbeatSDK / HapbeatSubsystem.h`。Pure（実行ピンなし）。

```cpp
int32 GetAliveDeviceCount() const;
```

### Is Device Alive (Hapbeat) — UHapbeatSubsystem.IsAlive

所有: `HapbeatSDK / HapbeatSubsystem.h`。Pure（実行ピンなし）。

```cpp
bool IsAlive() const;
```

### Is Streaming (Hapbeat) — UHapbeatSubsystem.IsStreaming

所有: `HapbeatSDK / HapbeatSubsystem.h`。Pure（実行ピンなし）。

```cpp
bool IsStreaming() const;
```

### Get Active Stream Playback (Hapbeat) — UHapbeatSubsystem.GetActivePlayback

所有: `HapbeatSDK / HapbeatSubsystem.h`。Pure（実行ピンなし）。

```cpp
UHapbeatStreamPlayback* GetActivePlayback() const;
```

### Build Target (Hapbeat) — UHapbeatTargetLibrary.BuildTarget

所有: `HapbeatSDK / HapbeatTargetLibrary.h`。Callable（実行ピンあり）。

```cpp
static FString BuildTarget(int32 Player = -1, const FString& Position = TEXT(""), int32 Group = -1);
```

### Parse Target (Hapbeat) — UHapbeatTargetLibrary.ParseTarget

所有: `HapbeatSDK / HapbeatTargetLibrary.h`。Callable（実行ピンあり）。

```cpp
static void ParseTarget(const FString& Target, int32& OutPlayer, FString& OutPosition, int32& OutGroup);
```

### Resolve Target (Hapbeat) — UHapbeatTargetLibrary.ResolveTarget

所有: `HapbeatSDK / HapbeatTargetLibrary.h`。Callable（実行ピンあり）。

```cpp
static FString ResolveTarget(const FString& Target, int32 OverridePlayer, int32 OverrideGroup);
```

### Apply Address Placeholders (Hapbeat) — UHapbeatTargetLibrary.ApplyAddressPlaceholders

所有: `HapbeatSDK / HapbeatTargetLibrary.h`。Callable（実行ピンあり）。

```cpp
static FString ApplyAddressPlaceholders(const FString& AppName, int32 OverridePlayer, int32 OverrideGroup);
```

### Does Address Match (Hapbeat) — UHapbeatTargetLibrary.AddressMatches

所有: `HapbeatSDK / HapbeatTargetLibrary.h`。Callable（実行ピンあり）。

```cpp
static bool AddressMatches(const FString& Target, const FString& DeviceAddress);
```

### Fire Tick From Value (Hapbeat) — UHapbeatTickEmitterComponent.FireFromValue

所有: `HapbeatSDK / HapbeatTickEmitterComponent.h`。Callable（実行ピンあり）。

```cpp
void FireFromValue(float Value);
```

### Fire Tick From Vector2D (Hapbeat) — UHapbeatTickEmitterComponent.FireFromVector2D

所有: `HapbeatSDK / HapbeatTickEmitterComponent.h`。Callable（実行ピンあり）。

```cpp
void FireFromVector2D(FVector2D Value);
```

### Fire Tick Now (Hapbeat) — UHapbeatTickEmitterComponent.FireNow

所有: `HapbeatSDK / HapbeatTickEmitterComponent.h`。Callable（実行ピンあり）。

```cpp
void FireNow();
```

### Reset Tick Reference (Hapbeat) — UHapbeatTickEmitterComponent.ResetReference

所有: `HapbeatSDK / HapbeatTickEmitterComponent.h`。Callable（実行ピンあり）。

```cpp
void ResetReference();
```

### Fire Trigger (Hapbeat) — UHapbeatTriggerComponent.Fire

所有: `HapbeatSDK / HapbeatTriggerComponent.h`。Callable（実行ピンあり）。

```cpp
virtual void Fire();
```

### Fire Trigger With Gain (Hapbeat) — UHapbeatTriggerComponent.FireWithGain

所有: `HapbeatSDK / HapbeatTriggerComponent.h`。Callable（実行ピンあり）。

```cpp
void FireWithGain(float GainOverride);
```

### Fire Trigger Scaled (Hapbeat) — UHapbeatTriggerComponent.FireScaled

所有: `HapbeatSDK / HapbeatTriggerComponent.h`。Callable（実行ピンあり）。

```cpp
void FireScaled(float Velocity, float MinVelocity = 0.0f, float MaxVelocity = 10.0f);
```

### Fire Trigger With Curve (Hapbeat) — UHapbeatTriggerComponent.FireWithCurve

所有: `HapbeatSDK / HapbeatTriggerComponent.h`。Callable（実行ピンあり）。

```cpp
void FireWithCurve(float Value, UCurveFloat* Curve);
```

### Stop Trigger (Hapbeat) — UHapbeatTriggerComponent.Stop

所有: `HapbeatSDK / HapbeatTriggerComponent.h`。Callable（実行ピンあり）。

```cpp
virtual void Stop();
```

### Set Trigger Gain Multiplier (Hapbeat) — UHapbeatTriggerComponent.SetGainMultiplier

所有: `HapbeatSDK / HapbeatTriggerComponent.h`。Callable（実行ピンあり）。

```cpp
void SetGainMultiplier(float NewMultiplier);
```

### Set Trigger Stream Pan (Hapbeat) — UHapbeatTriggerComponent.SetStreamPan

所有: `HapbeatSDK / HapbeatTriggerComponent.h`。Callable（実行ピンあり）。

```cpp
void SetStreamPan(float NewPan);
```

### Get Trigger Playback (Hapbeat) — UHapbeatTriggerComponent.GetActivePlayback

所有: `HapbeatSDK / HapbeatTriggerComponent.h`。Pure（実行ピンなし）。

```cpp
UHapbeatStreamPlayback* GetActivePlayback() const;
```

### Show Address Panel (Hapbeat) — UHapbeatAddressOverridePanelComponent.Show

所有: `HapbeatSDKSamples / HapbeatAddressOverridePanelComponent.h`。Callable（実行ピンあり）。

```cpp
void Show();
```

### Attach Address Panel (Hapbeat) — UHapbeatAddressOverridePanelComponent.AttachToWidgetComponent

所有: `HapbeatSDKSamples / HapbeatAddressOverridePanelComponent.h`。Callable（実行ピンあり）。

```cpp
void AttachToWidgetComponent(UWidgetComponent* Target);
```

### Hide Address Panel (Hapbeat) — UHapbeatAddressOverridePanelComponent.Hide

所有: `HapbeatSDKSamples / HapbeatAddressOverridePanelComponent.h`。Callable（実行ピンあり）。

```cpp
void Hide();
```

### Toggle Address Panel (Hapbeat) — UHapbeatAddressOverridePanelComponent.Toggle

所有: `HapbeatSDKSamples / HapbeatAddressOverridePanelComponent.h`。Callable（実行ピンあり）。

```cpp
void Toggle();
```

### Is Address Panel Shown (Hapbeat) — UHapbeatAddressOverridePanelComponent.IsShown

所有: `HapbeatSDKSamples / HapbeatAddressOverridePanelComponent.h`。Pure（実行ピンなし）。

```cpp
bool IsShown() const;
```

### Move Address Panel Focus (Hapbeat) — UHapbeatAddressOverridePanelComponent.MoveFocus

所有: `HapbeatSDKSamples / HapbeatAddressOverridePanelComponent.h`。Callable（実行ピンあり）。

```cpp
void MoveFocus(int32 Horizontal, int32 Vertical);
```

### Activate Address Panel Focus (Hapbeat) — UHapbeatAddressOverridePanelComponent.ActivateFocused

所有: `HapbeatSDKSamples / HapbeatAddressOverridePanelComponent.h`。Callable（実行ピンあり）。

```cpp
void ActivateFocused();
```

### Show Address Panel Focus (Hapbeat) — UHapbeatAddressOverridePanelComponent.ShowFocusHighlight

所有: `HapbeatSDKSamples / HapbeatAddressOverridePanelComponent.h`。Callable（実行ピンあり）。

```cpp
void ShowFocusHighlight();
```

### Log Event (Hapbeat) — UHapbeatEventLoggerComponent.LogEvent

所有: `HapbeatSDKSamples / HapbeatEventLoggerComponent.h`。Callable（実行ピンあり）。

```cpp
void LogEvent(const FString& Tag);
```

### Log Begin Overlap (Hapbeat) — UHapbeatEventLoggerComponent.LogBeginOverlap

所有: `HapbeatSDKSamples / HapbeatEventLoggerComponent.h`。Callable（実行ピンあり）。

```cpp
void LogBeginOverlap();
```

### Log End Overlap (Hapbeat) — UHapbeatEventLoggerComponent.LogEndOverlap

所有: `HapbeatSDKSamples / HapbeatEventLoggerComponent.h`。Callable（実行ピンあり）。

```cpp
void LogEndOverlap();
```

### Log Hit (Hapbeat) — UHapbeatEventLoggerComponent.LogHit

所有: `HapbeatSDKSamples / HapbeatEventLoggerComponent.h`。Callable（実行ピンあり）。

```cpp
void LogHit();
```

### Log Clicked (Hapbeat) — UHapbeatEventLoggerComponent.LogClicked

所有: `HapbeatSDKSamples / HapbeatEventLoggerComponent.h`。Callable（実行ピンあり）。

```cpp
void LogClicked();
```

### Log Released (Hapbeat) — UHapbeatEventLoggerComponent.LogReleased

所有: `HapbeatSDKSamples / HapbeatEventLoggerComponent.h`。Callable（実行ピンあり）。

```cpp
void LogReleased();
```

### Log Begin Cursor Over (Hapbeat) — UHapbeatEventLoggerComponent.LogBeginCursorOver

所有: `HapbeatSDKSamples / HapbeatEventLoggerComponent.h`。Callable（実行ピンあり）。

```cpp
void LogBeginCursorOver();
```

### Log End Cursor Over (Hapbeat) — UHapbeatEventLoggerComponent.LogEndCursorOver

所有: `HapbeatSDKSamples / HapbeatEventLoggerComponent.h`。Callable（実行ピンあり）。

```cpp
void LogEndCursorOver();
```

### Log Grabbed (Hapbeat) — UHapbeatEventLoggerComponent.LogGrabbed

所有: `HapbeatSDKSamples / HapbeatEventLoggerComponent.h`。Callable（実行ピンあり）。

```cpp
void LogGrabbed();
```

### Log Dropped (Hapbeat) — UHapbeatEventLoggerComponent.LogDropped

所有: `HapbeatSDKSamples / HapbeatEventLoggerComponent.h`。Callable（実行ピンあり）。

```cpp
void LogDropped();
```

### Log Activated (Hapbeat) — UHapbeatEventLoggerComponent.LogActivated

所有: `HapbeatSDKSamples / HapbeatEventLoggerComponent.h`。Callable（実行ピンあり）。

```cpp
void LogActivated();
```

### Log Deactivated (Hapbeat) — UHapbeatEventLoggerComponent.LogDeactivated

所有: `HapbeatSDKSamples / HapbeatEventLoggerComponent.h`。Callable（実行ピンあり）。

```cpp
void LogDeactivated();
```

### Log Status Message (Hapbeat) — UHapbeatStatusOverlayComponent.Log

所有: `HapbeatSDKSamples / HapbeatStatusOverlayComponent.h`。Callable（実行ピンあり）。

```cpp
void Log(const FString& Message);
```

### Clear Status Log (Hapbeat) — UHapbeatStatusOverlayComponent.ClearLog

所有: `HapbeatSDKSamples / HapbeatStatusOverlayComponent.h`。Callable（実行ピンあり）。

```cpp
void ClearLog();
```
<!-- api-index:end -->
