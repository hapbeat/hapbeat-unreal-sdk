// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatSubsystem.h"

#include "HapbeatProtocol.h"
#include "HapbeatConfig.h"
#include "HapbeatClip.h"
#include "HapbeatNetInterfaces.h"
#include "HapbeatStreamPlayback.h"
#include "HapbeatStreamRunnable.h"
#include "HapbeatTargetLibrary.h"
#include "Async/Async.h"
#include "Common/UdpSocketBuilder.h"
#include "Common/UdpSocketReceiver.h"
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

// Defined in the .cpp (not the header) where FHapbeatStreamRunnable is a
// complete type. The dtor teardown is a leak guard only — the normal teardown
// path is Deinitialize() -> StopStream(), which already joins + deletes and
// nulls both StreamThread and StreamRunnable.
UHapbeatSubsystem::UHapbeatSubsystem() = default;
UHapbeatSubsystem::~UHapbeatSubsystem()
{
	if (StreamThread != nullptr)
	{
		StreamThread->Kill(true); // calls StreamRunnable->Stop() then joins; null-safe if already exited
		delete StreamThread;
	}
	delete StreamRunnable; // null-safe; normally already nullptr via Deinitialize
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
		bStreamUnicast = Cfg->bStreamUnicast;
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

	// Auto-connect at startup, matching the Unity SDK's Awake() auto-connect.
	// GameInstanceSubsystems exist only in PIE / packaged game (not the editor
	// itself), so this never opens a socket in edit mode.
	Connect(Port, AppName);
}

