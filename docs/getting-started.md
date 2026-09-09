---
sidebar:
  order: 1
  label: Getting Started
---

# Getting Started

このページでは、プラグインを有効にして、同梱の `BasicExample` から Hapbeat を 1 回再生するところまでを行います。Event Map の作成、Blueprint / C++ の実装、複数端末への送信は次のページへ分けています。

:::tip[ヒント: AI に初期セットアップを任せる]

ファイル操作と Unreal の build を実行できる AI には、次の依頼文を渡します。`<ProjectRoot>` は対象プロジェクトの絶対パスに置き換えます。

```text
Unreal Engine 5.4 の C++ プロジェクト <ProjectRoot> に Hapbeat Unreal SDK を導入してください。
https://github.com/hapbeat/hapbeat-unreal-sdk.git を <ProjectRoot>/Plugins/HapbeatSDK に clone し、project files を再生成して Editor target を build してください。
Hapbeat SDK を有効化し、Plugin Content の BasicExample マップを開いてください。
PIE、Test Play、実機への触覚送信は実行しないでください。
```
:::

## 1. プロジェクトへ追加する

1. 次のいずれかで、このリポジトリの**内容**を C++ プロジェクトの `Plugins/HapbeatSDK/` に置きます。
   - Git: `git clone https://github.com/hapbeat/hapbeat-unreal-sdk.git <ProjectRoot>/Plugins/HapbeatSDK`
   - ZIP: GitHub から ZIP をダウンロードして解凍し、展開されたフォルダの中身を `<ProjectRoot>/Plugins/HapbeatSDK/` へコピーします。
2. `Plugins/HapbeatSDK/HapbeatSDK.uplugin` が存在することを確認します。ZIP の親フォルダまで入れて、`Plugins/HapbeatSDK/hapbeat-unreal-sdk-main/HapbeatSDK.uplugin` となる配置は誤りです。
3. project files を再生成して、プロジェクトを build します。

## 2. プラグインを有効にする

1. Unreal Editor の **Edit → Plugins** で `Hapbeat SDK` を有効にします。
2. **Tools → Hapbeat → Hapbeat Settings** を開きます。Project Settings の **Plugins → Hapbeat** で、`Connection > Port` を Hapbeat と同じネットワークで使う UDP port と一致させます。既定値は `7700` です。

:::note[音と触覚のタイミング]
音声出力の遅延は環境ごとに異なるため、触覚が音より先に感じられることがあります。`Haptic Delay Seconds` に少量の遅延を足し、音と触覚のタイミングを合わせます。
:::

## 3. BasicExample を再生する

1. Content Browser の Settings から **Show Plugin Content** を有効にします。
2. `Plugins/HapbeatSDK/Content/HapbeatSamples/BasicExample/Maps/BasicExample` を開きます。
3. PIE を開始し、`Space` を押します。

100 Hz の StreamClip が再生されれば、SDK の導入とネットワーク送信は完了です。この操作はデバイスへの Kit 配布を必要としません。

### Fire（Command）を試す

1. Hapbeat Studio で `Plugins/HapbeatSDK/Content/HapbeatSamples/BasicExample/Kit/basic-exam-kit/` の Kit を開きます。
2. 接続済みの Hapbeat を選び、**Deploy** で `basic-exam-kit` を Hapbeat へデプロイします。Kit 内の `install-clips/sine_200hz_1s.wav` がデバイスに配置されます。
3. BasicExample の PIE に戻り、`F` を押します。

`F` は Fire（Command）として `basic-exam-kit.sine_200hz_1s` を再生します。`Space` のストリーミング再生を先に確認してから、Kit に含めた install-clip の再生を確認する順序です。

反応しない場合は、Hapbeat の電源・同一ネットワーク・UDP port を確認してください。Editor の **Output Log** で `LogHapbeat` を検索すると、送信・PONG・エラーを確認できます。

## 同梱サンプル

| レベル | 確認できること |
| --- | --- |
| [BasicExample](./getting-started.md#3-basicexample-を再生する) | 最小構成の Event Map 再生 |
| [Showcase](./showcase-unreal.md) | Z1〜Z5 のゲーム内イベントと触覚再生の接続例 |
| [VR Config Example](./vr-config-example.md) | VR コントローラーでの Address Override 設定と Test 再生 |

## 次に読むもの

- [Event Map と再生](./event-map-and-playback.md) — Event Map を作り、Blueprint / C++ から再生する
- [Blueprint ノード](./blueprint-nodes.md) — 全 Blueprint node と component のリファレンス
- [Showcase](./showcase-unreal.md) — Z1〜Z5 のゲーム内イベントと触覚再生の接続例
- [宛先と複数 HMD](./targeting-and-multi-hmd.md) — Target と Address Override
