---
sidebar:
  order: 2
  label: 応用
---

# 応用

[はじめかた](./getting-started.md) を一通り終えたあとの話題です。

## 複数の HMD に 1 台ずつ Hapbeat を割り当てる

同一ビルドを複数台に配って、**端末ごとに別の Hapbeat へ送る**ための機能です。
EventMap やトリガを一切書き換えずに、**すべての送信先を実行時に上書き**します。

```cpp
Hb->SetAddressOverride(/*Player=*/1, /*Group=*/-1, /*bPersist=*/true);
```

- `-1` = その軸は上書きしない
- `bPersist = true` で端末に保存され、次回起動時に自動で復元されます
- App Name に `<p>` / `<g>` を含めておくと、デバイスの OLED に実際の番号が表示されます
  （例: `Booth <p>` → `Booth 1`）

## 送信先を絞る（Target）

`Target` は `player_1/pos_chest` のようなパス文字列です。空文字なら全デバイスに送ります。
`*` はワイルドカードとして使えます（例: `*/pos_neck` = 全プレイヤーの首）。

## Showcase サンプル

Z1〜Z5 の Actor / Component と Event Map の触覚配線、Details での調整は
[Showcase の触覚配線ガイド](./showcase-codex.md)を参照してください。

主要な実装手法をゾーン別に確認できます。コンテンツブラウザの設定で **Show Plugin Content**
（プラグインのコンテンツを表示）を有効にし、
`Plugins/HapbeatSDK/Content/HapbeatSamples/Showcase/Maps/Showcase` を開いて ▶ Play します。
床・ライト・GameMode まで設定済みのレベルなので、開いて再生するだけで動きます。

`W` `A` `S` `D` とマウスで移動、`1`〜`5` でゾーン切り替え、`Tab` でマウスカーソルの表示切り替えです。
表示されるのは常に 1 ゾーンだけで、切り替えるとそのゾーンの位置へ自動で移動します。
各ゾーンの操作キーは画面左上の一覧に表示されます。

### 配置を調整する

5 つのゾーンは**マップに配置済みのアクター**です（それぞれ専用の部屋を持ち、30 m 間隔で並んでいます）。
レーンの位置・ピンの並び・的の位置などは、アウトライナでゾーンアクターを選び
**Details パネルのコンポーネント transform** で動かして、マップを保存すれば残ります。

`Hapbeat Showcase` アクターは切り替え役です。表示中のゾーン以外を隠し、
そのゾーンの位置へプレイヤーを移動させます。
ゾーンが 1 つも置かれていないレベルに `Hapbeat Showcase` だけを置いた場合は、
従来どおり自分の位置にゾーンを 1 つずつ spawn する動作になります。

> `Scripts/generate_showcase_map.py` を再実行すると、このマップのアクターは
> **生成時の状態に戻ります**（調整内容は失われます）。

### 既存のレベルで試す

`Hapbeat Showcase` アクターを 1 つ置けば、自分のレベルでも同じものが動きます。
ただし **World Settings → GameMode Override を `Hapbeat Showcase Game Mode`** にしてください。
設定しないと、一人称プレイヤーではなくエンジン既定の飛行ポーンで再生されます。

各ゾーンのアクター（下表）を直接置くこともできます（1 ゾーンだけ単独で試したいとき）。

BasicExample と同じレベルに置くと、Z2 の `F` と BasicExample の `F` が衝突します。Showcase は別レベルに置いてください。

| アクター | 内容 | キー |
|---|---|---|
| `Z1 Bowling` | 衝突トリガ（速度連動）。**スクリプト無しで鳴る例** | 左クリック 発射 / `Space` リセット |
| `Z2 Door` | 状態遷移に合わせた発火 | `F` 開閉 / `G` 強打 / `L` 施錠 |
| `Z3 Fishing` | 掴む→保持→離す + 速度でゲイン変調 | 左クリック長押し |
| `Z4 Stream Console` | ストリームのゲイン / パンを実行時操作 | `Space` 開始 / 画面のスライダー |
| `Z5 Charge Shot` | コードから直接 API を叩く例（溜め→発射） | 左クリック長押し |

5 ゾーンは共通の EventMap アセット
`Plugins/HapbeatSDK/Content/HapbeatSamples/Showcase/EM_Showcase` を既定で参照します。
強さ・送信先・ループは、コードを触らずここで調整できます
（各アクターの **Event Map Override** を空にすると、コード生成の EventMap で動きます）。

18 エントリはすべて CLIP なので、Kit の書き込み無しで鳴ります。
`Plugins/HapbeatSDK/Content/HapbeatSamples/Showcase/Kit/showcase-kit/` には
波形と manifest が入っており、manifest は EventMap の
**Refresh Intensities**（強さの取り込み）が参照します。

---

## イベント ID を直接送る（特殊ケース）

> **通常は使いません。** EventMap のエントリを
> [はじめかた §4](./getting-started.md#4-自分のプロジェクトから鳴らす) の方法で
> 鳴らしてください。

`Play("<イベント ID>")` は EventMap を経由しないため、一元管理の利点
（詳細パネルでの値調整・Wiring 一覧・遅延補正）をすべて失います。

妥当なのは、**EventMap で扱える範囲を超える規模・動的性**が必要なときだけです。
たとえば 100 人ぶんの心拍をプレイヤー ID 付きで個別管理するようなケースでは、
EventMap に静的に並べると数百エントリになり GUI で管理できず、かつ
イベント ID を実行時に組み立てる必要が出てきます。この 2 つが揃ったときだけです。

```cpp
UHapbeatSubsystem* Hb = GetGameInstance()->GetSubsystem<UHapbeatSubsystem>();
Hb->Play(FString::Printf(TEXT("heartbeat.player_%d"), PlayerId), 0.8f);
```

Blueprint では **Get Hapbeat Subsystem → Play**（`Event Id` / `Gain` / `Target`）です。
Command モードなので **Kit の書き込みが必要**です。
