// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatSubsystem.h"

#include "HapbeatProtocol.h"
#include "HapbeatConfig.h"
#include "HapbeatClip.h"
#include "HapbeatNetInterfaces.h"
#include "HapbeatNetworkSafety.h"
#include "HapbeatEventEntry.h"
#include "HapbeatEventMap.h"
#include "HapbeatStreamPlayback.h"
#include "HapbeatStreamSessionContract.h"
#include "HapbeatStreamRunnable.h"
#include "HapbeatTargetLibrary.h"
#include "Async/Async.h"
#include "Common/UdpSocketBuilder.h"
#include "Common/UdpSocketReceiver.h"
#include "Engine/GameInstance.h"  // GetGameInstance()->GetTimerManager() — the haptic-delay timers
#include "TimerManager.h"
#include "HAL/RunnableThread.h"   // FRunnableThread::Create/Kill for the dedicated stream thread
#include "Interfaces/IPv4/IPv4Address.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h" // GConfig / GGameUserSettingsIni — address-override persistence
#include "Misc/ScopeLock.h"      // FScopeLock (SeqLock)
#include "Sockets.h"
#include "SocketSubsystem.h"

namespace
{
	// GameUserSettings ini section + keys for the persisted address override
	// (SetAddressOverride / ClearPersistedAddressOverride). Mirrors the Unity
	// SDK's PlayerPrefs keys (Hapbeat.OverridePlayer / Hapbeat.OverrideGroup),
	// renamed to fit the ini section/key idiom.
	const TCHAR* AddressOverrideConfigSection = TEXT("HapbeatSDK");
	const TCHAR* AddressOverridePlayerKey = TEXT("AddressOverridePlayer");
	const TCHAR* AddressOverrideGroupKey = TEXT("AddressOverrideGroup");
}

DEFINE_LOG_CATEGORY_STATIC(LogHapbeat, Log, All);

UHapbeatSubsystem::UHapbeatSubsystem() = default;
UHapbeatSubsystem::~UHapbeatSubsystem()
{
	StopStream();
}

void UHapbeatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Seed runtime settings from the project's Hapbeat config (Project Settings >
	// Plugins > Hapbeat). GetDefault is the CDO-backed settings instance.
	const UHapbeatConfig* Cfg = GetDefault<UHapbeatConfig>();
	if (Cfg != nullptr)
	{
		Port = Cfg->Port;
		PingInterval = Cfg->PingInterval;
		StreamSendAheadSeconds = Cfg->StreamSendAheadSeconds;
		bCommandUnicast = Cfg->bCommandUnicast;

		// AppName shows on the device OLED. Empty => fall back to the project name
		// (parity with the Unity SDK's Application.productName fallback), still
		// capped to the OLED grid width.
		FString ResolvedAppName = Cfg->AppName;
		if (ResolvedAppName.IsEmpty())
		{
			ResolvedAppName = FApp::GetProjectName();
		}
		// Stored RAW (uncapped, placeholders intact): AppNameForWire() substitutes
		// <p>/<g> per send and BuildConnectStatus applies the 16-char wire cap.
		AppName = ResolvedAppName;
	}

	// GConfig (if persisted) restores a per-device address override saved by a
	// prior SetAddressOverride(..., bPersist: true) call. There is no
	// config-level default to fall back to — an override is either persisted
	// from a previous run, or starts disabled. Mirrors HapbeatManager.Initialize
	// (Unity SDK) reading PlayerPrefs before auto-connect.
	int32 PersistedPlayer = AddressOverrideDisabled;
	int32 PersistedGroup = AddressOverrideDisabled;
	GConfig->GetInt(AddressOverrideConfigSection, AddressOverridePlayerKey, PersistedPlayer, GGameUserSettingsIni);
	GConfig->GetInt(AddressOverrideConfigSection, AddressOverrideGroupKey, PersistedGroup, GGameUserSettingsIni);
	OverridePlayer = NormalizeAddressOverride(PersistedPlayer);
	OverrideGroup = NormalizeAddressOverride(PersistedGroup);

	// A build-pinned axis wins over whatever was persisted: the point of pinning
	// is that this build always addresses the same seat, so a value left behind
	// by an earlier run on the same machine must not survive it.
	if (const UHapbeatConfig* PinConfig = GetDefault<UHapbeatConfig>())
	{
		if (NormalizeAddressOverride(PinConfig->ForcedOverridePlayer) != AddressOverrideDisabled)
		{
			OverridePlayer = NormalizeAddressOverride(PinConfig->ForcedOverridePlayer);
		}
		if (NormalizeAddressOverride(PinConfig->ForcedOverrideGroup) != AddressOverrideDisabled)
		{
			OverrideGroup = NormalizeAddressOverride(PinConfig->ForcedOverrideGroup);
		}
	}

	// Auto-connect at startup, matching the Unity SDK's Awake() auto-connect.
	// GameInstanceSubsystems exist only in PIE / packaged game (not the editor
	// itself), so this never opens a socket in edit mode.
	Connect(Port, AppName);
}

void UHapbeatSubsystem::Deinitialize()
{
	// Block any late SendPacket from lazily re-opening the socket during teardown.
	bShuttingDown = true;

	// Drop anything the haptic delay was still holding. The GameInstance timer
	// manager dies with the game instance anyway (PIE stop), but clearing here
	// covers the subsystem being torn down first, and releases the clip /
	// playback objects those records were keeping alive.
	CancelPendingSends();

	// End any active stream FIRST, while the socket is still open: this sends
	// STREAM_END to the device and removes the streaming ticker. Doing it before
	// the receiver/socket teardown guarantees the END actually goes out (and the
	// ticker can't fire into a half-destroyed subsystem).
	StopStream();

	// Stop the keep-alive ticker first so it can't fire mid-teardown.
	if (KeepAliveHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(KeepAliveHandle);
		KeepAliveHandle.Reset();
	}

	// Tear down the receiver BEFORE the socket: its dtor stops the worker thread
	// and joins it, guaranteeing no callback touches Socket after this point.
	if (Receiver != nullptr)
	{
		Receiver->Stop(); // explicit stop + thread join (don't rely on dtor join across engine versions)
		delete Receiver;  // blocks until the receive thread has exited
		Receiver = nullptr;
	}

	if (Socket != nullptr)
	{
		if (!AppName.IsEmpty())
		{
			// Tell the device this app is leaving so the OLED clears.
			SendDiscoveryPacket(FHapbeatProtocol::BuildConnectStatus(NextSeq(), false, ConnectStatusGroupByte(), AppNameForWire(), FString()));
		}
		Socket->Close();
		if (ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
		{
			SocketSubsystem->DestroySocket(Socket);
		}
		Socket = nullptr;
	}

	DevicePongTimes.Empty();
	StreamEndpoints.Empty();
	PendingPings.Empty();
	PrevAliveCount = -1;

	Super::Deinitialize();
}

void UHapbeatSubsystem::Connect(int32 InPort, const FString& InAppName)
{
	Port = InPort;
	AppName = InAppName; // raw/templated; see AppNameForWire()

	if (Socket == nullptr)
	{
		// Bind to an OS-assigned local port so device PONG replies land back on
		// THIS socket (the device unicasts its reply to the packet's source
		// addr/port). Reusable + broadcast-capable for send.
		Socket = FUdpSocketBuilder(TEXT("HapbeatUDP"))
					 .AsReusable()
					 .AsNonBlocking()
					 .WithBroadcast()
					 .BoundToAddress(FIPv4Address::Any)
					 .BoundToPort(0)
					 .WithReceiveBufferSize(64 * 1024)
					 .Build();
	}
	if (Socket == nullptr)
	{
		UE_LOG(LogHapbeat, Warning, TEXT("Failed to create/bind UDP socket on port %d"), Port);
		return;
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSubsystem == nullptr)
	{
		return;
	}
	BroadcastAddr = SocketSubsystem->CreateInternetAddr();
	bool bIsValid = false;
	BroadcastAddr->SetIp(TEXT("255.255.255.255"), bIsValid);
	BroadcastAddr->SetPort(Port);

	// Discovery destinations for this connection: one per local IPv4 subnet
	// plus the limited broadcast above as a catch-all. On a multi-homed host
	// 255.255.255.255 only leaves through the lowest-metric interface, which
	// may have no Hapbeat behind it (DEC-054).
	BroadcastRoutes = HapbeatEnumerateBroadcastRoutes(Port);
	LockedRouteIndex = INDEX_NONE;

	// Device knowledge is PER-CONNECTION: after a reconnect (Wi-Fi change, AP
	// switch, hand-off) the previously-seen IPs may belong to entirely different
	// devices, so unicasting to them would aim at the wrong hosts. Parity with
	// Unity HapbeatClient.OpenBroadcast.
	DevicePongTimes.Empty();
	DeviceAddresses.Empty();
	StreamEndpoints.Empty();
	// Outage state belongs to the connection we just replaced.
	bLoggedSendError = false;

	// Start receiving PONG/ERROR on a worker thread. 100 ms poll matches the
	// Unity client's Poll() cadence; results are marshalled to the game thread.
	if (Receiver == nullptr)
	{
		Receiver = new FUdpSocketReceiver(Socket, FTimespan::FromMilliseconds(100.0), TEXT("HapbeatReceiver"));
		Receiver->OnDataReceived().BindUObject(this, &UHapbeatSubsystem::HandleReceivedData);
		Receiver->Start();
	}

	// Start the keep-alive ticker. It runs at the fast DISCOVERY rate and decides
	// per tick whether to actually send (see KeepAliveIntervalSeconds), so it
	// costs nothing once a device is known but finds one quickly on a cold start.
	if (!KeepAliveHandle.IsValid())
	{
		KeepAliveHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UHapbeatSubsystem::TickKeepAlive), DiscoveryTickSeconds);
	}

	if (!AppName.IsEmpty())
	{
		SendDiscoveryPacket(FHapbeatProtocol::BuildConnectStatus(NextSeq(), true, ConnectStatusGroupByte(), AppNameForWire(), FString()));
	}

	// PING IMMEDIATELY. Devices only ever answer a PING (a CONNECT_STATUS draws no
	// PONG), so without this the first reply could not arrive until the ticker
	// first fired -- a full PingInterval (5 s by default) of GetAliveDeviceCount()
	// == 0. During that window every send falls back to broadcast, which Wi-Fi APs
	// batch at the DTIM interval: the user-visible symptom is several seconds of
	// stuttering haptics right after startup, then it suddenly smooths out.
	Ping();
	LastKeepAliveSendTime = FPlatformTime::Seconds();

	// NOTE: deliberately do NOT raise OnConnected here. socket-open != device
	// presence. OnConnected fires only once a device PONGs (liveness 0->positive),
	// which fixes the Unity double-fire (open + first-PONG both firing OnConnected).
}

