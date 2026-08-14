# はじめかた（Unreal Engine）

Hapbeat デバイスを Unreal Engine 5 から Wi-Fi 経由で鳴らすための導入手順です。
**上から順に進めれば、最短 15 分で「UE を再生したら Hapbeat が振動する」ところまで到達**します。

このページのゴール:

1. プラグインをプロジェクトに入れてビルドする
2. 付属サンプルで疎通確認する（**ここまでで「動く」ことが確認できます**）
3. EventMap に触覚イベントを定義する
4. 自分のプロジェクトから鳴らす

---

## 0. 事前に用意するもの

| 必要なもの | 補足 |
|---|---|
| **Unreal Engine 5.3 以降** | 5.4 でビルド・動作確認済み |
| **C++ が扱えるプロジェクト** | 理由と手順は [UE プロジェクトのビルド](./unreal-build.md) |
| **Hapbeat デバイス**（PC と同じ Wi-Fi / LAN） | ルーター経由でも、Hapbeat の SoftAP でも可 |
| **[Hapbeat Studio](https://devtools.hapbeat.com)** | Wi-Fi 設定と Kit の書き込みに使用 |

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

> **初回のエディタ起動は時間がかかります。** 画面右下に
> `Compiling Shaders (1234)` と出て、数分〜十数分かかることがあります。
> フリーズではないので、カウントが 0 になるまで待ってください（2 回目以降は速くなります）。

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

1. **ファイル → 新規レベル → Basic** を選んでレベルを作る（下の表を参照）
2. **コンテンツブラウザ**（画面下部）右上の **設定 → Show Plugin Content** に
   チェックを入れる（これを入れないとプラグイン同梱のものが検索に出ません）
3. コンテンツブラウザ上部の検索欄に `HapbeatBasicExample` と入力し、
   出てきた **Hapbeat Basic Example Actor** を**ビューポート（3D 画面）にドラッグ&ドロップ**
   （C++ クラスなので **C++ Classes → HapbeatSDKSamples** からでも辿れます）
4. 画面上部の **▶ Play** を押す（PIE = Play In Editor）

### レベルのテンプレートは Basic を選ぶ

| テンプレート | 推奨 | 理由 |
|---|---|---|
| **Basic** | ✅ | 床・ライト・PlayerStart が最初からあり、すぐ見えてすぐ動かせる。かつ軽い |
| Empty Level | △ | 何も無い＝**ライトが無いので画面が真っ暗**になり、動いているのか分かりにくい |
| Open World | ✗ | 広大な地形で重く、シェーダーのコンパイル量が大幅に増える |

触覚の確認に 3D の作り込みは不要なので、**軽いレベルほど確認が速く安定します**。

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
| **起動直後だけ**ガクガクする | v0.1.0 で修正済み。デバイス発見前は全送信がブロードキャストになるため。`reachable` が 1 になれば解消する |
| エディタ自体がクラッシュする | まず SDK 起因か切り分ける。`<プロジェクト>/Saved/Crashes/` の最新フォルダにある `CrashContext.runtime-xml` を開き、`<CallStack>` に `Hapbeat` が含まれるか確認する。含まれない場合（描画・シェーダー系が多い）は SDK とは無関係 |

> **このサンプルは EventMap アセットで動いています。**
> `Plugins/HapbeatSDK/Content/HapbeatSamples/BasicExample/EM_BasicExample` が
> プラグインに同梱されており、アクターは既定でこれを参照します。
> **開いて `Gain` を変えれば、そのままサンプルの強さが変わります** —
> 次章で説明する標準ワークフローを、このサンプル自身が使っています。

---

## 3. EventMap で「鳴らす場所」と「強さ」を分ける

`Play("...", 0.8f)` を直接書くと、**強さの調整のたびにコードを直す**ことになります。
SDK はこれを分離する仕組みを持っています。

- **鳴らす場所（起点）** — トリガコンポーネント、または `Play()` の呼び出し
- **鳴らし方（調整）** — **EventMap** アセットのエントリ（イベント ID・ゲイン・対象デバイス・ループ）

両者は**イベント ID ではなく安定した GUID** で結ばれるため、エントリを並べ替えても壊れません。

### 作り方

1. **コンテンツブラウザで `コンテンツ`（Content）フォルダを開く**

   > **`C++ クラス`（C++ Classes）フォルダでは作れません。** ここは C++ の
   > クラス階層を見るための表示専用フォルダで、右クリックしても
   > `Fab` と `新規 C++ クラス` しか出てきません。左のツリーで
   > プロジェクトの `コンテンツ` を選んでから進めてください。

2. **右側のアセット一覧の、何もないところで右クリック** →
   **Hapbeat → Hapbeat Event Map**

   > 左のフォルダツリー上で右クリックすると、フォルダ操作のメニュー
   >（新規フォルダ・名前変更など）が出ます。**右側の広い領域**で
   > 右クリックしてください。上部の **+ 追加** ボタンからでも同じです。

3. 作られたアセットに名前を付ける（例: `DA_HapbeatEventMap`）
4. 開いて `Entries` に `+` で追加し、各エントリを設定
   - `Mode`: `FIRE (Command)`（Kit 必要）か `CLIP (Stream Clip)`（Kit 不要）
   - `Category` / `Event Name`: 合わせて `<Category>.<EventName>` がイベント ID になる
   - `Gain`: `0`〜`2`
   - `Target`: 空 = 全デバイス
5. 上部の **Refresh Intensities** を押す
   → Kit の `manifest.json` に書かれた `intensity` を各エントリに焼き込みます
6. **Test Play** で、再生（PIE）せずにその場で鳴らして確認できます

#### `Category` / `Event Name` は Kit の manifest と一致させる

`Refresh Intensities` が `resolved 0 / unresolved N` になる場合、ほぼこれです。
**イベント ID と Mode の両方が manifest と一致した時だけ**強さが焼き込まれます。

同梱の `basic-exam-kit` なら、次の 2 通りが一致する組み合わせです:

| Mode | Category | Event Name | manifest 上の位置 |
|---|---|---|---|
| `FIRE (Command)` | `basic-exam-kit` | `sine_200hz_1s` | `events` |
| `CLIP (Stream Clip)` | `basic-exam-kit` | `sine_100hz_1s` | `stream_events` |

- `Category` は **Kit 名**（manifest の `name`）
- `Event Name` は **クリップのファイル名から拡張子を除いたもの**
- **Mode が違うと一致しません。** manifest では FIRE 用が `events`、CLIP 用が
  `stream_events` に分かれており、同じイベント ID でも別枠として扱われます

一致しない場合は `intensity` が `-1`（未解決）のままになり、`Gain` がそのまま
送られます。鳴らないわけではありませんが、Kit で意図した強さにはなりません。

### 専用ウィンドウで編集する（推奨）

エントリが増えると Details パネルは縦一列で見通しが悪くなります。
**Tools メニュー → Hapbeat Event Map** に専用エディタがあります
（アセットをダブルクリックして開く Details パネルとは別のウィンドウです）。

- **左**: エントリ一覧（表示名・イベント ID・モード）
- **右**: 選択したエントリだけを、**Identity / Event / Playback / Targeting /
  Notes / Test** のセクションに分けて表示

上部の **Event Map** 欄でアセットを切り替えられるので、ウィンドウはドッキング
したまま複数の EventMap を行き来できます。Targeting は `Player` / `Position` /
`Group` に分解して編集でき、結果の `Target` 文字列もその場で確認・直接編集
できます。Details パネル側の編集も従来どおり使えます。

**Test セクションの Test Play は Command / StreamClip の両方に対応**しています。
StreamClip のエントリでは再生（PIE）に入らずにその場でストリーミングし、
Stop で止まります。

### StreamClip に音源を割り当てる

`Mode` を **CLIP (Stream Clip)** にすると **Stream Clip** 欄が出ます。
右の **Import WAV...** を押して 16-bit PCM の `.wav` を選べば、
**Hapbeat Clip アセットが EventMap の隣に自動生成されて割り当てられます**
（既にアセットがあるならピッカーから選んでも構いません）。

> **なぜ `.wav` や Sound Wave を直接参照しないのか。**
> パッケージ後の `USoundWave` はプラットフォーム圧縮された音声しか持たず、
> このプロトコルが送る生 PCM を安定して取り出せません（エディタでは動いて
> パッケージ版で無音になる類の壊れ方をします）。そのため SDK 側の
> `Hapbeat Clip` アセットに PCM を保持しています。上のボタンは、その制約を
> ユーザーの手作業にしないためのものです。

> **`Hapbeat Clip`** アセット（ストリーミング用）も、同じ
> **Hapbeat** カテゴリから同じ手順で作成できます。

### クリップをストリーミングする（Kit 不要）

`UHapbeatClip` は 16kHz / PCM16 の WAV をそのまま扱えるアセットです。
再生中に**ゲインとパンをリアルタイムに変えられる**のが Command 再生との違いです。

**クリップアセットの作り方**: Content Browser → 右クリック → **Hapbeat → Hapbeat Clip**
で空のアセットを作り、開いて **[Import WAV...]** から `.wav` を読み込みます
（PCM 16bit であること。Kit と同じ 16kHz を推奨）。
`.wav` の通常インポート（`USoundWave`）とは別物なので、間違えないよう専用ボタンにしています。

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

### GUI で編集できる項目

エントリを開くと、以下がすべて Details パネル上で編集できます（各項目にツールチップ付き）:

| 項目 | 内容 |
|---|---|
| `Mode` | `Command` / `StreamClip`（プルダウン） |
| `Display Name` | 一覧やトリガのプルダウンに出る表示名 |
| `Category` / `Event Name` | 合わせて `<Category>.<EventName>` がイベント ID |
| **`Gain`** | **0.0〜2.0** |
| **`Target`** | **`player_1` / `*/pos_neck` など。空 = 全デバイス** |
| `Loop` | ループ再生（StreamClip 用） |
| `Delay Offset Seconds` | このイベントだけ発火を前後させる（±0.2 秒） |
| `Notes` | 制作メモ（送信されません） |
| `Stream Clip` | StreamClip モードで流す `UHapbeatClip` |

Details パネル上部には専用の操作列も出ます:

- **[Refresh Intensities]** — Kit の manifest を走査して intensity を全エントリに反映
- **エントリ選択 + [Test Play] / [Stop] / [Stop All] / [Ping]** — **PIE に入らずその場で試せます**

配列の追加・削除・並べ替え・複製、複数選択しての一括編集は UE 標準の機能がそのまま使えます。

> 実際にデバイスへ送られるゲインは
> **`Gain` × `Kit の intensity` × トリガ側の倍率** です。
> Studio で作り込んだ強さ（intensity）を土台に、UE 側で微調整する設計になっています。

### 同梱サンプルで試す

疎通確認に使った BasicExample は、**最初から EventMap アセットで動いています**。
新しく作らなくても、そのまま練習台になります。

1. コンテンツブラウザで **Show Plugin Content** を有効にする
2. `Plugins/HapbeatSDK/Content/HapbeatSamples/BasicExample/EM_BasicExample` を開く
3. `[0]` のエントリの **`Gain`** を `0.3` などに変える
4. **Test Play** で鳴らす（または ▶ Play して **Space**）

強さが変われば、コードを触らずに調整できる状態になっています。
`Target` を `player_1` にして送信先を絞る、なども同じ手順で試せます。

同梱の内容は次のとおりです。

| # | Mode | Category | Event Name | Gain | Loop | Stream Clip |
|---|---|---|---|---|---|---|
| 0 | CLIP (Stream Clip) | `basic-exam-kit` | `sine_100hz_1s` | `1.0` | off | `HC_sine_100hz_1s` |
| 1 | CLIP (Stream Clip) | `basic-exam-kit` | `sine_100hz_1s` | `1.0` | **on** | `HC_sine_100hz_1s` |
| 2 | FIRE (Command) | `basic-exam-kit` | `sine_200hz_1s` | `1.0` | off | — |

> **このサンプルだけはエントリの順番が固定です。**
> 上から `[0]` Space / `[1]` R / `[2]` F に対応付けるため、並べ替えると
> キーの割り当ても変わります（Showcase 側はイベント名で引くので影響しません）。
> アクターの **Event Map Override** を空にすると、コード生成の EventMap で動きます。

---

## 4. 自分のプロジェクトから鳴らす

前章で作ったエントリを発火させます。鳴らし方は 2 つで、**どちらも EventMap の
エントリを参照します**。強さ・送信先・ループはアセット側に残るので、調整のたびに
コードを触る必要はありません。

### 4-1. コードから鳴らす（Blueprint / C++）

**Play Entry** に EventMap とエントリを渡すだけです。Command か Stream Clip か、
どのクリップを使うか、ゲイン、送信先、ループは**すべてエントリ側の設定**が使われます。

**Blueprint**:

1. グラフを右クリック → `Hapbeat` で検索 → **Get Hapbeat Subsystem**
2. そこから **Play Entry** を繋ぐ
   - `Map`: §3 で作った EventMap
   - `Entry Id`: 鳴らしたいエントリの `Id`（Event Map ウィンドウの Identity からコピー）
3. 発火のきっかけ（`Event BeginPlay` やキー入力）を実行ピンに繋ぐ

**C++**:

```cpp
UHapbeatSubsystem* Hb = GetGameInstance()->GetSubsystem<UHapbeatSubsystem>();
Hb->PlayEntry(EventMap, EntryId);
```

停止は **Stop Entry**（`StopEntry(EventMap, EntryId)`）です。

> 第 3 引数の `Gain Multiplier` で、**その呼び出しだけ**強さを変えられます
>（エントリの設定は変わりません）。

### 4-2. トリガコンポーネントを使う（同じエントリを繰り返し鳴らす場合）

アクターに **Add Component** から追加できます。いずれも EventMap とエントリを指定して使います。

| コンポーネント | 用途 |
|---|---|
| **Hapbeat Trigger** | 基本形。Blueprint から `Fire()` / `Stop()` を呼ぶ |
| **Hapbeat Collision Trigger** | 物理の衝突・重なりで自動発火。**衝突速度に応じて強さを変えられる** |
| **Hapbeat Sequence** | 掴む→保持→離す の 3 段階（開始 1 発 → ループ → 終了 1 発） |
| **Hapbeat Parameter Binding** | 再生中のストリームのゲイン / パンを実行時に変化させる |

`Entry Id` はプルダウンから**エントリ名で選べます**（EventMap を指定すると一覧が出ます）。

---

4-1 との違いは、**呼ぶたびに指定するか、アクターに持たせておくか**だけです。
同じエントリを何度も鳴らすなら 4-2 のほうが短く済み、クールダウンや
ライブ変調も付いてきます。併用できます。

## 次に読むもの

- [応用](./advanced.md) — 複数 HMD への割り当て、送信先の絞り込み、Showcase
  サンプル、イベント ID を直接送る特殊ケース
- [UE プロジェクトのビルド](./unreal-build.md) — C++ プロジェクト化、`Build.cs`、
  Live Coding の効く範囲、ビルドが通らないときの対処
