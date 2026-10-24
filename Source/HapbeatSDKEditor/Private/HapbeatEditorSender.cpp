// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatEditorSender.h"

#include "HapbeatClip.h"
#include "HapbeatConfig.h"
#include "HapbeatNetInterfaces.h"
#include "HapbeatProtocol.h"
#include "HapbeatStreamRunnable.h"
#include "HapbeatStreamSessionContract.h"
#include "HapbeatSubsystem.h"
#include "HapbeatTargetLibrary.h"
#include "Common/UdpSocketBuilder.h"
#include "HAL/RunnableThread.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Misc/ScopeLock.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatEditorSender, Log, All);

FSocket* FHapbeatEditorSender::Socket = nullptr;
TSharedPtr<FInternetAddr> FHapbeatEditorSender::BroadcastAddr;
TMap<FString, double> FHapbeatEditorSender::DevicePongTimes;
TArray<FHapbeatBroadcastRoute> FHapbeatEditorSender::BroadcastRoutes;
uint16 FHapbeatEditorSender::Seq = 0;
FCriticalSection FHapbeatEditorSender::SeqLock;
TSharedPtr<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> FHapbeatEditorSender::StreamMirror;
TArray<FHapbeatEditorSender::FEditorStream> FHapbeatEditorSender::Streams;
TMap<FString, FHapbeatEditorSender::FEditorDevice> FHapbeatEditorSender::Devices;
TMap<uint16, FHapbeatPendingPing> FHapbeatEditorSender::PendingPings;
uint64 FHapbeatEditorSender::ClientIncarnation = 0;
int64 FHapbeatEditorSender::LastPingTimestampUs = 0;
TMap<FHapbeatStreamLeaseIdentity, uint32> FHapbeatEditorSender::NextGenerationByLease;
FCriticalSection FHapbeatEditorSender::GenerationLock;
TSet<FString> FHapbeatEditorSender::LoggedErrors;

namespace
{
	/**
	 * How long a device stays a unicast destination after its last PONG.
	 *
	 * Test Play is a human clicking a button, so this is generous compared with
	 * the runtime keep-alive: the point is that a second click a few seconds
	 * after the first still unicasts. A device that has been off longer than
	 * this simply drops back to broadcast.
	 */
	constexpr double EditorDeviceTtlSeconds = 15.0;

	/**
	 * Upper bound on waiting for the lease replies before a stream Test Play.
	 * A device on the same LAN answers within a few ms; this only bounds the
	 * click when one does not.
	 */
	constexpr double LeaseReplyWaitSeconds = 0.3;

	/** Stream Gen 1 firmware needs this long between an END and the next BEGIN. */
	constexpr double Gen1EndToBeginGuardSeconds = 0.3;

	/** Fresh nonzero random client incarnation (stream-session.md "Discovery lease"). */
	uint64 CreateClientIncarnation()
	{
		const FGuid Guid = FGuid::NewGuid();
		const uint64 Incarnation = (static_cast<uint64>(Guid.A) << 32) ^ static_cast<uint64>(Guid.B)
			^ (static_cast<uint64>(Guid.C) << 16) ^ (static_cast<uint64>(Guid.D) << 40);
		return Incarnation == 0 ? 1ULL : Incarnation;
	}

	int64 MonotonicMicros()
	{
		return static_cast<int64>(FPlatformTime::Seconds() * 1000000.0);
	}

	/** Little-endian unsigned field of an ERROR detail. */
	uint64 ReadDetailLe(const TArray<uint8>& Detail, int32 Offset, int32 Width)
	{
		uint64 Value = 0;
		for (int32 Index = 0; Index < Width; ++Index)
		{
			Value |= static_cast<uint64>(Detail[Offset + Index]) << (8 * Index);
		}
		return Value;
	}