void UHapbeatSubsystem::Play(const FString& EventId, float Gain, const FString& Target, float Pan)
{
	const FString ResolvedTarget = UHapbeatTargetLibrary::ResolveTarget(Target, OverridePlayer, OverrideGroup);
	SendCommandPacket(FHapbeatProtocol::BuildPlay(NextSeq(), EventId, ResolvedTarget, 0,
		FMath::Clamp(Gain, 0.0f, 1.0f), FMath::Clamp(Pan, -1.0f, 1.0f)), ResolvedTarget);
}

void UHapbeatSubsystem::Stop(const FString& EventId, const FString& Target)
{
	const FString ResolvedTarget = UHapbeatTargetLibrary::ResolveTarget(Target, OverridePlayer, OverrideGroup);
	SendCommandPacket(FHapbeatProtocol::BuildStop(NextSeq(), EventId, ResolvedTarget), ResolvedTarget);
}

void UHapbeatSubsystem::StopAll(const FString& Target)
{
	const FString ResolvedTarget = UHapbeatTargetLibrary::ResolveTarget(Target, OverridePlayer, OverrideGroup);
	SendCommandPacket(FHapbeatProtocol::BuildStopAll(NextSeq(), ResolvedTarget), ResolvedTarget);
}

UHapbeatStreamPlayback* UHapbeatSubsystem::PlayEntry(UHapbeatEventMap* Map, FGuid EntryId, float GainMultiplier,
	bool bForceNonLoop, float Pan, float ExtraDelaySeconds)
{
	FHapbeatEventEntry Entry;
	if (Map == nullptr || !Map->FindById(EntryId, Entry))
	{
		UE_LOG(LogHapbeat, Warning,
			TEXT("PlayEntry: entry %s is not in '%s'. It was probably deleted after the caller was authored."),
			*EntryId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(Map));
		return nullptr;
	}

	// Author intent. GetEffectiveGain() already folds in the manifest intensity,
	// because the device plays req.gain verbatim and never reads the manifest
	// itself. For a Stream Clip this is the BASELINE and GainMultiplier stays
	// separate as the initial modulator (see the header); for a Command there is
	// nothing to modulate later, so the two are multiplied for the wire value.
	const float Baseline = Entry.GetEffectiveGain();

	// Pan composes ADDITIVELY (gain multiplies): the entry authors where the event
	// normally sits and the call site nudges it from there, so the neutral pair
	// 0 + 0 stays dead centre. Clamped once, here, so every path below — command,
	// stream, immediate, deferred — puts the same value on the wire / mirror.
	const float EffectivePan = FMath::Clamp(Entry.Pan + Pan, -1.0f, 1.0f);

	// Audio-latency compensation. Validation stays here, at call time; only the
	// send itself moves. Zero (the default) takes the untouched synchronous path
	// below — no timer, no allocation, no behaviour change.
	const float Delay = ComputeEffectiveDelaySeconds(Entry, ExtraDelaySeconds);
	const bool bDeferred = Delay > KINDA_SMALL_NUMBER;

	if (Entry.Mode == EHapticMode::StreamClip)
	{
		UHapbeatClip* Clip = Entry.StreamClip.LoadSynchronous();
		if (Clip == nullptr)
		{
			UE_LOG(LogHapbeat, Warning,
				TEXT("PlayEntry: entry '%s' is Stream Clip mode but has no clip assigned."), *Entry.GetEventId());
			return nullptr;
		}
		// One-shot phases (sequence start / stop shots) must not leave a loop
		// running, whatever the entry says.
		const bool bLoop = bForceNonLoop ? false : Entry.bLoop;
		// Pan reaches a Stream Clip through the HANDLE, not the wire: the SDK
		// pre-multiplies the balance onto the (possibly upmixed) stereo PCM it
		// sends. It is handed to StreamClip rather than written on the returned
		// handle so it is already on the atomic mirror when the session starts --
		// a MONO clip is upmixed to stereo only if the pan is non-zero at that
		// moment (STREAM_BEGIN fixes the channel count for the session).
		if (!bDeferred)
		{
			return StreamClip(Clip, Baseline, GainMultiplier, Entry.Target, bLoop, EffectivePan);
		}

		// Deferred stream: the handle must exist NOW (the caller wires bindings /
		// keeps it to Stop()), so create it here and start the session when the
		// timer fires. Replacing the currently-running session is deliberately
		// part of that later step: killing it at call time would cut the previous
		// haptic short by exactly the delay.
		UHapbeatStreamPlayback* Playback = NewObject<UHapbeatStreamPlayback>(this);
		Playback->Init(Baseline, GainMultiplier);
		if (EffectivePan != 0.0f)
		{
			// Rides on the handle's atomic mirror, so the deferred start picks it
			// up -- no need to carry Pan in the pending record for this kind.
			Playback->SetPan(EffectivePan);
		}

		FHapbeatPendingSend Pending;
		Pending.Kind = EHapbeatPendingKind::StartStream;
		Pending.Clip = Clip;
		Pending.Playback = Playback;
		Pending.Target = Entry.Target;
		Pending.bLoop = bLoop;
		if (!SchedulePendingSend(MoveTemp(Pending), Delay))
		{
			// Could not schedule — fall back to firing now rather than handing
			// back a handle whose stream would never start. StreamClip makes its
			// OWN handle (the pre-panned one above is dropped), so the pan has to
			// be passed again here.
			return StreamClip(Clip, Baseline, GainMultiplier, Entry.Target, bLoop, EffectivePan);
		}
		return Playback;
	}

	const FString EventId = Entry.GetEventId();
	if (EventId.IsEmpty())
	{
		UE_LOG(LogHapbeat, Warning, TEXT("PlayEntry: Command entry has an empty event id (set Event Name)."));
		return nullptr;
	}

	const float Gain = Baseline * GainMultiplier;

	if (bDeferred)
	{
		FHapbeatPendingSend Pending;
		Pending.Kind = EHapbeatPendingKind::PlayCommand;
		Pending.EventId = EventId;
		Pending.Gain = Gain;
		Pending.Pan = EffectivePan;
		Pending.Target = Entry.Target;
		if (SchedulePendingSend(MoveTemp(Pending), Delay))
		{
			return nullptr;
		}
		// Scheduling failed; fall through and send immediately.
	}
	Play(EventId, Gain, Entry.Target, EffectivePan);
	return nullptr;
}

