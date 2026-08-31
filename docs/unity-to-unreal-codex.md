---
sidebar:
  order: 4
  label: UE Editor 補足
---

# Unity 経験者のための Unreal Editor 補足

このページは、Unity から Unreal Engine 5（UE5）へ移る開発者向けの補足です。Showcase の触覚配線そのものは、[Showcase の触覚配線ガイド](./showcase-codex.md)を先に参照してください。

## Editor 上の用語と保存場所

| Unity | UE5 | 用途 |
| --- | --- | --- |
| Scene | Level | 配置・照明・GameMode を保存するファイル。Showcase は `Showcase.umap`。 |
| Hierarchy | World Outliner | Level に配置した Actor を探す場所。 |
| GameObject | Actor | Level に配置できるゲーム object。 |
| MonoBehaviour | Actor / Actor Component | Actor のゲームロジック、または Actor に付ける機能 component。 |
| Prefab | Actor Class / Child Actor | 再利用する Actor class。親 Actor 内に Child Actor として配置できる。 |
| Inspector | Details | 選択中 Actor / component の保存可能な property を編集する場所。 |
| Play | Play In Editor（PIE） | Editor World のコピーを起動して遊ぶ実行モード。 |

Showcase では `Z1_Bowling` のような zone Actor が Level に置かれています。pin、Shark、Target は zone が持つ Child Actor です。位置・回転・scale は、内部 mesh ではなく原則として parent zone の Details にある Child Actor slot の Transform を調整します。

## Editor World と PIE World

UE5 の PIE は、編集中の Level を直接実行するのではなく、**Editor World のコピー（PIE World）** を作って実行します。

```text
Level を編集して保存
  → Editor World
  → PIE を開始
  → コピーされた PIE World で BeginPlay / physics / Tick が動く
  → PIE を停止
  → Editor World の保存済み値へ戻る
```

このため、PIE 中に物理で倒れた object、spawn された projectile、runtime 中に書き換わった component の値は、停止後に Level へは反映されません。Level に残したい配置変更は PIE を止め、Outliner で Actor を選び、Details で変更してから `Ctrl+S` で保存します。

PIE 中の Actor や component を調べる時は、World Outliner の world selector を **Play World** に切り替えます。そこで child Actor や `BeginPlay` が設定した runtime 値を確認できます。PIE 内のマウス cursor の捕捉は Showcase では `Tab` で切り替えます。

## C++ の初期化タイミング

Showcase の C++ では、値がいつ作られるかで Editor で見える内容が変わります。

| タイミング | 主な用途 | Editor / PIE での見え方 |
| --- | --- | --- |
| constructor | default component、既定 property を作る | component tree に常に出る。 |
| `OnConstruction` | property から派生する見た目を組み立てる | Actor の配置・property 変更時にも実行される。 |
| `BeginPlay` | input、physics、子 Actor への runtime 設定を始める | PIE を始めて初めて設定される。 |
| `Tick` | 毎 frame の動き・状態更新 | PIE 中だけ実行される。 |

Z1 pin、Z3 Shark、Z5 Target の Hapbeat Event Map / entry ID は `BeginPlay` に zone から設定します。これは、子 Actor を zone の Event Map override に必ず従わせるためです。したがって、PIE 前の Details だけでは最終的な runtime 値が見えない場合があります。

一方、mesh の補正や preview のように `OnConstruction` が計算する transform は派生値です。内部 mesh を直接動かしても、親 Actor の property を変更した時や再読み込み時に計算し直されることがあります。Showcase では zone / Child Actor slot の公開 property を正本として扱います。

## C++ を変更した後

通常の Details / Event Map の編集は保存して PIE を再起動すれば反映されます。C++ を変更した場合は build が必要です。特に constructor で作る component、既定値、Class Default Object（CDO）を変更した時は、Live Coding だけで既存 Level の instance に反映されないことがあります。その場合は Editor を閉じて通常 build を行い、Editor を開き直してください。
