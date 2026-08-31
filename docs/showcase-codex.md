# Showcase サンプルガイド

Showcase は、Unreal Engine 5（UE5）で Hapbeat を組み込む代表的な 5 パターンを、1 つのレベルで確認するサンプルです。触覚を鳴らす起点（入力・衝突・状態遷移）と、強さ・送信先・再生方法を決める `UHapbeatEventMap` を分けて扱います。

## 起動する

1. Content Browser の Settings で **Show Plugin Content** を有効にします。
2. `Plugins/HapbeatSDK/Content/HapbeatSamples/Showcase/Maps/Showcase` を開きます。
3. World Settings の **GameMode Override** が `Hapbeat Showcase Game Mode` であることを確認し、PIE（Play In Editor）の ▶ Play を実行します。配布済みマップには設定済みです。

`W` / `A` / `S` / `D` とマウスで移動し、`Tab` でカーソルの捕捉を切り替えます。`1`〜`5` はゾーン切替、`Q` は EventMap の手動エントリ、`P` は Ping です。ゾーン固有の操作は画面の HUD にも表示されます。

| ゾーン | 操作 | 学べること |
| --- | --- | --- |
| Z1 Bowling | 左クリックでボール発射、`Space` でリセット | `UHapbeatCollisionTriggerComponent` の Hit と `VelocityScaled` |
| Z2 Swing Door | `F` 開閉、`G` 強打、`L` 施錠 / 解錠 | `Tick` の状態遷移で `UHapbeatTriggerComponent::Fire()` を呼ぶ方法 |
| Z3 Fishing | 左クリック長押しで hook、離すと release | `UHapbeatSequenceComponent` と速度から gain を作る `UHapbeatParameterBinding` |
| Z4 Stream Console | `Space` でループ開始 / 停止、UI の slider を操作 | `External` 値で StreamClip の gain / pan を連続調整する方法 |
| Z5 Target Range | 左クリック長押しで charge、離して発射 | コードから loop・shot を明示的に制御し、的の Hit で別イベントを発火する方法 |

Showcase の全エントリは既定で StreamClip です。Hapbeat を接続していなければ視覚・操作だけを確認できます。接続済みの実機がある環境で PIE やキャプチャを実行すると送信され得るため、実機へ送らない検証ではデバイスを接続しないでください。

## Unity 版との用語対応

| Unity | UE5 で対応するもの | Showcase で見る場所 |
| --- | --- | --- |
| Scene | Level | `Showcase.umap` |
| Hierarchy | World Outliner / component tree | Outliner の `Z1_Bowling` などと Details の component tree |
| GameObject | Actor | `AHapbeatShowcaseZ1BowlingActor` など |
| MonoBehaviour | Actor / Actor Component | zone actor と `UHapbeatTriggerComponent` |
| Prefab | Child Actor / Actor Class | `Pin1`〜`Pin6`、`Shark`、`Target` の `UChildActorComponent` |
| Inspector | Details | actor・component を選択した右側パネル |
| Play | PIE | editor の ▶ Play |
| SerializeField | `UPROPERTY(EditAnywhere)` など | `Hapbeat|...` category の編集可能な property |
| Awake / Start / Update | constructor / `OnConstruction` / `BeginPlay` / `Tick` | 下の「編集時と再生時のライフサイクル」 |
| OnCollisionEnter | `OnComponentHit` | Z1 pin と Z5 target の collision trigger |

Unity の 1 Scene に置かれた object を編集する感覚に近い正本は、UE では Level に配置した Actor とその Details の値です。`Prefab` を直接複製する代わりに、親 zone の Child Actor slot を置き、その relative Transform を Level に保存します。

## 全体構成