void UHapbeatSubsystem::StopEntry(UHapbeatEventMap* Map, FGuid EntryId)
{
	FHapbeatEventEntry Entry;
	if (Map == nullptr || !Map->FindById(EntryId, Entry))
	{
		return;
	}

	// Stop is delayed by the SAME amount as Play so the interval between them
	// survives the compensation (Unity HapbeatTriggerBase.StopHaptic). A Stop
	// issued while its own Play is still pending therefore still lands after it.
	const float Delay = ComputeEffectiveDelaySeconds(Entry);
	const bool bDeferred = Delay > KINDA_SMALL_NUMBER;

	if (Entry.Mode == EHapticMode::StreamClip)
	{
		// One stream session at a time, so there is nothing finer to stop here.
		// Hold the handle PlayEntry returned to stop just that playback instead.
		if (bDeferred)
		{
			FHapbeatPendingSend Pending;
			Pending.Kind = EHapbeatPendingKind::StopStream;
			if (SchedulePendingSend(MoveTemp(Pending), Delay))
			{
				return;
			}
		}
		StopStream();
		return;
	}

	if (bDeferred)
	{
		FHapbeatPendingSend Pending;
		Pending.Kind = EHapbeatPendingKind::StopCommand;
		Pending.EventId = Entry.GetEventId();
		Pending.Target = Entry.Target;
		if (SchedulePendingSend(MoveTemp(Pending), Delay))
		{
			return;
		}
	}
	Stop(Entry.GetEventId(), Entry.Target);
}

float UHapbeatSubsystem::ComputeEffectiveDelaySeconds(const FHapbeatEventEntry& Entry, float ExtraDelaySeconds) const
{
	const UHapbeatConfig* Cfg = GetDefault<UHapbeatConfig>();
	const float Global = Cfg != nullptr ? Cfg->HapticDelaySeconds : 0.0f;
	// Clamped at 0: a negative per-entry offset (or a negative per-call extra) can
	// pull the haptic back towards "now", never before it (Unity
	// ComputeEffectiveDelaySeconds).
	return FMath::Max(0.0f, Global + Entry.DelayOffsetSeconds + ExtraDelaySeconds);
}

bool UHapbeatSubsystem::SchedulePendingSend(FHapbeatPendingSend&& Pending, float DelaySeconds)
{
	UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance == nullptr || bShuttingDown)
	{
		return false;
	}

	const uint32 PendingId = ++NextPendingSendId;
	FHapbeatPendingSend& Record = PendingSends.Add(PendingId, MoveTemp(Pending));

	// CreateUObject (not a raw lambda) so the timer is weak-bound to this
	// subsystem: a callback that outlives it simply never runs.
	GameInstance->GetTimerManager().SetTimer(
		Record.Handle,
		FTimerDelegate::CreateUObject(this, &UHapbeatSubsystem::FirePendingSend, PendingId),
		DelaySeconds,
		/*InbLoop=*/false);
	return true;
}

void UHapbeatSubsystem::FirePendingSend(uint32 PendingId)
{
	// Remove first: the record is done either way, and Play/StopStream below must
	// not see a half-live entry if they ever re-enter this map.
	FHapbeatPendingSend Pending;
	if (!PendingSends.RemoveAndCopyValue(PendingId, Pending))
	{
		return; // already cancelled
	}
	if (bShuttingDown)
	{
		return;
	}

	switch (Pending.Kind)
	{
	case EHapbeatPendingKind::PlayCommand:
		Play(Pending.EventId, Pending.Gain, Pending.Target, Pending.Pan);
		break;

	case EHapbeatPendingKind::StopCommand:
		Stop(Pending.EventId, Pending.Target);
		break;

	case EHapbeatPendingKind::StopStream:
		StopStream();
		break;

	case EHapbeatPendingKind::StartStream:
	{
		UHapbeatStreamPlayback* Playback = Pending.Playback;
		if (Playback == nullptr || Playback->IsStopped())
		{
			// The caller stopped the handle during the delay window — honour that
			// instead of starting a stream nobody asked for any more.
			break;
		}
		if (Pending.Clip == nullptr)
		{
			UE_LOG(LogHapbeat, Warning, TEXT("Delayed stream: the clip went away before the delay elapsed; nothing streamed."));
			Playback->Stop();
			break;
		}
		// Gain / Pan written on the handle during the delay are already in its
		// atomic mirror, which is what the stream thread reads — the session
		// starts at the modulated value, not at the initial one.
		if (!StartStreamSession(Pending.Clip, Playback, Pending.Target, Pending.bLoop))
		{
			// Mark the handle dead so a caller polling IsActive() isn't told a
			// stream is running when none is.
			Playback->Stop();
		}
		break;
	}
	}
}

void UHapbeatSubsystem::CancelPendingSends()
{
	if (PendingSends.Num() == 0)
	{
		return;
	}
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		FTimerManager& TimerManager = GameInstance->GetTimerManager();
		for (TPair<uint32, FHapbeatPendingSend>& Pair : PendingSends)
		{
			TimerManager.ClearTimer(Pair.Value.Handle);
		}
	}
	PendingSends.Empty();
}

void UHapbeatSubsystem::Ping()
{
	const uint16 PingSeq = NextSeq();
	// PING wire field is Unix-epoch microseconds (parity with Python/Web/Unity).
	const int64 TimestampUs = UnixMicros();
	// Track local monotonic send time for RTT (now - sent). Prune any stale
	// outstanding PINGs (device offline) so the map doesn't grow unbounded.
	if (PendingPings.Num() > 256)
	{
		const int64 Cutoff = NowMicros() - 10'000'000; // 10 s
		for (auto It = PendingPings.CreateIterator(); It; ++It)
		{
			if (It.Value() < Cutoff)
			{
				It.RemoveCurrent();
			}
		}
	}
	PendingPings.Add(PingSeq, NowMicros());
	SendDiscoveryPacket(FHapbeatProtocol::BuildPing(PingSeq, TimestampUs));
}

int64 UHapbeatSubsystem::ResolvePongRttUs(TMap<uint16, int64>& InOutPendingPings,
	uint16 PongSeq, int64 NowMonotonicUs, int64 EchoedTimestampUs, int64 NowUnixUs)
{
	// Timestamp 0 is the protocol sentinel for an unsolicited PONG. It takes
	// priority over the sequence number because the uint16 sequence can wrap;
	// consuming a coincident pending entry would turn discovery into a false RTT
	// sample and lose the real PING's measurement.
	if (EchoedTimestampUs <= 0)
	{
		return 0;
	}

	int64 SentMonotonicUs = 0;
	if (InOutPendingPings.RemoveAndCopyValue(PongSeq, SentMonotonicUs))
	{
		return FMath::Max<int64>(0, NowMonotonicUs - SentMonotonicUs);
	}

	return FMath::Max<int64>(0, NowUnixUs - EchoedTimestampUs);
}

UHapbeatStreamPlayback* UHapbeatSubsystem::GetActivePlayback() const
{
	for (UHapbeatStreamPlayback* Playback : ActivePlaybacks)
	{
		if (Playback != nullptr && Playback->IsActive())
		{
			return Playback;
		}
	}
	return nullptr;
}

