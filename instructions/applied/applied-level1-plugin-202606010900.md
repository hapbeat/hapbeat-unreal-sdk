# applied: level-1 Unreal プラグインを追加（workspace セッション）

- **編集元セッション**: hapbeat-sdk-workspace（統括）/ 2026-06-01
- **関連 DEC**: DEC-032（ツール向け SDK 拡張）

## この repo に入った変更

- `HapbeatSDK.uplugin`（新規）
- `Source/HapbeatSDK/`（新規）: `HapbeatSDK.Build.cs` / `Private/HapbeatSDKModule.cpp` /
  `Public/HapbeatProtocol.h` + `Private/HapbeatProtocol.cpp` /
  `Public/HapbeatSubsystem.h` + `Private/HapbeatSubsystem.cpp`
- `README.md` / `CLAUDE.md` — level-1 実装内容に全面更新
- `docs/getting-started.md`（新規）/ `docs/coming-soon.md`（削除：stale）
- `.gitignore` — UE 生成物の ignore を追加

## 背景

「unity-sdk に続き他ツールへ SDK 拡張」方針（DEC-032）の level-1 の一環。
contracts Layer 1 を C++ で実装し、`UHapbeatSubsystem`（BlueprintCallable）で C++/Blueprint 両用の
fire API（Connect/Play/Stop/StopAll/Ping）を提供。wire 仕様は `HapbeatProtocol.cs` / `protocol.py` と一致。

## 検証状況

- ⚠️ **未コンパイル**（workspace 環境に UE ツールチェーンが無い）。
- UE5 API（FUdpSocketBuilder / ISocketSubsystem / UGameInstanceSubsystem）に忠実に記述。

## この repo のエージェント / 人間へのアクション

1. **UE プロジェクトに入れてビルド確認**（最優先）。コンパイルエラーがあれば修正。
2. 実機で Play/Stop の wire 動作を確認（Studio で kit を配信したデバイス相手）。
3. 問題なければ本 note を `instructions/completed/` へ移動。
4. level-2: Blueprint Trigger コンポーネント / EventMap 風 Editor / device discovery。
