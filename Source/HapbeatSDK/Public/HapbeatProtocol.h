// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"

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

	/** PLAY: event_id\0 + target\0 + target_time(i64) + gain(f32). */
	static TArray<uint8> BuildPlay(uint16 Seq, const FString& EventId, const FString& Target, int64 TargetTimeUs, float Gain);
	/** STOP: event_id\0 + target\0. */
	static TArray<uint8> BuildStop(uint16 Seq, const FString& EventId, const FString& Target);
	/** STOP_ALL: target\0. */
	static TArray<uint8> BuildStopAll(uint16 Seq, const FString& Target);
	/** PING: timestamp(i64, microseconds). */
	static TArray<uint8> BuildPing(uint16 Seq, int64 TimestampUs);
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
	};

	/**
	 * Parse a PONG payload. Returns false if fewer than 16 bytes are available.
	 * When the available payload exceeds 16 bytes it is the device-extended form
	 * (device_name\0 + address\0 + firmware_version\0) and bExtended is set.
	 * AvailLen is FParsedPacket::PayloadAvail (NOT the header's declared length).
	 */
	static bool ParsePong(const uint8* Payload, int32 AvailLen, FPongInfo& Out);

	/** Parse an ERROR payload: error_code(u16) + message\0. Returns false if < 2 bytes. */
	static bool ParseError(const uint8* Payload, int32 AvailLen, uint16& OutErrorCode, FString& OutMessage);
};