UHapbeatStreamPlayback* UHapbeatSubsystem::StreamClip(UHapbeatClip* Clip, float BaselineGain, float InitialGain,
	const FString& Target, bool bLoop, float InitialPan)
{
	if (Clip == nullptr || Clip->Pcm16.Num() == 0)
	{
		UE_LOG(LogHapbeat, Warning, TEXT("StreamClip: clip is null or has no PCM data; ignoring."));
		return nullptr;
	}

	// GC-rooted via ActivePlaybacks for the source's lifetime (the caller may not
	// retain it). Created before the session so the delayed path in
	// FirePendingSend can reuse StartStreamSession with a handle that already
	// exists — this public entry point is otherwise unchanged.
	UHapbeatStreamPlayback* Playback = NewObject<UHapbeatStreamPlayback>(this);
	Playback->Init(BaselineGain, InitialGain);
	Playback->SetPan(InitialPan);
	Playback->SetLoop(bLoop);

	return StartStreamSession(Clip, Playback, Target, bLoop) ? Playback : nullptr;
}

bool UHapbeatSubsystem::StartStreamSession(UHapbeatClip* Clip, UHapbeatStreamPlayback* Playback,
	const FString& Target, bool bLoop)
{
	if (Clip == nullptr || Clip->Pcm16.Num() == 0 || Playback == nullptr)
	{
		UE_LOG(LogHapbeat, Warning, TEXT("StartStreamSession: clip is null or has no PCM data; ignoring."));
		return false;
	}

	// The stream thread must NOT lazily Connect() itself (that touches
	// Receiver/tickers/etc — game-thread-only machinery) — ensure the socket
	// exists here, on the game thread, before spinning the thread up. Same
	// teardown guard as SendPacket: never resurrect the socket / receiver /
	// keep-alive ticker on an already-deinitialized subsystem.
	if (Socket == nullptr)
	{
		if (bShuttingDown)
		{
			return false;
		}
		Connect(Port, AppName);
		if (Socket == nullptr)
		{
			UE_LOG(LogHapbeat, Warning, TEXT("StreamClip: no socket available; ignoring."));
			return false;
		}
	}

	TArray<uint8> CanonicalPcm16;
	if (!NormalizeClipToCanonical(Clip, CanonicalPcm16))
	{
		UE_LOG(LogHapbeat, Warning, TEXT("StreamClip: could not normalize clip to 16 kHz stereo PCM16; ignoring."));
		return false;
	}

	const FString ResolvedTarget = UHapbeatTargetLibrary::ResolveTarget(Target, OverridePlayer, OverrideGroup);
	Playback->SetLoop(bLoop);
	ActivePlaybacks.Add(Playback);
	FStreamSource& Source = StreamSources.Add(Playback->Id);
	Source.CanonicalPcm16 = MoveTemp(CanonicalPcm16);
	Source.AuthoredTarget = Target;
	Source.ResolvedTarget = ResolvedTarget;
	Source.Playback = Playback;

	// Watchdog: poll for the thread finishing on its own (natural EOF on a
	// non-loop clip, or the handle's own Stop()) and clean up when it does.
	if (!StreamTickHandle.IsValid())
	{
		StreamTickHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UHapbeatSubsystem::TickStream), 0.0f);
	}

	ReconcileStreamSources();
	UE_LOG(LogHapbeat, Log, TEXT("Stream source registered: 16000Hz stereo, target=%s, loop=%d"),
		ResolvedTarget.IsEmpty() ? TEXT("all") : *ResolvedTarget, bLoop ? 1 : 0);

	return true;
}

void UHapbeatSubsystem::RefreshStreamSourceTargets()
{
	for (TPair<FGuid, FStreamSource>& Pair : StreamSources)
	{
		FStreamSource& Source = Pair.Value;
		if (UHapbeatStreamPlayback* Playback = Source.Playback.Get(); Playback != nullptr && !Playback->IsStopped())
		{
			Source.ResolvedTarget = UHapbeatTargetLibrary::ResolveTarget(
				Source.AuthoredTarget, OverridePlayer, OverrideGroup);
		}
	}
}

void UHapbeatSubsystem::RequestStreamDiscoveryForDeferredSources()
{
	for (const TPair<FGuid, FStreamSource>& Pair : StreamSources)
	{
		const FStreamSource& Source = Pair.Value;
		UHapbeatStreamPlayback* Playback = Source.Playback.Get();
		if (Playback == nullptr || Playback->IsStopped() || Source.EndpointKeys.Num() != 0)
		{
			continue;
		}

#if WITH_DEV_AUTOMATION_TESTS
		if (bSuppressStreamDiscoveryForAutomationTest)
		{
			++StreamDiscoveryRequestCount;
			return;
		}
#endif
		Ping();
		return;
	}
}

bool UHapbeatSubsystem::NormalizeClipToCanonical(const UHapbeatClip* Clip, TArray<uint8>& OutPcm16) const
{
	if (Clip == nullptr || Clip->SampleRate <= 0 || Clip->NumChannels <= 0 || Clip->Pcm16.Num() < Clip->NumChannels * 2)
	{
		return false;
	}
	const int32 SourceFrames = Clip->Pcm16.Num() / (Clip->NumChannels * 2);
	const int32 OutputFrames = FMath::Max(1, FMath::CeilToInt(static_cast<float>(SourceFrames) * 16000.0f / Clip->SampleRate));
	OutPcm16.SetNumUninitialized(OutputFrames * 4);
	for (int32 OutputFrame = 0; OutputFrame < OutputFrames; ++OutputFrame)
	{
		const int32 SourceFrame = FMath::Min(SourceFrames - 1,
			FMath::FloorToInt(static_cast<float>(OutputFrame) * Clip->SampleRate / 16000.0f));
		for (int32 Channel = 0; Channel < 2; ++Channel)
		{
			const int32 SourceChannel = Clip->NumChannels == 1 ? 0 : FMath::Min(Channel, Clip->NumChannels - 1);
			const int32 SourceOffset = (SourceFrame * Clip->NumChannels + SourceChannel) * 2;
			const int32 OutputOffset = (OutputFrame * 2 + Channel) * 2;
			OutPcm16[OutputOffset] = Clip->Pcm16[SourceOffset];
			OutPcm16[OutputOffset + 1] = Clip->Pcm16[SourceOffset + 1];
		}
	}
	return true;
}

void UHapbeatSubsystem::RegisterStreamEndpoint(
	const FString& Ip, int32 InPort, const FString& Address, double NowSeconds)
{
	const FString EndpointKey = FString::Printf(TEXT("%s:%d|%s"), *Ip, InPort, *Address);
	TArray<FString> MigrationCandidates;
	const bool bExactSessionExists = StreamSessions.Contains(EndpointKey);
	for (const TPair<FString, FStreamEndpoint>& Existing : StreamEndpoints)
	{
		if (Existing.Key == EndpointKey
			|| !HapbeatStreamSessionContract::IsMigrationCandidate(
				Existing.Value.Ip, Existing.Value.Port, Existing.Value.Address,
				Ip, InPort, Address,
				NowSeconds - Existing.Value.LastPongSeconds <= AliveTimeoutSeconds()))
		{
			continue;
		}
		MigrationCandidates.Add(Existing.Key);
	}

	// More than one candidate has no trustworthy device identity. Preserve every
	// exact endpoint instead of guessing and collapsing independent sessions.
	if (!bExactSessionExists && MigrationCandidates.Num() == 1)
	{
		const FString OldKey = MigrationCandidates[0];
		FStreamSession Migrated;
		if (StreamSessions.RemoveAndCopyValue(OldKey, Migrated))
		{
			if (Migrated.Runnable != nullptr)
			{
				Migrated.Runnable->UpdateEndpoint(Ip, InPort);
			}
			StreamSessions.Add(EndpointKey, MoveTemp(Migrated));
		}
		if (const double* EndedAt = StreamSessionEndedAt.Find(OldKey))
		{
			StreamSessionEndedAt.Add(EndpointKey, *EndedAt);
		}
		for (TPair<FGuid, FStreamSource>& SourcePair : StreamSources)
		{
			if (SourcePair.Value.EndpointKeys.Remove(OldKey) > 0)
			{
				SourcePair.Value.EndpointKeys.Add(EndpointKey);
			}
			if (SourcePair.Value.CompletedEndpointKeys.Remove(OldKey) > 0)
			{
				SourcePair.Value.CompletedEndpointKeys.Add(EndpointKey);
			}
		}
		StreamSessionEndedAt.Remove(OldKey);
		StreamEndpoints.Remove(OldKey);
	}

	FStreamEndpoint& Endpoint = StreamEndpoints.FindOrAdd(EndpointKey);
	Endpoint.Ip = Ip;
	Endpoint.Port = InPort;
	Endpoint.Address = Address;
	Endpoint.LastPongSeconds = NowSeconds;
}

