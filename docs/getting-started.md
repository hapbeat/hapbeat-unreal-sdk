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

> Command 再生で**左右のバランス（パン）を指定する場合は、デバイスのファームウェアも
> 対応版が必要**です（[実行時に変えられるもの](#実行時に変えられるもの)）。未対応のファームウェアは中央で再生します。
> 更新は Studio から行えます。

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
| Haptic Delay Seconds | `0` | 音の遅延に触覚を合わせたいときだけ使う（下記） |

> **Unicast 設定について**: Wi-Fi のブロードキャストは、同じアクセスポイントに省電力状態の端末が
> 1 台でもいると AP 側で最大 100〜300ms 保留されることがあり、触覚が周期的に途切れる原因になります。
> 既定では PONG で存在が分かっているデバイスへ直接送ることでこれを回避します。
> 対象デバイスが 0 台のときは自動でブロードキャストに戻るため、通常は既定のままで構いません。

> **Haptic Delay Seconds について**: 触覚は約 10ms で届くため、Bluetooth ヘッドホンのように
> 音の出力が遅い環境では**触覚のほうが先に来ます**。その分だけ触覚の送信を遅らせる設定です。
> 実際の遅延量は **この値 ＋ エントリごとの `Delay Offset Seconds`**（0 未満は 0 に丸め）で、
> 発火と停止の両方に同じだけ効きます（間隔は保たれます）。
> `0`（既定）のときは何も挟まりません。

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

- **鳴らす場所（起点）** — `Play Hapbeat Event` の呼び出し（Blueprint ノード / C++）
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
| `Delay Offset Seconds` | このイベントだけ発火を前後させる（±0.2 秒）。§1 の `Haptic Delay Seconds` との**合計**が実際の遅延（0 未満は 0）。発火と停止の両方に同じだけ効き、CLIP では戻り値のハンドルは即座に返って**再生開始だけ**が遅れる |
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

エントリを発火させます。強さ・送信先・ループはアセット側に残るので、調整のたびに
コードを触る必要はありません。Command か Stream Clip か、どのクリップを使うかも
**すべてエントリ側の設定**が使われます。

**再生の経路は 1 本です。** どのエントリを鳴らすかは `Play Hapbeat Event`
（EventMap と、その中のエントリの 2 つを渡す）で指定します。Blueprint は同名のノード、
C++ は `UHapbeatBlueprintLibrary::PlayHapbeatEvent` を呼び、どちらも内部の
`PlayEntry` に合流します。衝突や掴む→離すを扱うパターン部品（4-3）も同じ経路です。

**エントリの指定に GUID を手入力する場面はありません。** 保存されるのは安定 GUID ですが、
Blueprint のピンでも C++ の詳細パネルでも**エントリ名のプルダウン**として編集します。

### 4-1. Blueprint: キーを押したら鳴らす

§2 の疎通確認（`reachable: 1`）が通っている状態から始めます。
ここでは**同梱の `EM_BasicExample` の 1 番目のエントリを G キーで鳴らす**ところまでを通します。

1. **Blueprint を作る**
   コンテンツブラウザの `コンテンツ` フォルダで右クリック →
   **ブループリント クラス** → **Actor** を選び、`BP_HapbeatKeyTest` と名付ける

2. **ブループリントエディタを開く → フルエディタに切り替える**
   作った `BP_HapbeatKeyTest` を**ダブルクリック**する
   （右クリック → **編集** でも同じです）

   **新規作成直後は、必ず次の簡易表示で開きます**（プロパティ一覧だけの画面）:

   > 注記: データ専用ブループリントであるためデフォルト値のみを表示します。
   > スクリプトや変数はありません。追加したい場合は、
   > **[フルブループリントエディタを開く]**

   **この注記の「フルブループリントエディタを開く」を押してください。**
   押すまで、次の手順で使う **コンポーネント** パネルも **イベントグラフ** タブも
   表示されません（この画面にあるのは「クラスのデフォルト」だけです）。

3. **キー入力を受け取れるようにする**
   **左上の「コンポーネント」パネル**の一番上（`BP_HapbeatKeyTest (self)`）を選び、
   Details → **Input** → **Auto Receive Input** を **Player 0** にする

   > **ここを飛ばすとキーを押しても何も起きません。** アクターは既定で入力を
   > 受け取らないので、この 1 項目が実質の「入力を有効化」スイッチです。

   > 「コンポーネント」パネルが見当たらない場合は閉じています。
   > メニューの **ウィンドウ → コンポーネント** で開き直せます。

4. **キー入力のノードを置く**（**イベントグラフ** タブ）
   何も無いところで右クリック → 出たメニューのツリーを
   **インプット → キーボード イベント → G** と辿って配置する
   （英語版は `Input → Keyboard Events → G`）

   > **検索欄に `G` と打っても見つかりません。** ノード名は `G` の 1 文字なので、
   > `G` を含む無関係なノードが大量に並びます。絞るなら **`キーボード`**
   >（英語版は `Keyboard`）と入力してから一覧の `G` を探してください。

5. **`Play Hapbeat Event` を置く**
   グラフの何も無いところで右クリック → 検索欄に **`Play Hapbeat Event`** と
   入力して配置する（**状況に合わせた表示のまま**で見つかります）

6. **鳴らすエントリを選ぶ**
   置いたノードに 2 つの入力ピンが出ます:
   - **`Map`**: EventMap のアセットピッカー → **`EM_BasicExample`**
     （プラグイン同梱。候補に出ない場合はコンテンツブラウザ右上の
     **設定 → Show Plugin Content** が off です）
   - **`Entry`**: そのマップのエントリが並ぶ**ドロップダウン** →
     **`demo_stream_sine_100hz`**
     （CLIP モードなので、**デバイスへの Kit 書き込みは不要**）

7. **G の `Pressed`** 実行ピンを `Play Hapbeat Event` の実行ピンへ接続する

8. **[コンパイル]** を押し、保存する

9. **レベルに配置する**
   コンテンツブラウザの `BP_HapbeatKeyTest` を、**ビューポート（3D 画面）へ
   ドラッグ&ドロップ**する。置く位置はどこでも構いません

   > **これを忘れると何も起きません。** ブループリントは「設計図」なので、
   > レベルに実体を置くまで実行されません。
   >
   > このアクターは見た目を持たないため、置いても 3D 画面にはほぼ何も現れません。
   > **アウトライナー**（画面右上のリスト）に `BP_HapbeatKeyTest` が
   > 増えていれば配置できています。

10. ▶ Play して **G** を押す → 100Hz が 1 回鳴る

鳴ったら、**PIE を動かしたまま** `EM_BasicExample` を開いて
`demo_stream_sine_100hz` の **`Gain`** を `0.3` に変え、ゲーム画面に戻って
もう一度 G を押してください。**BP を触らずに強さが変わります** —
これが §3 で分けた「鳴らす場所」と「鳴らし方」です。

> **PIE 中にエディタ側を操作するには `Shift + F1`。**
> マウスカーソルがエディタに戻り、**PIE は動いたまま**になります。
> ゲーム画面をクリックすれば操作に復帰します。
>
> **`Esc` は PIE の停止**なので、これで抜けると再生からやり直しになります。
> プレイヤーから離れて見回したい場合は `F8`（エジェクト）です。
>
> 変更は**次に発火した時点で反映されます**。発火のたびに EventMap を
> 引き直すので、PIE を再起動する必要はありません。

停止は **`Stop Hapbeat Event`** に同じ `Map` と `Entry` を渡します。

> **保存されるのはエントリの GUID です。** 名前を変えても並べ替えても壊れません。
> `EM_BasicExample` のように**同じイベント ID が 2 つある**場合（one-shot と loop）も、
> ドロップダウンで選んだ方が一意に指されます。
>
> **`Map` / `Entry` はどちらも変数から繋げます。** ただし `Map` に線を繋いだ瞬間、
> `Entry` のドロップダウンは選べなくなり **`(map is linked)`** と表示されます。
> どのマップが来るか実行時まで分からず、エントリ名の一覧を作れないためです。
>
> 選択済みのエントリを変数にしたい場合は、**`Map` にアセットを直接指定したノードで
> `Entry` を選んでから**、`Entry` ピンを右クリック → **変数に昇格** してください。
> 選んだ値がその変数の初期値になります。
>
> 逆に、**何も選ばないうちに `Entry` だけを BP 変数にすると名前で選べません** —
> エントリ名で選べるのは、**隣に EventMap がある場所**（同じノード上、または後述の
> C++ プロパティ）だけだからです。
> `Map` + `Entry` をひとまとめに使い回したい場合は、両方を引数に取る
> **BP 関数**にまとめるのが確実です。

> 連打で詰まるようなら、クールダウンを持つパターン部品（[4-3](#4-3-パターン部品)）を
> 使うか、Blueprint 側で発火間隔を制御してください。

### 4-2. C++ から鳴らす

4-1 と同じこと（`EM_BasicExample` のエントリをキーで鳴らす）を C++ で書きます。
違いは Blueprint ノードの代わりに **`UHapbeatBlueprintLibrary::PlayHapbeatEvent` を呼ぶ**ことだけで、
**どのエントリを鳴らすかは 4-1 と同じく詳細パネルのプルダウンで選びます**。
キーは 4-1 の BP と重ならないよう **H** にします。

> **先に 1 回だけ必要な準備があります。**
> [UE プロジェクトのビルド](./unreal-build.md) の「C++ コードを書く準備」を
> 済ませてください。要点は 2 つです:
> - Blueprint のみのプロジェクトなら、**C++ プロジェクトに変換**が必要
> - `Source/<プロジェクト名>/<プロジェクト名>.Build.cs` の
>   `PublicDependencyModuleNames` に **`"HapbeatSDK"` を追加**（これが無いと
>   `HapbeatSubsystem.h` が見つからずビルドが止まります）

1. **C++ クラスを作る**
   **ツール → 新規 C++ クラス → Actor** を選び、名前を `HapbeatCppTest` にして作成。
   エディタがコンパイルして開き直します

2. **ヘッダを書く**（`HapbeatCppTest.h` を丸ごと次で置き換える）

   ```cpp
   #pragma once

   #include "CoreMinimal.h"
   #include "GameFramework/Actor.h"
   #include "HapbeatEntryRef.h"   // FHapbeatEntryRef is held by value: needs the full type
   #include "HapbeatCppTest.generated.h"

   class UHapbeatEventMap;

   UCLASS()
   class あなたのプロジェクト名_API AHapbeatCppTest : public AActor
   {
       GENERATED_BODY()

   public:
       /** Pick EM_BasicExample in the Details panel. */
       UPROPERTY(EditAnywhere, Category = "Hapbeat")
       TObjectPtr<UHapbeatEventMap> EventMap;

       /**
        * The meta link makes the Details row for Entry a dropdown of THIS
        * EventMap's entry names.
        */
       UPROPERTY(EditAnywhere, Category = "Hapbeat", meta = (HapbeatEventMap = "EventMap"))
       FHapbeatEntryRef Entry;

   protected:
       virtual void BeginPlay() override;

   private:
       void HandleKey();
   };
   ```

   > **`#include "HapbeatEntryRef.h"` は `.generated.h` より前に置いてください。**
   > `FHapbeatEntryRef` は構造体を**値で**持つため、前方宣言では足りず実体が要ります。
   > include が無い / 順番が後ろだと、`FHapbeatEntryRef` が未定義というエラーを
   > 先頭に十数行のエラーが出ます（`.cpp` 側の `Entry` を触る行まで芋づる式に
   > 落ちるため、原因が分かりにくくなります）。
   > `.generated.h` は**常に最後**の include です。
   >
   > `UHapbeatEventMap` はポインタで持つだけなので、**前方宣言で足ります**
   >（実体が要るのは `.cpp` 側だけです）。

   > **`Entry` は `EventMap` と必ずセットで書きます。** どのマップのエントリかは
   > 常に「隣」が示す設計で、`meta = (HapbeatEventMap = "EventMap")` がその
   > 「隣」を名前で指しています。この meta が無いと、詳細パネルは名前の
   > プルダウンではなく生の GUID 欄になります（値は編集できます）。

   > `あなたのプロジェクト名_API` は、生成されたヘッダに元から入っているマクロを
   > そのまま使ってください（例: プロジェクトが `MyGame` なら `MYGAME_API`）。

3. **cpp を書く**（`HapbeatCppTest.cpp` を丸ごと次で置き換える）

   ```cpp
   #include "HapbeatCppTest.h"   // own header must be included first

   #include "HapbeatBlueprintLibrary.h"
   #include "HapbeatEventMap.h"   // TObjectPtr passed as an argument: needs the full type
   #include "Components/InputComponent.h"
   #include "Engine/World.h"
   #include "GameFramework/PlayerController.h"
   #include "InputCoreTypes.h"   // EKeys::H

   void AHapbeatCppTest::BeginPlay()
   {
       Super::BeginPlay();

       APlayerController* PC =
           GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
       if (PC == nullptr)
       {
           return;
       }

       // Equivalent of Blueprint's Auto Receive Input = Player 0.
       // Without it the key binding below is never reached.
       EnableInput(PC);
       if (InputComponent != nullptr)
       {
           InputComponent->BindKey(EKeys::H, IE_Pressed, this, &AHapbeatCppTest::HandleKey);
       }
   }

   void AHapbeatCppTest::HandleKey()
   {
       // Everything else (Command vs Stream Clip, clip, gain, target, loop)
       // comes from the entry the Details panel points at.
       UHapbeatBlueprintLibrary::PlayHapbeatEvent(this, EventMap, Entry);
   }
   ```

   > **エントリの選択はエディタ（詳細パネル）側の仕事**なので、コードには
   > `PlayHapbeatEvent` の 1 行しか出てきません。EventMap 未設定・エントリ未選択の
   > ときは `PlayHapbeatEvent` が Output Log に警告を出すので、呼び出し側での
   > null チェックは省いています。
   >
   > 第 1 引数の `this` は**ワールドを引くための文脈**です（Blueprint ノードでは
   > 自動で埋まる、隠しピンにあたります）。

   > **include の順番に注意。** UE は `.cpp` が自分のヘッダを最初に include して
   > いることを要求します。上に他のものを足すと
   > `Expected HapbeatCppTest.h to be first header included.` で止まります。

   > **コード中のコメントを英語にしてあるのは意図的です。**
   > 日本語のコメントを書く場合は、ファイルを **UTF-8（BOM 付き）** で保存して
   > ください。日本語版 Windows では BOM 無しのファイルが Shift-JIS と誤認され、
   > コメントが文字化けします。さらに一部の文字は 2 バイト目が `\`（0x5C）で、
   > 行末に来ると**次の行がコメントに飲み込まれて**原因不明のエラーになります。

4. **ビルドする**
   `Build.cs` を変更した直後は **Live Coding では反映されません**。
   エディタを閉じてリビルドし、開き直してください

5. **レベルに配置して鳴らすエントリを指定する**
   `HapbeatCppTest` をレベルにドラッグ&ドロップし、詳細パネルで
   `Event Map` に **`EM_BasicExample`**、`Entry` に
   **`demo_stream_sine_100hz`** を選ぶ（4-1 のノードと同じ 2 段です）

6. ▶ Play して **H** を押す → 100Hz が 1 回鳴る

停止は `UHapbeatBlueprintLibrary::StopHapbeatEvent(this, EventMap, Entry)` です。

> `PlayHapbeatEvent` の第 4 引数 `GainMultiplier` で、**その呼び出しだけ**強さを変えられます
>（エントリの設定は変わりません）。
>
> **`Play(TEXT("kit.clip"), Gain)` という直接送信の API もありますが、通常は使いません。**
> EventMap を経由しないため、ゲイン・送信先・ループをコード側に書くことになり、
> §3 で分けた「鳴らす場所」と「鳴らし方」が再び混ざります。

### 実行時に変えられるもの

エントリの設定（§3）は「既定値」です。呼び出し側からは次の 4 つを実行時に動かせます。

| 変えたいもの | 手段 | 対象 |
|---|---|---|
| 呼び出しごとの強さ | ノードの **`Gain Multiplier`** ピン（C++ は第 4 引数） | FIRE / CLIP |
| 左右のバランス（パン） | ノードの **`Pan`** ピン（呼び出し時）＋ 再生中は **戻り値の `Set Pan`** | 呼び出し時は FIRE / CLIP、再生中は **CLIP のみ** |
| 再生中の強さ・この再生だけの停止 | **ノードの戻り値**（`Apply Gain Modulation` / `Stop`） | **CLIP のみ** |
| 送信先 | エントリの `Target`（作成時）＋ 実行時は **`Set Address Override`**（[応用](./advanced.md#複数の-hmd-に-1-台ずつ-hapbeat-を割り当てる)） | FIRE / CLIP |

`Pan` は -1 = 左 / 0 = 中央 / +1 = 右。FIRE ではデバイス側が左右のゲインに展開し、
CLIP では SDK がステレオのサンプルへ事前に掛ける。**FIRE のパンにはデバイス
ファームウェアの対応版が必要**で、未対応のファームウェアは中央で再生する
（更新は [Hapbeat Studio](https://studio.hapbeat.com/) から）。

**FIRE は送信したあと何も変えられません。** 再生自体はデバイス内で完結するため、
呼び出し時に渡した値（ゲイン・パン）が最後まで使われます。
鳴っている最中に強弱を付けたい場合は CLIP を使ってください。

> **パンがエントリ（EventMap）側に無いのはなぜか**
> パンは「どこで起きたか」を知っている**呼び出し側の値**で、エントリの既定値として
> 決められる性質のものではないためです。まず呼び出し時の引数として成立させました。
> また CLIP のパンは SDK がステレオのサンプルへ事前に掛ける処理なので、
> モノラルのクリップには効きません（左右に振るにはアップミックスが要る）。
> 再生中に動かす場合は、引き続き戻り値の `Set Pan` と
> `Hapbeat Parameter Binding`（[4-3](#4-3-パターン部品)）を使ってください。

### 4-3. パターン部品

よくある発火パターンを 1 コンポーネントに畳んだ**任意の**部品です。内部では
4-1 と同じ再生経路（`PlayEntry`）を呼ぶので、**必須ではありません** — UE 標準のイベント
（`OnComponentHit` など）に `Play Hapbeat Event` を繋いでも同じことはできます。
検出やタイミング制御を自前で書きたくないときに使ってください。
再生経路が同じなので、**遅延補正（`Haptic Delay Seconds` ＋ `Delay Offset Seconds`）も
ゲインの計算もノードと同一**です。部品側の設定は、そこに掛かる倍率として乗ります。

いずれもアクターの **+ 追加** から足し、`Event Map` と `Entry Id` を指定します。

| コンポーネント | 用途 |
|---|---|
| **Hapbeat Collision Trigger** | 物理の衝突・重なりで自動発火。**衝突速度に応じて強さを変えられる** |
| **Hapbeat Sequence** | 掴む→保持→離す の 3 段階（開始 1 発 → ループ → 終了 1 発） |
| **Hapbeat Parameter Binding** | 再生中のストリームのゲイン / パンを実行時に変化させる |

`Collision Trigger` と `Sequence` は**自分で `Fire` を呼びません**。
発火のきっかけ（衝突、掴む→離す）をコンポーネント自身が検出します。

> 基底クラスの `Hapbeat Trigger` 自体は C++ 側に残っていますが、
> **+ 追加 の候補には出しません**。素の発火はグラフの
> `Play Hapbeat Event` が担当するためです。

#### Hapbeat Collision Trigger

グラフ配線は不要で、**当たった瞬間にコンポーネントが自分で鳴らします**。

1. **コライダーを持つアクターに付ける**
   ルートが Primitive ならそれ、無ければ最初に見つかった Primitive に自動で紐付きます
2. `Trigger Event` を選ぶ
   - **`Hit`** — ブロッキング衝突。コライダーの
     **「Simulation Generates Hit Events」を on** にしてください。
     off だと**何も起きません**（Output Log に警告が出ます）
   - **`Begin Overlap`** — 重なり開始。**「Generate Overlap Events」が on** であること
3. （任意）`Tag Filter` — 指定タグを持つアクターに当たった時だけ鳴らす
4. （任意）`Gain Mode` を **`Velocity Scaled`** にすると**衝突速度で強さが変わります**
   - `Velocity Threshold` 未満の衝突は無視
   - `Max Velocity` で 1.0 に正規化（`Velocity Curve` を入れればカーブで整形）

#### Hapbeat Sequence

`Fire()` を掴む操作に、`Stop()` を離す操作に繋ぐと、
**開始 1 発（`Start Entry Id`）→ ループ（`Entry Id`）→ 終了 1 発（`Stop Entry Id`）**
の 3 段階を 1 コンポーネントで扱えます。

終了 1 発は、ループを止めてから **`Stop Shot Delay`**（既定 `0.05` 秒）だけ遅れて
発火します。ループの停止でデバイス側のリングバッファがフラッシュされるため、
その直後に次の再生を始めると終了音が食われて途切れます。既定値はこれを避けるための
ごく短い間隔です（`0` にすると即座に発火します）。

**動く実例は Showcase の Z1 Bowling** です（ピン 1 本ごとに衝突トリガを持ち、
ボールの当たり方で強さが変わります）。`Sequence` の実例は Z3 Fishing です。
配置して試す手順は[応用](./advanced.md#showcase-サンプル)にあります。

## 次に読むもの

- [応用](./advanced.md) — 複数 HMD への割り当て、送信先の絞り込み、Showcase
  サンプル、イベント ID を直接送る特殊ケース
- [UE プロジェクトのビルド](./unreal-build.md) — C++ プロジェクト化、`Build.cs`、
  Live Coding の効く範囲、ビルドが通らないときの対処