	/** Resolve the authored target exactly as the next runtime launch will.
	 * Test Play is an editor transport, but it must not silently bypass the
	 * per-machine routing chosen in Hapbeat Runtime Status. */
	FString ResolveEditorTarget(const FString& AuthoredTarget)
	{
		int32 SavedPlayer = UHapbeatSubsystem::AddressOverrideDisabled;
		int32 SavedGroup = UHapbeatSubsystem::AddressOverrideDisabled;
		UHapbeatSubsystem::TryGetPersistedAddressOverride(SavedPlayer, SavedGroup);

		const UHapbeatConfig* Config = GetDefault<UHapbeatConfig>();
		const int32 ForcedPlayer = Config != nullptr
			? UHapbeatSubsystem::NormalizeAddressOverride(Config->ForcedOverridePlayer)
			: UHapbeatSubsystem::AddressOverrideDisabled;
		const int32 ForcedGroup = Config != nullptr
			? UHapbeatSubsystem::NormalizeAddressOverride(Config->ForcedOverrideGroup)
			: UHapbeatSubsystem::AddressOverrideDisabled;

		return UHapbeatTargetLibrary::ResolveTarget(AuthoredTarget,
			ForcedPlayer >= 1 ? ForcedPlayer : UHapbeatSubsystem::NormalizeAddressOverride(SavedPlayer),
			ForcedGroup >= 1 ? ForcedGroup : UHapbeatSubsystem::NormalizeAddressOverride(SavedGroup));
	}
}

bool FHapbeatEditorSender::EnsureSocket()
{
	if (Socket != nullptr)
	{
		return true;
	}

	// Reusable + broadcast-capable, bound to an OS-assigned ephemeral port (0).
	// The device unicasts its PONG back to the packet's source addr/port, so
	// binding 0 is enough to receive replies here -- the same arrangement the
	// runtime subsystem uses. (This sender used to be send-only, which is why
	// every Test Play went out as a broadcast; see SendRouted.)
	Socket = FUdpSocketBuilder(TEXT("HapbeatEditorSenderUDP"))
				 .AsReusable()
				 .AsNonBlocking()
				 .WithBroadcast()
				 .BoundToAddress(FIPv4Address::Any)
				 .BoundToPort(0)
				 .WithReceiveBufferSize(64 * 1024)
				 .Build();
	if (Socket == nullptr)
	{
		UE_LOG(LogHapbeatEditorSender, Warning, TEXT("[Hapbeat] Editor sender: failed to open a UDP broadcast socket."));
		return false;
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSubsystem == nullptr)
	{
		Shutdown();
		return false;
	}

	BroadcastAddr = SocketSubsystem->CreateInternetAddr();
	bool bIsValid = false;
	BroadcastAddr->SetIp(TEXT("255.255.255.255"), bIsValid);
	const int32 Port = (GetDefault<UHapbeatConfig>() != nullptr) ? GetDefault<UHapbeatConfig>()->Port : 7700;
	BroadcastAddr->SetPort(Port);

	// Device knowledge belongs to a socket: a fresh one may well be on a
	// different network than the last -- and so do the routes.
	DevicePongTimes.Empty();
	Devices.Empty();
	PendingPings.Empty();
	// Leases are issued to a client incarnation; a new socket asks under a new one.
	ClientIncarnation = CreateClientIncarnation();
	BroadcastRoutes = HapbeatEnumerateBroadcastRoutes(Port);

	// Probe immediately so the first Test Play click already has somewhere to
	// aim, instead of broadcasting and only discovering afterwards.
	SendDiscoveryBroadcast(FHapbeatProtocol::BuildPing(NextSeq(), UnixMicros()));

	return true;
}