void UHapbeatSubsystem::ReconcileStreamSources()
{
	const double Now = FPlatformTime::Seconds();
	TArray<FString> ExpiredEndpointKeys;
	for (const TPair<FString, FStreamEndpoint>& Pair : StreamEndpoints)
	{
		if (Now - Pair.Value.LastPongSeconds > AliveTimeoutSeconds())
		{
			ExpiredEndpointKeys.Add(Pair.Key);
		}
	}
	for (const FString& EndpointKey : ExpiredEndpointKeys)
	{
		AbandonStreamSession(EndpointKey);
		StreamEndpoints.Remove(EndpointKey);
	}
	for (TPair<FGuid, FStreamSource>& Pair : StreamSources)
	{
		FStreamSource& Source = Pair.Value;
		UHapbeatStreamPlayback* Playback = Source.Playback.Get();
		if (Playback == nullptr || Playback->IsStopped())
		{
			continue;
		}
		TSet<FString> NewEndpointKeys;
		for (const TPair<FString, FStreamEndpoint>& EndpointPair : StreamEndpoints)
		{
			const FStreamEndpoint& Endpoint = EndpointPair.Value;
			if (Now - Endpoint.LastPongSeconds <= AliveTimeoutSeconds()
				&& !Endpoint.Address.IsEmpty()
				&& UHapbeatTargetLibrary::AddressMatches(Source.ResolvedTarget, Endpoint.Address))
			{
				NewEndpointKeys.Add(EndpointPair.Key);
			}
		}
		for (const FString& PreviousEndpointKey : Source.EndpointKeys)
		{
			if (NewEndpointKeys.Contains(PreviousEndpointKey))
			{
				continue;
			}
			if (FStreamSession* PreviousSession = StreamSessions.Find(PreviousEndpointKey))
			{
				if (PreviousSession->Runnable != nullptr)
				{
					PreviousSession->Runnable->DetachSource(Pair.Key);
				}
				PreviousSession->SourceIds.Remove(Pair.Key);
			}
			Source.CompletedEndpointKeys.Remove(PreviousEndpointKey);
		}
		Source.EndpointKeys = MoveTemp(NewEndpointKeys);
		if (Source.EndpointKeys.Num() == 0)
		{
			Playback->SetDeferredNoEndpoint();
			continue;
		}
		Playback->SetActive();
		for (const FString& EndpointKey : Source.EndpointKeys)
		{
			FStreamSession* Existing = StreamSessions.Find(EndpointKey);
			if (Existing != nullptr && Existing->Runnable != nullptr)
			{
				if (!Existing->SourceIds.Contains(Pair.Key))
				{
					if (Existing->Runnable->AddSource(Pair.Key, TArray<uint8>(Source.CanonicalPcm16), Playback->GetMirror()))
					{
						Existing->SourceIds.Add(Pair.Key);
					}
					else
					{
						Playback->SetDeferredNoEndpoint();
					}
				}
				continue;
			}
			const FStreamEndpoint* Endpoint = StreamEndpoints.Find(EndpointKey);
			const FString RouteKey = Endpoint != nullptr ? Endpoint->Ip + TEXT("|") + Endpoint->Address : FString();
			const double* EndedAt = StreamSessionEndedAt.Find(EndpointKey);
			const double* RouteEndedAt = StreamRouteEndedAt.Find(RouteKey);
			if ((EndedAt == nullptr || Now - *EndedAt >= 0.300)
				&& (RouteEndedAt == nullptr || Now - *RouteEndedAt >= 0.300))
			{
				StartEndpointSession(EndpointKey);
			}
		}
	}
}

void UHapbeatSubsystem::StartEndpointSession(const FString& EndpointKey)
{
	const FStreamEndpoint* Endpoint = StreamEndpoints.Find(EndpointKey);
	if (Endpoint == nullptr || Socket == nullptr || StreamSessions.Contains(EndpointKey))
	{
		return;
	}
	FGuid FirstSourceId;
	FStreamSource* FirstSource = nullptr;
	UHapbeatStreamPlayback* FirstPlayback = nullptr;
	for (TPair<FGuid, FStreamSource>& Pair : StreamSources)
	{
		if (Pair.Value.EndpointKeys.Contains(EndpointKey)
			&& (FirstPlayback = Pair.Value.Playback.Get()) != nullptr && FirstPlayback->IsActive())
		{
			FirstSourceId = Pair.Key;
			FirstSource = &Pair.Value;
			break;
		}
	}
	if (FirstSource == nullptr)
	{
		return;
	}
	TArray<FString> EndpointIps;
	EndpointIps.Add(Endpoint->Ip);
	FStreamSession Session;
	Session.Runnable = new FHapbeatStreamRunnable(
		FirstSourceId, TArray<uint8>(FirstSource->CanonicalPcm16), 16000, 2, Endpoint->Address,
		FirstPlayback->GetMirror(), [this]() { return NextSeq(); }, Socket,
		Endpoint->Port, MoveTemp(EndpointIps), StreamSendAheadSeconds);
	Session.Thread = FRunnableThread::Create(Session.Runnable, TEXT("HapbeatStreamEndpointThread"), 0, TPri_AboveNormal);
	if (Session.Thread == nullptr)
	{
		delete Session.Runnable;
		FirstPlayback->SetDeferredNoEndpoint();
		return;
	}
	Session.SourceIds.Add(FirstSourceId);
	for (TPair<FGuid, FStreamSource>& Pair : StreamSources)
	{
		if (Pair.Key != FirstSourceId && Pair.Value.EndpointKeys.Contains(EndpointKey))
		{
			if (UHapbeatStreamPlayback* Playback = Pair.Value.Playback.Get())
			{
				if (Session.Runnable->AddSource(Pair.Key, TArray<uint8>(Pair.Value.CanonicalPcm16), Playback->GetMirror()))
				{
					Session.SourceIds.Add(Pair.Key);
				}
			}
		}
	}
	StreamSessions.Add(EndpointKey, MoveTemp(Session));
}

void UHapbeatSubsystem::StopStreamSession(const FString& EndpointKey)
{
	const FStreamEndpoint* Endpoint = StreamEndpoints.Find(EndpointKey);
	FStreamSession Session;
	if (!StreamSessions.RemoveAndCopyValue(EndpointKey, Session))
	{
		return;
	}
	if (Session.Thread != nullptr)
	{
		Session.Thread->Kill(true);
		delete Session.Thread;
	}
	delete Session.Runnable;
	const double EndedAt = FPlatformTime::Seconds();
	StreamSessionEndedAt.Add(EndpointKey, EndedAt);
	if (Endpoint != nullptr)
	{
		StreamRouteEndedAt.Add(Endpoint->Ip + TEXT("|") + Endpoint->Address, EndedAt);
	}
}

void UHapbeatSubsystem::AbandonStreamSession(const FString& EndpointKey)
{
	FStreamSession Session;
	if (!StreamSessions.RemoveAndCopyValue(EndpointKey, Session))
	{
		return;
	}
	if (Session.Runnable != nullptr)
	{
		Session.Runnable->Abandon();
	}
	if (Session.Thread != nullptr)
	{
		Session.Thread->Kill(true);
		delete Session.Thread;
	}
	delete Session.Runnable;
}

