# hapbeat-unreal-sdk

Unreal Engine 向けの薄いアダプタ。

## 概要

このリポジトリは、Hapbeat デバイスを Unreal Engine アプリケーションから制御するための SDK を提供します。共通仕様を利用する後段の SDK として位置づけられ、初期は UDP/OSC ベースで接続します。

## 全体の中での位置づけ

共通仕様（contracts）を利用する後段の SDK です。Unity SDK の単純移植ではなく、共通仕様に基づいて Unreal に適した形で実装します。

## 設計方針

- 最初から重いネイティブ統合を前提にしない
- Unity の単純移植ではなく共通仕様を利用する
- 独自プロトコルを作らない

## 依存関係

- [hapbeat-contracts](../hapbeat-contracts) — メッセージ仕様・Event ID 定義
- [hapbeat-bridge](../hapbeat-bridge) — デバイス通信の中継サーバ

## 今後の最初のタスク

1. UDP/OSC ベースの基本接続
2. Blueprint ノード検討

## 現状

現時点では実装コードはありません。設計・計画フェーズです。
