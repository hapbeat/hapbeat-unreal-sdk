---
kind: howto
sidebar:
  label: プロジェクトのビルド
---

# プロジェクトのビルド

ソース版 Hapbeat SDK を Windows の C++ プロジェクトに組み込む手順です。Visual Studio 2022 の「C++ によるゲーム開発」を導入しておきます。

## プラグインをビルドする

1. Unreal Editor を閉じます。
2. プロジェクトの `.uproject` を右クリックし、**Generate Visual Studio project files** を実行します。
3. 生成された `.sln` を開き、**Development Editor / Win64** を選んでプロジェクトをビルドします。
4. `.uproject` を開き、[Getting Started](./getting-started.md) のプラグイン有効化へ進みます。

失敗した場合は Visual Studio の **Output > Build** で最初のコンパイルエラーを確認します。「could not be compiled」だけでは原因は特定できません。

## C++ から SDK を使う

**Tools → Open Visual Studio** を開き、`Source/<Project>/<Project>.Build.cs` の依存モジュールへ `HapbeatSDK` を追加します。

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine", "HapbeatSDK"
});
```

Editor を閉じて上の手順で再ビルドします。再生処理の include と呼出例は [C++ API](./cpp-api.md) を参照してください。
