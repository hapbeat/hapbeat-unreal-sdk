// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"

/**
 * Device-issued stream lease (stream-session-v2.md "Discovery lease"): the
 * device boot id plus the monotonic ticket the device handed to this SDK
 * incarnation. Both halves are nonzero when valid.
 */
struct FHapbeatStreamLeaseIdentity
{
	uint64 DeviceBootId = 0;
	uint32 LeaseTicket = 0;

	FHapbeatStreamLeaseIdentity() = default;
	FHapbeatStreamLeaseIdentity(uint64 InDeviceBootId, uint32 InLeaseTicket)
		: DeviceBootId(InDeviceBootId), LeaseTicket(InLeaseTicket)
	{
	}

	bool IsValid() const { return DeviceBootId != 0 && LeaseTicket != 0; }
	bool operator==(const FHapbeatStreamLeaseIdentity& Other) const
	{
		return DeviceBootId == Other.DeviceBootId && LeaseTicket == Other.LeaseTicket;
	}
	bool operator!=(const FHapbeatStreamLeaseIdentity& Other) const { return !(*this == Other); }
	friend uint32 GetTypeHash(const FHapbeatStreamLeaseIdentity& Identity)
	{
		return HashCombine(GetTypeHash(Identity.DeviceBootId), GetTypeHash(Identity.LeaseTicket));
	}
};

/**
 * The 16-byte v2 stream envelope carried by every v2 BEGIN/DATA/END:
 * device_boot_id(u64) + lease_ticket(u32) + generation(u32). Fixed for the
 * lifetime of one wire session; a new session under the same lease uses a
 * strictly greater generation.
 */
struct FHapbeatStreamSessionIdentity
{
	uint64 DeviceBootId = 0;
	uint32 LeaseTicket = 0;
	uint32 Generation = 0;

	FHapbeatStreamSessionIdentity() = default;
	FHapbeatStreamSessionIdentity(const FHapbeatStreamLeaseIdentity& InLease, uint32 InGeneration)
		: DeviceBootId(InLease.DeviceBootId), LeaseTicket(InLease.LeaseTicket), Generation(InGeneration)
	{
	}

	FHapbeatStreamLeaseIdentity GetLease() const { return FHapbeatStreamLeaseIdentity(DeviceBootId, LeaseTicket); }
	bool IsValid() const { return GetLease().IsValid() && Generation != 0; }
	bool operator==(const FHapbeatStreamSessionIdentity& Other) const
	{
		return DeviceBootId == Other.DeviceBootId && LeaseTicket == Other.LeaseTicket && Generation == Other.Generation;
	}
	bool operator!=(const FHapbeatStreamSessionIdentity& Other) const { return !(*this == Other); }
};

/**
 * Wire format of one endpoint stream session. Chosen once per session from the
 * endpoint's classification and never changed while that session lives
 * (stream-session-v2.md "Legacy receiver fallback (SDK)").
 */
enum class EHapbeatStreamWireFormat : uint8
{
	/** Pre-v2 firmware: header version 1, no envelope, END with an empty payload. */
	LegacyV1,
	/** Stream session v2: header version 2 and the 16-byte envelope on BEGIN/DATA/END. */
	SessionV2,
};

/** Tri-state result of looking for the 32-byte HBS2 lease tail after the ordinary PONG fields. */
enum class EHapbeatPongLeaseTail : uint8
{
	/** No HBS2 marker after volume_wiper (PONG ends there, or other bytes follow). */
	Absent,
	/** Exactly 32 bytes, marker HBS2, extension_version 1, only known flag bits, reserved 0. */
	Valid,
	/** HBS2 marker present but length / version / reserved / flags are wrong. Never classifies. */
	Malformed,
};

/**
 * Hapbeat Layer 1 (SDK -> device) UDP protocol builders.
 *
 * Byte-for-byte compatible with hapbeat-contracts/specs/message-format.md.
 * Every multi-byte field is little-endian. Reference implementations:
 * HapbeatProtocol.cs (Unity), protocol.py (Python SDK).
 */