void UHapbeatSubsystem::Deinitialize()
{
	// Block any late SendPacket from lazily re-opening the socket during teardown.
	bShuttingDown = true;

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

void UHapbeatSubsystem::Play(const FString& EventId, float Gain, const FString& Target)
{
	const FString ResolvedTarget = UHapbeatTargetLibrary::ResolveTarget(Target, OverridePlayer, OverrideGroup);
	SendCommandPacket(FHapbeatProtocol::BuildPlay(NextSeq(), EventId, ResolvedTarget, 0, FMath::Clamp(Gain, 0.0f, 1.0f)), ResolvedTarget);
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

UHapbeatStreamPlayback* UHapbeatSubsystem::StreamClip(UHapbeatClip* Clip, float BaselineGain, float InitialGain,
	const FString& Target, bool bLoop)
{
	if (Clip == nullptr || Clip->Pcm16.Num() == 0)
	{
		UE_LOG(LogHapbeat, Warning, TEXT("StreamClip: clip is null or has no PCM data; ignoring."));
		return nullptr;
	}

	// Single active session, REPLACE semantics: end any current stream first
	// (joins the old thread before starting a new one).
	if (StreamRunnable != nullptr)
	{
		StopStream();
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
			return nullptr;
		}
		Connect(Port, AppName);
		if (Socket == nullptr)
		{
			UE_LOG(LogHapbeat, Warning, TEXT("StreamClip: no socket available; ignoring."));
			return nullptr;
		}
	}

	// GC-rooted via the UPROPERTY for the stream's lifetime (the caller may not retain it).
	UHapbeatStreamPlayback* Playback = NewObject<UHapbeatStreamPlayback>(this);
	Playback->Init(BaselineGain, InitialGain);
	ActivePlayback = Playback;

	// Resolve the global address override (if any) BEFORE the streamer captures
	// Target — it stores this string by value and reuses it, unmodified, for
	// every STREAM_BEGIN it sends (including the loop-wrap path, which reuses
	// the session rather than re-resolving). Triggers/EventMap entries stay
	// untouched: only the wire-bound copy is rewritten.
	const FString ResolvedTarget = UHapbeatTargetLibrary::ResolveTarget(Target, OverridePlayer, OverrideGroup);

	// Snapshot the unicast target list for THIS session before the thread
	// starts (Unity db6fd31 seeds SetStreamUnicastTargets at the same point).
	// StreamUnicastTargets (FInternetAddr, used elsewhere by StopStreamWithFlush's
	// game-thread flush pair) is still built here; extract plain IP strings from
	// it for the runnable — never hand a TSharedPtr<FInternetAddr> to another
	// thread (see FHapbeatStreamRunnable's threading contract).
	RefreshStreamUnicastTargets(ResolvedTarget);
	TArray<FString> UnicastIps;
	UnicastIps.Reserve(StreamUnicastTargets.Num());
	for (const TSharedPtr<FInternetAddr>& Addr : StreamUnicastTargets)
	{
		if (Addr.IsValid())
		{
			UnicastIps.Add(Addr->ToString(/*bAppendPort=*/false));
		}
	}

	// The runnable owns a COPY of the clip bytes (TArray copy ctor -> moved in)
	// so the source UHapbeatClip is never mutated by the premultiply.
	StreamRunnable = new FHapbeatStreamRunnable(
		TArray<uint8>(Clip->Pcm16),
		Clip->SampleRate,
		Clip->NumChannels,
		ResolvedTarget,
		bLoop,
		Playback->GetMirror(),
		[this]() { return NextSeq(); },
		Socket,
		Port,
		MoveTemp(UnicastIps),
		bStreamTargetsSnapshotted,
		StreamSendAheadSeconds,
		// The subnet a device answered on, so a broadcast-mode stream
		// (bStreamUnicast=false) still reaches it on a multi-homed host.
		CurrentBroadcastAddr().IsValid() ? CurrentBroadcastAddr()->ToString(false) : FString());

	// AboveNormal: a short, latency-sensitive pacing loop — not TimeCritical
	// (which risks starving the game/render/audio threads it shares a core
	// budget with), just enough priority to avoid being starved itself.
	StreamThread = FRunnableThread::Create(StreamRunnable, TEXT("HapbeatStreamThread"), 0, TPri_AboveNormal);
	if (StreamThread == nullptr)
	{
		UE_LOG(LogHapbeat, Warning, TEXT("StreamClip: failed to create the stream thread; aborting."));
		delete StreamRunnable;
		StreamRunnable = nullptr;
		ActivePlayback = nullptr;
		return nullptr;
	}

	// Watchdog: poll for the thread finishing on its own (natural EOF on a
	// non-loop clip, or the handle's own Stop()) and clean up when it does.
	if (!StreamTickHandle.IsValid())
	{
		StreamTickHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UHapbeatSubsystem::TickStream), 0.0f);
	}

	UE_LOG(LogHapbeat, Log, TEXT("Stream begin: %dHz %dch, baseline=%.3f initial=%.3f, target=%s, loop=%d"),
		Clip->SampleRate, Clip->NumChannels, BaselineGain, InitialGain,
		Target.IsEmpty() ? TEXT("broadcast") : *Target, bLoop ? 1 : 0);

	return ActivePlayback;
}

bool UHapbeatSubsystem::TickStream(float /*DeltaSeconds*/)
{
	if (StreamRunnable == nullptr)
	{
		// Nothing to drive — auto-unregister this ticker.
		StreamTickHandle.Reset();
		return false;
	}

	if (StreamRunnable->IsFinished())
	{
		// The thread ended on its own (clip end on a non-loop, or the handle's
		// own Stop() was honored) — STREAM_END has already been sent by the
		// time IsFinished() reports true (see FHapbeatStreamRunnable::Run()'s
		// class-doc guarantee). Reuse StopStream() for the join (instant —
		// the thread already exited) + cleanup, same code path as an explicit
		// user-initiated stop.
		//
		// Clear the handle FIRST so StopStream()'s RemoveTicker branch is
		// skipped: we are inside this very ticker's callback, and returning
		// false below is the reentrancy-safe way to unregister (never
		// RemoveTicker on the currently-firing handle).
		StreamTickHandle.Reset();
		StopStream();
		return false;
	}

	return true; // keep polling
}

