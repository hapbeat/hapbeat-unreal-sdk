# UE プロジェクトのビルド

Hapbeat SDK 固有の話ではなく、**Unreal Engine 側の作法**をまとめたページです。
[はじめかた](./getting-started.md) から必要になったときだけ参照してください。

## なぜ C++ プロジェクトが必要か

本 SDK は**ソース形式のプラグイン**（コンパイル済みバイナリを同梱していない）です。
そのため Blueprint だけで作るプロジェクトでも、**一度だけ**ビルドが必要になります。

Blueprint プロジェクトしか無い場合は、エディタで
**Tools → New C++ Class → None → Create Class** を一度実行すれば C++ プロジェクトになります
（以降の作業はすべて Blueprint だけで進められます）。

> Visual Studio 2022（ワークロード「**C++ によるゲーム開発**」）が必要です。

## C++ コードを書く準備

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
// UE が生成した「自分のヘッダ」を必ず 1 行目に置く。
// この 2 本はその「後ろ」に足すこと（下の注意を参照）
#include "MyHapticActor.h"

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

> **include を足す位置に注意。** UE は `.cpp` が**自分のヘッダを最初に
> include している**ことを要求します。エディタが生成した `.cpp` の先頭には
> すでに `#include "MyHapticActor.h"` が入っているので、**その下に**
> `HapbeatSubsystem.h` などを足してください。上に貼ると
> `Expected MyHapticActor.h to be first header included.` でビルドが止まります。

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

## C++ を編集したあと、どこまでエディタを開いたままにできるか

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

###### エディタ上でコンパイルする（Unity の `Ctrl` + `R` に相当）

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

##### ビルドでつまずいたときのメモ

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
