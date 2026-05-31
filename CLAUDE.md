# CLAUDE.md — hapbeat-unreal-sdk

## repo の目的

Unreal Engine 5 向け SDK。runtime プラグイン（`HapbeatSDK`）で Hapbeat を Wi-Fi UDP broadcast 駆動する。
C++ / Blueprint 両用。AAA / インディー・VR/XR のゲーム開発者が主対象。

## 全体アーキテクチャ上の役割

contracts の Layer 1 仕様の上に載る code SDK。Unity SDK と同じ
「起点(fire) ↔ 調整(EventMap)」分離を踏襲（L1 では fire 側を実装、EventMap/Trigger は L2）。

## 責務

- Layer 1 protocol の C++ 実装（`Source/HapbeatSDK/.../HapbeatProtocol.*`、手動 little-endian）
- `UHapbeatSubsystem`（GameInstanceSubsystem、BlueprintCallable）: Connect / Play / Stop / StopAll / Ping
- FSocket(UDP, FUdpSocketBuilder) による broadcast 送信
- `.uplugin` / Build.cs / module

## 管理対象 / 対象外

- 対象: `Source/HapbeatSDK/`、`HapbeatSDK.uplugin`、docs
- 対象外: Layer 1 仕様の改変（→ contracts）、Unity/その他コード、ファームウェア

## 依存関係

- hapbeat-contracts（wire 仕様）。UE モジュール依存: Core/CoreUObject/Engine/Sockets/Networking。

## やってはいけないこと

- 独自プロトコルを作る（contracts に従う）
- Unity SDK の単純コピー
- 後方互換 alias を作る（リリース前）

## まだ作らないもの（level-2 / 3）

- Blueprint Trigger コンポーネント（衝突/アニメ通知 → 自動 fire）
- EventMap 風 Editor ツール（event id → default gain）
- デバイス検出（PONG 受信）/ 定期 CONNECT_STATUS keep-alive（現状は Connect 時 1 回）
- showcase サンプルプロジェクト

## 重要な制約・正直な現状

- **本 repo は UE ツールチェーン無しで author**。UE5 API（`FUdpSocketBuilder` / `ISocketSubsystem` /
  `UGameInstanceSubsystem` / `UFUNCTION(BlueprintCallable)`）に忠実に書いたが、
  **コンパイル検証は未了**。次に UE プロジェクトに入れてビルド確認すること。
- wire 互換の正は firmware が受理する byte 列。`HapbeatProtocol.cs` / `protocol.py` が参照実装。
- CONNECT_STATUS byte 順は `HapbeatProtocol.cs`（connected,group,appName,deviceName）。

## 指示書 / メモリ

- `instructions/`（`completed/` `applied/`）。
- セッション知見は workspace の `../docs/claude-memory/`（INDEX.md 更新）。
