// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatEditorSender.h"

#include "HapbeatConfig.h"
#include "HapbeatProtocol.h"
#include "Common/UdpSocketBuilder.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatEditorSender, Log, All);

FSocket* FHapbeatEditorSender::Socket = nullptr;
TSharedPtr<FInternetAddr> FHapbeatEditorSender::BroadcastAddr;
TMap<FString, double> FHapbeatEditorSender::DevicePongTimes;
uint16 FHapbeatEditorSender::Seq = 0;

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
	// different network than the last.
	DevicePongTimes.Empty();

	// Probe immediately so the first Test Play click already has somewhere to
	// aim, instead of broadcasting and only discovering afterwards.
	SendBroadcast(FHapbeatProtocol::BuildPing(NextSeq(), UnixMicros()));

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

void FHapbeatEditorSender::SendBroadcast(const TArray<uint8>& Packet)
{
	if (!EnsureSocket() || !BroadcastAddr.IsValid())
	{
		return;
	}
	int32 BytesSent = 0;
	Socket->SendTo(Packet.GetData(), Packet.Num(), BytesSent, *BroadcastAddr);
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
		SendBroadcast(Packet);
	}

	// Keep the table warm for the next click.
	SendBroadcast(FHapbeatProtocol::BuildPing(NextSeq(), UnixMicros()));
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
	SendBroadcast(FHapbeatProtocol::BuildPing(NextSeq(), UnixMicros()));
}

void FHapbeatEditorSender::Shutdown()
{
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
}
