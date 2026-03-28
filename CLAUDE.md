# CLAUDE.md — hapbeat-unreal-sdk

## repo の目的

Unreal Engine 向けの薄いアダプタ。共通仕様に基づき、Unreal に適した形で Bridge と接続する。

## 全体アーキテクチャ上の役割

contracts / bridge の共通仕様を利用する後段 SDK。

## 責務

- UDP/OSC クライアント
- Blueprint ノード（将来）
- 基本的な接続管理

## 管理対象

- Unreal C++ / Blueprint コード
- プラグイン設定

## 管理対象外

- Bridge 実装
- ファームウェア
- Pack ツール
- Unity コード

## 依存関係

### 依存してよい repo

- hapbeat-contracts
- hapbeat-bridge

## 壊してはいけないもの

- 公開 API（将来の Blueprint ノード）

## やってはいけないこと

- 独自プロトコルを作る
- Unity SDK からの単純コピー
- 送信機ファームと直接通信する

## まだ作らないもの

- 重いネイティブ統合
- エディタ拡張の大規模実装

## 最初の着手タスク

1. UDP/OSC 基本接続
2. 最小サンプル

## テスト

- UDP 送受信テスト

## オフライン動作

Bridge がローカルにいれば動作可能。

## 重要な概念

- **Event ID** — 再生指示の識別子
- **Bridge** — UDP/OSC の接続先

## 指示書

- `instructions/` — 他セッションからの未実行の指示書
- `instructions/completed/` — 完了済みの指示書
- セッション開始時に `instructions/` を確認し、該当する指示書があれば適用する