class HAPBEATSDK_API FHapbeatProtocol
{
public:
	static constexpr uint16 Magic = 0x4842; // "HB"
	static constexpr uint8 Version = 0x01;
	/** Header protocol_version for v2 STREAM_BEGIN/DATA/END only (stream-session-v2.md). */
	static constexpr uint8 StreamProtocolVersion = 0x02;
	/** device_boot_id(8) + lease_ticket(4) + generation(4). */
	static constexpr int32 StreamSessionEnvelopeSize = 16;
	/** Exact size of the HBS2 PONG lease tail. */
	static constexpr int32 StreamLeasePongTailSize = 32;
	static constexpr int32 HeaderSize = 8;
	static constexpr int32 MaxPacketSize = 512;
	/** STREAM_DATA packet cap (1500 MTU - 20 IP - 8 UDP). Parity with Unity MAX_STREAM_PACKET_SIZE. */
	static constexpr int32 MaxStreamPacketSize = 1472;
	/** Max STREAM_DATA payload to stay MTU-safe (1472 - 8 header). Parity with Unity STREAM_DATA_MAX_PAYLOAD. */
	static constexpr int32 StreamDataMaxPayload = 1400;
	static constexpr int32 MaxAppNameLen = 16; // device OLED grid width

	// Command types (SDK -> device).
	static constexpr uint8 CmdPlay = 0x01;
	static constexpr uint8 CmdStop = 0x02;
	static constexpr uint8 CmdStopAll = 0x03;
	static constexpr uint8 CmdPing = 0x10;
	static constexpr uint8 CmdConnectStatus = 0x20;
	static constexpr uint8 CmdStreamBegin = 0x30;
	static constexpr uint8 CmdStreamData = 0x31;
	static constexpr uint8 CmdStreamEnd = 0x32;

	// Response types (device -> SDK).
	static constexpr uint8 CmdPong = 0x11;
	static constexpr uint8 CmdError = 0xFF;

	// Audio format ids (STREAM_BEGIN.format).
	static constexpr uint8 AudioFormatPcm16 = 0;
	static constexpr uint8 AudioFormatImaAdpcm = 1;

	// ---- Builders (SDK -> device) ----

	/**
	 * PLAY: event_id\0 + target\0 + target_time(i64) + gain(f32) + pan(f32).
	 *
	 * pan is -1 (left) .. 0 (center) .. +1 (right) and is the LAST field. The
	 * device treats it as optional (absent = 0.0) for older senders, but this
	 * SDK always writes it, so a FIRE (Command) entry can be panned too -- the
	 * device mixer expands it to per-voice linear-balance L/R gains, the same
	 * law the SDK pre-multiplies onto stereo StreamClip PCM (contracts
	 * message-format.md 0x01, DEC-055).
	 */
	static TArray<uint8> BuildPlay(uint16 Seq, const FString& EventId, const FString& Target, int64 TargetTimeUs, float Gain, float Pan);
	/** STOP: event_id\0 + target\0. */
	static TArray<uint8> BuildStop(uint16 Seq, const FString& EventId, const FString& Target);
	/** STOP_ALL: target\0. */
	static TArray<uint8> BuildStopAll(uint16 Seq, const FString& Target);
	/** PING: timestamp(i64, microseconds). Discovery-only: never allocates a stream lease. */
	static TArray<uint8> BuildPing(uint16 Seq, int64 TimestampUs);
	/**
	 * Extended PING (still header version 1): timestamp(i64) + client_incarnation(u64),
	 * 16-byte payload. Requests a stream lease from v2 receivers; pre-v2 firmware
	 * ignores the extra 8 bytes and answers with an ordinary PONG. ClientIncarnation
	 * must be nonzero.
	 */
	static TArray<uint8> BuildPing(uint16 Seq, int64 TimestampUs, uint64 ClientIncarnation);
	/** CONNECT_STATUS: connected(u8) + group(u8) + app_name\0 + device_name\0. */
	static TArray<uint8> BuildConnectStatus(uint16 Seq, bool bConnected, uint8 Group, const FString& AppName, const FString& DeviceName);

	/**
	 * STREAM_BEGIN (0x30): sample_rate(u16) + channels(u8) + format(u8) +
	 * total_samples(u32) + gain(f32) + optional target\0. Fixed part = 12 bytes;
	 * an empty Target omits the target field entirely (0 trailing bytes).
	 */
	static TArray<uint8> BuildStreamBegin(uint16 Seq, uint16 SampleRate, uint8 Channels, uint8 Format,
		uint32 TotalSamples, float Gain, const FString& Target = FString());
	/**
	 * STREAM_DATA (0x31): offset(u32) + raw PCM16/ADPCM bytes. Built with the
	 * larger MaxStreamPacketSize cap (NOT MaxPacketSize). NumBytes is the number
	 * of audio bytes to copy from Data.
	 */
	static TArray<uint8> BuildStreamData(uint16 Seq, uint32 Offset, const uint8* Data, int32 NumBytes);
	/** STREAM_END (0x32): empty payload. */
	static TArray<uint8> BuildStreamEnd(uint16 Seq);

