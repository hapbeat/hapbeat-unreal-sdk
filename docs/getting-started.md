---
kind: tutorial
sidebar:
  order: 1
  label: Getting Started
---

# Getting Started

このページでは、空のプロジェクトを新しく作り、SDK を入れて、同梱の `BasicExample` から Hapbeat を 1 回再生するところまでを行います。既存のプロジェクトに入れる場合は、1. を飛ばして 2. から進めてください。Event Map の作成、Blueprint / C++ の実装、複数端末への送信は次のページへ分けています。

:::tip[ヒント: AI に初期セットアップを任せる]

ファイル操作と Unreal の build を実行できる AI には、次の依頼文を渡します。`<ProjectRoot>` は対象プロジェクトの絶対パスに置き換えます。

```text
Unreal Engine のプロジェクト <ProjectRoot> に Hapbeat Unreal SDK を導入してください。
https://github.com/hapbeat/hapbeat-unreal-sdk/releases/latest から、<ProjectRoot> の .uproject の EngineAssociation に合う HapbeatSDK-<version>-UE<バージョン>.zip をダウンロードし、<ProjectRoot>/Plugins/HapbeatSDK/ に中身を展開してください（Plugins/HapbeatSDK/HapbeatSDK.uplugin ができる配置）。
合う ZIP が無い場合だけ、https://github.com/hapbeat/hapbeat-unreal-sdk.git を <ProjectRoot>/Plugins/HapbeatSDK に clone し、project files を再生成して Editor target を build してください。
PIE、Test Play、実機への触覚送信は実行しないでください。
```

:::

## 動作環境

-   Unreal Engine 5.3 以上で動作する想定です（動作確認済み: 5.6.1、5.4）。特にこだわりがなければ、Hapbeat のデモ制作で使っている 5.6.1 がおすすめです。
-   Windows では、ビルド済み ZIP（UE 5.4 / 5.5 / 5.6）をエディタでそのまま使えます。Visual Studio は要りません。Blueprint だけのプロジェクトでも使えます。
-   ソースから入れる場合（Mac・ZIP の無いバージョン）と、ゲームをパッケージする場合（プラグインが `Source/` からビルドされます）は、C++ のビルド環境（Windows は Visual Studio 2022）が必要です。

## 1\. 新しいプロジェクトを作る

1.  Epic Games Launcher の **Unreal Engine → ライブラリ** で、使う Unreal Engine（例: 5.6）の **起動** を押します。
2.  Unreal Project Browser で **GAMES → Blank** を選びます。
3.  右側の Project Defaults を次のようにします。
    -   **Blueprint**（C++ ではなく）を選びます。Visual Studio が無くても作れます。
    -   **Starter Content** はオフにします（使いません）。
4.  **Project Location** に保存先のフォルダ、**Project Name** に英数字の名前（例: `HapbeatTest`）を入れて **Create** を押します。
5.  エディタが開いたら、**File → Exit** で一度閉じます。SDK はエディタを閉じた状態で入れます。

## 2\. SDK を入れる

