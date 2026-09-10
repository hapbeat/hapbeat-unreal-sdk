---
kind: reference
sidebar:
  order: 300
  label: Editor メニュー
---

# Editor メニュー一覧

Hapbeat SDK が Unreal Editor に追加する設定画面・asset 作成・診断メニューのリファレンスです。

## Tools

トップメニューの `Tools` にある **Hapbeat** セクションです。

| 項目 | 用途 | 保存先・適用範囲 |
| --- | --- | --- |
| `Hapbeat Event Map` | Event Map asset の entry を一覧・編集する。Clip の import、Test Play、entry の追加・削除もここで行う。 | 選択中の `UHapbeatEventMap` asset |
| `Hapbeat Settings` | Project Settings の `Plugins → Hapbeat` を直接開く。 | プロジェクト全体（`DefaultGame.ini`） |
| `Hapbeat Runtime Status` | this machine の Address Override を確認・保存する。PIE 中は実効値も表示する。 | この端末（`GameUserSettings.ini`） |
| `Check for SDK Updates` | SDK の更新を今すぐ確認する。 | 保存しない |
| `Check for SDK Updates on Startup` | Editor 起動時の更新確認を切り替える。 | このプロジェクトの Editor 設定 |
| `Turn Off Verbose Log on All Triggers` | 開いている level の全 Hapbeat Trigger の `Verbose Log` を off にする。 | level asset |
| `Export Event Map to Markdown` | 実行後に Event Map を選び、entry を Markdown 表として asset の隣へ出力する。Content Browser の事前選択は不要。 | `<EventMap名>.md` |

### Hapbeat Settings

`Hapbeat Settings` は Project Settings の `Plugins → Hapbeat` と同じ画面を直接開きます。

| カテゴリ | 設定 | 説明 |
| --- | --- | --- |
| Connection | `Port` | Hapbeat と通信する UDP port。既定は `7700`。 |
| Connection | `App Name` | Hapbeat の OLED に表示するアプリ名。最大 16 文字。空欄ではプロジェクト名を使う。 |
| Behavior | `Ping Interval` | PING / CONNECT_STATUS の間隔（秒）。 |
| Behavior | `Stream Send Ahead Seconds` | Stream を先行送信する時間。小さいほど停止は速いが、ネットワーク状況によって途切れやすい。 |
| Behavior | `Command Unicast` | PONG 済み device には unicast を使う。対象が未検出のときは broadcast にフォールバックする。 |
| Behavior | `Haptic Delay Seconds` | Event Map 経由の再生に加える触覚遅延。既定 0 秒、範囲 0〜0.5 秒。直接送信 API の Play / StreamClip は対象外。 |
| Addressing | `Forced Override Player` / `Group` | build に固定する player / group。`-1` は固定しない。 |
| Logging | `Enable Logging` / `Verbose Logging` | Output Log の出力量を設定する。 |

`Haptic Delay Seconds` は映像や音と触覚がずれる場合の補正値です。Target と Address Override の意味は[ターゲティング](./targeting-and-multi-hmd.md)を参照してください。

### Hapbeat Runtime Status

`Hapbeat Runtime Status` は、端末ごとに変わる Address Override 専用の画面です。

| 項目 | 内容 |
| --- | --- |
| `Player` / `Group` | 保存・適用する override 値。`-1` はその軸を上書きしない。 |
| `Save to This Machine` | PIE 外では次回起動用に保存する。PIE 中は実行中の Subsystem へ即時適用し、次回起動用にも保存する。 |
| `Clear Saved Override` | 保存済み設定を削除し、build で固定されていない軸を off にする。 |
| Runtime status | PIE 中の socket 状態と実効 player / group を表示する。 |
| `Open Project Settings` | プロジェクト共通の接続・遅延・build 固定 override の設定を開く。 |

設定の既定値: `App Name` は空、`Ping Interval` は 5 秒（1〜60）、`Stream Send Ahead Seconds` は 0.05 秒（0.01〜0.2）、`Command Unicast` と `Enable Logging` は on、`Verbose Logging` は off、両 `Forced Override` は −1（off、指定可能範囲 1〜99）。Port は 1〜65535 です。接続設定を変えた後は PIE を停止・再開して確認します。

## Content Browser

Content Browser で右クリックし、`Create → Hapbeat` から次の asset を作成できます。

| Asset | クラス | 用途 |
| --- | --- | --- |
| `Hapbeat Event Map` | `UHapbeatEventMap` | Event ID、Stream Clip、Gain、Pan、Target、Loop を entry ごとに保持する。 |
| `Hapbeat Clip` | `UHapbeatClip` | PCM16 の Stream 用 clip。通常は Event Map window の `Import WAV...` で作成し、entry に割り当てる。 |

`Hapbeat Event Map` をダブルクリックすると、専用の Event Map window が開きます。

## Add Component

Actor の `Add Component` で `Hapbeat` を検索すると、SDK の component を追加できます。

| Component | 用途 |
| --- | --- |
| `Hapbeat Trigger Component` | Event Map entry を任意のタイミングで Fire / Stop する基本 component。 |
| `Hapbeat Collision Trigger Component` | Hit または Begin Overlap から発火する。 |
| `Hapbeat Sequence Component` | Start / loop / stop の 3 段階の触覚を扱う。 |
| `Hapbeat Tick Emitter` | 連続値の閾値通過ごとに tick を発火する。 |
| `Hapbeat Parameter Binding` | Actor、UI、外部値を Stream Gain または Pan へ反映する。 |

各 component のプロパティ・Blueprint node・C++ 関数は[Blueprint ノード](./blueprint-nodes.md)と[C++ API](./cpp-api.md)を参照してください。
