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
uint16 FHapbeatEditorSender::Seq = 0;

bool FHapbeatEditorSender::EnsureSocket()
{
	if (Socket != nullptr)
	{
		return true;
	}

	// Reusable + broadcast-capable, bound to an OS-assigned ephemeral port (0):
	// this sender never reads replies, so there is no fixed local port to
	// protect -- only the destination (255.255.255.255:Port) matters.
	Socket = FUdpSocketBuilder(TEXT("HapbeatEditorSenderUDP"))
				 .AsReusable()
				 .AsNonBlocking()
				 .WithBroadcast()
				 .BoundToAddress(FIPv4Address::Any)
				 .BoundToPort(0)
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

	return true;
}

void FHapbeatEditorSender::SendPacket(const TArray<uint8>& Packet)
{
	if (!EnsureSocket() || !BroadcastAddr.IsValid())
	{
		return;
	}
	int32 BytesSent = 0;
	Socket->SendTo(Packet.GetData(), Packet.Num(), BytesSent, *BroadcastAddr);
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
	SendPacket(FHapbeatProtocol::BuildPlay(NextSeq(), EventId, Target, /*TargetTimeUs=*/0, Gain));
}

void FHapbeatEditorSender::SendStop(const FString& EventId, const FString& Target)
{
	if (EventId.IsEmpty())
	{
		return;
	}
	SendPacket(FHapbeatProtocol::BuildStop(NextSeq(), EventId, Target));
}

void FHapbeatEditorSender::SendStopAll(const FString& Target)
{
	SendPacket(FHapbeatProtocol::BuildStopAll(NextSeq(), Target));
}

void FHapbeatEditorSender::SendPing()
{
	SendPacket(FHapbeatProtocol::BuildPing(NextSeq(), UnixMicros()));
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
}