```text
Showcase.umap
├─ AHapbeatShowcaseActor                 zone の切替、HUD、Q / P、player teleport
├─ AHapbeatShowcaseRoomActor × 5         床と壁
├─ Z1〜Z5 の zone Actor × 5              各デモの物理・入力・触覚起点
├─ AHapbeatShowcaseGameMode              AHapbeatShowcaseCharacter を spawn
└─ AHapbeatShowcaseCharacter             一人称移動、カメラ、手持ち item、Tab

UHapbeatEventMap (EM_Showcase)           event の Mode / Gain / Target / Clip
showcase-kit                             manifest と Command / StreamClip 用 clip
```

`AHapbeatShowcaseActor` は `IHapbeatShowcaseZone` を実装する配置済み zone を集め、`GetZoneIndex()` の順に `1`〜`5` へ割り当てます。切替時は表示、collision、Tick、input を対象 zone だけ有効にし、player をその zone の spawn pose へ移動します。各 zone の `OnZoneActivated()` / `OnZoneDeactivated()` は、ボールのリセットや stream 停止など、その zone 固有の状態を処理します。

配布マップでは 5 zone がそれぞれ別の Room に事前配置されています。別 Level に `Hapbeat Showcase` actor だけを置いた場合は、配置済み zone が無いことを検出して、`Zones` の actor class を switcher の位置へ 1 つずつ spawn する fallback になります。この fallback は配置調整を保存する用途には向きません。

## 編集時と再生時のライフサイクル

UE では同じ class に対して、既定値、Level に置いた instance、Editor World、PIE World を区別します。

| 段階 | 役割 | Showcase の扱い |
| --- | --- | --- |
| CDO（Class Default Object） | class の constructor が作る既定値と default subobject | 固定 mesh / material や component 構成の初期値 |
| placed instance | Level に置かれた Actor の保存値 | zone と Child Actor slot の Transform、Details で変えた値 |
| Editor World | PIE 前に編集する Level | 静的な配置・見た目を確認して `Ctrl+S` で保存する場所 |
| `OnConstruction` | 配置・property 変更時の派生見た目の組立て | Z1 pin、Z3 rod preview / shark、Z5 target の mesh fit を再計算 |
| `BeginPlay` | PIE / packaged game 開始時の runtime 初期化 | input、physics、EventMap、trigger wiring、runtime widget を初期化 |
| `Tick` | 毎 frame の runtime 更新 | Z2 door の state machine、Z3 line physics、Z4 / Z5 の連続更新 |

PIE は Editor World のコピーを作って実行します。そのため PIE 中に物理で倒れた pin、door の state、runtime で変更した component 値、spawn した projectile は、PIE を止めると Editor World へ戻りません。通常の調整は PIE を止め、Outliner で zone を選び、Details で変更して `Ctrl+S` します。

`OnConstruction` が計算する mesh の local transform は派生値です。たとえば Z1 の `Pin1`〜`Pin6`、Z3 の `Shark`、Z5 の `Target` は親 zone の component tree にある slot の Transform を調整します。内部 mesh、Z3 の `RodPreviewMount` / `RodPreviewMesh` は直接動かしても次の construction で計算し直されます。Z3 の釣り糸の始点だけは `RodTipMarker` の Transform が保存対象です。

## 触覚イベントが UDP になるまで

```text
input / physics / Tick / UI
  → Trigger または zone の関数
  → EventMap entry（Id、Mode、Gain、Target、Clip）
  → UHapbeatSubsystem::PlayEntry / StopEntry
  → UDP command または StreamClip の endpoint session
```

`EM_Showcase.uasset` は `Content/HapbeatSamples/Showcase/EM_Showcase` にあります。各 zone はこの EventMap を既定で参照し、`EventMapOverride` で差し替えられます。エントリは list index ではなく stable `FGuid` で選ばれます。

