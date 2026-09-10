---
kind: explanation
sidebar:
  order: 6
  label: ターゲティング
---

# ターゲティング

<!-- hapbeat:include targeting-overview -->

## Unreal Engine で設定する

### Event Map の Target を設定する

通常の送信先は Event Map entry ごとに決めます。Content Browser で `Hapbeat Event Map` asset を開き、entry の `Target` を編集します。例えば `*/pos_neck` を指定すると、その entry を再生する Blueprint node や Trigger component はすべて neck の Hapbeat だけへ送ります。

Event Map の Target は、触覚演出そのものに属する送信先です。Actor や component ごとに同じ Target を重複して設定する必要はありません。SDK は既知 device には unicast で送信し、device 側でも Target が一致しないイベントを捨てます。

### Address Override を選ぶ

複数の HMD や PC 等の端末で同じ Event Map を使い、端末ごとに送信先だけ変えたい場合は Address Override を使います。Event Map の Target は変更されず、送信時だけ player / group が置き換わります。

-   **this build** — Project Settings の `Plugins → Hapbeat → Addressing` で `Forced Override Player` / `Forced Override Group` を設定します。展示端末用に固定する場合です。`-1` は off です。
-   **this device** — `Tools → Hapbeat → Hapbeat Runtime Status` で保存・確認します。PIE 中は実効値を確認できます。実行中の UI には下記 API または Address Override Panel を使います。

Address Override は `Hapbeat Subsystem` に設定します。

```cpp
UHapbeatSubsystem* Hapbeat = GetGameInstance()->GetSubsystem<UHapbeatSubsystem>();
Hapbeat->SetAddressOverride(/* Player */ 1, /* Group */ -1, /* Persist */ true);
```

-   `Player` / `Group` に `-1` を指定すると、その軸を上書きしません。
-   `Persist` を有効にすると、同じ端末の次回起動でも設定を復元します。
-   `Clear Saved Address Override (Hapbeat)` は保存済み設定を消し、固定されていない軸を off に戻します。`Forced Override Player / Group` を設定した軸は build の固定値が優先され、Clear でも解除されません。

Blueprint では `Get Game Instance Subsystem` から `Hapbeat Subsystem` を取得し、`Set Address Override (Hapbeat)` を使います。Target の組み立て・検証 node は[Blueprint ノード](./blueprint-nodes.md#7-target-%E3%82%92%E6%89%B1%E3%81%86-node)にあります。

## event ID を直接送る場合

通常は Event Map entry を再生します。event ID を実行時に組み立てる必要があるときだけ、`Hapbeat Subsystem` の `Play Event (Hapbeat)` を使います。

```cpp
UHapbeatSubsystem* Hapbeat = GetGameInstance()->GetSubsystem<UHapbeatSubsystem>();
Hapbeat->Play(TEXT("basic-exam-kit.sine_200hz_1s"), 0.8f,
    FString::Printf(TEXT("player_%d"), PlayerId));
```

この経路は Event Map の Clip・Gain・Target・Delay を使いません。device に対象の Kit event ID が配備済みである必要があります。
