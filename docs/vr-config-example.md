---
sidebar:
  order: 5
  label: VR Config Example
---

# VR Config Example

`VRConfigExample` は、VR 内で Address Override を設定し、選択した Hapbeat へ 100 Hz のテスト波形を送るサンプルです。

## 開く

1. プラグインの `Scripts/generate_vr_config_input_assets.py` を一度実行し、Editor を再起動します。これは OpenXR が VR controller の Input Mapping Context を VR session 開始前に登録するために必要です。
2. Content Browser の Settings から **Show Plugin Content** を有効にし、`Plugins/HapbeatSDK/Content/HapbeatSamples/VRConfigExample/Maps/VRConfigExample` を開きます。

VR Preview を開始すると、HMD 前方約 1.9 m に幅約 1 m の設定パネルが表示されます。

## 操作

パネルには起動時から黄色の選択カーソルがあります。コントローラーの位置やレイは使いません。

| 操作 | 結果 |
| --- | --- |
| 左右どちらかのスティック | 倒した方向へ選択カーソルを移動する。押し続けると一定間隔で移動を繰り返す。 |
| 左右どちらかのトリガー | 現在の選択を決定する。 |
| `Player` / `Group` | 送信先の player / group を選ぶ |
| `Apply` | 選択値を Address Override として適用する |
| `Play` | 適用済み Target に 100 Hz の StreamClip を再生する。端末への Kit インストールは不要。 |
| `Exit` | パネルを閉じる。`P` で再表示できる。 |
| 左右どちらかのスティック押し込み | パネルを正面へ戻す |

同じ build を複数の HMD へ配るときは、各 HMD で `Player` または `Group` を変えて `Apply` します。Address Override の仕組みは[宛先と複数 HMD](./targeting-and-multi-hmd.md)を参照してください。

100 Hz を再生するには、スティックで `Play` を選び、左右どちらかのトリガーを引きます。トリガーは現在選択中のボタンを決定するため、ほかのボタンを選択中はそのボタンの操作になります。
