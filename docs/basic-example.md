---
kind: tutorial
sidebar:
  order: 2
  label: BasicExample
---

# BasicExample（サンプルレベル）

プラグインに同梱する `BasicExample` レベルです。Hapbeat の 2 つの再生方法、Stream と Fire（Command）を最小構成で試せます。導入と最初の再生は [Getting Started](./getting-started.md) を参照してください。

## 開く

Content Browser の Settings から **Show Plugin Content** を有効にし、`Plugins/HapbeatSDK/Content/HapbeatSamples/BasicExample/Maps/BasicExample` を開きます。

## 操作

| 入力 | 再生・操作 | Kit のデプロイ |
| --- | --- | --- |
| `Space` | 100 Hz の Stream を 1 回再生 | 不要 |
| `R` | 100 Hz の Stream を loop 再生 | 不要 |
| `F` | `basic-exam-kit.sine_200hz_1s` を Fire（Command）で再生 | 必要 |
| `S` | Stream と Fire をすべて停止 | 不要 |
| `C` | Hapbeat を ping する | 不要 |

`F` を試す前に、`Plugins/HapbeatSDK/Content/HapbeatSamples/BasicExample/Kit/basic-exam-kit/` を Hapbeat Studio で開き、接続した Hapbeat へ Deploy します。`Space` は raw PCM の Stream を送るため、Kit を配置しなくても再生できます。

## 構成

World Outliner の `HapbeatBasicExampleActor` が `EM_BasicExample` を参照します。Actor 内の Trigger が入力を受け、Event Map entry を通じて Stream または Fire を送ります。

```text
入力
  → HapbeatBasicExampleActor の Trigger
  → EM_BasicExample
  → Hapbeat SDK
```