void FHapbeatEditorSender::DrainReplies()
{
	if (Socket == nullptr)
	{
		return;
	}
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSubsystem == nullptr)
	{
		return;
	}

	const TSharedRef<FInternetAddr> Sender = SocketSubsystem->CreateInternetAddr();
	uint8 Buffer[1024];
	uint32 PendingSize = 0;
	// Bounded: a flood must not stall the editor's game thread on a UI click.
	for (int32 Guard = 0; Guard < 64 && Socket->HasPendingData(PendingSize); ++Guard)
	{
		int32 BytesRead = 0;
		if (!Socket->RecvFrom(Buffer, sizeof(Buffer), BytesRead, *Sender) || BytesRead <= 0)
		{
			break;
		}
		FHapbeatProtocol::FParsedPacket Parsed;
		if (!FHapbeatProtocol::ParsePacket(Buffer, BytesRead, Parsed))
		{
			continue;
		}
		const FString SenderIp = Sender->ToString(/*bAppendPort=*/false);
		const int32 SenderPort = Sender->GetPort();
		const double Now = FPlatformTime::Seconds();

		if (Parsed.Cmd == FHapbeatProtocol::CmdError)
		{
			FHapbeatProtocol::FErrorInfo Error;
			if (FHapbeatProtocol::ParseErrorExtended(Parsed.Payload, Parsed.PayloadAvail, Error))
			{
				HandleError(SenderIp, SenderPort, Error);
			}
			continue;
		}
		if (Parsed.Cmd != FHapbeatProtocol::CmdPong)
		{
			continue;
		}

		FHapbeatProtocol::FPongInfo Pong;
		if (!FHapbeatProtocol::ParsePong(Parsed.Payload, Parsed.PayloadAvail, Pong))
		{
			continue;
		}
		if (Parsed.PayloadLength > Parsed.PayloadAvail)
		{
			// Truncated datagram: its tail is unknowable, so it must not classify.
			Pong.LeaseTail = EHapbeatPongLeaseTail::Ignored;
		}

		// Same rules as the runtime (stream-session.md "Receiver classification"):
		// only a matched reply to this sender's own unicast lease request changes
		// the device's stream class / lease.
		const FEditorDevice* Existing = Devices.Find(SenderIp);
		FHapbeatStreamEndpointLeaseState NextLeaseState;
		const HapbeatStreamSessionContract::EPongLeaseDecision Decision =
			HapbeatStreamSessionContract::EvaluatePongLease(Pong, Parsed.Seq, PendingPings, ClientIncarnation,
				Existing != nullptr ? &Existing->LeaseState : nullptr, NextLeaseState, SenderIp);
		if (Decision == HapbeatStreamSessionContract::EPongLeaseDecision::Discard)
		{
			continue;
		}

		DevicePongTimes.Add(SenderIp, Now);
		if (Pong.Address.IsEmpty())
		{
			// A PONG without an address: a command destination only.
			continue;
		}
		FEditorDevice& Device = Devices.FindOrAdd(SenderIp);
		Device.Ip = SenderIp;
		Device.Port = SenderPort;
		Device.Address = Pong.Address;
		Device.LastPongSeconds = Now;
		if (Decision == HapbeatStreamSessionContract::EPongLeaseDecision::Accept)
		{
			Device.LeaseState = NextLeaseState;
		}
		else if (Decision == HapbeatStreamSessionContract::EPongLeaseDecision::IgnoredMatched)
		{
			const FString WarnKey = FString::Printf(TEXT("%s|tail"), *SenderIp);
			if (!LoggedErrors.Contains(WarnKey))
			{
				LoggedErrors.Add(WarnKey);
				UE_LOG(LogHapbeatEditorSender, Warning,
					TEXT("[Hapbeat] Device %s answered with a stream lease this SDK cannot read; update the Hapbeat Unreal SDK to stream to it."),
					*SenderIp);
			}
		}
	}
}

void FHapbeatEditorSender::HandleError(const FString& SenderIp, int32 SenderPort, const FHapbeatProtocol::FErrorInfo& Error)
{
	// Only from a device this sender talks to, on the device port.
	const int32 DevicePort = BroadcastAddr.IsValid() ? BroadcastAddr->GetPort() : 7700;
	if (SenderPort != DevicePort || (!DevicePongTimes.Contains(SenderIp) && !Devices.Contains(SenderIp)))
	{
		return;
	}
	const uint16 Code = Error.ErrorCode;
	if (Code >= FHapbeatProtocol::ErrorStreamLeaseInvalid && Code <= FHapbeatProtocol::ErrorStreamTargetMismatch)
	{
		// detail = the rejected packet's envelope; it must name a session this
		// editor runs (or just moved on from) on that device.
		if (Error.Detail.Num() != FHapbeatProtocol::StreamSessionEnvelopeSize)
		{
			return;
		}
		const FHapbeatStreamSessionIdentity Envelope(
			FHapbeatStreamLeaseIdentity(ReadDetailLe(Error.Detail, 0, 8), static_cast<uint32>(ReadDetailLe(Error.Detail, 8, 4))),
			static_cast<uint32>(ReadDetailLe(Error.Detail, 12, 4)));
		bool bOwnSession = false;
		for (const FEditorStream& Stream : Streams)
		{
			if (Stream.Ip == SenderIp && Stream.Runnable != nullptr
				&& (Stream.Runnable->GetCurrentIdentity() == Envelope || Stream.Runnable->GetPreviousIdentity() == Envelope))
			{
				bOwnSession = true;
				break;
			}
		}
		if (!bOwnSession)
		{
			return;
		}
	}

	// Test Play is short and the user is watching: one line per device and code,
	// no recovery (there is no Playback to re-join here).
	const FString LogKey = FString::Printf(TEXT("%s|%u"), *SenderIp, Code);
	if (!LoggedErrors.Contains(LogKey))
	{
		LoggedErrors.Add(LogKey);
		UE_LOG(LogHapbeatEditorSender, Warning, TEXT("[Hapbeat] Device %s reported error 0x%04x: %s"),
			*SenderIp, Code, *Error.Message);
	}
}

