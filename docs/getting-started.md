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

> **このサンプルには EventMap アセットがありません。**
> 「アクターを置くだけで動く」ことを優先し、イベント定義（ゲイン・対象・モード）を
> **C++ で組み立てています**（`AHapbeatBasicExampleActor::BuildEventMap()`）。
> そのため **Kit の `manifest.json` を編集しても、このサンプルの強さは変わりません**
> （intensity の `0.5` はコードに直接書かれています）。
> 実際の開発では次章の **EventMap アセット**を作り、GUI で編集するのが標準の流れです。
> このサンプル自体を GUI 編集に切り替えることもできます（§4 の最後を参照）。

---

## 3. 自分のプロジェクトから鳴らす

サブシステムは**起動時に自動接続**します（`Initialize()` の時点。レベルの `BeginPlay` より前）。
そのため `Connect` を呼ばなくても、いきなり `Play` して構いません。

### Blueprint の場合（最短で試す）

**レベルブループリント**を使うのが一番早く、アセットを一つも作らずに試せます。

1. §2 で作った **Basic** レベルを開いた状態で、ツールバーの
   **ブループリント → レベルブループリントを開く**
2. グラフの空白を右クリック → 検索欄に `Hapbeat` と入力 →
   **Get Hapbeat Subsystem** を追加
   （サブシステムは UE が自動でノード化するので、この 1 個で取得できます）
3. そのノードの青いピンから線を引き、`Play` を検索して **Play** を追加
   - `Event Id`: `basic-exam-kit.sine_200hz_1s`（`<kit名>.<ファイル名>`）
   - `Gain`: `0.5` など
   - `Target`: 空のまま（全デバイス宛て）
4. **発火のきっかけ**を繋ぐ。どちらかで確認できます:
   - **確実な方法**: 右クリック → `Event BeginPlay` を追加 →
     `Delay`（Duration `2.0`）→ `Play` の順に白い実行ピンを繋ぐ
     → **Play を押して 2 秒後に自動で振動**します
   - **キーで試す**: 右クリック → `Keyboard Events` → `G` などを追加し、
     その `Pressed` から `Play` に繋ぐ
5. **コンパイル**（ブループリントエディタ左上）→ レベルに戻って **▶ Play**

> Command モード（イベント ID を送る方式）なので、**Kit の書き込みが必要**です。
> 書き込んでいない場合は、代わりに §2 の Space（StreamClip）で確認してください。

### C++ の場合

1. エディタで **ツール → 新規 C++ クラス → Actor** を選び、名前を付けて作成
   （例: `MyHapticActor`）
2. **自分のプロジェクトの `*.Build.cs`** を開き、`PublicDependencyModuleNames` に
   `"HapbeatSDK"` を足す

   **`Build.cs` は Unreal エディタでは開けません。** ディスク上のテキストファイル
   （C# のビルド設定）なので、エディタの外で編集します。

   場所（`<>` は自分のプロジェクト名に読み替え）:

   ```
   <プロジェクトフォルダ>\Source\<プロジェクト名>\<プロジェクト名>.Build.cs
   ```

   開き方は 3 通り、どれでも構いません:

   | 方法 | 手順 |
   |---|---|
   | **エディタから** | **ツール → Visual Studio を開く**（`Open Visual Studio`）→ VS の Solution Explorer で `Games → <プロジェクト名> → Source` を展開 |
   | **VS から直接** | `.sln` を開き、同じく Solution Explorer から辿る |
   | **エクスプローラから** | 上のパスのファイルを右クリック → メモ帳や VS Code で開く（ただのテキストです） |

   編集後の中身（既存の行に `"HapbeatSDK"` を足すだけ）:

   ```csharp
   PublicDependencyModuleNames.AddRange(new string[] {
       "Core", "CoreUObject", "Engine", "InputCore", "HapbeatSDK" });
   ```

3. 作った Actor に下のコードを書く
4. **エディタを閉じてリビルド**（`Build.cs` を変えたので Live Coding では反映されません）
5. エディタを開き直し、作った Actor をレベルに置いて **▶ Play**


```cpp
#include "HapbeatSubsystem.h"
#include "Engine/GameInstance.h"

void AMyHapticActor::BeginPlay()
{
    Super::BeginPlay();

    if (UHapbeatSubsystem* Hb = GetGameInstance()->GetSubsystem<UHapbeatSubsystem>())
    {
        Hb->Play(TEXT("basic-exam-kit.sine_200hz_1s"), 0.5f);
    }
}
```