1.  [最新のリリース](https://github.com/hapbeat/hapbeat-unreal-sdk/releases/latest) の Assets から、1. で起動した Unreal Engine に合う ZIP をダウンロードします（例: UE 5.6 なら `HapbeatSDK-1.0.0-UE5.6.zip`）。Unreal Engine のバージョンは、エディタの **Help → About Unreal Editor** でも確認できます。
2.  プロジェクトのフォルダ（`HapbeatTest.uproject` があるフォルダ）に `Plugins` フォルダを作り、その中に `HapbeatSDK` フォルダを作ります。
3.  ZIP の**中身**を `Plugins/HapbeatSDK/` に展開します。エクスプローラーの「すべて展開」は ZIP と同じ名前のフォルダを作ろうとするので、展開先を `Plugins\HapbeatSDK` に変えてください。`Plugins/HapbeatSDK/HapbeatSDK.uplugin` があれば正しい配置です。
4.  `HapbeatTest.uproject` をダブルクリックしてプロジェクトを開きます。ZIP にはビルド済みのバイナリが入っているため、ビルドは不要です。「モジュールを再ビルドしますか」と聞かれた場合は、[うまくいかないとき](#うまくいかないとき) を見てください。

**更新**: Unreal Editor を閉じ、`.uproject` があるフォルダで次を実行します。最新の ZIP をダウンロードして SHA256 を確認し、古いフォルダを `Saved/HapbeatSDK-backup-<バージョン>-<日時>/` へ移してから入れ替えます（`-Version vX.Y.Z` で版を指定、`-WhatIf` で試行のみ）。

```
powershell -ExecutionPolicy Bypass -File Plugins/HapbeatSDK/Scripts/Update-HapbeatSDK.ps1
```

<details>
<summary>補足: ソースから入れる場合（git clone）・複数プロジェクト</summary>

-   **ソースから入れる場合**（ZIP の無い UE 5.3 以上のバージョン、ソースビルドのエンジン、SDK の開発）: `.uproject` があるフォルダで `git clone https://github.com/hapbeat/hapbeat-unreal-sdk.git Plugins/HapbeatSDK` を実行し、project files を再生成して **Development Editor / Win64** を build します。[ビルド手順](./unreal-build.md)
-   **パスを指定して clone する場合**: パスにスペースが含まれるときは `"..."` で囲みます。囲まないと `Too many arguments` で失敗します。
-   **ソース版の更新**: `Plugins/HapbeatSDK` で `git pull`（リリースに固定するなら `git checkout vX.Y.Z`）を実行し、build し直します。更新スクリプトは git clone を対象にしません。
-   **複数プロジェクト**: プロジェクトごとに入れます。プロジェクトを Git で管理しているなら `git submodule add` でも構いません。
-   **1 つのプラグインフォルダをリンクで複数プロジェクトから共有しないでください。** build 成果物がプラグインのフォルダ内に作られるため、プロジェクト間で DLL が上書きされます。

</details>

## 3\. プラグインを確認する

1.  Unreal Editor の **Edit → Plugins** で `Hapbeat SDK` が有効になっていることを確認します（無効なら有効にして、エディタを再起動します）。
2.  **Tools → Hapbeat → Hapbeat Settings** を開きます。Project Settings の **Plugins → Hapbeat** で、`Connection > Port` を Hapbeat と同じネットワークで使う UDP port と一致させます。既定値は `7700` です。

:::note[音と触覚のタイミング]

音声出力の遅延は環境ごとに異なるため、触覚が音より先に感じられることがあります。`Haptic Delay Seconds` に少量の遅延を足し、音と触覚のタイミングを合わせます。

:::

## 4\. BasicExample を再生する

1.  Content Browser の Settings から **Show Plugin Content** を有効にします。
2.  `Plugins/HapbeatSDK/Content/HapbeatSamples/BasicExample/Maps/BasicExample` を開きます。
3.  PIE を開始し、`Space` を押します。

100 Hz の StreamClip が再生されれば、SDK の導入とネットワーク送信は完了です。この操作はデバイスへの Kit 配布を必要としません。

### Fire（Command）を試す

1.  Hapbeat Studio で `Plugins/HapbeatSDK/Content/HapbeatSamples/BasicExample/Kit/basic-exam-kit/` の Kit を開きます。
2.  接続済みの Hapbeat を選び、**Deploy** で `basic-exam-kit` を Hapbeat へデプロイします。Kit 内の `install-clips/sine_200hz_1s.wav` がデバイスに配置されます。
3.  BasicExample の PIE に戻り、`F` を押します。

`F` は Fire（Command）として `basic-exam-kit.sine_200hz_1s` を再生します。`Space` のストリーミング再生を先に確認してから、Kit に含めた install-clip の再生を確認する順序です。

反応しない場合は、Hapbeat の電源・同一ネットワーク・UDP port と、OS のファイアウォールが Unreal Editor の UDP 通信を許可しているかを確認してください。**Output Log** の `LogHapbeat` で PONG・エラーを確認できます。

## うまくいかないとき

-   **「モジュールを再ビルドしますか」「could not be compiled」が出る**: ZIP がエンジンのバージョンと合っていません。Help → About Unreal Editor のバージョン（5.4 / 5.5 / 5.6）と ZIP の名前を見比べ、合う ZIP を入れ直してください。ZIP は Epic Games Launcher 版のエンジン（UE 5.4.4 / 5.5.4 / 5.6.1）で作っています。ソースからビルドしたエンジンでは使えないので、ソースから入れてください（[2. の補足](#2-sdk-を入れる)）。
-   **`Plugins/HapbeatSDK/HapbeatSDK.uplugin` が無い**: ZIP を 1 階層深く展開しています（`Plugins/HapbeatSDK/HapbeatSDK-1.0.0-UE5.6/HapbeatSDK.uplugin` など）。中のファイルを `Plugins/HapbeatSDK/` の直下へ移してください。
-   **Space を押しても振動しない**: 4. の最後の項目（電源・同一ネットワーク・UDP port・ファイアウォール）を確認してください。

## 同梱サンプル

| レベル | 確認できること |
| --- | --- |
| [BasicExample](./basic-example.md) | 最小構成の Stream / Fire（Command）再生 |
| [Showcase](./showcase-unreal.md) | Z1〜Z5 のゲーム内イベントと触覚再生の接続例 |
| [VR Config Example](./vr-config-example.md) | VR コントローラーでの Address Override 設定と Test 再生 |

## 次に読むもの

-   [Event Map と再生](./event-map-and-playback.md) — Event Map を作り、Blueprint / C++ から再生する
-   [C++ API](./cpp-api.md) — Event Map 再生と直接送信を C++ で呼ぶ
-   [Blueprint ノード](./blueprint-nodes.md) — 全 Blueprint node と component のリファレンス
-   [Showcase](./showcase-unreal.md) — Z1〜Z5 のゲーム内イベントと触覚再生の接続例
-   [ターゲティング](./targeting-and-multi-hmd.md) — Target と Address Override