void FHapbeatEditorSender::SendSingleBroadcast(const TArray<uint8>& Packet)
{
	if (!EnsureSocket() || !BroadcastAddr.IsValid())
	{
		return;
	}
	int32 BytesSent = 0;
	Socket->SendTo(Packet.GetData(), Packet.Num(), BytesSent, *BroadcastAddr);
}

void FHapbeatEditorSender::SendDiscoveryBroadcast(const TArray<uint8>& Packet)
{
	if (!EnsureSocket() || !BroadcastAddr.IsValid())
	{
		return;
	}

	// Fan out across every local subnet rather than trusting 255.255.255.255,
	// which on a multi-homed host leaves through the lowest-metric interface and
	// may never reach the device (see HapbeatNetInterfaces.h). Without this,
	// Test Play on such a machine would discover nothing and fall back to
	// broadcasting every command forever.
	if (BroadcastRoutes.Num() == 0)
	{
		SendSingleBroadcast(Packet);
		return;
	}
	int32 BytesSent = 0;
	for (const FHapbeatBroadcastRoute& Route : BroadcastRoutes)
	{
		if (Route.EndPoint.IsValid())
		{
			// Quiet on failure: a host normally carries an adapter that cannot
			// take a broadcast (Bluetooth PAN, Wi-Fi Direct, an idle virtual
			// switch), and letting the others through is the point.
			Socket->SendTo(Packet.GetData(), Packet.Num(), BytesSent, *Route.EndPoint);
		}
	}
}

int32 FHapbeatEditorSender::SendToKnownDevices(const TArray<uint8>& Packet)
{
	if (!EnsureSocket())
	{
		return 0;
	}

	// Pick up any PONGs that arrived since the last send, so the destination
	// list is as current as it can be without a receive thread.
	DrainReplies();

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	const double Now = FPlatformTime::Seconds();
	int32 SentTo = 0;

	if (SocketSubsystem != nullptr)
	{
		for (const TPair<FString, double>& Pair : DevicePongTimes)
		{
			if (Now - Pair.Value > EditorDeviceTtlSeconds)
			{
				continue;
			}
			const TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
			bool bValid = false;
			Addr->SetIp(*Pair.Key, bValid);
			if (!bValid)
			{
				continue;
			}
			Addr->SetPort(BroadcastAddr->GetPort());
			int32 BytesSent = 0;
			Socket->SendTo(Packet.GetData(), Packet.Num(), BytesSent, *Addr);
			++SentTo;
		}
	}

	return SentTo;
}

void FHapbeatEditorSender::SendRouted(const TArray<uint8>& Packet)
{
	if (SendToKnownDevices(Packet) == 0)
	{
		// Nobody has answered yet. Broadcasting is the fallback, not an
		// addition: sending both would fire the haptic twice on firmware
		// without (source endpoint, seq) de-duplication.
		//
		// A broadcast also costs latency the designer can hear -- the AP holds
		// group-addressed frames until the next DTIM beacon (100-300 ms)
		// whenever any client on it is power-saving -- which is exactly why
		// this path exists at all.
		//
		// Single destination, NOT the discovery fan-out: this carries PLAY, and
		// firmware without (source endpoint, seq) de-duplication would fire the
		// haptic once per route.
		SendSingleBroadcast(Packet);
	}

	// Re-arm discovery for the next click. Cheap here because a command is one
	// packet per click; the stream path sends from its own worker thread.
	SendDiscoveryBroadcast(FHapbeatProtocol::BuildPing(NextSeq(), UnixMicros()));
}

uint16 FHapbeatEditorSender::NextSeq()
{
	// Locked because the counter is now shared: the game thread takes numbers for
	// PLAY / STOP / PING, and the stream worker takes them for every STREAM_DATA
	// chunk. Unlocked, the two could hand out the same value, and a device that
	// de-duplicates on (source endpoint, seq) would then drop a real packet.
	// Same reasoning, and the same fix, as UHapbeatSubsystem::NextSeq.
	FScopeLock Lock(&SeqLock);
	Seq = static_cast<uint16>((Seq + 1) & 0xFFFF);
	return Seq;
}

