---
sidebar:
  order: 5
  label: VR Config Example
---

# VR Config Example

`VRConfigExample` は、VR 内で Address Override を設定し、選択した Hapbeat へ Test event を送るサンプルです。

## 開く

Content Browser の Settings から **Show Plugin Content** を有効にし、`Plugins/HapbeatSDK/Content/HapbeatSamples/VRConfigExample/Maps/VRConfigExample` を開きます。VR Preview を開始すると、HMD 前方に設定パネルが表示されます。

## 操作

右手コントローラーのレイをパネルに向け、トリガーで操作します。

| 操作 | 結果 |
| --- | --- |
| `Player` / `Group` | 送信先の player / group を選ぶ |
| `Apply` | 選択値を Address Override として適用する |
| `Test` | 適用済み Target に `sample-kit.sine_100hz` を再生する |
| `Clear` | 保存済み Address Override を消し、player / group を off に戻す |
| 右スティック押し込み | パネルを正面へ戻す |

同じ build を複数の HMD へ配るときは、各 HMD で `Player` または `Group` を変えて `Apply` します。Address Override の仕組みは[宛先と複数 HMD](./targeting-and-multi-hmd.md)を参照してください。
