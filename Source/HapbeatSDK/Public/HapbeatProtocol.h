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
	static constexpr int32 MaxAppNameLen = 16; // device OLED grid width

	static constexpr uint8 CmdPlay = 0x01;
	static constexpr uint8 CmdStop = 0x02;
	static constexpr uint8 CmdStopAll = 0x03;
	static constexpr uint8 CmdPing = 0x10;
	static constexpr uint8 CmdConnectStatus = 0x20;

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
};