- **CLIP（StreamClip）**: `UHapbeatClip` の PCM16 を SDK が UDP で stream します。entry の `bLoop`、baseline gain、trigger の gain multiplier、binding の現在値が再生に関わります。対象に一致する PONG 済み endpoint ごとに 1 session を持ち、同じ endpoint 宛ての logical source はそこで混合されます。解決済み endpoint が無い source は送信せず Deferred になります。
- **COMMAND**: event ID と gain / target を送ります。Hapbeat 本体が local Kit の clip を再生するため、対応する Kit を device へ配備する必要があります。
- **Target**: entry の `Target` は device address string です。空文字は「address で絞り込まない」、例として `player_1/pos_chest` は対象を絞ります。これは論理的な対象指定であり、常に UDP broadcast する指定ではありません。command は既知 device へ unicast し、既知 device が 0 台のときだけ broadcast へ fallback します。StreamClip は PONG で解決した endpoint へ送ります。`SetAddressOverride(Player, Group, bPersist)` は、EventMap や trigger を書き換えず、送信時の player / group segment を上書きします。

EventMap から直接ではなく trigger / `PlayEntry` を通すことで、Command と StreamClip の分岐、effective gain、Target、loop、active playback の管理を同じ経路に保てます。`Play(EventId, Gain, Target)` は EventMap を経由しない特殊用途です。

## ゾーン別の実装の読み方

### Z1 Bowling — collision を component に任せる

1. 左クリックで `AHapbeatShowcaseZ1BowlingActor::HandleLaunchKey()` が ball を発射します。
2. ball が pin の primitive に Hit します。
3. 各 `AHapbeatShowcaseZ1PinActor` の `HitTrigger` (`UHapbeatCollisionTriggerComponent`) が `OnComponentHit` を受け、tag、cooldown、速度を判定します。
4. `VelocityScaled` が衝突速度を gain multiplier にし、`z1_pin_hit` entry を発火します。

zone 自身は imperative な Hapbeat call をせず、pin が所有する trigger が触覚を発火します。Unity の `HapbeatCollisionTrigger` に対応する構成です。Details では `Pin1`〜`Pin6` の slot Transform、`Ball`、`Launch Speed`、`Pin Height Cm`、`Pin Diameter Cm` を調整します。

### Z2 Swing Door — state transition の瞬間に発火する

1. `F` / `G` / `L` を `HandleToggleKey()` / `HandleActionKey()` / `HandleLockKey()` が受けます。
2. `EHapbeatZ2DoorState` と `Tick()` が open / close / slam / lock / unlock / rattle の遷移を進めます。
3. 遷移開始の時点で `OpenTrigger` など 6 個の `UHapbeatTriggerComponent` が対応する `z2_door_*` entry を `Fire()` します。
4. `DoorHinge` だけが回転し、`DoorFrameMesh` は固定です。

Unity の Animator と `HapbeatStateBehaviour` の組合せに相当しますが、この UE sample は Animator asset ではなく Tick 駆動の state machine です。Details では `Open Yaw Degrees`、open / close / slam duration、rattle 値を変えられます。door の見た目を動かす場合は leaf の hinge 構造を保ち、frame を親 actor ごと回転させません。

### Z3 Fishing — sequence と連続 parameter binding

1. 左クリック press / release が `HandleFirePressed()` / `HandleFireReleased()` から `SetHooked()` を呼びます。
2. shark actor の `HookSequence` (`UHapbeatSequenceComponent`) が start one-shot、loop、release one-shot を順に扱います。
3. shark actor の `HookVelocityBinding` (`UHapbeatParameterBinding`) が `VelocityMagnitude` を `StreamGain` へ変換します。
4. zone の `Tick()` が rod tip と shark の line physics / visual を更新します。

Unity の `HapbeatSequenceTrigger` と parameter binding に対応します。binding は owner の root velocity を読むため、`HookSequence` と `HookVelocityBinding` は zone ではなく Shark child actor にあります。Details では `Shark` slot の Transform、`RodTipMarker`、`Max Line Length`、`Rod Mount Camera Offset Cm`、`Rod Mount Unity Euler Deg`、`Rod Length Cm` を調整します。

### Z4 Stream Console — External 値で gain / pan を操作する