	// The three builders above are the LEGACY (pre-v2, header version 1) stream
	// format and must stay byte-identical: they are what devices on firmware
	// predating stream session v2 receive (message-format.md "v1 stream").

	/**
	 * v2 STREAM_BEGIN: header version 2, payload = 16-byte envelope + the same
	 * 12-byte body + optional target\0 as BuildStreamBegin. Identity must be valid.
	 */
	static TArray<uint8> BuildStreamBeginV2(uint16 Seq, const FHapbeatStreamSessionIdentity& Identity,
		uint16 SampleRate, uint8 Channels, uint8 Format, uint32 TotalSamples, float Gain,
		const FString& Target = FString());
	/** v2 STREAM_DATA: header version 2, payload = envelope + offset(u32) + audio bytes. */
	static TArray<uint8> BuildStreamDataV2(uint16 Seq, const FHapbeatStreamSessionIdentity& Identity,
		uint32 Offset, const uint8* Data, int32 NumBytes);
	/** v2 STREAM_END: header version 2, payload = exactly the 16-byte envelope. */
	static TArray<uint8> BuildStreamEndV2(uint16 Seq, const FHapbeatStreamSessionIdentity& Identity);

	// ---- Parsers (device -> SDK) ----

	/** Validated header + a view into the payload (Payload points INTO Buf; no copy). */
	struct FParsedPacket
	{
		uint16 Magic = 0;
		uint8 Version = 0;
		uint8 Cmd = 0;
		uint16 Seq = 0;
		uint16 PayloadLength = 0;  // payload_length from the header
		const uint8* Payload = nullptr;  // points into the caller's buffer (Buf + HeaderSize)
		int32 PayloadAvail = 0;  // bytes actually available at Payload (== min(PayloadLength, Len-HeaderSize))
	};

	/**
	 * Parse + validate the 8-byte header. Returns false on a short buffer, bad
	 * magic, or wrong version. On success, Out.Payload aliases Buf (valid only
	 * while Buf lives) and Out.PayloadAvail is the number of payload bytes that
	 * are actually present (the header's declared length may exceed what arrived;
	 * we clamp so parsers never read out of bounds).
	 */
	static bool ParsePacket(const uint8* Buf, int32 Len, FParsedPacket& Out);

	/** Decoded PONG payload. bExtended = device-form (payload_length > 16). */
	struct FPongInfo
	{
		int64 Timestamp = 0;    // echoed PING timestamp (µs)
		int64 ServerTime = 0;   // device/bridge send time (µs)
		bool bExtended = false; // true => DeviceName/Address/Firmware are present
		FString DeviceName;
		FString Address;
		FString Firmware;
		/** -1 when the byte was not present. */
		int32 VolumeLevel = -1;
		int32 VolumeWiper = -1;

		/** HBS2 stream-lease tail classification (see EHapbeatPongLeaseTail). */
		EHapbeatPongLeaseTail LeaseTail = EHapbeatPongLeaseTail::Absent;
		// The fields below are meaningful only when LeaseTail == Valid.
		/** flags bit 0 AND a valid (nonzero boot id + ticket) lease. */
		bool bLeaseValid = false;
		/** flags bit 1. */
		bool bLeaseSuperseded = false;
		uint64 EchoedClientIncarnation = 0;
		FHapbeatStreamLeaseIdentity Lease;
		uint32 HighWaterTicket = 0;
	};

	/**
	 * Parse a PONG payload. Returns false if fewer than 16 bytes are available.
	 * When the available payload exceeds 16 bytes it is the device-extended form
	 * (device_name\0 + address\0 + firmware_version\0) and bExtended is set.
	 * AvailLen is FParsedPacket::PayloadAvail (NOT the header's declared length).
	 *
	 * After firmware_version the optional volume_level / volume_wiper bytes are
	 * read, then the remaining bytes are checked for the 32-byte HBS2 lease tail
	 * (stream-session-v2.md), exactly like Unity ParsePongExtendedInfo. A partial
	 * marker (1..3 trailing bytes that are a prefix of "HBS2") is reported as
	 * Malformed rather than Absent, so a truncated v2 reply can never be taken as
	 * a pre-v2 device.
	 */
	static bool ParsePong(const uint8* Payload, int32 AvailLen, FPongInfo& Out);

	/** Parse an ERROR payload: error_code(u16) + message\0. Returns false if < 2 bytes. */
	static bool ParseError(const uint8* Payload, int32 AvailLen, uint16& OutErrorCode, FString& OutMessage);
};