int64 FHapbeatEditorSender::UnixMicros()
{
	static const int64 UnixEpochTicks = FDateTime(1970, 1, 1).GetTicks();
	return (FDateTime::UtcNow().GetTicks() - UnixEpochTicks) / 10;
}

int64 FHapbeatEditorSender::NextPingTimestampUs()
{
	// Strictly increasing: a lease reply is adopted only if it answers a newer
	// request than the last accepted one (same as UHapbeatSubsystem).
	int64 TimestampUs = UnixMicros();
	if (TimestampUs <= LastPingTimestampUs)
	{
		TimestampUs = LastPingTimestampUs + 1;
	}
	LastPingTimestampUs = TimestampUs;
	return TimestampUs;
}

uint32 FHapbeatEditorSender::AllocateGeneration(const FHapbeatStreamLeaseIdentity& Lease)
{
	// Per-lease monotonic, never reused; the stream workers take the next one
	// when they restart a session on their own (send gap).
	FScopeLock Lock(&GenerationLock);
	uint32& LastGeneration = NextGenerationByLease.FindOrAdd(Lease);
	if (LastGeneration == MAX_uint32)
	{
		return 0;
	}
	return ++LastGeneration;
}

void FHapbeatEditorSender::SendLeasePing(const FEditorDevice& Device)
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (Socket == nullptr || SocketSubsystem == nullptr)
	{
		return;
	}
	const TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
	bool bValid = false;
	Addr->SetIp(*Device.Ip, bValid);
	if (!bValid)
	{
		return;
	}
	Addr->SetPort(Device.Port);

	// Old requests only matter for a reply still in flight.
	const int64 NowUs = MonotonicMicros();
	for (auto It = PendingPings.CreateIterator(); It; ++It)
	{
		if (NowUs - It.Value().SentMonotonicUs > static_cast<int64>(EditorDeviceTtlSeconds * 1000000.0))
		{
			It.RemoveCurrent();
		}
	}

	// Extended (lease-request) PINGs are unicast only (stream-session.md "[1.x]
	// How SDKs send extended PINGs"); discovery stays the 8-byte broadcast.
	const uint16 PingSeq = NextSeq();
	FHapbeatPendingPing Pending;
	Pending.SentMonotonicUs = NowUs;
	Pending.TimestampUs = NextPingTimestampUs();
	Pending.ClientIncarnation = ClientIncarnation;
	Pending.bLeaseRequest = true;
	Pending.TargetIp = Device.Ip;
	PendingPings.Add(PingSeq, Pending);

	const TArray<uint8> Packet = FHapbeatProtocol::BuildPing(PingSeq, Pending.TimestampUs, ClientIncarnation);
	int32 BytesSent = 0;
	Socket->SendTo(Packet.GetData(), Packet.Num(), BytesSent, *Addr);
}

bool FHapbeatEditorSender::RequestLeases(const TArray<FString>& DeviceIps)
{
	// The newest request per device: a reply to it is the one to wait for.
	TMap<FString, int64> RequestedAt;
	for (const FString& Ip : DeviceIps)
	{
		if (const FEditorDevice* Device = Devices.Find(Ip))
		{
			SendLeasePing(*Device);
			RequestedAt.Add(Ip, LastPingTimestampUs);
		}
	}

	const double Deadline = FPlatformTime::Seconds() + LeaseReplyWaitSeconds;
	while (true)
	{
		DrainReplies();
		bool bAllAnswered = true;
		for (const TPair<FString, int64>& Pair : RequestedAt)
		{
			const FEditorDevice* Device = Devices.Find(Pair.Key);
			bAllAnswered &= Device != nullptr && Device->LeaseState.LastAcceptedTimestampUs >= Pair.Value;
		}
		if (bAllAnswered)
		{
			return true;
		}
		if (FPlatformTime::Seconds() >= Deadline)
		{
			return false;
		}
		FPlatformProcess::Sleep(0.005f);
	}
}