1. `Space` の `HandleToggleKey()` が `LoopTrigger` の loop entry を開始 / 停止します。
2. slider 操作が `GainBinding` と `PanBinding` の `SetValue()` に値を渡します。
3. 2 つの `UHapbeatParameterBinding` は `SourceProperty = External` として、active playback の gain / pan を更新します。
4. slider の detent では `TickTrigger` が one-shot entry を発火します。

Unity の UI Slider、`HapbeatTickEmitter`、`HapbeatStreamPlayback.Gain` / `Pan` に対応します。Details の `AddressPanel` では target address override を操作できます。loop と tick は、古い単一 stream の replace 前提ではなく、現在の endpoint session model で source として扱われます。

### Z5 Target Range — コードで loop、shot、target hit を組み立てる

1. 左クリック press で `HandleChargeBegin()` が `z5_charge_loop` を開始します。
2. `Tick()` が charge 値と threshold を更新し、threshold を初めて越えた時だけ `z5_charge_thd` を発火します。
3. release の `HandleChargeRelease()` は loop を止め、`ShotDelayAfterLoop` 後に `FireShotAfterDelay()` が light / heavy shot を発火して projectile を spawn します。
4. target の `LightHitTrigger` / `HeavyHitTrigger` が tagged projectile の Hit を受け、`z5_tar_hit_light` / `z5_tar_hit_heavy` を発火します。

Unity の `ChargeShooter` による直接 API 呼出しと target receiver に対応します。Details では `Target` slot の Transform、`Heavy Threshold`、`Max Charge Seconds`、`Max Launch Speed`、projectile の scale、`Target Size Cm`、反発・摩擦を調整できます。

## 自分のゲームへ移す

1. まず自分の Content に **Hapbeat Event Map** Data Asset を作成し、event ごとに Mode、Gain、Target、Clip / event ID を定義します。StreamClip を使うなら `UHapbeatClip` を指定し、Command を使うなら対応する Kit を device へ配備します。
2. 起点になる Actor に `UHapbeatTriggerComponent`、`UHapbeatCollisionTriggerComponent`、`UHapbeatSequenceComponent`、または `UHapbeatParameterBinding` を追加します。EventMap と entry ID は Details の picker で結びます。
3. trigger だけで表せない game rule は、Z2 / Z5 のように Actor の関数から `UHapbeatSubsystem::PlayEntry()` / `StopEntry()` を呼びます。raw event ID の `Play()` は EventMap の設定を通らないため、通常のゲームロジックには使いません。
4. 送信先を design time に固定するなら EventMap の `Target` を設定します。配布先の player / group だけを実行時に変えるなら `SetAddressOverride()` を使います。
5. Showcase の Level、GameMode、Character は学習用です。自ゲームの pawn / input / UI を置き換える必要はありません。必要な EventMap と component / API 呼出しだけを自分の Actor に採用します。

## アセットと生成スクリプト

| 対象 | スクリプト | 使う場面 |
| --- | --- | --- |
| EventMap と `UHapbeatClip` asset | `Scripts/generate_sample_assets.py` | manifest / sample actor / sample clip の契約を変えた開発時 |
| mesh、texture、material instance、sound | `Scripts/import_showcase_assets.py` | `Content/HapbeatSamples/Showcase/Source/` の元 asset を更新した開発時 |
| Showcase Level | `Scripts/generate_showcase_map.py` | 初期レイアウトを作り直す時だけ。full editor の `-ExecCmds="py ..."` で実行 |
| PIE screenshot | `Scripts/capture_showcase_views.py` | レイアウト変更の目視検証。PIE を起動して最後に停止 |

通常の利用・Details による位置調整・EventMap の値調整では再生成しません。生成済みの `.uasset` と `Showcase.umap` が配布物であり、map generator は自分が所有する actor を削除して初期状態へ作り直すため、保存済みの配置調整を失います。asset generator は manifest や clip の整合を作り直す開発用です。キャプチャ script は Z4 の loop などを実行するので、実機への触覚送信をしない環境で使います。

