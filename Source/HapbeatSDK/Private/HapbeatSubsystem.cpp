// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatSubsystem.h"

#include "HapbeatProtocol.h"
#include "HapbeatConfig.h"
#include "HapbeatClip.h"
#include "HapbeatStreamPlayback.h"
#include "HapbeatStreamer.h"
#include "HapbeatTargetLibrary.h"
#include "Async/Async.h"
#include "Common/UdpSocketBuilder.h"
#include "Common/UdpSocketReceiver.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h" // GConfig / GGameUserSettingsIni — address-override persistence
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

// Defined in the .cpp (not the header) where FHapbeatStreamer is a complete
// type. The dtor delete is a leak guard only — the normal teardown path is
// Deinitialize() -> StopStream(), which already deletes and nulls Streamer.
UHapbeatSubsystem::UHapbeatSubsystem() = default;
UHapbeatSubsystem::~UHapbeatSubsystem()
{
	delete Streamer; // null-safe; normally already nullptr via Deinitialize
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
		// NOTE: UHapbeatConfig::Group is deliberately NOT consumed here. The
		// CONNECT_STATUS group byte (OLED display) tracks the address override
		// exclusively — see ConnectStatusGroupByte(), verbatim Unity parity.

		// AppName shows on the device OLED. Empty => fall back to the project name
		// (parity with the Unity SDK's Application.productName fallback), still
		// capped to the OLED grid width.
		FString ResolvedAppName = Cfg->AppName;
		if (ResolvedAppName.IsEmpty())
		{
			ResolvedAppName = FApp::GetProjectName();
		}
		AppName = ResolvedAppName.Left(FHapbeatProtocol::MaxAppNameLen);
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
			SendPacket(FHapbeatProtocol::BuildConnectStatus(NextSeq(), false, ConnectStatusGroupByte(), AppName, FString()));
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
	AppName = InAppName.Left(FHapbeatProtocol::MaxAppNameLen);

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

	// Start receiving PONG/ERROR on a worker thread. 100 ms poll matches the
	// Unity client's Poll() cadence; results are marshalled to the game thread.
	if (Receiver == nullptr)
	{
		Receiver = new FUdpSocketReceiver(Socket, FTimespan::FromMilliseconds(100.0), TEXT("HapbeatReceiver"));
		Receiver->OnDataReceived().BindUObject(this, &UHapbeatSubsystem::HandleReceivedData);
		Receiver->Start();
	}

	// Start the keep-alive ticker (PING + CONNECT_STATUS, then liveness diff).
	if (!KeepAliveHandle.IsValid() && PingInterval > 0.0f)
	{
		KeepAliveHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UHapbeatSubsystem::TickKeepAlive), PingInterval);
	}

	if (!AppName.IsEmpty())
	{
		SendPacket(FHapbeatProtocol::BuildConnectStatus(NextSeq(), true, ConnectStatusGroupByte(), AppName, FString()));
	}

	// NOTE: deliberately do NOT raise OnConnected here. socket-open != device
	// presence. OnConnected fires only once a device PONGs (liveness 0->positive),
	// which fixes the Unity double-fire (open + first-PONG both firing OnConnected).
}

void UHapbeatSubsystem::Play(const FString& EventId, float Gain, const FString& Target)
{
	const FString ResolvedTarget = UHapbeatTargetLibrary::ResolveTarget(Target, OverridePlayer, OverrideGroup);
	SendPacket(FHapbeatProtocol::BuildPlay(NextSeq(), EventId, ResolvedTarget, 0, FMath::Clamp(Gain, 0.0f, 1.0f)));
}

void UHapbeatSubsystem::Stop(const FString& EventId, const FString& Target)
{
	const FString ResolvedTarget = UHapbeatTargetLibrary::ResolveTarget(Target, OverridePlayer, OverrideGroup);
	SendPacket(FHapbeatProtocol::BuildStop(NextSeq(), EventId, ResolvedTarget));
}

void UHapbeatSubsystem::StopAll(const FString& Target)
{
	const FString ResolvedTarget = UHapbeatTargetLibrary::ResolveTarget(Target, OverridePlayer, OverrideGroup);
	SendPacket(FHapbeatProtocol::BuildStopAll(NextSeq(), ResolvedTarget));
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
	SendPacket(FHapbeatProtocol::BuildPing(PingSeq, TimestampUs));
}

