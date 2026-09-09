---
sidebar:
  order: 1
  label: はじめかた
---

# はじめかた

このページでは、プラグインを有効にして、同梱の `BasicExample` から Hapbeat を 1 回再生するところまでを行います。Event Map の作成、Blueprint / C++ の実装、複数端末への送信は次のページへ分けています。

## 1. プラグインを有効にする

1. Unreal Editor の **Edit → Plugins** で `Hapbeat SDK` を有効にします。
2. 再起動を求められた場合は Editor を再起動します。
3. **Tools → Hapbeat → Hapbeat Settings** を開き、Hapbeat と同じネットワークで使う UDP port を確認します。既定値は `7700` です。

:::note[音と触覚のタイミング]
音声出力の遅延は環境ごとに異なるため、触覚が音より先に感じられることがあります。`Haptic Delay Seconds` に少量の遅延を足し、音と触覚のタイミングを合わせます。
:::

## 2. BasicExample を再生する

1. Content Browser の Settings から **Show Plugin Content** を有効にします。
2. `Plugins/HapbeatSDK/Content/HapbeatSamples/BasicExample/Maps/BasicExample` を開きます。
3. PIE を開始し、`F` を押します。

`basic-example.pulse` が再生されれば、SDK の導入とネットワーク送信は完了です。

反応しない場合は、Hapbeat の電源・同一ネットワーク・UDP port を確認してください。Editor の **Output Log** で `LogHapbeat` を検索すると、送信・PONG・エラーを確認できます。

## 同梱サンプル

| レベル | 確認できること |
| --- | --- |
| [BasicExample](./getting-started.md#2-basicexample-を再生する) | 最小構成の Event Map 再生 |
| [VR Config Example](./vr-config-example.md) | VR コントローラーでの Address Override 設定と Test 再生 |

## 次に読むもの

- [Event Map と再生](./event-map-and-playback.md) — Event Map を作り、Blueprint / C++ から再生する
- [Blueprint ノード](./blueprint-nodes.md) — 全 Blueprint node と component のリファレンス
- [Showcase](./showcase-unreal.md) — Z1〜Z5 のゲーム内イベントと触覚再生の接続例
- [宛先と複数 HMD](./targeting-and-multi-hmd.md) — Target と Address Override
