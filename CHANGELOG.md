# Changelog

Hapbeat Unreal Engine SDK の主要な変更点をまとめます。

形式は [Keep a Changelog](https://keepachangelog.com/ja/1.1.0/) に、
バージョン付けは [Semantic Versioning](https://semver.org/lang/ja/) に従います。

---

## [Unreleased]

Hapbeat 1.0（DEC-096）に合わせた変更。対応する仕様は contracts `46f246e`（1.0 の仕様。release 時に contracts の版へ置き換える）の `specs/stream-session.md`・`specs/sdk-multi-stream.md`・`specs/message-format.md`。

### 追加
- `EHapbeatStreamDeferredReason` に `UnknownClass`（機体の stream の世代がまだ分からない）・`LeaseUnavailable`（機体が lease を出していない）・`Superseded`（別の送信元が stream を取った）を追加。一致する機体の理由が分かれる時は `Superseded` > `LeaseUnavailable` > `UnknownClass` > `NoResolvedEndpoint` の順で 1 つを返す。Stream Gen 1 の 300 ms の間隔を待つ間は `Active` のまま。
- `UHapbeatSubsystem::GetStreamEndpoints()`（Blueprint: `Get Stream Endpoints (Hapbeat)`）と `OnStreamEndpointChanged`: 機体ごとの address・ip・port・stream の世代（`Gen1` / `Gen2` / `Unknown`）・lease の状態（`Valid` / `Unavailable` / `Superseded` / `None`）・CAPS の generation を返し、世代か lease が変わると通知する。
- CAPS_REQ / CAPS: 照合済みの PONG を返した機体に能力の申告を求める（500 ms ごとに 3 回まで。応答が無ければ「申告なし」）。SDK より新しい major の機体には一度だけ警告する。`haptic_channels` が 1 の機体（モーター 1 個）へは pan をかけずに混ぜる。
- ERROR の受信: 既知の機体の port 7700 から来た ERROR だけを受け付け、stream の ERROR は自分の session の envelope と一致するものだけを扱う。code ごとに 1 回ログに出し、STREAM_NOT_STARTED で BEGIN を送り直し、STREAM_TIMED_OUT・STREAM_LEASE_INVALID で新しい世代を始め、STREAM_SUPERSEDED でその機体を `Superseded` で待たせ、STREAM_STOPPED でその機体から source を外し、STREAM_GEN_UNSUPPORTED・STREAM_TARGET_MISMATCH で機体に問い直す。
- Stream Gen 2 の BEGIN の再送: 最初の 3 ブロックの前と、以後 50 ブロック（500 ms）ごとに同じ BEGIN を送る。再送する BEGIN の target は、その時点の機体の address。
- 差し替え（re-splice）: Stream Gen 2 の機体で、送り済みの音が 40 ms 以上残っている時に新しい source が始まったら、送り済みのブロックを新しい世代で作り直し、新しい source を要求した時刻から鳴らす。

### 変更
- PONG の tail の読み方: tail なしは Stream Gen 1、32 byte 以上で `HBS2`・extension_version 1 は Stream Gen 2（先頭 32 byte を読み、reserved と未知の flag は無視）、それ以外は無視（世代・lease を変えず、照合済みの応答なら機体ごとに 1 回警告）。
- 拡張 PING（lease の要求）は unicast だけで、stream する機体（再生中・待機中の Playback の target に一致する機体、session がある機体、session の終了から 15 s 以内の機体）へだけ送る。間隔は応答済みの機体で 5 s（lease の TTL の 1/3）、未応答の機体で 1 s。探索の broadcast は 8 byte の PING。8 byte の PING への応答では世代を決めない。
- superseded の保留は、lease_reissued の付いた照合済みの応答（または `ReacquireStreamOwnership`）でだけ解く。
- 同じ incarnation のまま lease の ticket・boot・世代が変わった時は、旧い session を END し、新しい session で各 source を続きの位置から送る。
- 自分の送信が 3 s 以上止まった Stream Gen 2 の session は、END して新しい世代で始め直す（溜まった分を一度に送らない）。旧い識別に戻った session は、送信スレッドでも BEGIN・DATA を止める。
- 機体の address が変わった PONG では、同じ IP・port の endpoint をすぐ新しい address へ移す（旧い address が 15 s 残らない）。
- Editor の Test Play の stream を Stream Gen 2 に対応: target の address に一致する機体ごとに session を作り、Stream Gen 2 の機体には lease を unicast で求めてから送る（Stream Gen 1 の機体には従来の形式）。別の送信元に取られた機体は、Test Play が明示の操作なので取り戻す。
- ログ・コメントの呼び方を Stream Gen 1 / Stream Gen 2 に揃えた（`versioning.md` §4.2）。

## [0.2.0] - 2026-10-17

### 追加
- Stream session v2（contracts `specs/stream-session-v2.md`, DEC-074 / DEC-075）: 拡張 PING（timestamp + client incarnation）と PONG の HBS2 tail で、端末ごとに v2 か v2 以前かを判定し、v2 端末には lease + generation 付きの v2 形式で、v2 以前のファームウェアには従来形式で stream する。v2 端末は v1 の stream を受け付けないため、ファームウェア 0.5.0 以降の Hapbeat へ stream するにはこの版が必要（FIRE / PLAY は従来どおり）。
- `UHapbeatSubsystem::ReacquireStreamOwnership()`（Blueprint: `Reacquire Stream Ownership (Hapbeat)`）: 別のアプリに stream の lease を奪われた端末を、ユーザー操作から明示的に取り戻す。SDK は自分からは取り戻さず、アプリが前面に戻った時だけ自動で呼ぶ（1 秒に 1 回まで）。lease を奪われた時は `LogHapbeat` に一度だけ警告を出す。
- `UHapbeatStreamPlayback::SetPlaybackRate` / `GetPlaybackRate`（0.25〜4）: stream の再生速度（= ピッチ）。PC 側の再サンプリングで、プロトコル・ファームウェアの変更はない。rate 1 は従来と同じ出力。

### 変更
- stream の mix（contracts `sdk-multi-stream.md` §5.7〜5.9, DEC-081）: source ごとの gain / pan をサンプル単位で前の chunk の値から ramp する。最終の飽和の前に limiter（目標 0.9 FS、0.95 FS 以上は tanh の soft knee）を入れ、ピークが 32768 を超えた時だけ働く（full scale 以内の chunk はビット一致で通す）。clip の 16 kHz への変換を最近傍から線形補間にした。
- keep-alive の PING 間隔は、v2 端末がいる間は最大 5 s（端末は最後の PING から 15 s で lease を捨てる）。
- アドレスが変わった PONG で新しい endpoint が現れた時、PING を 1 回送って（1 秒に 1 回まで）PingInterval を待たずに判定する。
- Windows 以外（Quest 等）では、stream 送出スレッドの 10 ms 間隔の待ちを 1 ms 単位の sleep にし、最後の 1.5 ms だけ spin する（従来は 1 コアを占有していた）。

### 修正
- Android でサンプルモジュールが UObject 初期化より前に読み込まれ、アドレスパネルのフォント取得が早すぎた。フォントは `Construct` で解決する。

### 既知の制限
- Editor の Test Play は従来形式でしか stream しないため、v2 端末（ファームウェア 0.5.0 以降）へは StreamClip を Test Play できない（初回に警告を出す）。PIE では stream できる。

## [0.1.0] - 2026-09-10

- Replaced the single-stream runner with endpoint-scoped StreamHub sessions:
  each exact PONG endpoint receives one unicast STREAM session, while logical
  sources on that endpoint are mixed independently with gain/pan/loop/stop.
- Stream output is normalized to 16 kHz stereo PCM16. Unresolved sources stay
  `Deferred(NoResolvedEndpoint)` and never broadcast STREAM packets; empty
  endpoint sessions linger for 300 ms before END.
- An unambiguous logical-route change now migrates its endpoint session in
  place, preserving runner/cursors without END/BEGIN. Live exact endpoints
  that merely share an IP or address remain separate; ambiguous routes are not
  collapsed. Sources that stop matching a migrated address are detached from
  that runner immediately and become Deferred when no endpoint remains.
- Runtime `Playback.Loop` is authoritative for existing, late, and rejoined
  endpoints. Source admission no longer restores the authored initial loop
  value, and an EOF source can rejoin at frame 0 when loop is enabled.
- Removed the remaining public runnable broadcast fallback parameters. STREAM
  sessions accept exact unicast endpoints only.

### 修正（Fixed）

Unity SDK v0.4.0 で確定した接続まわりの修正を移植した。

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

- サンプルプロジェクトで Basic Example、Showcase、VR Config Example の
  Editor/PIE 動作を確認済み。
- Unreal Engine のコンパイルツールチェーンはこのリポジトリの CI にはないため、
  自動コンパイルは実行していない。配布前に確認すべき箇所は `HapbeatSDK.Build.cs`、
  `HapbeatNetInterfaces.cpp`、エディタ送信側の PONG 回収経路である。

---

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

[0.2.0]: https://github.com/hapbeat/hapbeat-unreal-sdk/releases/tag/v0.2.0
[0.1.0]: https://github.com/hapbeat/hapbeat-unreal-sdk/releases/tag/v0.1.0
