// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatEditorSender.h"

#include "HapbeatClip.h"
#include "HapbeatConfig.h"
#include "HapbeatNetInterfaces.h"
#include "HapbeatProtocol.h"
#include "HapbeatStreamRunnable.h"
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
FHapbeatStreamRunnable* FHapbeatEditorSender::StreamRunnable = nullptr;
FRunnableThread* FHapbeatEditorSender::StreamThread = nullptr;

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
		if (!FHapbeatProtocol::ParsePacket(Buffer, BytesRead, Parsed)
			|| Parsed.Cmd != FHapbeatProtocol::CmdPong)
		{
			continue;
		}
		// Only the sender's IP matters here; the PONG body is for diagnostics
		// the editor sender does not surface.
		DevicePongTimes.Add(Sender->ToString(/*bAppendPort=*/false), FPlatformTime::Seconds());
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

void FHapbeatEditorSender::SendPlay(const FString& EventId, float Gain, const FString& Target, float Pan)
{
	if (EventId.IsEmpty())
	{
		UE_LOG(LogHapbeatEditorSender, Warning, TEXT("[Hapbeat] Test Play: entry has no event id; ignored."));
		return;
	}
	// The caller passes the ENTRY's authored pan: Test Play auditions what the
	// entry says, without the per-call value a runtime node would add on top.
	SendRouted(FHapbeatProtocol::BuildPlay(NextSeq(), EventId, Target, /*TargetTimeUs=*/0, Gain, Pan));
}

void FHapbeatEditorSender::SendStop(const FString& EventId, const FString& Target)
{
	if (EventId.IsEmpty())
	{
		return;
	}
	SendRouted(FHapbeatProtocol::BuildStop(NextSeq(), EventId, Target));
}

void FHapbeatEditorSender::SendStopAll(const FString& Target)
{
	SendRouted(FHapbeatProtocol::BuildStopAll(NextSeq(), Target));
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

void FHapbeatEditorSender::StartStream(const UHapbeatClip* Clip, float Gain, const FString& Target, bool bLoop, float Pan)
{
	if (Clip == nullptr || Clip->Pcm16.Num() == 0 || Clip->SampleRate <= 0 || Clip->NumChannels <= 0)
	{
		UE_LOG(LogHapbeatEditorSender, Warning,
			TEXT("[Hapbeat] Editor stream: the entry has no usable clip (import a 16-bit PCM .wav into the Hapbeat Clip asset)."));
		return;
	}
	if (!EnsureSocket())
	{
		return;
	}

	// Only one at a time: the device mixes into a single ring buffer, so two
	// editor streams would interleave into noise rather than layer.
	StopStream();

	// The runnable snapshots its destinations at construction, so discovery has
	// to have happened by now or the entire stream goes out as broadcast.
	WaitForFirstDevice();

	TArray<FString> UnicastIps;
	const double Now = FPlatformTime::Seconds();
	for (const TPair<FString, double>& Pair : DevicePongTimes)
	{
		if (Now - Pair.Value <= EditorDeviceTtlSeconds)
		{
			UnicastIps.Add(Pair.Key);
		}
	}
	const bool bHasSnapshot = UnicastIps.Num() > 0;

	// Gain rides on the mirror, not STREAM_BEGIN: the runnable scales every sample
	// by it, and the device applies the BEGIN gain verbatim, so setting both would
	// square it.
	StreamMirror = MakeShared<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>();
	StreamMirror->Gain.store(Gain, std::memory_order_release);
	// BEFORE the runnable is constructed: the streamer reads the pan once, there,
	// to decide whether a mono clip has to be upmixed to stereo to be pannable at
	// all (STREAM_BEGIN fixes the channel count for the session).
	StreamMirror->Pan.store(FMath::Clamp(Pan, -1.0f, 1.0f), std::memory_order_release);

	const UHapbeatConfig* Config = GetDefault<UHapbeatConfig>();
	const int32 Port = Config != nullptr ? Config->Port : 7700;
	const float SendAhead = Config != nullptr ? Config->StreamSendAheadSeconds : 0.05f;

	StreamRunnable = new FHapbeatStreamRunnable(
		TArray<uint8>(Clip->Pcm16),
		Clip->SampleRate,
		Clip->NumChannels,
		Target,
		bLoop,
		StreamMirror.ToSharedRef(),
		[]() { return NextSeq(); },
		Socket,
		Port,
		MoveTemp(UnicastIps),
		bHasSnapshot,
		SendAhead,
		BroadcastAddr.IsValid() ? BroadcastAddr->ToString(false) : FString());

	StreamThread = FRunnableThread::Create(StreamRunnable, TEXT("HapbeatEditorStream"), 0, TPri_AboveNormal);

	if (!bHasSnapshot)
	{
		UE_LOG(LogHapbeatEditorSender, Warning,
			TEXT("[Hapbeat] Editor stream: no device answered, so this streams as broadcast and will likely drop out. Check the device is powered on and on this network."));
	}
}

bool FHapbeatEditorSender::IsStreaming()
{
	return StreamRunnable != nullptr;
}

void FHapbeatEditorSender::StopStream()
{
	if (StreamThread != nullptr)
	{
		// Kill(true) flags the runnable and BLOCKS until Run() returns, by which
		// point STREAM_END has gone out -- the same contract the runtime relies on.
		StreamThread->Kill(true);
		delete StreamThread;
		StreamThread = nullptr;
	}
	if (StreamRunnable != nullptr)
	{
		delete StreamRunnable;
		StreamRunnable = nullptr;
	}
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
	BroadcastRoutes.Empty();
}
