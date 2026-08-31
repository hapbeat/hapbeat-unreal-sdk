---
sidebar:
  order: 4
  label: Showcase の動作
---

# Showcase の動作

このページは Showcase 各 zone のゲーム側の動作を示します。触覚の Clip、Gain、Target、entry の編集は [Showcase の触覚配線](./showcase-unreal.md) を参照してください。

## Z1 Bowling

- 左クリックでボールを発射し、Space でボールと pin を初期位置へ戻します。
- ボールまたは別の pin が pin に衝突すると、各 pin の `HitTrigger` が pin-hit entry を発火します。

## Z2 Swing Door

- F で開き、G で閉じ、L で lock event を発火します。
- `DoorMotion` Timeline が `DoorHinge` を回転させます。

## Z3 Fishing

- input の press / release で hook sequence を開始・終了します。
- 釣れた Shark は hook state に応じて rod tip 側へ移動します。

## Z4 Stream Console

- F で loop を開始し、G で停止、T で tick event を発火します。
- `GainBinding` と `PanBinding` は再生中の stream parameter を更新します。

## Z5 Target Range

- charge input の開始・しきい値到達・release でそれぞれの entry を発火します。
- projectile が target に当たると、hit strength に対応する target-hit entry を発火します。