void FHapbeatEditorSender::SendPlay(const FString& EventId, float Gain, const FString& Target, float Pan)
{
	if (EventId.IsEmpty())
	{
		UE_LOG(LogHapbeatEditorSender, Warning, TEXT("[Hapbeat] Test Play: entry has no event id; ignored."));
		return;
	}
	// The caller passes the ENTRY's authored pan: Test Play auditions what the
	// entry says, without the per-call value a runtime node would add on top.
	SendRouted(FHapbeatProtocol::BuildPlay(NextSeq(), EventId, ResolveEditorTarget(Target), /*TargetTimeUs=*/0, Gain, Pan));
}

void FHapbeatEditorSender::SendStop(const FString& EventId, const FString& Target)
{
	if (EventId.IsEmpty())
	{
		return;
	}
	SendRouted(FHapbeatProtocol::BuildStop(NextSeq(), EventId, ResolveEditorTarget(Target)));
}

void FHapbeatEditorSender::SendStopAll(const FString& Target)
{
	SendRouted(FHapbeatProtocol::BuildStopAll(NextSeq(), ResolveEditorTarget(Target)));
}

void FHapbeatEditorSender::SendPing()
{
	// Discovery, so it broadcasts: it has to reach devices we have not heard
	// from. Replies land on this socket and are picked up by the next send.
	SendDiscoveryBroadcast(FHapbeatProtocol::BuildPing(NextSeq(), UnixMicros()));
}

// ---------------------------------------------------------------------------
// Editor-time streaming
// ---------------------------------------------------------------------------

void FHapbeatEditorSender::WaitForFirstDevice()
{
	DrainReplies();
	const double Now = FPlatformTime::Seconds();
	for (const TPair<FString, double>& Pair : DevicePongTimes)
	{
		if (Now - Pair.Value <= EditorDeviceTtlSeconds)
		{
			return; // somebody is already known
		}
	}

	SendPing();

	const double Deadline = FPlatformTime::Seconds() + 0.25;
	while (FPlatformTime::Seconds() < Deadline)
	{
		FPlatformProcess::Sleep(0.005f);
		DrainReplies();
		if (DevicePongTimes.Num() > 0)
		{
			return;
		}
	}
}

TArray<FHapbeatEditorSender::FEditorStreamPlan> FHapbeatEditorSender::PlanStreamSessions(
	const TArray<FEditorDevice>& InDevices, const FString& ResolvedTarget, uint64 Incarnation, double NowSeconds)
{
	TArray<FEditorStreamPlan> Plans;
	for (const FEditorDevice& Device : InDevices)
	{
		// One session per device whose address matches the target
		// (sdk-multi-stream.md §4), never a broadcast stream.
		if (NowSeconds - Device.LastPongSeconds > EditorDeviceTtlSeconds || Device.Address.IsEmpty()
			|| !UHapbeatTargetLibrary::AddressMatches(ResolvedTarget, Device.Address))
		{
			continue;
		}
		FEditorStreamPlan Plan;
		Plan.Ip = Device.Ip;
		Plan.Port = Device.Port;
		Plan.Address = Device.Address;
		if (Device.LeaseState.Class == EHapbeatStreamEndpointClass::Legacy)
		{
			Plan.Format = EHapbeatStreamWireFormat::LegacyV1;
		}
		else if (Device.LeaseState.IsUsableV2Lease(Incarnation))
		{
			Plan.Format = EHapbeatStreamWireFormat::SessionV2;
			Plan.Lease = Device.LeaseState.Lease;
		}
		else
		{
			continue;
		}
		Plans.Add(Plan);
	}
	Plans.Sort([](const FEditorStreamPlan& A, const FEditorStreamPlan& B) { return A.Ip < B.Ip; });
	return Plans;
}