bool UHapbeatSubsystem::TickStream(float /*DeltaSeconds*/)
{
	for (TPair<FString, FStreamSession>& SessionPair : StreamSessions)
	{
		if (SessionPair.Value.Runnable == nullptr)
		{
			continue;
		}
		TArray<FGuid> FinishedSourceIds;
		SessionPair.Value.Runnable->DrainFinishedSourceIds(FinishedSourceIds);
		for (const FGuid& SourceId : FinishedSourceIds)
		{
			SessionPair.Value.SourceIds.Remove(SourceId);
			if (FStreamSource* Source = StreamSources.Find(SourceId))
			{
				UHapbeatStreamPlayback* Playback = Source->Playback.Get();
				if (Playback != nullptr && Playback->GetLoop())
				{
					// Loop may have been enabled after this endpoint reached EOF.
					// Leave the source alive so Reconcile can re-admit it at frame 0.
					Source->CompletedEndpointKeys.Remove(SessionPair.Key);
				}
				else
				{
					Source->CompletedEndpointKeys.Add(SessionPair.Key);
				}
			}
		}
	}
	for (TPair<FGuid, FStreamSource>& Pair : StreamSources)
	{
		FStreamSource& Source = Pair.Value;
		UHapbeatStreamPlayback* Playback = Source.Playback.Get();
		if (Playback != nullptr && Playback->GetLoop())
		{
			Source.CompletedEndpointKeys.Reset();
			continue;
		}
		if (Source.EndpointKeys.Num() == 0)
		{
			continue;
		}
		bool bFinishedOnEveryEndpoint = true;
		for (const FString& EndpointKey : Source.EndpointKeys)
		{
			if (!Source.CompletedEndpointKeys.Contains(EndpointKey))
			{
				bFinishedOnEveryEndpoint = false;
				break;
			}
		}
		if (bFinishedOnEveryEndpoint)
		{
			if (Playback != nullptr)
			{
				Playback->Stop();
			}
		}
	}
	ActivePlaybacks.RemoveAllSwap([](const TObjectPtr<UHapbeatStreamPlayback>& Playback)
	{
		return Playback == nullptr || Playback->IsStopped();
	}, /*bAllowShrinking=*/false);

	for (auto It = StreamSources.CreateIterator(); It; ++It)
	{
		UHapbeatStreamPlayback* Playback = It.Value().Playback.Get();
		if (Playback == nullptr || Playback->IsStopped())
		{
			It.RemoveCurrent();
		}
	}
	TSet<FGuid> CompletedCandidates;
	for (auto It = StreamSessions.CreateIterator(); It; ++It)
	{
		FStreamSession& Session = It.Value();
		if (Session.Runnable != nullptr && Session.Runnable->IsFinished())
		{
			for (const FGuid& SourceId : Session.SourceIds)
			{
				CompletedCandidates.Add(SourceId);
			}
			if (Session.Thread != nullptr)
			{
				Session.Thread->Kill(true);
				delete Session.Thread;
			}
			delete Session.Runnable;
			const double EndedAt = FPlatformTime::Seconds();
			StreamSessionEndedAt.Add(It.Key(), EndedAt);
			if (const FStreamEndpoint* Endpoint = StreamEndpoints.Find(It.Key()))
			{
				StreamRouteEndedAt.Add(Endpoint->Ip + TEXT("|") + Endpoint->Address, EndedAt);
			}
			It.RemoveCurrent();
		}
	}
	for (const FGuid& SourceId : CompletedCandidates)
	{
		bool bStillRunning = false;
		for (const TPair<FString, FStreamSession>& Pair : StreamSessions)
		{
			if (Pair.Value.SourceIds.Contains(SourceId))
			{
				bStillRunning = true;
				break;
			}
		}
		if (!bStillRunning)
		{
			if (FStreamSource* Source = StreamSources.Find(SourceId))
			{
				if (UHapbeatStreamPlayback* Playback = Source->Playback.Get())
				{
					Playback->Stop();
				}
			}
		}
	}
	ReconcileStreamSources();
	if (StreamSources.Num() > 0 || StreamSessions.Num() > 0)
	{
		return true;
	}
	StreamTickHandle.Reset();
	return false;
}

void UHapbeatSubsystem::StopStream()
{
	TArray<FString> SessionKeys;
	StreamSessions.GetKeys(SessionKeys);
	for (const FString& Key : SessionKeys)
	{
		StopStreamSession(Key);
	}

	for (UHapbeatStreamPlayback* Playback : ActivePlaybacks)
	{
		if (Playback != nullptr)
		{
			Playback->Stop();
		}
	}
	ActivePlaybacks.Empty();
	StreamSources.Empty();

	// Remove the ticker explicitly. Callers from OUTSIDE the tick callback
	// (StreamClip's replace path, Deinitialize) land here with a valid handle.
	// TickStream's watchdog path deliberately Reset()s the handle before
	// calling us, so this branch is skipped there and its `return false`
	// stays the sole unregister mechanism (never RemoveTicker the
	// currently-firing handle).
	if (StreamTickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(StreamTickHandle);
		StreamTickHandle.Reset();
	}
}

void UHapbeatSubsystem::SetAddressOverride(int32 Player, int32 InGroup, bool bPersist)
{
	const UHapbeatConfig* Config = GetDefault<UHapbeatConfig>();
	const int32 ForcedPlayer = Config != nullptr ? NormalizeAddressOverride(Config->ForcedOverridePlayer) : AddressOverrideDisabled;
	const int32 ForcedGroup = Config != nullptr ? NormalizeAddressOverride(Config->ForcedOverrideGroup) : AddressOverrideDisabled;

	// A build-pinned axis is intentionally not mutable at runtime. This has to
	// live at the one send-boundary state owner, not merely in the Slate panel:
	// Blueprint callers and C++ callers must obey the same deployment contract.
	OverridePlayer = ForcedPlayer >= 1 ? ForcedPlayer : NormalizeAddressOverride(Player);
	OverrideGroup = ForcedGroup >= 1 ? ForcedGroup : NormalizeAddressOverride(InGroup);

	// STREAM packets carry no target and must never be broadcast. Keep the
	// authored source target intact, then atomically reassign each active source
	// to the exact PONG-confirmed endpoint(s) for the new effective target. A
	// source with no match stays Deferred and requests a PING; the normal PONG
	// path registers that endpoint and reconciles it without replaying the clip.
	RefreshStreamSourceTargets();
	ReconcileStreamSources();
	RequestStreamDiscoveryForDeferredSources();

	if (bPersist)
	{
		SavePersistedAddressOverride(Player, InGroup);
	}

	UE_LOG(LogHapbeat, Log, TEXT("Address override set: player=%d, group=%d, persist=%d"),
		OverridePlayer, OverrideGroup, bPersist ? 1 : 0);

	// Keep the device's CONNECT_STATUS (OLED) group display in sync with the new
	// routing immediately, instead of waiting up to PingInterval seconds for the
	// next periodic push. Mirrors HapbeatManager.SetAddressOverride (Unity SDK).
	if (Socket != nullptr && !bShuttingDown && !AppName.IsEmpty())
	{
		SendDiscoveryPacket(FHapbeatProtocol::BuildConnectStatus(NextSeq(), true, ConnectStatusGroupByte(), AppNameForWire(), FString()));
	}
}

void UHapbeatSubsystem::ClearPersistedAddressOverride()
{
	RemovePersistedAddressOverride();

	// Reuses SetAddressOverride (bPersist: false, so the just-cleared keys
	// aren't immediately re-saved) to push the reverted values to the runtime
	// state — same path a normal override change takes. Mirrors
	// HapbeatManager.ClearPersistedAddressOverride (Unity SDK).
	SetAddressOverride(AddressOverrideDisabled, AddressOverrideDisabled, /*bPersist=*/false);

	UE_LOG(LogHapbeat, Log, TEXT("Persisted address override cleared - reverted to disabled."));
}

bool UHapbeatSubsystem::TryGetPersistedAddressOverride(int32& OutPlayer, int32& OutGroup)
{
	// GetInt leaves its out param untouched when the key is absent, so both are
	// seeded with the disabled sentinel first: "not saved" and "saved as
	// something out of range" then read the same way to the caller, which is what
	// Initialize() already assumes when it normalizes what it read.
	OutPlayer = AddressOverrideDisabled;
	OutGroup = AddressOverrideDisabled;
	const bool bHasPlayer =
		GConfig->GetInt(AddressOverrideConfigSection, AddressOverridePlayerKey, OutPlayer, GGameUserSettingsIni);
	const bool bHasGroup =
		GConfig->GetInt(AddressOverrideConfigSection, AddressOverrideGroupKey, OutGroup, GGameUserSettingsIni);
	return bHasPlayer || bHasGroup;
}

