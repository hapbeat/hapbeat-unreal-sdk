// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatEditorSender.h"

#include "HapbeatClip.h"
#include "HapbeatConfig.h"
#include "HapbeatNetInterfaces.h"
#include "HapbeatProtocol.h"
#include "Common/UdpSocketBuilder.h"
#include "Containers/Ticker.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatEditorSender, Log, All);

FSocket* FHapbeatEditorSender::Socket = nullptr;
TSharedPtr<FInternetAddr> FHapbeatEditorSender::BroadcastAddr;
TMap<FString, double> FHapbeatEditorSender::DevicePongTimes;
TArray<FHapbeatBroadcastRoute> FHapbeatEditorSender::BroadcastRoutes;
uint16 FHapbeatEditorSender::Seq = 0;
TUniquePtr<FHapbeatEditorSender::FStreamState> FHapbeatEditorSender::Stream;
FTSTicker::FDelegateHandle FHapbeatEditorSender::StreamTickerHandle;

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

void FHapbeatEditorSender::SendRouted(const TArray<uint8>& Packet)
{
	if (!EnsureSocket())
	{
		return;
	}

	// Refresh what we know, then re-arm discovery for the next click. The PING
	// is idempotent, so sending it alongside a command is free.
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

	if (SentTo == 0)
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

	// Keep the table warm for the next click.
	SendDiscoveryBroadcast(FHapbeatProtocol::BuildPing(NextSeq(), UnixMicros()));
}

uint16 FHapbeatEditorSender::NextSeq()
{
	// Wraps at 0xFFFF; matches UHapbeatSubsystem::NextSeq() exactly.
	Seq = static_cast<uint16>((Seq + 1) & 0xFFFF);
	return Seq;
}

int64 FHapbeatEditorSender::UnixMicros()
{
	static const int64 UnixEpochTicks = FDateTime(1970, 1, 1).GetTicks();
	return (FDateTime::UtcNow().GetTicks() - UnixEpochTicks) / 10;
}

void FHapbeatEditorSender::SendPlay(const FString& EventId, float Gain, const FString& Target)
{
	if (EventId.IsEmpty())
	{
		UE_LOG(LogHapbeatEditorSender, Warning, TEXT("[Hapbeat] Test Play: entry has no event id; ignored."));
		return;
	}
	SendRouted(FHapbeatProtocol::BuildPlay(NextSeq(), EventId, Target, /*TargetTimeUs=*/0, Gain));
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

void FHapbeatEditorSender::StartStream(const UHapbeatClip* Clip, float Gain, const FString& Target, bool bLoop)
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

	// Learn who is out there BEFORE the first chunk. Routing only unicasts to
	// devices that have answered a PING, and nothing else in the editor sends
	// one -- so without this a test stream goes out as broadcast, which Wi-Fi
	// access points batch against their DTIM interval and chop the audio into
	// audible gaps. Replies are drained on every send, so the first chunk or two
	// may still broadcast before routing settles.
	SendPing();

	Stream = MakeUnique<FStreamState>();
	Stream->SampleRate = Clip->SampleRate;
	Stream->Channels = Clip->NumChannels;
	Stream->Target = Target;
	Stream->bLoop = bLoop;
	Stream->Offset = 0;
	Stream->StartTime = FPlatformTime::Seconds();

	// Premultiply here so STREAM_BEGIN can carry 1.0, matching the runtime
	// streamer -- the device applies the BEGIN gain verbatim, so sending the
	// gain there as well would square it.
	Stream->Pcm16 = Clip->Pcm16;
	if (!FMath::IsNearlyEqual(Gain, 1.0f))
	{
		int16* Samples = reinterpret_cast<int16*>(Stream->Pcm16.GetData());
		const int32 SampleCount = Stream->Pcm16.Num() / 2;
		for (int32 Index = 0; Index < SampleCount; ++Index)
		{
			Samples[Index] = static_cast<int16>(FMath::Clamp(
				FMath::RoundToInt(static_cast<float>(Samples[Index]) * Gain), -32768, 32767));
		}
	}

	const uint32 TotalSamples = static_cast<uint32>(Stream->Pcm16.Num() / 2);
	SendRouted(FHapbeatProtocol::BuildStreamBegin(
		NextSeq(),
		static_cast<uint16>(Stream->SampleRate),
		static_cast<uint8>(Stream->Channels),
		FHapbeatProtocol::AudioFormatPcm16,
		TotalSamples,
		1.0f,
		Target));

	StreamTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateStatic(&FHapbeatEditorSender::TickStream));
}

bool FHapbeatEditorSender::IsStreaming()
{
	return Stream.IsValid();
}

void FHapbeatEditorSender::StopStream()
{
	if (StreamTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(StreamTickerHandle);
		StreamTickerHandle.Reset();
	}
	if (Stream.IsValid())
	{
		SendRouted(FHapbeatProtocol::BuildStreamEnd(NextSeq()));
		Stream.Reset();
	}
}

bool FHapbeatEditorSender::TickStream(float /*DeltaSeconds*/)
{
	if (!Stream.IsValid())
	{
		StreamTickerHandle.Reset();
		return false;
	}

	const int32 BytesPerFrame = Stream->Channels * 2;
	const double BytesPerSecond = static_cast<double>(Stream->SampleRate) * BytesPerFrame;

	// Stay this far ahead of the wall clock. The device plays from a ring
	// buffer, so the lead has to cover the gap between two editor ticks --
	// which is not a fixed rate and stalls outright while a modal dialog is up.
	constexpr double LeadSeconds = 0.15;

	const double Elapsed = FPlatformTime::Seconds() - Stream->StartTime;
	int32 SendUpTo = static_cast<int32>((Elapsed + LeadSeconds) * BytesPerSecond);
	SendUpTo -= SendUpTo % BytesPerFrame; // never split a frame
	SendUpTo = FMath::Min(SendUpTo, Stream->Pcm16.Num());

	while (Stream->Offset < SendUpTo)
	{
		int32 ChunkBytes = FMath::Min(SendUpTo - Stream->Offset, FHapbeatProtocol::StreamDataMaxPayload);
		ChunkBytes -= ChunkBytes % BytesPerFrame;
		if (ChunkBytes <= 0)
		{
			break;
		}
		SendRouted(FHapbeatProtocol::BuildStreamData(
			NextSeq(), static_cast<uint32>(Stream->Offset), Stream->Pcm16.GetData() + Stream->Offset, ChunkBytes));
		Stream->Offset += ChunkBytes;
	}

	if (Stream->Offset >= Stream->Pcm16.Num())
	{
		if (Stream->bLoop)
		{
			// Re-BEGIN rather than continuing the offset: the device treats the
			// offset as a position inside one clip, so it has to be restarted.
			Stream->Offset = 0;
			Stream->StartTime = FPlatformTime::Seconds();
			SendRouted(FHapbeatProtocol::BuildStreamBegin(
				NextSeq(),
				static_cast<uint16>(Stream->SampleRate),
				static_cast<uint8>(Stream->Channels),
				FHapbeatProtocol::AudioFormatPcm16,
				static_cast<uint32>(Stream->Pcm16.Num() / 2),
				1.0f,
				Stream->Target));
			return true;
		}

		SendRouted(FHapbeatProtocol::BuildStreamEnd(NextSeq()));
		Stream.Reset();
		StreamTickerHandle.Reset();
		return false;
	}

	return true;
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
