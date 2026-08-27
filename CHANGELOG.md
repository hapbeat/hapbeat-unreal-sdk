# Changelog

## Unreleased

- Replaced the single-stream runner with endpoint-scoped StreamHub sessions:
  each exact PONG endpoint receives one unicast STREAM session, while logical
  sources on that endpoint are mixed independently with gain/pan/loop/stop.
- Stream output is normalized to 16 kHz stereo PCM16. Unresolved sources stay
  `Deferred(NoResolvedEndpoint)` and never broadcast STREAM packets; empty
  endpoint sessions linger for 300 ms before END.
- IP, port, or reported-address changes now migrate one logical endpoint
  session in place, preserving its runner and source cursors without END/BEGIN;
  stale duplicate routes are abandoned without END.
- Runtime `Playback.Loop` is authoritative for existing, late, and rejoined
  endpoints. Source admission no longer restores the authored initial loop
  value, and an EOF source can rejoin at frame 0 when loop is enabled.
- Removed the remaining public runnable broadcast fallback parameters. STREAM
  sessions accept exact unicast endpoints only.

Hapbeat Unreal Engine SDK の主要な変更点をまとめます。

形式は [Keep a Changelog](https://keepachangelog.com/ja/1.1.0/) に、
バージョン付けは [Semantic Versioning](https://semver.org/lang/ja/) に従います。

---

## [Unreleased]

Unity SDK v0.4.0 で確定した接続まわりの修正を移植した。

### 修正（Fixed）

- **Test Play（EventMap のエディタ再生）が常にブロードキャストになっていた**のを
  修正した。`FHapbeatEditorSender` は送信専用でリプライを読まなかったため、既知デバイスが
  0 件のままだった。ブロードキャストは AP が DTIM ビーコン（100〜300 ms 周期）まで保留
  するため、デザイナーには発火遅れとして体感される。
  - ソケット作成直後に PING し、送信のたびに届いている PONG を回収するようにした
    （このクラスは tick を持たないため、送信直前の機会的な回収で足りる。各送信のあとにも
    PING を撃つので、2 回目のクリック時には 1 回目の応答が既にキューにある）。
  - 応答があったデバイスへユニキャストする。**1 台も無い間だけ**ブロードキャストへ
    フォールバックする。ファームウェア v0.3.0 未満は PLAY/STOP の seq 重複排除を
    持たないため、二重送信すると触覚が 2 回鳴る。
- **送信エラーが完全に不可視だった**のを修正した。`UHapbeatSubsystem::SendPacket` は
  `SendTo` の戻り値を捨てていた。ソケットは壊さないまま（UDP の送信失敗はデータグラムが
  失われただけで、ソケットは有効）、障害ごとに 1 回だけ警告し、復旧時に 1 行出す。
  ストリームは 1 秒あたり約 100 パケット送るため、抑制しないと現場調査に使えるログが
  残らない。

- **マルチホーム PC でデバイスを 1 台も発見できない問題**を修正した。
  `255.255.255.255`（limited broadcast）は**インターフェイスメトリックが最小の 1 本から
  しか送出されない**。Hyper-V / WSL2 / Docker を入れると作られる仮想アダプタは
  **LAN ケーブルを繋いでいなくても常時 Connected** で、Windows の既定で Wi-Fi より
  優先されることがある。この場合パケットは Hapbeat のいるネットワークに一生届かない。
  - 探索（PING / CONNECT_STATUS）を**ローカルの各サブネット宛て**へ送るようにした。
    宛先は各 NIC の実ネットマスクから `(ip & mask) | ~mask` で算出する（/16 は
    `x.y.255.255`、/25 は `x.y.z.127` になるため `.255` 決め打ちにはできない）。
    同一サブネットに NIC が 2 枚ある場合は宛先で重複排除する。
  - **UE の公開 API はネットマスクを返さない**（`ISocketSubsystem::GetLocalAdapterAddresses`
    はアドレスのみ）ため、`HapbeatNetInterfaces.cpp` でプラットフォーム API を直接呼ぶ
    （Windows は `GetAdaptersAddresses`、Linux/macOS は `getifaddrs`）。**プラグイン内で
    唯一のプラットフォーム依存コードをこの 1 ファイルに隔離**してある。取得できない
    プラットフォームは空を返し、従来どおり limited broadcast のみで動く。
  - **PONG が返ったサブネットに確定**し、以後のブロードキャストをそこへ向ける。
    endpoint-scoped stream session にも適用される。
  - **PLAY / STOP / STOP_ALL / STREAM_\* は従来どおり単一宛先**。ファームウェア v0.3.0
    未満は seq 重複排除を持たないため、複数経路へ送ると触覚が 2 回鳴る。Test Play の
    ブロードキャストフォールバックも単一宛先のまま（`SendSingleBroadcast`）。
  - Win64 のみ `iphlpapi.lib` をリンクする（`HapbeatSDK.Build.cs`）。

### 検証

- **未検証**: UE でのコンパイル・実機とも。**次の UE セッションでコンパイルを通し、
  エラーがあれば修正すること。** 特に確認したい箇所:
  - `HapbeatSDK.Build.cs` の `PublicSystemLibraries`（古い UE では
    `PublicAdditionalLibraries`）
  - `HapbeatNetInterfaces.cpp` の Windows ヘッダ取り込み順
    （`AllowWindowsPlatformTypes.h` → `winsock2.h` → `iphlpapi.h`）
  - `Socket->HasPendingData` / `RecvFrom`（エディタ送信側の PONG 回収）

---

## [0.1.0] - Unreleased

Initial release. Hapbeat Unity SDK の Unreal 版として、wire protocol・EventMap・
ストリーミング・Trigger コンポーネント・Editor 支援・サンプルを一式実装。

### Added（追加）

**Core runtime (`HapbeatSDK` module)**

- `FHapbeatProtocol` — PLAY / STOP / STOP_ALL / PING / CONNECT_STATUS /
  STREAM_BEGIN / STREAM_DATA / STREAM_END のパケットビルダーと PONG / ERROR の
  パーサー（Unity `HapbeatProtocol.cs` とバイト互換）。
- `UHapbeatSubsystem` (`UGameInstanceSubsystem`) — UDP broadcast の送受信、
  `Connect` / `Play` / `Stop` / `StopAll` / `Ping`、PONG ベースの疎通判定
  (`AliveDeviceCount` / `IsAlive`)、`OnConnected` / `OnDisconnected` / `OnError` /
  `OnPong` の Blueprint delegate。
- `UHapbeatConfig` — Port・AppName・PingInterval・StreamSendAheadSeconds・
  HapticDelaySeconds・StreamUnicast・CommandUnicast 等の接続設定。
- **専用スレッドによる CLIP 送出** — STREAM_DATA を game thread ではなく専用
  スレッドから ~10ms 等間隔で送出（`FHapbeatStreamRunnable`）。フレームヒッチ
  （GC / 描画 / 物理）でデバイスのリングバッファが枯渇して起きる不定期な途切れを
  根治。Gain/Pan は atomic ミラー経由で読むため送出スレッドは UObject に触れない。
- **unicast 送信（DTIM 対策）** — Wi-Fi AP の省電力バッチングでブロードキャストが
  最大 1 ビーコン間隔（~100-300ms）保留される問題を回避するため、PONG で既知の
  デバイスへ直接送信。`StreamUnicast`（CLIP）と `CommandUnicast`（Play/Stop/
  StopAll）を個別に制御でき、既定は両方 on。宛先はデバイスが PONG で報告した
  アドレスで絞り込み（未報告のデバイスは fail-open で送信対象に残す）。
  該当デバイスが 0 件のとき、コマンドはブロードキャストにフォールバック
  （デバイス側が同じ target フィルタを再適用するため誤発火しない）。
  PING / CONNECT_STATUS は discovery のため常にブロードキャスト。
- **Address Override（player/group の実行時上書き）** —
  `SetAddressOverride(Player, Group, bPersist)` / `ClearPersistedAddressOverride()` /
  `GetOverridePlayer()` / `GetOverrideGroup()`。設定すると EventMap 側の target
  文字列に関わらず、Play/Stop/StopAll/StreamClip の全送信経路にこの player/group
  が強制適用される。`UHapbeatTargetLibrary::ResolveTarget` は Unity
  `HapbeatClient.ResolveTarget` の verbatim 移植。永続化は `GConfig` /
  `GGameUserSettingsIni`（`HapbeatSDK` セクション）。

**EventMap（調整側）**

- `UHapbeatEventMap` (`UDataAsset`) + `FHapbeatEventEntry` — event id・
  `EHapticMode`（Command / StreamClip）・gain・target・loop・delay offset を
  ネイティブ Details パネルで編集。Trigger からは安定 GUID (`FGuid Id`) で参照。
- `GetEffectiveGain()` — `entry.Gain × CachedManifestIntensity` を計算
  （intensity 未 bake 時はスキップ）。

**ストリーミング**

- `UHapbeatClip` (`UDataAsset`) — 16 kHz PCM16 WAV を実行時に `FFileHelper` で
  ロード（`USoundWave` decode を経由しない全バージョン安全な経路）。
- `UHapbeatStreamPlayback` — 再生中ハンドル。`Gain` / `Pan`
  （リニア L/R バランス）をゲームスレッドからリアルタイム変更可能。
- `FHapbeatStreamer` + `UHapbeatSubsystem::StreamClip/StopStream` —
  ゲームスレッド `FTSTicker` ペーシングで PCM16 chunk を送信先行バッファ付きで
  送出。単一セッション・新規呼び出しで既存ストリームを置換 (REPLACE semantics)。

**Trigger コンポーネント（起点側）**

- `UHapbeatTriggerComponent` — EventMap entry を解決して Command/StreamClip を
  発火する基底コンポーネント。`Fire()` / `FireWithGain()` / `Stop()`。
- `UHapbeatCollisionTriggerComponent` — Hit / BeginOverlap に連動、
  タグ・コリジョンチャンネルフィルタ、速度スケールゲイン (`FRuntimeFloatCurve`)。
- `UHapbeatSequenceComponent` — grab/hold/release の 3-phase シーケンス
  (On-Start one-shot + Loop StreamClip + 遅延 On-Stop one-shot)。
- `UHapbeatParameterBinding` — Transform/Rigidbody の値（位置・速度・
  角速度・フレーム差分・外部値）を正規化 → カーブ → StreamGain/StreamPan に
  マッピングする `TickComponent`。

**Editor module (`HapbeatSDKEditor`)**

- Kit manifest（schema 2.0.0）の intensity を event id + mode で解決し、
  `UHapbeatEventMap` の `CachedManifestIntensity` を一括 bake する
  "Refresh Intensities" アクション。
- EventMap の各エントリに "Test Play" ボタンを追加する `IDetailCustomization`
  （PIE 用ソケットとは独立した Editor 専用送信経路）。
- `UHapbeatTargetLibrary` の target 文字列コーデック
  (`ParseTarget` / `BuildTargetFromParts`) + `StandardPositions`。

**サンプル (`HapbeatSDKSamples` module)**

- `AHapbeatBasicExampleActor` — Space/R/F/S/C キーで
  StreamClip 1-shot・StreamClip loop・Command play・stop-all・ping を確認する
  最小構成 (`basic-exam-kit`)。
- Showcase 5 ゾーン（`showcase-kit`）— Z1 Bowling（衝突トリガー、速度スケール）/
  Z2 Door（状態遷移で命令的に発火）/ Z3 Fishing
  (`UHapbeatSequenceComponent` + velocity→StreamGain binding) / Z4 Stream
  Console（スライダー→Gain/Pan binding）/ Z5 ChargeShot（チャージループ +
  `ApplyGainModulation` + playback `Stop()`）。全ゾーン C++ 製 Actor + engine
  primitive visuals（同梱 WAV・Kit manifest は Unity Showcase と共通）。

**パッケージング**

- `CanContainContent: true` + `Config/FilterPlugin.ini`
  （`/Content/HapbeatSamples/...` を raw WAV/manifest ごとパッケージに含める）。
- CI: タグ push (`v*`) から CHANGELOG を抽出して GitHub Release を自動公開。

[0.1.0]: https://github.com/hapbeat/hapbeat-unreal-sdk/releases/tag/v0.1.0