void UHapbeatSubsystem::SavePersistedAddressOverride(int32 Player, int32 InGroup)
{
	const UHapbeatConfig* Config = GetDefault<UHapbeatConfig>();
	const bool bPlayerForced = Config != nullptr && NormalizeAddressOverride(Config->ForcedOverridePlayer) >= 1;
	const bool bGroupForced = Config != nullptr && NormalizeAddressOverride(Config->ForcedOverrideGroup) >= 1;

	// Do not create a hidden per-machine fallback underneath a build-pinned
	// axis. If a later build deliberately removes the pin, its Addressing
	// defaults remain the only project-authored source of truth.
	if (!bPlayerForced)
	{
		GConfig->SetInt(AddressOverrideConfigSection, AddressOverridePlayerKey,
			NormalizeAddressOverride(Player), GGameUserSettingsIni);
	}
	if (!bGroupForced)
	{
		GConfig->SetInt(AddressOverrideConfigSection, AddressOverrideGroupKey,
			NormalizeAddressOverride(InGroup), GGameUserSettingsIni);
	}
	GConfig->Flush(false, GGameUserSettingsIni);
}

void UHapbeatSubsystem::RemovePersistedAddressOverride()
{
	GConfig->RemoveKey(AddressOverrideConfigSection, AddressOverridePlayerKey, GGameUserSettingsIni);
	GConfig->RemoveKey(AddressOverrideConfigSection, AddressOverrideGroupKey, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

bool UHapbeatSubsystem::TickKeepAlive(float /*DeltaSeconds*/)
{
	if (Socket == nullptr)
	{
		// Socket gone (shouldn't happen while the ticker lives) — stop ticking.
		return false;
	}

	// The ticker runs at DiscoveryTickSeconds; only send when the adaptive
	// interval has elapsed (fast while nothing has answered, PingInterval once a
	// device is known). Evaluated BEFORE the send so a freshly-discovered device
	// immediately drops us back to the slow cadence.
	const double Now = FPlatformTime::Seconds();
	if (Now - LastKeepAliveSendTime >= KeepAliveIntervalSeconds())
	{
		LastKeepAliveSendTime = Now;
		Ping();
		// Periodic presence beacon so the device shows this app on its OLED. Per the
		// v1 design the device_name field is left empty (the OLED shows app_name).
		SendDiscoveryPacket(FHapbeatProtocol::BuildConnectStatus(NextSeq(), true, ConnectStatusGroupByte(), AppNameForWire(), FString()));
	}

	// Every tick (not just on send): a device may have aged out since its last
	// PONG, and at DiscoveryTickSeconds this also makes OnDisconnected prompt.
	EvaluateLivenessTransition();

	return true; // keep ticking
}

void UHapbeatSubsystem::HandleReceivedData(const FArrayReaderPtr& Reader, const FIPv4Endpoint& Sender)
{
	// ---- Receiver worker thread ----
	if (!Reader.IsValid())
	{
		return;
	}
	const uint8* Buf = Reader->GetData();
	const int32 Len = Reader->Num();

	FHapbeatProtocol::FParsedPacket Pkt;
	if (!FHapbeatProtocol::ParsePacket(Buf, Len, Pkt))
	{
		return;
	}

	const FString SenderIp = Sender.Address.ToString(); // "a.b.c.d" (no port)
	const int32 SenderPort = Sender.Port;

	if (Pkt.Cmd == FHapbeatProtocol::CmdPong)
	{
		FHapbeatProtocol::FPongInfo Pong;
		if (!FHapbeatProtocol::ParsePong(Pkt.Payload, Pkt.PayloadAvail, Pong))
		{
			return;
		}

		// RTT: prefer now - sentTime[seq]; fall back to now - echoed timestamp.
		// PendingPings is game-thread-owned, so we compute the fallback here and
		// resolve the precise value on the game thread (where the map lives).
		const uint16 PongSeq = Pkt.Seq;
		const int64 NowMono = NowMicros();
		const int64 EchoedTs = Pong.Timestamp;
		const FString DeviceName = Pong.DeviceName;
		const FString Address = Pong.Address;
		const FString Firmware = Pong.Firmware;

		// Weak self-guard: a queued game-thread task may run after the subsystem
		// is torn down. Bail if 'this' is gone (use-after-free guard).
		TWeakObjectPtr<UHapbeatSubsystem> WeakThis(this);
		AsyncTask(ENamedThreads::GameThread,
			[WeakThis, SenderIp, SenderPort, PongSeq, NowMono, EchoedTs, DeviceName, Address, Firmware]()
			{
				UHapbeatSubsystem* Self = WeakThis.Get();
				if (Self == nullptr)
				{
					return;
				}
				const int64 RttUs = ResolvePongRttUs(
					Self->PendingPings, PongSeq, NowMono, EchoedTs, Self->UnixMicros());

				Self->DevicePongTimes.Add(SenderIp, FPlatformTime::Seconds());

				// A reply proves which subnet the device is really on, so pin
				// broadcasts there. Until this happens a broadcast still goes out
				// limited, which on a multi-homed host may be leaving through an
				// interface with no Hapbeat behind it (see HapbeatNetInterfaces.h).
				Self->LockRouteFor(SenderIp);
				if (!Address.IsEmpty())
				{
					// Only recorded when the PONG actually carried the address
					// extension. A device with NO entry stays "unknown" and is
					// kept by both unicast filters (fail-open) — older firmware
					// must not silently lose its haptics.
					Self->DeviceAddresses.Add(SenderIp, Address);
					Self->RegisterStreamEndpoint(
						SenderIp, SenderPort, Address, FPlatformTime::Seconds());
				}
				Self->ReconcileStreamSources();

				Self->OnPongGameThread(SenderIp, RttUs, DeviceName, Address, Firmware);
				Self->EvaluateLivenessTransition();
			});
	}
	else if (Pkt.Cmd == FHapbeatProtocol::CmdError)
	{
		uint16 ErrorCode = 0;
		FString Message;
		if (!FHapbeatProtocol::ParseError(Pkt.Payload, Pkt.PayloadAvail, ErrorCode, Message))
		{
			return;
		}
		const FString Composed = FString::Printf(TEXT("Error (code=%u): %s"), ErrorCode, *Message);
		TWeakObjectPtr<UHapbeatSubsystem> WeakThis(this);
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Composed]()
		{
			if (UHapbeatSubsystem* Self = WeakThis.Get())
			{
				Self->OnErrorGameThread(Composed);
			}
		});
	}
	// Other command ids (PLAY/STOP/...) are SDK->device only; ignore inbound.
}

void UHapbeatSubsystem::OnPongGameThread(const FString& Endpoint, int64 RttUs, const FString& DeviceName, const FString& Address, const FString& Firmware)
{
	OnPong.Broadcast(Endpoint, RttUs, DeviceName, Address, Firmware);
}

void UHapbeatSubsystem::OnErrorGameThread(const FString& Message)
{
	UE_LOG(LogHapbeat, Warning, TEXT("%s"), *Message);
	OnError.Broadcast(Message);
}

void UHapbeatSubsystem::EvaluateLivenessTransition()
{
	const int32 Cur = GetAliveDeviceCount();
	if (Cur == PrevAliveCount)
	{
		return;
	}

	// Edge-detect a 0 <-> positive change only (liveness, never socket-open).
	if (PrevAliveCount <= 0 && Cur > 0)
	{
		OnConnected.Broadcast();
	}
	else if (PrevAliveCount > 0 && Cur == 0)
	{
		OnDisconnected.Broadcast();
	}
	PrevAliveCount = Cur;
}

int32 UHapbeatSubsystem::GetAliveDeviceCount() const
{
	if (Socket == nullptr)
	{
		return 0;
	}
	const double Now = FPlatformTime::Seconds();
	const double Timeout = AliveTimeoutSeconds();
	int32 Count = 0;
	for (const TPair<FString, double>& Pair : DevicePongTimes)
	{
		if (Now - Pair.Value <= Timeout)
		{
			++Count;
		}
	}
	return Count;
}

int64 UHapbeatSubsystem::NowMicros() const
{
	// Monotonic high-resolution clock for RTT. FPlatformTime::Seconds() is a
	// steady up-counter (not wall time), which is exactly what RTT wants.
	return static_cast<int64>(FPlatformTime::Seconds() * 1'000'000.0);
}

