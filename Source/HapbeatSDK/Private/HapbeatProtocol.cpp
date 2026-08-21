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

	void WriteHeader(TArray<uint8>& P, uint8 Cmd, uint16 Seq, uint16 PayloadLen)
	{
		AppendU16(P, FHapbeatProtocol::Magic);
		P.Add(FHapbeatProtocol::Version);
		P.Add(Cmd);
		AppendU16(P, Seq);
		AppendU16(P, PayloadLen);
	}

	TArray<uint8> MakePacket(uint8 Cmd, uint16 Seq, const TArray<uint8>& Payload)
	{
		checkf(FHapbeatProtocol::HeaderSize + Payload.Num() <= FHapbeatProtocol::MaxPacketSize,
			TEXT("Hapbeat: command packet exceeds 512 bytes"));
		TArray<uint8> P;
		P.Reserve(FHapbeatProtocol::HeaderSize + Payload.Num());
		WriteHeader(P, Cmd, Seq, static_cast<uint16>(Payload.Num()));
		P.Append(Payload);
		return P;
	}

	// ---- little-endian read helpers (parsing) ----

	uint16 ReadU16(const uint8* B)
	{
		return static_cast<uint16>(B[0]) | (static_cast<uint16>(B[1]) << 8);
	}

	int64 ReadI64(const uint8* B)
	{
		uint64 U = 0;
		for (int32 i = 0; i < 8; ++i)
		{
			U |= static_cast<uint64>(B[i]) << (8 * i);
		}
		return static_cast<int64>(U);
	}

	/**
	 * Read a UTF-8 null-terminated string from a byte view starting at Off.
	 * Advances Off past the terminator. If no NUL is found within [Off, Len),
	 * consumes to Len (treating the remainder as the string body) so a missing
	 * terminator at the end of the packet does not lose bytes.
	 */
	FString ReadCStr(const uint8* B, int32 Len, int32& Off)
	{
		int32 Start = Off;
		while (Off < Len && B[Off] != 0)
		{
			++Off;
		}
		const int32 StrBytes = Off - Start;
		FString Result;
		if (StrBytes > 0)
		{
			// Copy the slice and null-terminate so UTF8_TO_TCHAR has a valid C
			// string (the wire bytes here are not guaranteed NUL-terminated in
			// range — e.g. a truncated final field). This idiom is stable across
			// UE 5.3..latest and avoids the count-based FString ctor churn.
			TArray<ANSICHAR> Cstr;
			Cstr.AddUninitialized(StrBytes + 1);
			FMemory::Memcpy(Cstr.GetData(), B + Start, StrBytes);
			Cstr[StrBytes] = '\0';
			Result = FString(UTF8_TO_TCHAR(Cstr.GetData()));
		}
		if (Off < Len)
		{
			++Off; // skip the NUL terminator
		}
		return Result;
	}
}

