# Changelog

Hapbeat Unreal Engine SDK の主要な変更点をまとめます。

形式は [Keep a Changelog](https://keepachangelog.com/ja/1.1.0/) に、
バージョン付けは [Semantic Versioning](https://semver.org/lang/ja/) に従います。

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
- `UHapbeatConfig` — Port・Group・AppName・PingInterval・
  StreamSendAheadSeconds・HapticDelaySeconds 等の接続設定。
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
- `FHapbeatStreamer` + `UHapbeatSubsystem::StreamClip/StopStream/StopStreamWithFlush` —
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
  `ApplyGainModulation` + `StopStreamWithFlush`）。全ゾーン C++ 製 Actor + engine
  primitive visuals（同梱 WAV・Kit manifest は Unity Showcase と共通）。

**パッケージング**

- `CanContainContent: true` + `Config/FilterPlugin.ini`
  （`/Content/HapbeatSamples/...` を raw WAV/manifest ごとパッケージに含める）。
- CI: タグ push (`v*`) から CHANGELOG を抽出して GitHub Release を自動公開。

[0.1.0]: https://github.com/hapbeat/hapbeat-unreal-sdk/releases/tag/v0.1.0
