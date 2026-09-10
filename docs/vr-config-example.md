---
kind: howto
sidebar:
  order: 5
  label: VR Config Example
---

# VR Config Example（サンプルレベル）

プラグインに同梱する `VRConfigExample` レベルです。VR 内で Address Override を設定し、選択した Hapbeat へ 100 Hz のテスト波形を送るサンプルです。

## 開く

1.  **Edit → Plugins** で `OpenXR` と `Python Editor Script Plugin` を有効にし、Editor を再起動します。
2.  **Tools → Execute Python Script** から `Plugins/HapbeatSDK/Scripts/generate_vr_config_input_assets.py` を実行し、完了後に Editor を再起動します。入力 asset と `DefaultInput.ini` をプロジェクトに設定する初回のみの手順です。
3.  Content Browser の Settings から **Show Plugin Content** を有効にし、`Plugins/HapbeatSDK/Content/HapbeatSamples/VRConfigExample/Maps/VRConfigExample` を開きます。
4.  Quest Link / Air Link で HMD を PC に接続し、Play のメニューから **VR Preview** を選びます。

VR Preview を開始すると、HMD 前方約 1.9 m に幅約 1 m の設定パネルが表示されます。

:::note[Quest 単体で実行する場合]
Android 向けのビルド設定に加え、Packaging の Maps to Include に `VRConfigExample` と、使用する `Return Level` を含めます。上のスクリプトで作成した `Content/HapbeatVRConfig/Input` と `Config/DefaultInput.ini` もプロジェクトに保持します。HMD と Hapbeat は同じネットワークへ接続してください。VR Preview の成功だけでは Android パッケージの動作確認にはなりません。
:::

## 操作

パネルには起動時から黄色の選択カーソルがあります。コントローラーの位置やレイは使いません。

| 操作 | 結果 |
| --- | --- |
| 左右どちらかのスティック | 倒した方向へ選択カーソルを移動する。押し続けると一定間隔で移動を繰り返す。 |
| 左右どちらかのトリガー | 現在の選択を決定する。 |
| `Player` / `Group` | 送信先の player / group を選ぶ |
| `Apply` | 選択値を Address Override として適用する |
| `Play` | 適用済み Target に 100 Hz の StreamClip を再生する。端末への Kit インストールは不要。 |
| `Exit` | Actor の `Return Level` が指定されていればそのレベルへ遷移する。未指定ならパネルを閉じ、`P` で再表示できる。 |
| 左右どちらかのスティック押し込み | パネルを正面へ戻す |

同じ build を複数の HMD へインストールするときは、各 HMD で `Player` または `Group` を変えて `Apply` します。Address Override の仕組みは[ターゲティング](./targeting-and-multi-hmd.md)を参照してください。

100 Hz を再生するには、スティックで `Play` を選び、左右どちらかのトリガーを引きます。トリガーは現在選択中のボタンを決定するため、ほかのボタンを選択中はそのボタンの操作になります。