TArray<uint8> FHapbeatProtocol::BuildPlay(uint16 Seq, const FString& EventId, const FString& Target, int64 TargetTimeUs, float Gain, float Pan)
{
	TArray<uint8> Payload;
	AppendCStr(Payload, EventId);
	AppendCStr(Payload, Target);
	AppendI64(Payload, TargetTimeUs);
	AppendF32(Payload, Gain);
	// Always written, never omitted: the field is only "optional" so that a
	// device can read packets from an older sender (contracts 0x01 / DEC-055).
	AppendF32(Payload, Pan);
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

TArray<uint8> FHapbeatProtocol::BuildStreamBegin(uint16 Seq, uint16 SampleRate, uint8 Channels, uint8 Format,
	uint32 TotalSamples, float Gain, const FString& Target)
{
	TArray<uint8> Payload;
	AppendU16(Payload, SampleRate);
	Payload.Add(Channels);
	Payload.Add(Format);
	AppendU32(Payload, TotalSamples);
	AppendF32(Payload, Gain);
	// target is optional: an empty string omits the field entirely (parity with
	// the Unity builder, where target==null/"" produces 0 trailing bytes).
	if (!Target.IsEmpty())
	{
		AppendCStr(Payload, Target);
	}
	return MakePacket(CmdStreamBegin, Seq, Payload);
}

TArray<uint8> FHapbeatProtocol::BuildStreamData(uint16 Seq, uint32 Offset, const uint8* Data, int32 NumBytes)
{
	const int32 SafeBytes = (Data != nullptr && NumBytes > 0) ? NumBytes : 0;
	checkf(HeaderSize + 4 + SafeBytes <= MaxStreamPacketSize,
		TEXT("Hapbeat: stream packet exceeds %d bytes"), MaxStreamPacketSize);

	TArray<uint8> P;
	P.Reserve(HeaderSize + 4 + SafeBytes);
	// payload_length = offset(4) + audio bytes. Built directly (not via MakePacket)
	// because STREAM_DATA uses the larger MTU cap, not the 512-byte command cap.
	// NOTE: MaxStreamPacketSize (1472) is only the hard MTU guardrail asserted
	// above. The Phase-4 streaming loop must still chunk audio at
	// <= StreamDataMaxPayload (1400, the spec STREAM_DATA_MAX_PAYLOAD budget),
	// NOT at the MTU cap — keep chunk sizing in the caller, parity with Unity.
	WriteHeader(P, CmdStreamData, Seq, static_cast<uint16>(4 + SafeBytes));
	AppendU32(P, Offset);
	if (SafeBytes > 0)
	{
		P.Append(Data, SafeBytes);
	}
	return P;
}

TArray<uint8> FHapbeatProtocol::BuildStreamEnd(uint16 Seq)
{
	return MakePacket(CmdStreamEnd, Seq, TArray<uint8>());
}

bool FHapbeatProtocol::ParsePacket(const uint8* Buf, int32 Len, FParsedPacket& Out)
{
	if (Buf == nullptr || Len < HeaderSize)
	{
		return false;
	}
	const uint16 PktMagic = ReadU16(Buf + 0);
	if (PktMagic != Magic)
	{
		return false;
	}
	const uint8 PktVersion = Buf[2];
	if (PktVersion != Version)
	{
		return false;
	}

	Out.Magic = PktMagic;
	Out.Version = PktVersion;
	Out.Cmd = Buf[3];
	Out.Seq = ReadU16(Buf + 4);
	Out.PayloadLength = ReadU16(Buf + 6);
	Out.Payload = Buf + HeaderSize;
	// Clamp the available payload to what actually arrived. A truncated datagram
	// (declared length > received) is tolerated: parsers only ever read up to
	// PayloadAvail, so they can't run off the end of Buf.
	Out.PayloadAvail = FMath::Min(static_cast<int32>(Out.PayloadLength), Len - HeaderSize);
	return true;
}

bool FHapbeatProtocol::ParsePong(const uint8* Payload, int32 AvailLen, FPongInfo& Out)
{
	if (Payload == nullptr || AvailLen < 16)
	{
		return false;
	}
	Out.Timestamp = ReadI64(Payload + 0);
	Out.ServerTime = ReadI64(Payload + 8);

	// > 16 bytes available => device-extended form. Use the actually-available
	// length (not the header's declared length) to bound the cstring reads.
	if (AvailLen > 16)
	{
		Out.bExtended = true;
		int32 Off = 16;
		Out.DeviceName = ReadCStr(Payload, AvailLen, Off);
		Out.Address = ReadCStr(Payload, AvailLen, Off);
		Out.Firmware = ReadCStr(Payload, AvailLen, Off);
	}
	else
	{
		Out.bExtended = false;
	}
	return true;
}

bool FHapbeatProtocol::ParseError(const uint8* Payload, int32 AvailLen, uint16& OutErrorCode, FString& OutMessage)
{
	if (Payload == nullptr || AvailLen < 2)
	{
		return false;
	}
	OutErrorCode = ReadU16(Payload + 0);
	OutMessage.Reset();
	if (AvailLen > 2)
	{
		int32 Off = 2;
		OutMessage = ReadCStr(Payload, AvailLen, Off);
	}
	return true;
}
