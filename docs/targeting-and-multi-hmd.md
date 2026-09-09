---
sidebar:
  order: 6
  label: 宛先と複数 HMD
---

# 宛先と複数 HMD

## Target で送信先を絞る

`Target` は、どの Hapbeat が再生を受け取るかを表す論理アドレスです。

- 空文字: すべての接続済み device が候補
- `player_1/pos_chest`: player 1 の chest
- `*/pos_neck`: すべての player の neck
- `player_2/pos_chest/group_1`: player と group の両方を指定

通常は Event Map entry の `Target` に設定します。SDK は既知 device には unicast で送信し、device 側でも Target が一致しないイベントを捨てます。

## 同じビルドを複数 HMD で使う

Address Override は、各 HMD が送る Target の player / group だけを実行時に上書きします。Event Map や Actor の設定を複製する必要はありません。

```cpp
UHapbeatSubsystem* Hapbeat = GetGameInstance()->GetSubsystem<UHapbeatSubsystem>();
Hapbeat->SetAddressOverride(/* Player */ 1, /* Group */ -1, /* Persist */ true);
```

- `Player` / `Group` に `-1` を指定すると、その軸を上書きしません。
- `Persist` を有効にすると、同じ端末の次回起動でも設定を復元します。
- `Clear Saved Address Override (Hapbeat)` は保存済みの設定を消し、両軸を off に戻します。

Blueprint では `Get Game Instance Subsystem` から `Hapbeat Subsystem` を取得し、`Set Address Override (Hapbeat)` を使います。Target の組み立て・検証 node は[Blueprint ノード](./blueprint-nodes.md#7-target-を扱う-node)にあります。

## event ID を直接送る場合

通常は Event Map entry を再生します。event ID を実行時に組み立てる必要があるときだけ、`Hapbeat Subsystem` の `Play Event (Hapbeat)` を使います。

```cpp
UHapbeatSubsystem* Hapbeat = GetGameInstance()->GetSubsystem<UHapbeatSubsystem>();
Hapbeat->Play(FString::Printf(TEXT("heartbeat.player_%d"), PlayerId), 0.8f);
```

この経路は Event Map の Clip・Gain・Target・Delay を使いません。device に対象の Kit event ID が配備済みである必要があります。