## 主要ファイル

| ファイル | 内容 |
| --- | --- |
| [`HapbeatShowcaseActor.h`](../Source/HapbeatSDKSamples/Public/HapbeatShowcaseActor.h) / [`HapbeatShowcaseActor.cpp`](../Source/HapbeatSDKSamples/Private/HapbeatShowcaseActor.cpp) | zone collect / switch、HUD、player teleport、fallback spawn |
| [`HapbeatShowcaseZone.h`](../Source/HapbeatSDKSamples/Public/HapbeatShowcaseZone.h) / [`HapbeatShowcaseZone.cpp`](../Source/HapbeatSDKSamples/Private/HapbeatShowcaseZone.cpp) | zone interface と表示・collision・Tick・input の切替 |
| [`HapbeatShowcaseCharacter.h`](../Source/HapbeatSDKSamples/Public/HapbeatShowcaseCharacter.h) | first-person input、camera、hand mount、cursor |
| [`HapbeatShowcaseGameMode.h`](../Source/HapbeatSDKSamples/Public/HapbeatShowcaseGameMode.h) | Showcase character を使う GameMode |
| [`HapbeatShowcaseRoomActor.h`](../Source/HapbeatSDKSamples/Public/HapbeatShowcaseRoomActor.h) | zone ごとの床と壁 |
| [`HapbeatShowcaseZ1BowlingActor.h`](../Source/HapbeatSDKSamples/Public/HapbeatShowcaseZ1BowlingActor.h) | Z1 pin / ball / collision trigger |
| [`HapbeatShowcaseZ2DoorActor.h`](../Source/HapbeatSDKSamples/Public/HapbeatShowcaseZ2DoorActor.h) | Z2 door state machine と six triggers |
| [`HapbeatShowcaseZ3FishingActor.h`](../Source/HapbeatSDKSamples/Public/HapbeatShowcaseZ3FishingActor.h) | Z3 shark sequence、velocity binding、rod / line |
| [`HapbeatShowcaseZ4StreamConsoleActor.h`](../Source/HapbeatSDKSamples/Public/HapbeatShowcaseZ4StreamConsoleActor.h) | Z4 loop / tick trigger、External gain / pan binding |
| [`HapbeatShowcaseZ5ChargeShotActor.h`](../Source/HapbeatSDKSamples/Public/HapbeatShowcaseZ5ChargeShotActor.h) | Z5 charge、projectile、target Hit trigger |
| [`HapbeatEventEntry.h`](../Source/HapbeatSDK/Public/HapbeatEventEntry.h) / [`HapbeatTriggerComponent.h`](../Source/HapbeatSDK/Public/HapbeatTriggerComponent.h) | EventMap entry と trigger の共通契約 |
| [`HapbeatSubsystem.h`](../Source/HapbeatSDK/Public/HapbeatSubsystem.h) | UDP 接続、`PlayEntry` / `StopEntry`、stream、address override |

## 安全な調整とビルド時の注意

- 安全に変えられるのは、PIE を止めた Editor World での zone / Child Actor slot の Transform、EventMap の Gain・Target・loop、公開された `Hapbeat|...` property です。変更後は Level または Data Asset を保存します。
- mesh の size / axis 補正、rod preview、target の内部 mesh transform は `OnConstruction` が導出します。見た目だけを直接動かして保存対象と誤認しないでください。
- sample C++ を変更した場合は通常 build を行ってから Editor を開き直します。Live Coding だけでは constructor-created component や CDO の変更が既存 map に反映されない場合があります。
- `Showcase.umap` を再生成する前に、保持したい調整を version control に保存します。再生成は通常の配置調整の手段ではありません。
- editor の Test Play、PIE、capture script は UDP を送信し得ます。実機を使わない自動検証では device を接続せず、音声を出す検証も行わないでください。
