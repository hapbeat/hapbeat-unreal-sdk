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

	void AppendU64(TArray<uint8>& B, uint64 V)
	{
		for (int32 i = 0; i < 8; ++i)
		{
			B.Add(static_cast<uint8>((V >> (8 * i)) & 0xFF));
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

	void WriteHeader(TArray<uint8>& P, uint8 Cmd, uint16 Seq, uint16 PayloadLen,
		uint8 HeaderVersion = FHapbeatProtocol::Version)
	{
		AppendU16(P, FHapbeatProtocol::Magic);
		P.Add(HeaderVersion);
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

	/** Stream Gen 2 packet: header version 2, MTU-safe stream cap (not the 512-byte command cap). */
	TArray<uint8> MakeStreamV2Packet(uint8 Cmd, uint16 Seq, const TArray<uint8>& Payload)
	{
		checkf(FHapbeatProtocol::HeaderSize + Payload.Num() <= FHapbeatProtocol::MaxStreamPacketSize,
			TEXT("Hapbeat: Stream Gen 2 packet exceeds %d bytes"), FHapbeatProtocol::MaxStreamPacketSize);
		TArray<uint8> P;
		P.Reserve(FHapbeatProtocol::HeaderSize + Payload.Num());
		WriteHeader(P, Cmd, Seq, static_cast<uint16>(Payload.Num()), FHapbeatProtocol::StreamProtocolVersion);
		P.Append(Payload);
		return P;
	}

	void AppendStreamIdentity(TArray<uint8>& B, const FHapbeatStreamSessionIdentity& Identity)
	{
		checkf(Identity.IsValid(),
			TEXT("Hapbeat: a Stream Gen 2 packet requires a valid boot id, lease ticket and generation"));
		AppendU64(B, Identity.DeviceBootId);
		AppendU32(B, Identity.LeaseTicket);
		AppendU32(B, Identity.Generation);
	}

	// ---- little-endian read helpers (parsing) ----

	uint16 ReadU16(const uint8* B)
	{
		return static_cast<uint16>(B[0]) | (static_cast<uint16>(B[1]) << 8);
	}

	uint32 ReadU32(const uint8* B)
	{
		return static_cast<uint32>(B[0]) | (static_cast<uint32>(B[1]) << 8)
			| (static_cast<uint32>(B[2]) << 16) | (static_cast<uint32>(B[3]) << 24);
	}

	uint64 ReadU64(const uint8* B)
	{
		uint64 U = 0;
		for (int32 i = 0; i < 8; ++i)
		{
			U |= static_cast<uint64>(B[i]) << (8 * i);
		}
		return U;
	}

	int64 ReadI64(const uint8* B)
	{
		return static_cast<int64>(ReadU64(B));
	}

	/**
	 * Classify the bytes after volume_wiper by the [1.x] table of
	 * stream-session.md "Receiver classification" (reference: contracts
	 * tests/test_wire_compat.py classify_1x). Only the first 32 bytes of a Stream
	 * Gen 2 tail are read: bytes after them, the reserved field and flag bits
	 * other than 0-2 belong to a later major and are ignored here.
	 */
	void ParseStreamLeaseTail(const uint8* Payload, int32 AvailLen, int32 Off, FHapbeatProtocol::FPongInfo& Out)
	{
		static const uint8 Marker[4] = { 'H', 'B', 'S', '2' };
		const int32 Remaining = AvailLen - Off;
		Out.LeaseTail = EHapbeatPongLeaseTail::Absent;
		if (Remaining <= 0)
		{
			return; // PONG ends at/before volume_wiper: Stream Gen 1
		}
		if (Remaining < FHapbeatProtocol::StreamLeasePongTailSize
			|| FMemory::Memcmp(Payload + Off, Marker, 4) != 0
			|| Payload[Off + 4] != 1)
		{
			// Too short, another extension_version, or bytes that are not HBS2:
			// neither a lease reply nor proof of firmware before 0.5.0.
			Out.LeaseTail = EHapbeatPongLeaseTail::Ignored;
			return;
		}

		const uint8 Flags = Payload[Off + 5];
		Out.LeaseTail = EHapbeatPongLeaseTail::Valid;
		Out.EchoedClientIncarnation = ReadU64(Payload + Off + 8);
		Out.Lease = FHapbeatStreamLeaseIdentity(ReadU64(Payload + Off + 16), ReadU32(Payload + Off + 24));
		Out.HighWaterTicket = ReadU32(Payload + Off + 28);
		Out.bLeaseValid = (Flags & 0x01) != 0 && Out.Lease.IsValid();
		Out.bLeaseSuperseded = (Flags & 0x02) != 0;
		Out.bLeaseReissued = (Flags & 0x04) != 0;
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

TArray<uint8> FHapbeatProtocol::BuildPing(uint16 Seq, int64 TimestampUs, uint64 ClientIncarnation)
{
	checkf(ClientIncarnation != 0, TEXT("Hapbeat: a stream lease PING requires a nonzero client incarnation"));
	TArray<uint8> Payload;
	AppendI64(Payload, TimestampUs);
	AppendU64(Payload, ClientIncarnation);
	return MakePacket(CmdPing, Seq, Payload);
}

TArray<uint8> FHapbeatProtocol::BuildCapsRequest(uint16 Seq)
{
	TArray<uint8> Payload;
	Payload.Add(CapsVersion);
	return MakePacket(CmdCapsReq, Seq, Payload);
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

TArray<uint8> FHapbeatProtocol::BuildStreamBeginV2(uint16 Seq, const FHapbeatStreamSessionIdentity& Identity,
	uint16 SampleRate, uint8 Channels, uint8 Format, uint32 TotalSamples, float Gain, const FString& Target)
{
	TArray<uint8> Payload;
	AppendStreamIdentity(Payload, Identity);
	AppendU16(Payload, SampleRate);
	Payload.Add(Channels);
	Payload.Add(Format);
	AppendU32(Payload, TotalSamples);
	AppendF32(Payload, Gain);
	if (!Target.IsEmpty())
	{
		AppendCStr(Payload, Target);
	}
	return MakeStreamV2Packet(CmdStreamBegin, Seq, Payload);
}

TArray<uint8> FHapbeatProtocol::BuildStreamDataV2(uint16 Seq, const FHapbeatStreamSessionIdentity& Identity,
	uint32 Offset, const uint8* Data, int32 NumBytes)
{
	const int32 SafeBytes = (Data != nullptr && NumBytes > 0) ? NumBytes : 0;
	TArray<uint8> Payload;
	Payload.Reserve(StreamSessionEnvelopeSize + 4 + SafeBytes);
	AppendStreamIdentity(Payload, Identity);
	AppendU32(Payload, Offset);
	if (SafeBytes > 0)
	{
		Payload.Append(Data, SafeBytes);
	}
	return MakeStreamV2Packet(CmdStreamData, Seq, Payload);
}

TArray<uint8> FHapbeatProtocol::BuildStreamEndV2(uint16 Seq, const FHapbeatStreamSessionIdentity& Identity)
{
	TArray<uint8> Payload;
	AppendStreamIdentity(Payload, Identity);
	return MakeStreamV2Packet(CmdStreamEnd, Seq, Payload);
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
		Out.VolumeLevel = Off < AvailLen ? static_cast<int32>(Payload[Off++]) : -1;
		Out.VolumeWiper = Off < AvailLen ? static_cast<int32>(Payload[Off++]) : -1;
		ParseStreamLeaseTail(Payload, AvailLen, Off, Out);
	}
	else
	{
		Out.bExtended = false;
		Out.VolumeLevel = -1;
		Out.VolumeWiper = -1;
		Out.LeaseTail = EHapbeatPongLeaseTail::Absent;
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

bool FHapbeatProtocol::ParseErrorExtended(const uint8* Payload, int32 AvailLen, FErrorInfo& Out)
{
	Out = FErrorInfo();
	if (Payload == nullptr || AvailLen < 2)
	{
		return false;
	}
	Out.ErrorCode = ReadU16(Payload + 0);
	int32 Off = 2;
	Out.Message = ReadCStr(Payload, AvailLen, Off);
	// ref_command(u8) + ref_seq(u16) + detail_length(u8), then detail. Absent in
	// the 0.x form, which ends after the message (contracts parse_error).
	if (AvailLen >= Off + 4)
	{
		Out.bHasReference = true;
		Out.RefCommand = Payload[Off];
		Out.RefSeq = ReadU16(Payload + Off + 1);
		const int32 DetailLength = Payload[Off + 3];
		const int32 DetailAvail = FMath::Clamp(AvailLen - (Off + 4), 0, DetailLength);
		Out.Detail.Append(Payload + Off + 4, DetailAvail);
	}
	return true;
}

bool FHapbeatProtocol::ParseCaps(const uint8* Payload, int32 AvailLen, FCapsInfo& Out)
{
	Out = FCapsInfo();
	if (Payload == nullptr || AvailLen < 2)
	{
		return false;
	}
	Out.CapsVersion = Payload[0];
	// Defined value sizes of caps_version 1; 0 = variable (firmware_version).
	auto DefinedSize = [](uint8 Type) -> int32
	{
		switch (Type)
		{
		case 0x01: case 0x03: case 0x04: case 0x06: case 0x07: return 1;
		case 0x05: case 0x08: return 4;
		case 0x02: return 0;
		default: return -1;
		}
	};
	TSet<uint8> Seen;
	int32 Off = 2;
	while (Off + 2 <= AvailLen)
	{
		const uint8 Type = Payload[Off];
		const int32 Size = Payload[Off + 1];
		const uint8* Value = Payload + Off + 2;
		const bool bComplete = Off + 2 + Size <= AvailLen;
		Off += 2 + Size;
		const int32 Defined = DefinedSize(Type);
		// Cut by the end of the payload, unknown, already taken or too short: skip.
		if (!bComplete || Defined < 0 || Seen.Contains(Type) || Size < Defined)
		{
			continue;
		}
		Seen.Add(Type);
		switch (Type)
		{
		case 0x01: Out.Generation = Value[0]; break;
		case 0x02:
		{
			TArray<ANSICHAR> Text;
			Text.Append(reinterpret_cast<const ANSICHAR*>(Value), Size);
			Text.Add('\0');
			Out.FirmwareVersion = FString(UTF8_TO_TCHAR(Text.GetData()));
			break;
		}
		case 0x03: Out.StreamGens = Value[0]; break;
		case 0x04: Out.StreamFormats = Value[0]; break;
		case 0x05:
			Out.LeaseTtlSeconds = Value[0];
			Out.LeaseSlots = Value[1];
			Out.InactivityTimeoutSeconds = Value[2];
			break;
		case 0x06: Out.HapticChannels = Value[0]; break;
		case 0x07: Out.AddressMaxLen = Value[0]; break;
		case 0x08: Out.Features = static_cast<int64>(ReadU32(Value)); break;
		default: break;
		}
	}
	return true;
}
