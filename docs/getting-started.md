# はじめかた（Unreal Engine）

Hapbeat デバイスを Unreal Engine 5 から Wi-Fi 経由で鳴らすための導入手順です。
**上から順に進めれば、最短 15 分で「UE を再生したら Hapbeat が振動する」ところまで到達**します。

このページのゴール:

1. プラグインをプロジェクトに入れてビルドする
2. 付属サンプルで疎通確認する（**ここまでで「動く」ことが確認できます**）
3. 自分のプロジェクトから鳴らす

---

## 0. 事前に用意するもの

| 必要なもの | 補足 |
|---|---|
| **Unreal Engine 5.3 以降** | 5.4 でビルド・動作確認済み |
| **C++ が扱えるプロジェクト** | 下の「なぜ C++ プロジェクトが必要か」を参照 |
| **Hapbeat デバイス**（PC と同じ Wi-Fi / LAN） | ルーター経由でも、Hapbeat の SoftAP でも可 |
| **[Hapbeat Studio](https://devtools.hapbeat.com)** | Wi-Fi 設定と Kit の書き込みに使用 |

### なぜ C++ プロジェクトが必要か

本 SDK は**ソース形式のプラグイン**（コンパイル済みバイナリを同梱していない）です。
そのため Blueprint だけで作るプロジェクトでも、**一度だけ**ビルドが必要になります。

Blueprint プロジェクトしか無い場合は、エディタで
**Tools → New C++ Class → None → Create Class** を一度実行すれば C++ プロジェクトになります
（以降の作業はすべて Blueprint だけで進められます）。

> Visual Studio 2022（ワークロード「**C++ によるゲーム開発**」）が必要です。

### デバイス側の準備

Studio で以下を済ませておきます（SDK 側の作業ではありません）:

1. デバイスを PC と**同じネットワーク**に接続する（Studio の Wi-Fi 設定）
2. **Kit を書き込む**

Kit の要否は再生方式で変わります。ここが最初のつまずきポイントなので先に把握してください:

| 再生方式 | Kit の書き込み | 何が起きるか |
|---|---|---|
| **Command**（イベント ID で鳴らす） | **必要** | SDK は ID だけ送り、デバイスが内蔵クリップを鳴らす |
| **StreamClip**（音声を流し込む） | 不要 | SDK が PCM を毎フレーム送る |

疎通確認だけなら **StreamClip は Kit なしで鳴る**ので、まずそちらで確認するのが確実です。

---

## 1. プラグインを入れる

1. 本リポジトリを `あなたのプロジェクト/Plugins/HapbeatSDK/` に配置する
   （`Plugins/HapbeatSDK/HapbeatSDK.uplugin` が存在する形）
2. `.uproject` を右クリック → **Generate Visual Studio project files**
3. 生成された `.sln` を Visual Studio で開き、構成 **Development Editor / Win64** でビルド
   （`.uproject` をダブルクリックして「ビルドしますか？」→ はい、でも可）
4. エディタを開き **Edit → Plugins → Hardware → Hapbeat SDK** が **Enabled** になっていることを確認

> **ビルドでメモリ不足のエラー**（`C1060` / `PCH の仮想メモリを作成できませんでした`）が出る場合は、
> UE のビルド並列数が搭載メモリに対して多すぎます。
> `%APPDATA%\Unreal Engine\UnrealBuildTool\BuildConfiguration.xml` に
> `<ParallelExecutor><MaxProcessorCount>8</MaxProcessorCount></ParallelExecutor>` を設定してください。

### 設定項目の場所

**Edit → Project Settings → Plugins → Hapbeat**（`UHapbeatConfig`）。
最初は**すべて既定値のままで動きます**。

| 項目 | 既定値 | 意味 |
|---|---|---|
| Port | `7700` | デバイスとの通信ポート |
| App Name | 空 | デバイスの OLED に出る名前（空ならプロジェクト名）。最大 16 文字 |
| Ping Interval | `5` 秒 | 死活監視の間隔 |
| Stream Send Ahead Seconds | `0.05` | ストリーミングで先送りする秒数 |
| Stream Unicast / Command Unicast | 両方 on | 既知デバイスへ直接送信（Wi-Fi の遅延対策。下記) |
| Haptic Delay Seconds | `0` | 音の遅延に触覚を合わせたいときだけ使う |

> **Unicast 設定について**: Wi-Fi のブロードキャストは、同じアクセスポイントに省電力状態の端末が
> 1 台でもいると AP 側で最大 100〜300ms 保留されることがあり、触覚が周期的に途切れる原因になります。
> 既定では PONG で存在が分かっているデバイスへ直接送ることでこれを回避します。
> 対象デバイスが 0 台のときは自動でブロードキャストに戻るため、通常は既定のままで構いません。

---

## 2. サンプルで疎通確認する（最重要）

**まずここを通してください。** 自分のコードを書く前に、環境が正しいことを確認できます。

