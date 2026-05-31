// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatProtocol.h"

namespace
{
	void AppendU16(TArray<uint8>& B, uint16 V)
	{
		B.Add(static_cast<uint8>(V & 0xFF));
		B.Add(static_cast<uint8>((V >> 8) & 0xFF));
	}

	void AppendU32(TArray<uint8>& B, uint32 V)
	{
		for (int32 i = 0; i < 4; ++i)
		{
			B.Add(static_cast<uint8>((V >> (8 * i)) & 0xFF));
		}
	}

	void AppendI64(TArray<uint8>& B, int64 V)
	{
		const uint64 U = static_cast<uint64>(V);
		for (int32 i = 0; i < 8; ++i)
		{
			B.Add(static_cast<uint8>((U >> (8 * i)) & 0xFF));
		}
	}

	void AppendF32(TArray<uint8>& B, float V)
	{
		uint32 Bits = 0;
		FMemory::Memcpy(&Bits, &V, sizeof(Bits));
		AppendU32(B, Bits);
	}

	void AppendCStr(TArray<uint8>& B, const FString& S)
	{
		const FTCHARToUTF8 Utf8(*S);
		B.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		B.Add(0); // null terminator
	}

	TArray<uint8> MakePacket(uint8 Cmd, uint16 Seq, const TArray<uint8>& Payload)
	{
		checkf(FHapbeatProtocol::HeaderSize + Payload.Num() <= FHapbeatProtocol::MaxPacketSize,
			TEXT("Hapbeat: packet exceeds 512 bytes"));
		TArray<uint8> P;
		AppendU16(P, FHapbeatProtocol::Magic);
		P.Add(FHapbeatProtocol::Version);
		P.Add(Cmd);
		AppendU16(P, Seq);
		AppendU16(P, static_cast<uint16>(Payload.Num()));
		P.Append(Payload);
		return P;
	}
}

TArray<uint8> FHapbeatProtocol::BuildPlay(uint16 Seq, const FString& EventId, const FString& Target, int64 TargetTimeUs, float Gain)
{
	TArray<uint8> Payload;
	AppendCStr(Payload, EventId);
	AppendCStr(Payload, Target);
	AppendI64(Payload, TargetTimeUs);
	AppendF32(Payload, Gain);
	return MakePacket(CmdPlay, Seq, Payload);
}

TArray<uint8> FHapbeatProtocol::BuildStop(uint16 Seq, const FString& EventId, const FString& Target)
{
	TArray<uint8> Payload;
	AppendCStr(Payload, EventId);
	AppendCStr(Payload, Target);
	return MakePacket(CmdStop, Seq, Payload);
}

TArray<uint8> FHapbeatProtocol::BuildStopAll(uint16 Seq, const FString& Target)
{
	TArray<uint8> Payload;
	AppendCStr(Payload, Target);
	return MakePacket(CmdStopAll, Seq, Payload);
}

TArray<uint8> FHapbeatProtocol::BuildPing(uint16 Seq, int64 TimestampUs)
{
	TArray<uint8> Payload;
	AppendI64(Payload, TimestampUs);
	return MakePacket(CmdPing, Seq, Payload);
}

TArray<uint8> FHapbeatProtocol::BuildConnectStatus(uint16 Seq, bool bConnected, uint8 Group, const FString& AppName, const FString& DeviceName)
{
	TArray<uint8> Payload;
	Payload.Add(bConnected ? 1 : 0);
	Payload.Add(Group);
	AppendCStr(Payload, AppName.Left(MaxAppNameLen));
	AppendCStr(Payload, DeviceName);
	return MakePacket(CmdConnectStatus, Seq, Payload);
}