FHapbeatStreamRunnable* FHapbeatEditorSender::CreateSessionRunnable(const FEditorStreamPlan& Plan, TArray<uint8>&& Pcm16,
	int32 SampleRate, int32 NumChannels, TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> Mirror,
	FSocket* InSocket, float SendAheadSeconds)
{
	FHapbeatStreamSessionIdentity Identity;
	if (Plan.Format == EHapbeatStreamWireFormat::SessionV2)
	{
		const uint32 Generation = AllocateGeneration(Plan.Lease);
		if (Generation == 0)
		{
			return nullptr;
		}
		Identity = FHapbeatStreamSessionIdentity(Plan.Lease, Generation);
	}

	TArray<FString> Destination;
	Destination.Add(Plan.Ip);
	FHapbeatStreamRunnable* Runnable = new FHapbeatStreamRunnable(
		FGuid::NewGuid(),
		MoveTemp(Pcm16),
		SampleRate,
		NumChannels,
		Plan.Address,
		Mirror,
		[]() { return NextSeq(); },
		InSocket,
		Plan.Port,
		MoveTemp(Destination),
		SendAheadSeconds,
		Plan.Format,
		Identity);
	if (Plan.Format == EHapbeatStreamWireFormat::SessionV2)
	{
		// The worker moves to a new generation of the same lease after its own
		// send gap (stream-session.md Sender requirements).
		const FHapbeatStreamLeaseIdentity Lease = Plan.Lease;
		Runnable->SetGenerationAllocator([Lease]() { return AllocateGeneration(Lease); });
	}
	return Runnable;
}

void FHapbeatEditorSender::StartStream(const UHapbeatClip* Clip, float Gain, const FString& Target, bool bLoop, float Pan)
{
	if (Clip == nullptr || Clip->Pcm16.Num() == 0 || Clip->SampleRate <= 0 || Clip->NumChannels <= 0)
	{
		UE_LOG(LogHapbeatEditorSender, Warning,
			TEXT("[Hapbeat] Editor stream: the entry has no usable clip (import a 16-bit PCM .wav into the Hapbeat Clip asset)."));
		return;
	}
	if (Clip->SampleRate != 16000 || Clip->NumChannels != 2)
	{
		UE_LOG(LogHapbeatEditorSender, Warning,
			TEXT("[Hapbeat] Editor stream requires the StreamHub canonical 16 kHz stereo PCM16 profile."));
		return;
	}
	if (!EnsureSocket())
	{
		return;
	}

	// Only one Test Play at a time: a second one would mix into the same
	// devices' ring buffers as noise rather than layer.
	StopStream();

	WaitForFirstDevice();

	const FString ResolvedTarget = ResolveEditorTarget(Target);
	TArray<FString> CandidateIps;
	double Now = FPlatformTime::Seconds();
	bool bTakeBack = false;
	for (const TPair<FString, FEditorDevice>& Pair : Devices)
	{
		const FEditorDevice& Device = Pair.Value;
		if (Now - Device.LastPongSeconds <= EditorDeviceTtlSeconds && !Device.Address.IsEmpty()
			&& UHapbeatTargetLibrary::AddressMatches(ResolvedTarget, Device.Address))
		{
			CandidateIps.Add(Device.Ip);
			bTakeBack |= Device.LeaseState.bLeaseSuperseded && Device.LeaseState.ClientIncarnation == ClientIncarnation;
		}
	}
	if (CandidateIps.Num() == 0)
	{
		UE_LOG(LogHapbeatEditorSender, Warning,
			TEXT("[Hapbeat] Editor stream: no device answering on this network matches target '%s'. STREAM packets are never broadcast."),
			*ResolvedTarget);
		return;
	}
	if (bTakeBack)
	{
		// Test Play is an explicit request, so a device another sender took over
		// is taken back with a fresh incarnation (stream-session.md "Incarnation
		// and takeover"; Unity HapbeatEditorTransport does the same).
		ClientIncarnation = CreateClientIncarnation();
	}

	// Ask every matching device for a lease (and, for firmware before 0.5.0, its
	// class) right before the stream: a lease from minutes ago may be gone.
	if (!RequestLeases(CandidateIps))
	{
		UE_LOG(LogHapbeatEditorSender, Verbose, TEXT("[Hapbeat] Editor stream: not every device answered the lease request in time."));
	}

	Now = FPlatformTime::Seconds();
	TArray<FEditorDevice> DeviceList;
	Devices.GenerateValueArray(DeviceList);
	const TArray<FEditorStreamPlan> Plans = PlanStreamSessions(DeviceList, ResolvedTarget, ClientIncarnation, Now);
	for (const FString& Ip : CandidateIps)
	{
		if (Plans.ContainsByPredicate([&Ip](const FEditorStreamPlan& Plan) { return Plan.Ip == Ip; }))
		{
			continue;
		}
		const FEditorDevice& Device = Devices.FindChecked(Ip);
		const TCHAR* Reason = Device.LeaseState.Class == EHapbeatStreamEndpointClass::Unknown
			? TEXT("it did not answer the stream lease request")
			: Device.LeaseState.bLeaseSuperseded ? TEXT("another sender holds its stream")
			: TEXT("it has no stream lease available");
		UE_LOG(LogHapbeatEditorSender, Warning, TEXT("[Hapbeat] Editor stream: not streaming to %s (%s): %s."),
			*Device.Ip, *Device.Address, Reason);
	}
	if (Plans.Num() == 0)
	{
		return;
	}

	// Stream Gen 1 firmware drops a BEGIN that follows its END too closely.
	double Gen1Wait = 0.0;
	for (const FEditorStreamPlan& Plan : Plans)
	{
		if (Plan.Format == EHapbeatStreamWireFormat::LegacyV1)
		{
			Gen1Wait = FMath::Max(Gen1Wait, Devices.FindChecked(Plan.Ip).LastGen1EndSeconds + Gen1EndToBeginGuardSeconds - Now);
		}
	}
	if (Gen1Wait > 0.0)
	{
		FPlatformProcess::Sleep(static_cast<float>(FMath::Min(Gen1Wait, Gen1EndToBeginGuardSeconds)));
	}

	// Gain rides on the mirror, not STREAM_BEGIN: the runnable scales every sample
	// by it, and the device applies the BEGIN gain verbatim, so setting both would
	// square it. One mirror for every device's session of this Test Play.
	StreamMirror = MakeShared<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>();
	StreamMirror->Gain.store(Gain, std::memory_order_release);
	// BEFORE the runnables are constructed: the streamer reads the pan once, there,
	// to decide whether a mono clip has to be upmixed to stereo to be pannable at
	// all (STREAM_BEGIN fixes the channel count for the session).
	StreamMirror->Pan.store(FMath::Clamp(Pan, -1.0f, 1.0f), std::memory_order_release);
	StreamMirror->bLoop.store(bLoop, std::memory_order_release);

	const UHapbeatConfig* Config = GetDefault<UHapbeatConfig>();
	const float SendAhead = Config != nullptr ? Config->StreamSendAheadSeconds : 0.05f;

	for (const FEditorStreamPlan& Plan : Plans)
	{
		FHapbeatStreamRunnable* Runnable = CreateSessionRunnable(Plan, TArray<uint8>(Clip->Pcm16), Clip->SampleRate,
			Clip->NumChannels, StreamMirror.ToSharedRef(), Socket, SendAhead);
		if (Runnable == nullptr)
		{
			UE_LOG(LogHapbeatEditorSender, Warning,
				TEXT("[Hapbeat] Editor stream: the lease of %s has no generation left; Test Play again to renew it."), *Plan.Ip);
			continue;
		}
		FEditorStream Stream;
		Stream.Runnable = Runnable;
		Stream.Ip = Plan.Ip;
		Stream.Format = Plan.Format;
		Stream.Thread = FRunnableThread::Create(Runnable, TEXT("HapbeatEditorStream"), 0, TPri_AboveNormal);
		Streams.Add(Stream);
	}
}