1. 空のレベルを新規作成する（既存のレベルでも可）
2. **Content Browser → Settings → Show Plugin Content** を有効にする
3. コンテンツブラウザ上部の検索欄で `HapbeatBasicExample` を検索し、
   **Hapbeat Basic Example Actor** をレベルにドラッグ&ドロップ
   （C++ クラスなので **C++ Classes → HapbeatSDKSamples** からでも辿れます）
4. **Play**（PIE）を押す

画面に次の 2 行が出ます:

```
Hapbeat BasicExample -- Space: stream 1-shot | R: stream loop | F: command play | S: stop all | C: ping
Hapbeat devices reachable: 1
```

**2 行目が緑で `1` 以上**になっていれば、デバイスと通信できています。

### 押すキーと期待される動作

| キー | 動作 | Kit の書き込み |
|---|---|---|
| **Space** | 100Hz のクリップを 1 回ストリーミング再生 | **不要** |
| **R** | 同じクリップをループ再生（もう一度 S で停止） | **不要** |
| **F** | `basic-exam-kit.sine_200hz_1s` を Command 再生 | **必要** |
| **S** | すべて停止 | — |
| **C** | Ping 送信（`reachable` の数が更新される） | — |

**Space で振動すれば導入は成功です。**

F だけ鳴らない場合は Kit 未書き込みが原因です。Studio で
`Plugins/HapbeatSDK/Content/HapbeatSamples/BasicExample/Kit/basic-exam-kit/` を書き込んでください。

### 動かないときの切り分け

上から順に確認してください。

| 症状 | 確認すること |
|---|---|
| `reachable: 0` のまま | デバイスの電源と Wi-Fi 接続。PC とデバイスが**同じネットワーク**か。ファイアウォールが UDP 7700 を塞いでいないか |
| `reachable: 0` だが Studio では見える | **PC が有線と Wi-Fi に同時接続**していると、ブロードキャストが有線側に出て届かないことがあります。一時的に有線を切って確認してください |
| Space は鳴るが **F だけ鳴らない** | Kit 未書き込み（上記）。イベント ID がデバイス内の Kit と一致しているか |
| 何も鳴らないが `reachable` は 1 以上 | デバイス本体の音量。Output Log に `LogHapbeat` の警告が出ていないか |
| 途切れ・ブツブツする | Project Settings の **Stream Unicast** が on か。Wi-Fi の電波状況 |

---

## 3. 自分のプロジェクトから鳴らす

サブシステムは**起動時に自動接続**します（`Initialize()` の時点。レベルの `BeginPlay` より前）。
そのため `Connect` を呼ばなくても、いきなり `Play` して構いません。

### Blueprint の場合

1. **Get Game Instance** → **Get Subsystem**（Class に `Hapbeat Subsystem` を指定）
2. そこから **Play** を繋ぐ
   - `Event Id`: `basic-exam-kit.sine_200hz_1s` のような `<kit名>.<ファイル名>`
   - `Gain`: `0.0`〜`1.0`
   - `Target`: 空でよい（全デバイス宛て）

### C++ の場合

自分のモジュールの `*.Build.cs` に `"HapbeatSDK"` を追加してから:

```cpp
#include "HapbeatSubsystem.h"

void AMyActor::OnHit()
{
    if (UHapbeatSubsystem* Hb = GetGameInstance()->GetSubsystem<UHapbeatSubsystem>())
    {
        Hb->Play(TEXT("basic-exam-kit.sine_200hz_1s"), 0.8f);
    }
}
```

主な API:

```cpp
void Play(const FString& EventId, float Gain = 1.0f, const FString& Target = TEXT(""));
void Stop(const FString& EventId, const FString& Target = TEXT(""));
void StopAll(const FString& Target = TEXT(""));
void Ping();

int32 GetAliveDeviceCount() const;   // 応答のあるデバイス数
bool  IsAlive() const;               // 1 台以上応答しているか
bool  IsConnected() const;           // ソケットが開いているか（≠ デバイスの有無）
```

> `IsConnected()` は「ソケットが開いているか」であり、**デバイスの有無ではありません**。
> UDP はコネクションレスのため、デバイスの電源が入っていなくても `true` になります。
> 実際に届いているかは `IsAlive()` / `GetAliveDeviceCount()` で判断してください。

---

## 4. EventMap で「鳴らす場所」と「強さ」を分ける

`Play("...", 0.8f)` を直接書くと、**強さの調整のたびにコードを直す**ことになります。
SDK はこれを分離する仕組みを持っています。

- **鳴らす場所（起点）** — トリガコンポーネント、または `Play()` の呼び出し
- **鳴らし方（調整）** — **EventMap** アセットのエントリ（イベント ID・ゲイン・対象デバイス・ループ）

両者は**イベント ID ではなく安定した GUID** で結ばれるため、エントリを並べ替えても壊れません。

### 作り方

