---
kind: howto
sidebar:
  order: 2
  label: Event Map と再生
---

# Event Map と再生

Event Map は、再生する触覚刺激のパラメータ（Clip・Gain・Target・Loop）を一元管理する Data Asset です。ゲーム実装は「いつ、どの entry を再生するか」だけを指定し、触覚の種類・強さ・宛先・継続方法は Event Map で調整します。

## Event Map を作る

1.  Content Browser で **Add → Hapbeat → Hapbeat Event Map** を選びます。
2.  asset を開き、entry を追加します。
3.  `Display Name` と再生方法を設定します。`Stream Clip` は Clip asset、`Command` は配備済み Kit の `Category` と `Event Name` を指定します。必要に応じて `Gain`、`Pan`、`Target`、`Loop` を調整します。

各 entry は次の内容を持ちます。

-   `Id`: 自動発行される GUID。entry の並べ替え後も参照を保つ識別子で、手入力は不要です。
-   `Clip`: 再生する触覚クリップ、または device に配備済みの event
-   `Gain`: 基本の強さ
-   `Pan`: 左右の定位
-   `Target`: 送信対象となる Hapbeat の論理アドレス
-   `Loop`: Stream Clip を継続再生するか（Command の繰り返しは Kit 側の定義によります）

`Target` が空なら接続済みの全 Hapbeat が候補になります。player / group を指定して複数 HMD の送信先を分ける方法は[ターゲティング](./targeting-and-multi-hmd.md)を参照してください。

## Blueprint から再生する

Event Graph で `Play Event (Hapbeat)` を追加し、`Map` と `Entry` を指定します。

```text
ゲームイベント
  → Play Event (Hapbeat)
      Map: DA_HapbeatEventMap
      Entry: pickup
```

`Gain Multiplier`、`Pan`、`Delay Seconds` は詳細ピンです。entry の設定を変更せず、その呼び出しだけを補正します。`Stream Clip` entry の戻り値は `Hapbeat Stream Playback` で、Gain / Pan / Stop を実行時に操作できます。

node の入力と戻り値は[Blueprint ノード](./blueprint-nodes.md#1-event-map-%E3%82%92%E5%86%8D%E7%94%9F%E3%81%99%E3%82%8B-node)を参照してください。

## C++ から再生する

`UHapbeatBlueprintLibrary` は Blueprint node と同じ Event Map 経路を C++ に公開します。

```cpp
#include "HapbeatBlueprintLibrary.h"


UHapbeatBlueprintLibrary::PlayHapbeatEvent(
    this,
    EventMap,
    Entry,
    /* GainMultiplier */ 1.0f,
    /* Pan */ 0.0f,
    /* DelaySeconds */ 0.0f);
```

`UHapbeatSubsystem::Play(EventId, Gain, Target, Pan)` は Event Map を経由しない直接送信です。実行時に event ID を組み立てるなど、Data Asset で管理できない場合だけ使います。

C++ の API と直接送信との使い分けは[C++ API](./cpp-api.md)を参照してください。
## 再生方法を選ぶ

| 目的 | 使うもの |
| --- | --- |
| ボタン・UI・独自ゲームイベントから entry を 1 回再生 | `Play Event (Hapbeat)` |
| Hit / Overlap を監視して再生 | `Hapbeat Collision Trigger` component |
| 開始・保持 loop・終了をまとめる | `Hapbeat Sequence` component |
| Stream Clip の Gain / Pan を連続値で変える | `Hapbeat Parameter Binding` component |
| ノブや slider の目盛りに one-shot を鳴らす | `Hapbeat Tick Emitter` component |