ヘッダ側はこうなります:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MyHapticActor.generated.h"

UCLASS()
class あなたのプロジェクト名_API AMyHapticActor : public AActor
{
    GENERATED_BODY()

protected:
    virtual void BeginPlay() override;
};
```

> `あなたのプロジェクト名_API` は、UE が生成したクラスに元から入っている
> マクロ（例: プロジェクト名が `MyGame` なら `MYGAME_API`）をそのまま使ってください。

#### C++ を編集したあと、どこまでエディタを開いたままにできるか

**▶ Play（PIE）はコンパイルしません。** `.cpp` を保存しただけでは何も変わらず、
必ずどこかでコンパイルを挟む必要があります。方法は 2 つあり、
**変更の種類によってどちらを使えるかが決まります**。

| 変更した内容 | 反映方法 |
|---|---|
| 既存の関数の**中身**だけを書き換えた | **Live Coding**。エディタを開いたまま反映されます（次項） |
| `.h` / `.cpp` を**新規追加**した（新しいクラスを作った） | エディタを閉じてビルド |
| `*.Build.cs` を変更した | エディタを閉じてビルド |
| `UPROPERTY` / `UFUNCTION` / `UCLASS` / `USTRUCT` を追加・変更した | エディタを閉じてビルド |
| メンバ変数の追加など、**クラスのレイアウトが変わる**変更をした | エディタを閉じてビルド |

Live Coding は実行中のプロセスに機械語パッチを当てる仕組みなので、
**既存関数の差し替えはできても、リフレクション情報やクラスの形が変わる変更は扱えません**。
上の手順 1〜2 は「新規クラス作成」と「`Build.cs` 変更」の両方に当たるため、
**初回は必ずエディタを閉じてビルド**が必要です。そのあと `BeginPlay()` の中身を
調整していく段階からは、下の方法でエディタ上からコンパイルできます。

##### エディタ上でコンパイルする（Unity の `Ctrl` + `R` に相当）

3 通りあり、**どれも同じ Live Coding のコンパイルを呼びます**。好きなものを使ってください。

| 方法 | 場所・キー |
|---|---|
| **Compile ボタン** | エディタ**下部のステータスバー**にあるコンパイルアイコン。隣のコンボボタンから Live Coding の設定も開けます |
| **キーボード** | `Ctrl` + `Alt` + `Shift` + `P`（コマンド名は `Recompile Game Code`。**編集 → エディタの環境設定 → キーボードショートカット** で変更できます） |
| **Live Coding のホットキー** | `Ctrl` + `Alt` + `F11`。エディタではなく Live Coding コンソール側が持つグローバルホットキーで、コンソールの設定（`compile_shortcut`）で変更できます |

コンパイル結果は画面右下に通知として出ます。失敗した場合は通知から
出力ログを開いてエラーを確認してください。

> Live Coding が使えるかは **編集 → エディタの環境設定 → Live Coding** で確認できます。
> 無効な場合は、常にエディタを閉じてビルドしてください。

> **ビルド時に `Unable to build while Live Coding is active` と出たら**、
> エディタがまだ起動しています。完全に終了してからビルドし直してください。

#### ビルドでつまずいたときのメモ

- **Visual Studio では「Build」を使い、「Rebuild」は使わない。**
  Rebuild は中間生成物を捨てて共有 PCH から作り直すため、時間がかかるうえに
  下のツールチェーン起因の失敗を踏みやすくなります。増分 Build で十分です。
- **VS の「エラー一覧」ウィンドウはあてになりません。** IntelliSense の解析エラー
  （`識別子 "FTextureBuildSettings" が定義されていません`、根拠のない `override`
  エラーなど）が混ざります。実際に何が失敗したかは「出力」ウィンドウか、
  `%LOCALAPPDATA%\UnrealBuildTool\Log.txt` で確認してください。
- **`ConcurrentLinearAllocator.h` で `__has_feature` が未定義、というエラーが出たら**、
  それは Hapbeat SDK ではなく MSVC ツールチェーンの問題です。UE 5.4 はこの箇所を
  `<sanitizer/asan_interface.h>` の有無で切り替えますが、このヘッダを同梱する
  MSVC と同梱しない MSVC があり、Visual Studio に複数バージョンが入っていると
  組み合わせによって Clang 専用の分岐に落ちます（UE は `/we4668` で
  これをエラーに昇格させます）。プロジェクト直下の `BuildConfiguration.xml` で
  使うツールセットを固定すると再発しません:

  ```xml
  <?xml version="1.0" encoding="utf-8" ?>
  <Configuration xmlns="https://www.unrealengine.com/BuildConfiguration">
    <WindowsPlatform>
      <CompilerVersion>14.44.35207</CompilerVersion>
    </WindowsPlatform>
  </Configuration>
  ```

  バージョン番号は `C:\Program Files\Microsoft Visual Studio\2022\<エディション>\VC\Tools\MSVC\`
  にあるフォルダ名から、`include\sanitizer\` を**持たない**方を選びます。

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
   - `Mode`: `Command`（Kit 必要）か `StreamClip`（Kit 不要）
   - `Category` / `Event Name`: 合わせて `<Category>.<EventName>` がイベント ID になる
   - `Gain`: `0`〜`2`
   - `Target`: 空 = 全デバイス
5. Details パネル上部の **Refresh Intensities** を押す
   → Kit の `manifest.json` に書かれた `intensity` を各エントリに焼き込みます
6. **Test Play** で、再生（PIE）せずにその場で鳴らして確認できます

> **`Hapbeat Clip`** アセット（§6 のストリーミング用）も、同じ
> **Hapbeat** カテゴリから同じ手順で作成できます。

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

> **なぜ専用ウィンドウではなく Details パネルなのか**
> Unity SDK は専用のエディタウィンドウを持っていますが、UE ではこれらの操作を
> Details パネルが標準で提供するため、あえて独自ウィンドウを作っていません。
> 学習することが少なく、UE の他のアセットと同じ操作感で扱えます。

> 実際にデバイスへ送られるゲインは
> **`Gain` × `Kit の intensity` × トリガ側の倍率** です。
> Studio で作り込んだ強さ（intensity）を土台に、UE 側で微調整する設計になっています。

### BasicExample を GUI 編集に切り替える（任意）

疎通確認に使ったサンプルを、そのまま GUI 編集の練習台にできます。

1. **先に触覚クリップのアセットを作る**（StreamClip エントリで使います）
   - Content Browser → 右クリック → **Miscellaneous → Data Asset** → **Hapbeat Clip**
   - 開いて **[Import WAV...]** を押し、
     `Plugins/HapbeatSDK/Content/HapbeatSamples/BasicExample/Kit/basic-exam-kit/stream-clips/sine_100hz_1s.wav`
     を選ぶ（`16000 Hz, 1 ch, 1.00 s` と表示されれば成功。保存を忘れずに）
2. EventMap アセットを作り、エントリを **3 つ**追加する
   （順番が固定です: `[0]` Space、`[1]` R、`[2]` F）
3. 値はコード側と同じにすると挙動が揃います（`Stream Clip` には 1. で作ったアセットを指定）:

   | # | Mode | Category | Event Name | Gain | Loop | Stream Clip |
   |---|---|---|---|---|---|---|
   | 0 | StreamClip | `basic-exam-kit` | `sine_100hz_1s` | `1.0` | off | `sine_100hz_1s` |
   | 1 | StreamClip | `basic-exam-kit` | `sine_100hz_1s_loop` | `1.0` | **on** | `sine_100hz_1s` |
   | 2 | Command | `basic-exam-kit` | `sine_200hz_1s` | `1.0` | off | — |

4. **Refresh Intensities** を押す（各エントリの intensity が `0.5` になります）
5. レベルに置いた **Hapbeat Basic Example Actor** を選択し、Details の
   **Event Map Override** に作った EventMap アセットを指定する

以降このサンプルはアセット側の値で鳴るので、**Gain や Target を変えて即座に体感差を確認**できます。
未指定（空）のままなら従来どおりコード生成で動きます。

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

**クリップアセットの作り方**: Content Browser → 右クリック → **Miscellaneous → Data Asset**
→ **Hapbeat Clip** で空のアセットを作り、開いて **[Import WAV...]** から `.wav` を読み込みます
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
