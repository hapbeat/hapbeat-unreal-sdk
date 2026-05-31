// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatSubsystem.h"

#include "HapbeatProtocol.h"
#include "Common/UdpSocketBuilder.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

void UHapbeatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UHapbeatSubsystem::Deinitialize()
{
	if (Socket != nullptr)
	{
		if (!AppName.IsEmpty())
		{
			// Tell the device this app is leaving so the OLED clears.
			SendPacket(FHapbeatProtocol::BuildConnectStatus(NextSeq(), false, Group, AppName, FString()));
		}
		Socket->Close();
		if (ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
		{
			SocketSubsystem->DestroySocket(Socket);
		}
		Socket = nullptr;
	}
	Super::Deinitialize();
}

void UHapbeatSubsystem::Connect(int32 InPort, const FString& InAppName)
{
	Port = InPort;
	AppName = InAppName.Left(FHapbeatProtocol::MaxAppNameLen);

	if (Socket == nullptr)
	{
		Socket = FUdpSocketBuilder(TEXT("HapbeatUDP"))
					 .AsReusable()
					 .WithBroadcast()
					 .Build();
	}
	if (Socket == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("Hapbeat: failed to create UDP socket"));
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

	if (!AppName.IsEmpty())
	{
		SendPacket(FHapbeatProtocol::BuildConnectStatus(NextSeq(), true, Group, AppName, FString()));
	}
}

void UHapbeatSubsystem::Play(const FString& EventId, float Gain, const FString& Target)
{
	SendPacket(FHapbeatProtocol::BuildPlay(NextSeq(), EventId, Target, 0, FMath::Clamp(Gain, 0.0f, 1.0f)));
}

void UHapbeatSubsystem::Stop(const FString& EventId, const FString& Target)
{
	SendPacket(FHapbeatProtocol::BuildStop(NextSeq(), EventId, Target));
}

void UHapbeatSubsystem::StopAll(const FString& Target)
{
	SendPacket(FHapbeatProtocol::BuildStopAll(NextSeq(), Target));
}

void UHapbeatSubsystem::Ping()
{
	// Unix-epoch microseconds. ToUnixTimestamp() is whole seconds, so derive µs
	// from ticks (100 ns each) to keep the int64 field at true microsecond
	// resolution (parity with the Python/Web/Unity SDKs).
	static const int64 UnixEpochTicks = FDateTime(1970, 1, 1).GetTicks();
	const int64 NowUs = (FDateTime::UtcNow().GetTicks() - UnixEpochTicks) / 10;
	SendPacket(FHapbeatProtocol::BuildPing(NextSeq(), NowUs));
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