void UHapbeatSubsystem::StopStream()
{
	if (StreamThread != nullptr)
	{
		// Kill(true) calls StreamRunnable->Stop() (flags the atomic) then BLOCKS
		// until Run() returns — by then STREAM_END has already gone out. Brief:
		// the thread notices within one ~10ms pacing tick at most; instant if it
		// already finished on its own (TickStream's watchdog path).
		StreamThread->Kill(true);
		delete StreamThread;
		StreamThread = nullptr;
	}
	if (StreamRunnable != nullptr)
	{
		delete StreamRunnable;
		StreamRunnable = nullptr;
	}

	if (ActivePlayback != nullptr)
	{
		ActivePlayback->Stop();
		ActivePlayback = nullptr;
	}

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

void UHapbeatSubsystem::StopStreamWithFlush(const FString& Target)
{
	StopStream();

	const FString ResolvedTarget = UHapbeatTargetLibrary::ResolveTarget(Target, OverridePlayer, OverrideGroup);

	// Force the device ring buffer to flush: a STREAM_BEGIN + STREAM_END pair
	// trips the firmware's BEGIN_FLUSH_THRESHOLD path (ringReset when residual
	// > 32 ms), silencing within a few ms. gain = 1.0 (any format works); parity
	// with Unity StopStreamWithFlush (SendStreamBegin(16000,1,PCM16,0,1.0,target)
	// + SendStreamEnd()). SendPacket no-ops if the socket is gone.
	SendStreamPacket(FHapbeatProtocol::BuildStreamBegin(
		NextSeq(), 16000, 1, FHapbeatProtocol::AudioFormatPcm16, 0, 1.0f, ResolvedTarget));
	SendStreamPacket(FHapbeatProtocol::BuildStreamEnd(NextSeq()));
}

void UHapbeatSubsystem::SetAddressOverride(int32 Player, int32 InGroup, bool bPersist)
{
	OverridePlayer = NormalizeAddressOverride(Player);
	OverrideGroup = NormalizeAddressOverride(InGroup);

	if (bPersist)
	{
		GConfig->SetInt(AddressOverrideConfigSection, AddressOverridePlayerKey, OverridePlayer, GGameUserSettingsIni);
		GConfig->SetInt(AddressOverrideConfigSection, AddressOverrideGroupKey, OverrideGroup, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
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
	GConfig->RemoveKey(AddressOverrideConfigSection, AddressOverridePlayerKey, GGameUserSettingsIni);
	GConfig->RemoveKey(AddressOverrideConfigSection, AddressOverrideGroupKey, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);

	// Reuses SetAddressOverride (bPersist: false, so the just-cleared keys
	// aren't immediately re-saved) to push the reverted values to the runtime
	// state — same path a normal override change takes. Mirrors
	// HapbeatManager.ClearPersistedAddressOverride (Unity SDK).
	SetAddressOverride(AddressOverrideDisabled, AddressOverrideDisabled, /*bPersist=*/false);

	UE_LOG(LogHapbeat, Log, TEXT("Persisted address override cleared - reverted to disabled."));
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
			[WeakThis, SenderIp, PongSeq, NowMono, EchoedTs, DeviceName, Address, Firmware]()
			{
				UHapbeatSubsystem* Self = WeakThis.Get();
				if (Self == nullptr)
				{
					return;
				}
				int64 SentMono = 0;
				int64 RttUs;
				if (Self->PendingPings.RemoveAndCopyValue(PongSeq, SentMono))
				{
					RttUs = NowMono - SentMono;
				}
				else
				{
					// Echoed timestamp is Unix-epoch µs; compare against Unix now.
					RttUs = Self->UnixMicros() - EchoedTs;
				}
				if (RttUs < 0)
				{
					RttUs = 0;
				}

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
				}

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
	// the game thread (Play/Stop/StopAll/Ping/CONNECT_STATUS/StopStreamWithFlush)
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

void UHapbeatSubsystem::RefreshStreamUnicastTargets(const FString& ResolvedTarget)
{
	StreamUnicastTargets.Reset();
	bStreamTargetsSnapshotted = false;
	if (!bStreamUnicast)
	{
		return; // feature off -> no snapshot -> SendStreamPacket broadcasts
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSubsystem == nullptr)
	{
		return;
	}

	// Same liveness window as GetAliveDeviceCount(). Snapshotting ONCE per stream
	// session (instead of re-reading per packet) matches Unity db6fd31: a device
	// whose first PONG lands mid-session joins the NEXT session.
	const double Now = FPlatformTime::Seconds();
	const double Timeout = AliveTimeoutSeconds();
	int32 LiveCount = 0;
	int32 SkippedByAddress = 0;
	for (const TPair<FString, double>& Pair : DevicePongTimes)
	{
		if (Now - Pair.Value > Timeout)
		{
			continue;
		}
		++LiveCount;

		// Send-side target filter (Unity 029efc1): don't fan every chunk out to
		// devices this stream isn't addressed to (one person wearing several
		// units, or several pairs sharing a LAN). Fail open on an unknown
		// address — firmware re-applies its own filter on receipt, so the worst
		// case is one extra unicast, never a silently lost stream.
		if (const FString* KnownAddress = DeviceAddresses.Find(Pair.Key))
		{
			if (!UHapbeatTargetLibrary::AddressMatches(ResolvedTarget, *KnownAddress))
			{
				++SkippedByAddress;
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
		StreamUnicastTargets.Add(Addr);
	}

	// A snapshot counts as "taken" only when at least one device was actually
	// live. With nobody alive we leave it un-snapshotted so the broadcast
	// fallback stays available (a device that PONGs later still gets audio);
	// with live devices that ALL mismatched, the snapshot IS taken and stays
	// empty => send nowhere. See the header for the three-state contract.
	bStreamTargetsSnapshotted = LiveCount > 0;

	if (StreamUnicastTargets.Num() > 0)
	{
		UE_LOG(LogHapbeat, Log, TEXT("Stream unicast: targeting %d of %d live device(s)."),
			StreamUnicastTargets.Num(), LiveCount);
	}
	else if (SkippedByAddress > 0)
	{
		UE_LOG(LogHapbeat, Log,
			TEXT("Stream unicast: all %d live device(s) filtered out by target '%s'; this session sends nowhere."),
			SkippedByAddress, *ResolvedTarget);
	}
}

void UHapbeatSubsystem::SendStreamPacket(const TArray<uint8>& Packet)
{
	// Three-state (see the header): no snapshot -> broadcast; snapshot with no
	// surviving target -> send NOWHERE (the filter said this stream isn't for
	// anyone here); snapshot with targets -> unicast below.
	if (!bStreamTargetsSnapshotted)
	{
		SendPacket(Packet);
		return;
	}
	if (StreamUnicastTargets.Num() == 0)
	{
		return;
	}
	if (Socket == nullptr || bShuttingDown)
	{
		return;
	}

	// ONE seq per logical packet (the caller already stamped it) — the same bytes
	// go to every target, matching Unity SendStreamRaw. Per-target failures are
	// logged and skipped so one unreachable device can't kill the session.
	for (const TSharedPtr<FInternetAddr>& Addr : StreamUnicastTargets)
	{
		if (!Addr.IsValid())
		{
			continue;
		}
		int32 BytesSent = 0;
		if (!Socket->SendTo(Packet.GetData(), Packet.Num(), BytesSent, *Addr))
		{
			UE_LOG(LogHapbeat, Verbose, TEXT("Stream unicast send to %s failed; continuing with the other targets."),
				*Addr->ToString(true));
		}
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