1. コンテンツブラウザで右クリック → **Miscellaneous → Data Asset** → **Hapbeat Event Map**
2. 開いて `Entries` に `+` で追加し、各エントリを設定
   - `Mode`: `Command`（Kit 必要）か `StreamClip`（Kit 不要）
   - `Category` / `Event Name`: 合わせて `<Category>.<EventName>` がイベント ID になる
   - `Gain`: `0`〜`2`
   - `Target`: 空 = 全デバイス
3. Details パネル上部の **Refresh Intensities** を押す
   → Kit の `manifest.json` に書かれた `intensity` を各エントリに焼き込みます
4. **Test Play** で、再生（PIE）せずにその場で鳴らして確認できます

> 実際にデバイスへ送られるゲインは
> **`Gain` × `Kit の intensity` × トリガ側の倍率** です。
> Studio で作り込んだ強さ（intensity）を土台に、UE 側で微調整する設計になっています。

---

## 5. トリガコンポーネントを使う（コードなしで鳴らす）

アクターに **Add Component** から追加できます。いずれも EventMap とエントリを指定して使います。

| コンポーネント | 用途 |
|---|---|
| **Hapbeat Trigger** | 基本形。Blueprint から `Fire()` / `Stop()` を呼ぶ |
| **Hapbeat Collision Trigger** | 物理の衝突・重なりで自動発火。**衝突速度に応じて強さを変えられる** |
| **Hapbeat Sequence** | 掴む→保持→離す の 3 段階（開始 1 発 → ループ → 終了 1 発） |
| **Hapbeat Parameter Binding** | 再生中のストリームのゲイン / パンを実行時に変化させる |

`Entry Id` はプルダウンから**エントリ名で選べます**（EventMap を指定すると一覧が出ます）。

---

## 6. クリップをストリーミングする（Kit 不要）

`UHapbeatClip` は 16kHz / PCM16 の WAV をそのまま扱えるアセットです。
再生中に**ゲインとパンをリアルタイムに変えられる**のが Command 再生との違いです。

```cpp
UHapbeatStreamPlayback* Playback =
    Hb->StreamClip(MyClip, /*BaselineGain=*/1.0f, /*InitialGain=*/1.0f, TEXT(""), /*bLoop=*/true);

// 毎フレーム変調する（例: 速度に応じて強くする）
Playback->ApplyGainModulation(FMath::Clamp(Speed / 300.0f, 0.0f, 1.0f));
Playback->SetPan(-1.0f);  // -1 = 左, +1 = 右

Hb->StopStream();  // 停止
```

同時に流せるストリームは **1 本**です。新しく `StreamClip` を呼ぶと前のものは停止します。

---

## 7. 応用

### 複数の HMD に 1 台ずつ Hapbeat を割り当てる

同一ビルドを複数台に配って、**端末ごとに別の Hapbeat へ送る**ための機能です。
EventMap やトリガを一切書き換えずに、**すべての送信先を実行時に上書き**します。

```cpp
Hb->SetAddressOverride(/*Player=*/1, /*Group=*/-1, /*bPersist=*/true);
```

- `-1` = その軸は上書きしない
- `bPersist = true` で端末に保存され、次回起動時に自動で復元されます
- App Name に `<p>` / `<g>` を含めておくと、デバイスの OLED に実際の番号が表示されます
  （例: `Booth <p>` → `Booth 1`）

### 送信先を絞る（Target）

`Target` は `player_1/pos_chest` のようなパス文字列です。空文字なら全デバイスに送ります。
`*` はワイルドカードとして使えます（例: `*/pos_neck` = 全プレイヤーの首）。

### Showcase サンプル

主要な実装手法をゾーン別に確認できます。BasicExample と同様、アクターをレベルに置いて再生します。

| アクター | 内容 | キー |
|---|---|---|
| `Z1 Bowling` | 衝突トリガ（速度連動）。**スクリプト無しで鳴る例** | `B` 発射 |
| `Z2 Door` | 状態遷移に合わせた発火 | `F` 開閉 / `G` 強打 / `L` 施錠 |
| `Z3 Fishing` | 掴む→保持→離す + 速度でゲイン変調 | `H` |
| `Z4 Stream Console` | ストリームのゲイン / パンを実行時操作 | `T` 開始 / `U` `J` 強弱 / `N` `M` 左右 |
| `Z5 Charge Shot` | コードから直接 API を叩く例（溜め→発射） | `V` 長押し |

Showcase のイベントは Command が中心のため、
`Plugins/HapbeatSDK/Content/HapbeatSamples/Showcase/Kit/showcase-kit/` の書き込みが必要です。

---

## 次に読むもの

- [README](../README.md) — 機能一覧と API の入口
- [AGENTS.md](../AGENTS.md) — AI コーディングエージェント向けの自己完結リファレンス
- [公式ドキュメントポータル](https://devtools.hapbeat.com/) — 他 SDK と共通の概念解説