UHapbeatStreamPlayback* UHapbeatSubsystem::StreamClip(UHapbeatClip* Clip, float BaselineGain, float InitialGain,
	const FString& Target, bool bLoop)
{
	if (Clip == nullptr || Clip->Pcm16.Num() == 0)
	{
		UE_LOG(LogHapbeat, Warning, TEXT("StreamClip: clip is null or has no PCM data; ignoring."));
		return nullptr;
	}

	// Single active session, REPLACE semantics: end any current stream first.
	if (Streamer != nullptr)
	{
		StopStream();
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

	// The streamer owns a COPY of the clip bytes (TArray copy ctor -> moved into the
	// streamer) so the source UHapbeatClip is never mutated by the premultiply.
	Streamer = new FHapbeatStreamer(
		TArray<uint8>(Clip->Pcm16),
		Clip->SampleRate,
		Clip->NumChannels,
		ResolvedTarget,
		bLoop,
		TWeakObjectPtr<UHapbeatStreamPlayback>(Playback),
		[this]() { return NextSeq(); },
		[this](const TArray<uint8>& Packet) { SendPacket(Packet); },
		StreamSendAheadSeconds);

	// Sends STREAM_BEGIN (gain = 1.0) and records the wall-clock start.
	Streamer->Start(FPlatformTime::Seconds());

	// Drive sending every frame (0.0 delay => every tick) until the stream ends.
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
	if (Streamer == nullptr)
	{
		// Nothing to drive — auto-unregister this ticker.
		StreamTickHandle.Reset();
		return false;
	}

	Streamer->Tick(FPlatformTime::Seconds());

	if (Streamer->IsDone())
	{
		// Stream finished (clip end on a non-loop, or a stop was honored). Tear
		// down and return false so the core ticker removes this delegate for us
		// (do NOT RemoveTicker from inside the callback — returning false is the
		// reentrancy-safe way).
		delete Streamer;
		Streamer = nullptr;
		ActivePlayback = nullptr;
		StreamTickHandle.Reset();
		return false;
	}

	return true; // keep ticking
}

void UHapbeatSubsystem::StopStream()
{
	if (Streamer != nullptr)
	{
		// Send STREAM_END (idempotent) before discarding the streamer.
		Streamer->SendEnd();
		delete Streamer;
		Streamer = nullptr;
	}

	if (ActivePlayback != nullptr)
	{
		ActivePlayback->Stop();
		ActivePlayback = nullptr;
	}

	// Remove the ticker explicitly (StopStream is only ever called OUTSIDE the
	// tick callback — from StreamClip's replace path or Deinitialize — so this is
	// not reentrant; TickStream itself returns false instead of removing).
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
	SendPacket(FHapbeatProtocol::BuildStreamBegin(
		NextSeq(), 16000, 1, FHapbeatProtocol::AudioFormatPcm16, 0, 1.0f, ResolvedTarget));
	SendPacket(FHapbeatProtocol::BuildStreamEnd(NextSeq()));
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
		SendPacket(FHapbeatProtocol::BuildConnectStatus(NextSeq(), true, ConnectStatusGroupByte(), AppName, FString()));
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

	Ping();
	// Periodic presence beacon so the device shows this app on its OLED. Per the
	// v1 design the device_name field is left empty (the OLED shows app_name).
	SendPacket(FHapbeatProtocol::BuildConnectStatus(NextSeq(), true, ConnectStatusGroupByte(), AppName, FString()));

	// A device may have aged out since the last PONG even if none arrived this
	// tick, so re-evaluate liveness here too (drives OnDisconnected on timeout).
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
	Seq = static_cast<uint16>((Seq + 1) & 0xFFFF);
	return Seq;
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
	if (Socket == nullptr || !BroadcastAddr.IsValid())
	{
		return;
	}
	int32 BytesSent = 0;
	Socket->SendTo(Packet.GetData(), Packet.Num(), BytesSent, *BroadcastAddr);
}