bool FHapbeatEditorSender::IsStreaming()
{
	return Streams.Num() > 0;
}

void FHapbeatEditorSender::StopStream()
{
	for (FEditorStream& Stream : Streams)
	{
		if (Stream.Thread != nullptr)
		{
			// Kill(true) flags the runnable and BLOCKS until Run() returns, by which
			// point STREAM_END has gone out -- the same contract the runtime relies on.
			Stream.Thread->Kill(true);
			delete Stream.Thread;
			Stream.Thread = nullptr;
		}
		if (Stream.Format == EHapbeatStreamWireFormat::LegacyV1)
		{
			if (FEditorDevice* Device = Devices.Find(Stream.Ip))
			{
				Device->LastGen1EndSeconds = FPlatformTime::Seconds();
			}
		}
		delete Stream.Runnable;
		Stream.Runnable = nullptr;
	}
	Streams.Empty();
	StreamMirror.Reset();
}

void FHapbeatEditorSender::Shutdown()
{
	StopStream();

	if (Socket != nullptr)
	{
		if (ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
		{
			SocketSubsystem->DestroySocket(Socket);
		}
		Socket = nullptr;
	}
	BroadcastAddr.Reset();
	// Learned on the socket we just closed; the next one may be on another network.
	DevicePongTimes.Empty();
	Devices.Empty();
	PendingPings.Empty();
	BroadcastRoutes.Empty();
}
