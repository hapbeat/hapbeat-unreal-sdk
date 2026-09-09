---
sidebar:
  order: 2
  label: Event Map と再生
---

# Event Map と再生

Event Map は、ゲーム側の「いつ鳴らすか」と、触覚側の Clip・Gain・Target・Loop を分ける Data Asset です。ゲーム実装は entry を指定して再生し、触覚の調整は Event Map で行います。

## Event Map を作る

1. Content Browser で **Add → Miscellaneous → Data Asset** を選び、`Hapbeat Event Map` を作成します。
2. asset を開き、entry を追加します。
3. entry の `Id`、`Clip`、`Gain`、`Pan`、`Target`、`Loop` を設定します。

`Target` が空なら接続済みの全 Hapbeat が候補になります。特定の player / group へ送る方法は[宛先と複数 HMD](./targeting-and-multi-hmd.md)を参照してください。

## Blueprint から再生する

Event Graph で `Play Event (Hapbeat)` を追加し、`Map` と `Entry` を指定します。

```text
ゲームイベント
  → Play Event (Hapbeat)
      Map: DA_HapbeatEventMap
      Entry: pickup
```

`Gain Multiplier`、`Pan`、`Delay Seconds` は詳細ピンです。entry の設定を変更せず、その呼び出しだけを補正します。`Stream Clip` entry の戻り値は `Hapbeat Stream Playback` で、Gain / Pan / Stop を実行時に操作できます。

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

## 再生方法を選ぶ

| 目的 | 使うもの |
| --- | --- |
| ボタン・UI・独自ゲームイベントから entry を 1 回再生 | `Play Event (Hapbeat)` |
| Hit / Overlap を監視して再生 | `Hapbeat Collision Trigger` component |
| 開始・保持 loop・終了をまとめる | `Hapbeat Sequence` component |
| Stream Clip の Gain / Pan を連続値で変える | `Hapbeat Parameter Binding` component |
| ノブや slider の目盛りに one-shot を鳴らす | `Hapbeat Tick Emitter` component |

各 node の pin、component の設定、直接送信 API は[Blueprint ノード](./blueprint-nodes.md)で参照できます。
