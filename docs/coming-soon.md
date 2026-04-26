---
title: Unreal SDK は準備中です
description: Unreal Engine 向け Hapbeat SDK は現在未着手です。実装後に本ページが拡充されます。
---

:::caution
**Unreal SDK は現在未着手です。** 本ページはプレースホルダーです。
:::

## 概要（予定）

Unreal Engine から Hapbeat を制御する薄いアダプタ SDK。Unity SDK と同じ UDP プロトコル ([contracts](/docs/_fetched/contracts/)) を利用するため、機能セットは Unity SDK にほぼ準じる予定です。

- Wi-Fi UDP broadcast による直接通信
- Blueprint 対応の Trigger コンポーネント
- C++ API（HapbeatBridge / HapbeatEventTrigger 相当）
- EventMap 風の Editor ツール

## 進捗

[hapbeat-unreal-sdk リポジトリ](https://github.com/Hapbeat/hapbeat-unreal-sdk) で進捗を追跡してください。

## 代替手段

Unity を使わず Unreal で先行検証したい場合、現状は OSC / UDP ライブラリで [contracts の message-format spec](/docs/_fetched/contracts/) を参照しながら直接実装することも可能です。
