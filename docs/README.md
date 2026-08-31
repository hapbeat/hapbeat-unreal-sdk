# docs/ — ユーザー向けドキュメント

このディレクトリは、本リポジトリの **ユーザー向け公開ドキュメント** 置き場である。

- 想定読者: Unreal Engine 開発者
- 集約先: [hapbeat-devtools-site](https://devtools.hapbeat.com/) が build 時に自動取得し、`/docs/unreal-sdk/` の URL で公開する

## ディレクトリ分類

| ディレクトリ | 用途 | 公開対象 |
|------|-----|--------|
| `docs/` | ユーザー向け解説（このディレクトリ） | ◯ portal site に掲載 |
| `dev-notes/` | （存在する場合）内部実装の知見・履歴 | ✗ portal には載らない |

## 収録ドキュメント

- [getting-started.md](./getting-started.md) — インストール / 最短の疎通確認 / EventMap とトリガの使い方
- [blueprint-nodes.md](./blueprint-nodes.md) — Blueprint に公開しているノード、component、接続・診断 API の用途
- [advanced.md](./advanced.md) — address override、Target、Showcase の補足
- [showcase-unreal.md](./showcase-unreal.md) — Showcase Z1〜Z5 の Actor / Component と Hapbeat Event Map の触覚配線
- [unity-to-unreal-codex.md](./unity-to-unreal-codex.md) — Unity 経験者向けの Unreal Editor、PIE、Actor / Component の補足

リポジトリ直下にも次がある:

- [README.md](../README.md) — 概要・機能一覧・API の入口
- [AGENTS.md](../AGENTS.md) — AI コーディングエージェント向けの自己完結リファレンス
- [CHANGELOG.md](../CHANGELOG.md) — 変更履歴