int64 UHapbeatSubsystem::UnixMicros() const
{
	// ToUnixTimestamp() is whole seconds, so derive µs from ticks (100 ns each)
	// to keep true microsecond resolution (parity with the other SDKs).
	static const int64 UnixEpochTicks = FDateTime(1970, 1, 1).GetTicks();
	return (FDateTime::UtcNow().GetTicks() - UnixEpochTicks) / 10;
}

uint16 UHapbeatSubsystem::NextSeq()
{
	// Locked: since the 2026-07-25 thread migration this is called from both
	// the game thread (Play/Stop/StopAll/Ping/CONNECT_STATUS)
	// and the dedicated stream thread (STREAM_BEGIN/DATA/END) — the SAME
	// counter, matching Unity's single locked _sequenceNumber (HapbeatClient.cs
	// _seqLock) shared across its main + background mixer threads. Contention
	// is negligible (at most ~100 stream sends/sec vs. rare game-thread sends).
	FScopeLock Lock(&SeqLock);
	Seq = static_cast<uint16>((Seq + 1) & 0xFFFF);
	return Seq;
}

FString UHapbeatSubsystem::AppNameForWire() const
{
	return UHapbeatTargetLibrary::ApplyAddressPlaceholders(AppName, OverridePlayer, OverrideGroup);
}

void UHapbeatSubsystem::SendCommandPacket(const TArray<uint8>& Packet, const FString& ResolvedTarget)
{
	if (HapbeatIsNetworkSuppressedForEditor())
	{
		return;
	}

	// (a) Feature off -> plain broadcast, exactly as before this existed.
	if (!bCommandUnicast)
	{
		SendPacket(Packet);
		return;
	}
	if (Socket == nullptr || bShuttingDown)
	{
		// Let SendPacket own the lazy-connect / teardown guards (single place).
		SendPacket(Packet);
		return;
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSubsystem == nullptr)
	{
		SendPacket(Packet);
		return;
	}

	const double Now = FPlatformTime::Seconds();
	const double Timeout = AliveTimeoutSeconds();
	bool bSentAny = false;

	for (const TPair<FString, double>& Pair : DevicePongTimes)
	{
		// Device stopped answering PINGs (powered off, left the network, rebooting
		// after an OTA). Skip it so we stop aiming datagrams at a dead host — each
		// one draws an ICMP port-unreachable that Windows reports back on this
		// socket (see SuppressUdpConnReset) — and so the live set can empty out
		// and let the broadcast fallback below take over instead of unicasting
		// into the void.
		if (Now - Pair.Value > Timeout)
		{
			continue;
		}

		// (c) Fail open: address unknown => send anyway. Known => must match.
		if (const FString* KnownAddress = DeviceAddresses.Find(Pair.Key))
		{
			if (!UHapbeatTargetLibrary::AddressMatches(ResolvedTarget, *KnownAddress))
			{
				continue;
			}
		}

		TSharedPtr<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
		bool bIsValid = false;
		Addr->SetIp(*Pair.Key, bIsValid);
		if (!bIsValid)
		{
			continue;
		}
		Addr->SetPort(Port);

		// (b) Unicast. A single unreachable target must not block the rest.
		bSentAny = true;
		int32 BytesSent = 0;
		if (!Socket->SendTo(Packet.GetData(), Packet.Num(), BytesSent, *Addr))
		{
			UE_LOG(LogHapbeat, Verbose, TEXT("Command unicast send to %s failed; continuing with the other devices."),
				*Pair.Key);
		}
	}

	// (d) Nothing went out (no live device, or every known address mismatched)
	// -> BROADCAST. Never in addition to a unicast: the same PLAY would fire
	// twice. See the header for why this is a fallback and not a skip.
	if (!bSentAny)
	{
		SendPacket(Packet);
	}
}

const TSharedPtr<FInternetAddr>& UHapbeatSubsystem::CurrentBroadcastAddr() const
{
	// Once a device has answered we know which subnet it is on, so send there
	// instead of relying on the limited broadcast reaching it. Before that this
	// is the unchanged 255.255.255.255.
	if (BroadcastRoutes.IsValidIndex(LockedRouteIndex))
	{
		return BroadcastRoutes[LockedRouteIndex].EndPoint;
	}
	return BroadcastAddr;
}

void UHapbeatSubsystem::LockRouteFor(const FString& DeviceIp)
{
	// First reply wins, and the lock is dropped with the connection. With
	// devices on two subnets at once this settles on whichever answered first;
	// broadcasts do not cross subnets anyway, so the alternative is not
	// reaching both, it is reaching neither reliably.
	if (LockedRouteIndex != INDEX_NONE || BroadcastRoutes.Num() == 0)
	{
		return;
	}
	uint32 Ip = 0;
	if (!HapbeatParseIPv4(DeviceIp, Ip))
	{
		return;
	}
	for (int32 Index = 0; Index < BroadcastRoutes.Num(); ++Index)
	{
		if (!BroadcastRoutes[Index].Contains(Ip))
		{
			continue;
		}
		LockedRouteIndex = Index;
		UE_LOG(LogHapbeat, Log, TEXT("Broadcasting to %s (a device answered from %s)."),
			*BroadcastRoutes[Index].EndPoint->ToString(false), *DeviceIp);
		return;
	}
}

void UHapbeatSubsystem::SendDiscoveryPacket(const TArray<uint8>& Packet)
{
	if (HapbeatIsNetworkSuppressedForEditor())
	{
		return;
	}

	if (Socket == nullptr)
	{
		if (bShuttingDown)
		{
			return;
		}
		Connect(Port, AppName);
	}
	// Already pinned to a subnet, or nothing enumerated to fan out over: one
	// destination, same as any other packet.
	if (Socket == nullptr || BroadcastRoutes.Num() == 0 || LockedRouteIndex != INDEX_NONE)
	{
		SendPacket(Packet);
		return;
	}

	for (const FHapbeatBroadcastRoute& Route : BroadcastRoutes)
	{
		if (!Route.EndPoint.IsValid())
		{
			continue;
		}
		int32 BytesSent = 0;
		// Failures are routine and deliberately quiet here: most hosts carry an
		// adapter that can never take a broadcast (Bluetooth PAN, Wi-Fi Direct,
		// an idle virtual switch). Trying anyway and letting the others through
		// is exactly what this fan-out is for. Only the single-destination path
		// below reports outages, because that one carries playback.
		Socket->SendTo(Packet.GetData(), Packet.Num(), BytesSent, *Route.EndPoint);
	}
}

void UHapbeatSubsystem::SendPacket(const TArray<uint8>& Packet)
{
	if (HapbeatIsNetworkSuppressedForEditor())
	{
		return;
	}

	if (Socket == nullptr)
	{
		if (bShuttingDown)
		{
			return; // never resurrect the socket/receiver/ticker during teardown
		}
		// Lazily connect with defaults if the caller forgot to.
		Connect(Port, AppName);
	}
	const TSharedPtr<FInternetAddr>& Destination = CurrentBroadcastAddr();
	if (Socket == nullptr || !Destination.IsValid())
	{
		return;
	}
	int32 BytesSent = 0;
	if (Socket->SendTo(Packet.GetData(), Packet.Num(), BytesSent, *Destination))
	{
		if (bLoggedSendError)
		{
			// Without this line an unattended installation's log shows when
			// haptics broke but never whether they came back.
			bLoggedSendError = false;
			UE_LOG(LogHapbeat, Log, TEXT("Sending recovered."));
		}
		return;
	}

	// A failed UDP send says nothing about whether the socket is still usable:
	// the datagram is lost, the socket is not. Nothing is torn down here --
	// doing so is what turns one Wi-Fi re-association into a session that never
	// recovers. Reported once per outage because a clip stream pushes roughly
	// 100 packets a second through here, and an unguarded warning would bury
	// the very log an operator needs to read.
	if (!bLoggedSendError)
	{
		bLoggedSendError = true;
		UE_LOG(LogHapbeat, Warning,
			TEXT("Send to %s failed. Keeping the socket open; further send errors are silenced until sending recovers."),
			*Destination->ToString(true));
	}
}
