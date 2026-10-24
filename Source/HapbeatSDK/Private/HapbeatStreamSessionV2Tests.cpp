// Copyright (c) 2026 Hapbeat. MIT License.
//
// Stream Gen 2 + per-endpoint Stream Gen 1 fallback (contracts
// specs/stream-session.md, DEC-074 / DEC-075 / DEC-096). Every test here is
// in-memory: no socket is opened and nothing is sent on the network.
#include "HapbeatProtocol.h"
#include "HapbeatStreamer.h"
#include "HapbeatStreamLease.h"
#include "HapbeatStreamPlayback.h"
#include "HapbeatStreamRunnable.h"
#include "HapbeatStreamSessionContract.h"
#include "HapbeatSubsystem.h"
#include "Engine/GameInstance.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Misc/ScopeLock.h"

#if WITH_DEV_AUTOMATION_TESTS

// Named (not anonymous) so a unity build that merges this file with
// HapbeatStreamerTests.cpp cannot collide with that file's helpers; each test
// opens it with a function-local using-directive.
namespace HapbeatStreamSessionV2Test
{
	using HapbeatStreamSessionContract::EPongLeaseDecision;

	TArray<uint8> FromHex(const TCHAR* Hex)
	{
		TArray<uint8> Bytes;
		const FString Text(Hex);
		for (int32 Index = 0; Index + 1 < Text.Len(); Index += 2)
		{
			Bytes.Add(static_cast<uint8>(FParse::HexDigit(Text[Index]) * 16 + FParse::HexDigit(Text[Index + 1])));
		}
		return Bytes;
	}

	void AppendLe(TArray<uint8>& Bytes, uint64 Value, int32 Width)
	{
		for (int32 Index = 0; Index < Width; ++Index)
		{
			Bytes.Add(static_cast<uint8>((Value >> (8 * Index)) & 0xFF));
		}
	}

	void AppendCString(TArray<uint8>& Bytes, const char* Text)
	{
		while (*Text != '\0')
		{
			Bytes.Add(static_cast<uint8>(*Text++));
		}
		Bytes.Add(0);
	}

	uint64 ReadLe(const TArray<uint8>& Bytes, int32 Offset, int32 Width)
	{
		uint64 Value = 0;
		for (int32 Index = 0; Index < Width; ++Index)
		{
			Value |= static_cast<uint64>(Bytes[Offset + Index]) << (8 * Index);
		}
		return Value;
	}

	/** Ordinary device PONG payload up to and including volume_level / volume_wiper. */
	TArray<uint8> MakePongPayload(int64 TimestampUs, const char* Address, bool bVolumeBytes = true)
	{
		TArray<uint8> Payload;
		AppendLe(Payload, static_cast<uint64>(TimestampUs), 8);
		AppendLe(Payload, 0, 8); // server_time
		AppendCString(Payload, "Hapbeat");
		AppendCString(Payload, Address);
		AppendCString(Payload, "1.2.3");
		if (bVolumeBytes)
		{
			Payload.Add(5);  // volume_level
			Payload.Add(7);  // volume_wiper
		}
		return Payload;
	}

	/** Append an HBS2 tail. Defaults produce a valid 32-byte tail. */
	void AppendLeaseTail(TArray<uint8>& Payload, uint8 Flags, uint64 Incarnation, uint64 BootId, uint32 Ticket,
		uint32 HighWater = 0, uint8 Version = 1, uint16 Reserved = 0)
	{
		Payload.Add('H');
		Payload.Add('B');
		Payload.Add('S');
		Payload.Add('2');
		Payload.Add(Version);
		Payload.Add(Flags);
		AppendLe(Payload, Reserved, 2);
		AppendLe(Payload, Incarnation, 8);
		AppendLe(Payload, BootId, 8);
		AppendLe(Payload, Ticket, 4);
		AppendLe(Payload, HighWater, 4);
	}

	FHapbeatProtocol::FPongInfo Parse(const TArray<uint8>& Payload)
	{
		FHapbeatProtocol::FPongInfo Info;
		FHapbeatProtocol::ParsePong(Payload.GetData(), Payload.Num(), Info);
		return Info;
	}

	FHapbeatProtocol::FPongInfo LegacyPong(int64 TimestampUs, const char* Address)
	{
		return Parse(MakePongPayload(TimestampUs, Address));
	}

	FHapbeatProtocol::FPongInfo V2Pong(int64 TimestampUs, const char* Address, uint64 Incarnation,
		uint64 BootId, uint32 Ticket, uint8 Flags = 0x01)
	{
		TArray<uint8> Payload = MakePongPayload(TimestampUs, Address);
		AppendLeaseTail(Payload, Flags, Incarnation, BootId, Ticket);
		return Parse(Payload);
	}

	/** A tail of a later major (extension_version 2): Ignored by the [1.x] rule. */
	FHapbeatProtocol::FPongInfo IgnoredPong(int64 TimestampUs, const char* Address, uint64 Incarnation)
	{
		TArray<uint8> Payload = MakePongPayload(TimestampUs, Address);
		AppendLeaseTail(Payload, 0x01, Incarnation, 0x11, 3, 0, /*Version=*/2);
		return Parse(Payload);
	}

	/**
	 * A pending lease request (extended PING). TargetIp empty = not bound to one
	 * device, which lets one request stand for the unicast PINGs to several
	 * devices in the subsystem tests; the IP binding itself is covered in
	 * Hapbeat.Streaming.SessionV2.Classification.
	 */
	FHapbeatPendingPing Pending(int64 TimestampUs, uint64 Incarnation, const TCHAR* TargetIp = TEXT(""))
	{
		FHapbeatPendingPing Result;
		// Sent "now", so the subsystem's own pruning (15 s) keeps it.
		Result.SentMonotonicUs = static_cast<int64>(FPlatformTime::Seconds() * 1'000'000.0);
		Result.TimestampUs = TimestampUs;
		Result.ClientIncarnation = Incarnation;
		Result.bLeaseRequest = true;
		Result.TargetIp = TargetIp;
		return Result;
	}

	/** A pending 8-byte discovery PING (broadcast, never a lease request). */
	FHapbeatPendingPing DiscoveryPending(int64 TimestampUs, uint64 Incarnation)
	{
		FHapbeatPendingPing Result = Pending(TimestampUs, Incarnation);
		Result.bLeaseRequest = false;
		return Result;
	}

	/** Thread-safe record of every packet any logging runner sent, tagged by endpoint address. */
	struct FStreamPacketLog
	{
		struct FEntry
		{
			FString Endpoint;
			TArray<uint8> Packet;
		};

		void Add(const FString& Endpoint, const TArray<uint8>& Packet)
		{
			FScopeLock Guard(&Lock);
			FEntry& Entry = Entries.AddDefaulted_GetRef();
			Entry.Endpoint = Endpoint;
			Entry.Packet = Packet;
		}

		TArray<FEntry> Snapshot(const FString& Endpoint) const
		{
			FScopeLock Guard(&Lock);
			TArray<FEntry> Result;
			for (const FEntry& Entry : Entries)
			{
				if (Entry.Endpoint == Endpoint)
				{
					Result.Add(Entry);
				}
			}
			return Result;
		}

		int32 Count(const FString& Endpoint, uint8 Cmd) const
		{
			int32 Result = 0;
			for (const FEntry& Entry : Snapshot(Endpoint))
			{
				if (Entry.Packet.Num() >= FHapbeatProtocol::HeaderSize && Entry.Packet[3] == Cmd)
				{
					++Result;
				}
			}
			return Result;
		}

		mutable FCriticalSection Lock;
		TArray<FEntry> Entries;
	};

	TArray<FString> LoggingEndpoint()
	{
		TArray<FString> Result;
		Result.Add(TEXT("192.0.2.99"));
		return Result;
	}

	/** Real FHapbeatStreamRunnable whose packet boundary writes into a log instead of a socket. */
	class FLoggingStreamRunnable final : public FHapbeatStreamRunnable
	{
	public:
		FLoggingStreamRunnable(const FGuid& SourceId, TArray<uint8>&& Pcm16, const FString& Target,
			TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> Mirror, EHapbeatStreamWireFormat Format,
			const FHapbeatStreamSessionIdentity& Identity, TSharedRef<FStreamPacketLog, ESPMode::ThreadSafe> InLog)
			: FHapbeatStreamRunnable(SourceId, MoveTemp(Pcm16), 16000, 2, Target, Mirror,
				[]() { return static_cast<uint16>(1); }, nullptr, 7700, LoggingEndpoint(), 0.05f, Format, Identity)
			, Log(InLog)
			, Endpoint(Target)
		{
		}

	protected:
		virtual void SendRaw(const TArray<uint8>& Packet) override
		{
			Log->Add(Endpoint, Packet);
		}

	private:
		TSharedRef<FStreamPacketLog, ESPMode::ThreadSafe> Log;
		FString Endpoint;
	};

	TArray<uint8> MakeSessionTestPcm(int16 Sample, int32 Frames)
	{
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(Frames * 4);
		const uint16 Value = static_cast<uint16>(Sample);
		for (int32 Index = 0; Index < Frames * 2; ++Index)
		{
			Bytes[Index * 2] = static_cast<uint8>(Value & 0xFF);
			Bytes[Index * 2 + 1] = static_cast<uint8>((Value >> 8) & 0xFF);
		}
		return Bytes;
	}

	bool WaitForCount(const FStreamPacketLog& Log, const FString& Endpoint, uint8 Cmd, int32 AtLeast)
	{
		const double Deadline = FPlatformTime::Seconds() + 2.0;
		while (Log.Count(Endpoint, Cmd) < AtLeast && FPlatformTime::Seconds() < Deadline)
		{
			FPlatformProcess::Yield();
		}
		return Log.Count(Endpoint, Cmd) >= AtLeast;
	}

	/** Envelope of a Stream Gen 2 packet (header version 2); invalid identity for anything else. */
	FHapbeatStreamSessionIdentity EnvelopeOf(const TArray<uint8>& Packet)
	{
		if (Packet.Num() < FHapbeatProtocol::HeaderSize + FHapbeatProtocol::StreamSessionEnvelopeSize
			|| Packet[2] != FHapbeatProtocol::StreamProtocolVersion)
		{
			return FHapbeatStreamSessionIdentity();
		}
		return FHapbeatStreamSessionIdentity(
			FHapbeatStreamLeaseIdentity(ReadLe(Packet, 8, 8), static_cast<uint32>(ReadLe(Packet, 16, 4))),
			static_cast<uint32>(ReadLe(Packet, 20, 4)));
	}
}

// ---------------------------------------------------------------------------
// Wire bytes: legacy regression + v2 contract fixture
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatStreamSessionV2WireTest,
	"Hapbeat.Streaming.SessionV2.WireFormat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatStreamSessionV2WireTest::RunTest(const FString& Parameters)
{
	using namespace HapbeatStreamSessionV2Test;
	const uint16 Seq = 0x1234;
	const uint8 Audio[4] = { 0x10, 0x00, 0xF0, 0xFF }; // PCM16 LE samples 16, -16 (contract fixture)

	// Legacy (pre-v2) builders must stay byte-identical to message-format.md
	// "v1 stream": header version 1, no envelope, END with an empty payload.
	TestEqual(TEXT("legacy BEGIN bytes"),
		FHapbeatProtocol::BuildStreamBegin(Seq, 16000, 2, FHapbeatProtocol::AudioFormatPcm16, 0, 1.0f, TEXT("*")),
		FromHex(TEXT("424801303412" "0e00" "803e" "02" "00" "00000000" "0000803f" "2a00")));
	TestEqual(TEXT("legacy BEGIN without target omits the field"),
		FHapbeatProtocol::BuildStreamBegin(Seq, 16000, 2, FHapbeatProtocol::AudioFormatPcm16, 0, 1.0f),
		FromHex(TEXT("424801303412" "0c00" "803e" "02" "00" "00000000" "0000803f")));
	TestEqual(TEXT("legacy DATA bytes"),
		FHapbeatProtocol::BuildStreamData(Seq, 0, Audio, 4),
		FromHex(TEXT("424801313412" "0800" "00000000" "1000f0ff")));
	TestEqual(TEXT("legacy END bytes"),
		FHapbeatProtocol::BuildStreamEnd(Seq), FromHex(TEXT("4248013234120000")));

	// v2: repos-core/hapbeat-contracts/fixtures/stream-session-v2.json
	// (boot 0102030405060708, ticket 7, generation 42, seq 4660).
	const FHapbeatStreamSessionIdentity Identity(FHapbeatStreamLeaseIdentity(0x0102030405060708ULL, 7), 42);
	TestEqual(TEXT("v2 BEGIN matches the contract fixture"),
		FHapbeatProtocol::BuildStreamBeginV2(Seq, Identity, 16000, 2, FHapbeatProtocol::AudioFormatPcm16, 0, 1.0f, TEXT("*")),
		FromHex(TEXT("4248023034121e000807060504030201070000002a000000803e0200000000000000803f2a00")));
	TestEqual(TEXT("v2 DATA matches the contract fixture"),
		FHapbeatProtocol::BuildStreamDataV2(Seq, Identity, 0, Audio, 4),
		FromHex(TEXT("42480231341218000807060504030201070000002a000000000000001000f0ff")));
	TestEqual(TEXT("v2 END matches the contract fixture"),
		FHapbeatProtocol::BuildStreamEndV2(Seq, Identity),
		FromHex(TEXT("42480232341210000807060504030201070000002a000000")));

	// Extended PING stays header version 1 with a 16-byte payload; the legacy
	// 8-byte PING is unchanged (old firmware accepts both).
	const TArray<uint8> ExtendedPing = FHapbeatProtocol::BuildPing(5, 0x0102030405060708LL, 0x1122334455667788ULL);
	TestEqual(TEXT("extended PING bytes"), ExtendedPing,
		FromHex(TEXT("424801100500" "1000" "0807060504030201" "8877665544332211")));
	TestEqual(TEXT("plain PING stays 8 bytes"), FHapbeatProtocol::BuildPing(5, 1).Num(), FHapbeatProtocol::HeaderSize + 8);

	// The streamer keeps one format for every packet of a session.
	for (const EHapbeatStreamWireFormat Format : { EHapbeatStreamWireFormat::LegacyV1, EHapbeatStreamWireFormat::SessionV2 })
	{
		const bool bV2 = Format == EHapbeatStreamWireFormat::SessionV2;
		TArray<TArray<uint8>> Packets;
		auto Mirror = MakeShared<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>();
		FHapbeatStreamer Streamer(16000, 2, TEXT("player_1"), Mirror,
			[]() { return static_cast<uint16>(9); },
			[&Packets](const TArray<uint8>& Packet) { Packets.Add(Packet); },
			0.05f, Format, bV2 ? Identity : FHapbeatStreamSessionIdentity());
		Streamer.AddSource(FGuid::NewGuid(), MakeSessionTestPcm(1000, 400), Mirror);
		Streamer.Start(0.0);
		Streamer.Tick(0.0);
		Streamer.SendEnd();
		TestTrue(TEXT("streamer produced BEGIN, DATA and END"), Packets.Num() >= 3);
		const uint8 ExpectedVersion = bV2 ? FHapbeatProtocol::StreamProtocolVersion : FHapbeatProtocol::Version;
		bool bAllVersion = true;
		bool bAllEnvelope = true;
		for (const TArray<uint8>& Packet : Packets)
		{
			bAllVersion &= Packet[2] == ExpectedVersion;
			bAllEnvelope &= !bV2 || EnvelopeOf(Packet) == Identity;
		}
		TestTrue(bV2 ? TEXT("v2 session: every packet is header version 2") : TEXT("legacy session: every packet is header version 1"), bAllVersion);
		TestTrue(TEXT("v2 session: every packet carries the session envelope"), bAllEnvelope);
		const TArray<uint8>& Begin = Packets[0];
		const TArray<uint8>& End = Packets.Last();
		TestEqual(TEXT("first packet is BEGIN"), Begin[3], FHapbeatProtocol::CmdStreamBegin);
		TestEqual(TEXT("BEGIN payload length"), static_cast<int32>(ReadLe(Begin, 6, 2)),
			(bV2 ? FHapbeatProtocol::StreamSessionEnvelopeSize : 0) + 12 + 9 /* "player_1\0" */);
		TestEqual(TEXT("last packet is END"), End[3], FHapbeatProtocol::CmdStreamEnd);
		TestEqual(TEXT("END payload is the envelope (v2) or empty (legacy)"), static_cast<int32>(ReadLe(End, 6, 2)),
			bV2 ? FHapbeatProtocol::StreamSessionEnvelopeSize : 0);
		TestEqual(TEXT("second packet is DATA"), Packets[1][3], FHapbeatProtocol::CmdStreamData);
		TestEqual(TEXT("DATA byte offset starts at 0"),
			static_cast<uint32>(ReadLe(Packets[1], FHapbeatProtocol::HeaderSize + (bV2 ? 16 : 0), 4)), 0u);
	}
	return true;
}

// ---------------------------------------------------------------------------
// PONG HBS2 tail: Absent / Valid / Ignored ([1.x] rule)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatStreamSessionV2PongTailTest,
	"Hapbeat.Streaming.SessionV2.PongLeaseTail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatStreamSessionV2PongTailTest::RunTest(const FString& Parameters)
{
	using namespace HapbeatStreamSessionV2Test;
	const FHapbeatProtocol::FPongInfo Plain = LegacyPong(1000, "player_1/pos_l_arm");
	TestEqual(TEXT("ordinary PONG ending at volume_wiper has no tail"), Plain.LeaseTail, EHapbeatPongLeaseTail::Absent);
	TestEqual(TEXT("address still parsed"), Plain.Address, FString(TEXT("player_1/pos_l_arm")));
	TestEqual(TEXT("volume_level parsed"), Plain.VolumeLevel, 5);
	TestEqual(TEXT("volume_wiper parsed"), Plain.VolumeWiper, 7);

	TestEqual(TEXT("PONG ending before volume_wiper has no tail"),
		Parse(MakePongPayload(1000, "player_1", /*bVolumeBytes=*/false)).LeaseTail, EHapbeatPongLeaseTail::Absent);
	TArray<uint8> Base16;
	AppendLe(Base16, 1000, 8);
	AppendLe(Base16, 0, 8);
	TestEqual(TEXT("bridge-form 16-byte PONG has no tail"), Parse(Base16).LeaseTail, EHapbeatPongLeaseTail::Absent);

	TArray<uint8> OtherBytes = MakePongPayload(1000, "player_1");
	OtherBytes.Append(FromHex(TEXT("0102030405060708")));
	TestEqual(TEXT("non-HBS2 bytes after volume_wiper are ignored, not Stream Gen 1"),
		Parse(OtherBytes).LeaseTail, EHapbeatPongLeaseTail::Ignored);

	TArray<uint8> ValidPayload = MakePongPayload(1000, "player_1");
	AppendLeaseTail(ValidPayload, 0x01, 0xA1A2A3A4A5A6A7A8ULL, 0x0102030405060708ULL, 7, 6);
	const FHapbeatProtocol::FPongInfo Valid = Parse(ValidPayload);
	TestEqual(TEXT("32-byte HBS2 tail is valid"), Valid.LeaseTail, EHapbeatPongLeaseTail::Valid);
	TestEqual(TEXT("echoed incarnation"), Valid.EchoedClientIncarnation, 0xA1A2A3A4A5A6A7A8ULL);
	TestEqual(TEXT("boot id"), Valid.Lease.DeviceBootId, 0x0102030405060708ULL);
	TestEqual(TEXT("ticket"), Valid.Lease.LeaseTicket, 7u);
	TestEqual(TEXT("high water"), Valid.HighWaterTicket, 6u);
	TestTrue(TEXT("lease_valid flag"), Valid.bLeaseValid);
	TestFalse(TEXT("not superseded"), Valid.bLeaseSuperseded);
	TestFalse(TEXT("not re-issued"), Valid.bLeaseReissued);

	TArray<uint8> ZeroBoot = MakePongPayload(1000, "player_1");
	AppendLeaseTail(ZeroBoot, 0x01, 1, 0, 7);
	TestFalse(TEXT("lease_valid with a zero boot id is not a valid lease"), Parse(ZeroBoot).bLeaseValid);

	struct FTailCase
	{
		const TCHAR* Name;
		TArray<uint8> Payload;
		EHapbeatPongLeaseTail Expected;
	};
	TArray<FTailCase> Cases;
	{
		TArray<uint8> P = MakePongPayload(1000, "player_1");
		AppendLeaseTail(P, 0x01, 1, 2, 3, 0, /*Version=*/2);
		Cases.Add({ TEXT("another extension version is ignored"), P, EHapbeatPongLeaseTail::Ignored });
	}
	{
		TArray<uint8> P = MakePongPayload(1000, "player_1");
		AppendLeaseTail(P, 0x09, 1, 2, 3);
		Cases.Add({ TEXT("an unknown flag bit (3) is ignored, the tail stays valid"), P, EHapbeatPongLeaseTail::Valid });
	}
	{
		TArray<uint8> P = MakePongPayload(1000, "player_1");
		AppendLeaseTail(P, 0x01, 1, 2, 3, 0, 1, /*Reserved=*/1);
		Cases.Add({ TEXT("a nonzero reserved field is ignored, the tail stays valid"), P, EHapbeatPongLeaseTail::Valid });
	}
	{
		TArray<uint8> P = MakePongPayload(1000, "player_1");
		AppendLeaseTail(P, 0x01, 1, 2, 3);
		P.Pop();
		Cases.Add({ TEXT("a 31-byte tail is ignored"), P, EHapbeatPongLeaseTail::Ignored });
	}
	{
		TArray<uint8> P = MakePongPayload(1000, "player_1");
		AppendLeaseTail(P, 0x01, 1, 2, 3);
		P.Add(0);
		Cases.Add({ TEXT("a 33-byte tail is read from its first 32 bytes"), P, EHapbeatPongLeaseTail::Valid });
	}
	{
		TArray<uint8> P = MakePongPayload(1000, "player_1");
		P.Add('H');
		P.Add('B');
		Cases.Add({ TEXT("a truncated marker prefix is ignored"), P, EHapbeatPongLeaseTail::Ignored });
	}
	for (const FTailCase& Case : Cases)
	{
		TestEqual(Case.Name, Parse(Case.Payload).LeaseTail, Case.Expected);
	}

	TArray<uint8> Reissued = MakePongPayload(1000, "player_1");
	AppendLeaseTail(Reissued, 0x05, 1, 2, 3); // bit 0 lease_valid + bit 2 lease_reissued
	const FHapbeatProtocol::FPongInfo ReissuedInfo = Parse(Reissued);
	TestEqual(TEXT("flags 0x05 is a valid tail"), ReissuedInfo.LeaseTail, EHapbeatPongLeaseTail::Valid);
	TestTrue(TEXT("... with lease_reissued"), ReissuedInfo.bLeaseReissued);
	TestTrue(TEXT("... and lease_valid"), ReissuedInfo.bLeaseValid);
	return true;
}

// ---------------------------------------------------------------------------
// Pure classification (HapbeatStreamSessionContract::EvaluatePongLease)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatStreamSessionV2ClassificationTest,
	"Hapbeat.Streaming.SessionV2.Classification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatStreamSessionV2ClassificationTest::RunTest(const FString& Parameters)
{
	using namespace HapbeatStreamSessionV2Test;
	using HapbeatStreamSessionContract::EvaluatePongLease;
	const uint64 Inc = 0xAAAA0000BBBB1111ULL;
	const char* Address = "player_1/pos_l_arm";
	const FString Ip = TEXT("192.0.2.30");
	TMap<uint16, FHapbeatPendingPing> Pings;
	Pings.Add(10, Pending(1000, Inc, TEXT("192.0.2.30")));
	Pings.Add(11, Pending(2000, Inc, TEXT("192.0.2.30")));
	Pings.Add(12, Pending(3000, 0x1234ULL, TEXT("192.0.2.30"))); // issued by a previous incarnation
	Pings.Add(13, DiscoveryPending(4000, Inc));                  // 8-byte discovery PING (broadcast)
	FHapbeatStreamEndpointLeaseState Next;

	TestEqual(TEXT("matched HBS2 reply is accepted"),
		EvaluatePongLease(V2Pong(1000, Address, Inc, 0x11, 3), 10, Pings, Inc, nullptr, Next, Ip), EPongLeaseDecision::Accept);
	TestEqual(TEXT("... as v2"), Next.Class, EHapbeatStreamEndpointClass::V2);
	TestTrue(TEXT("... with a usable lease"), Next.IsUsableV2Lease(Inc));
	TestEqual(TEXT("... recording the accepted request"), Next.LastAcceptedTimestampUs, 1000LL);
	const FHapbeatStreamEndpointLeaseState AcceptedV2 = Next;

	TestEqual(TEXT("matched reply without a tail is accepted"),
		EvaluatePongLease(LegacyPong(1000, Address), 10, Pings, Inc, nullptr, Next, Ip), EPongLeaseDecision::Accept);
	TestEqual(TEXT("... as legacy"), Next.Class, EHapbeatStreamEndpointClass::Legacy);
	TestTrue(TEXT("legacy endpoints are streamable"), Next.IsStreamable(Inc));

	FHapbeatStreamEndpointLeaseState Untouched;
	TestEqual(TEXT("a matched reply with an ignored tail is reported for the one-time warning"),
		EvaluatePongLease(IgnoredPong(1000, Address, Inc), 10, Pings, Inc, nullptr, Untouched, Ip), EPongLeaseDecision::IgnoredMatched);
	TestEqual(TEXT("an ignored tail leaves the class unknown"), Untouched.Class, EHapbeatStreamEndpointClass::Unknown);
	TestFalse(TEXT("unknown endpoints defer"), Untouched.IsStreamable(Inc));
	TestEqual(TEXT("an unmatched reply with an ignored tail is plain discovery"),
		EvaluatePongLease(IgnoredPong(1000, Address, Inc), 99, Pings, Inc, nullptr, Untouched, Ip), EPongLeaseDecision::NoChange);

	// [1.x] Every firmware answers the 8-byte discovery PING without a tail:
	// that reply must never make a device Stream Gen 1.
	TestEqual(TEXT("a tail-less reply to an 8-byte discovery PING never selects Stream Gen 1"),
		EvaluatePongLease(LegacyPong(4000, Address), 13, Pings, Inc, nullptr, Next, Ip), EPongLeaseDecision::NoChange);
	// Lease requests are unicast: a reply from another IPv4 address is not theirs.
	TestEqual(TEXT("a tail-less reply from another IP to a unicast lease request is not matched"),
		EvaluatePongLease(LegacyPong(1000, Address), 10, Pings, Inc, nullptr, Next, TEXT("192.0.2.31")), EPongLeaseDecision::NoChange);
	TestEqual(TEXT("an HBS2 reply from another IP to a unicast lease request is discarded"),
		EvaluatePongLease(V2Pong(1000, Address, Inc, 0x11, 3), 10, Pings, Inc, nullptr, Next, TEXT("192.0.2.31")), EPongLeaseDecision::Discard);

	TestEqual(TEXT("unsolicited (timestamp 0) PONG never selects legacy"),
		EvaluatePongLease(LegacyPong(0, Address), 10, Pings, Inc, nullptr, Next, Ip), EPongLeaseDecision::NoChange);
	TestEqual(TEXT("reply to an unknown seq never selects legacy"),
		EvaluatePongLease(LegacyPong(1000, Address), 99, Pings, Inc, nullptr, Next, Ip), EPongLeaseDecision::NoChange);
	TestEqual(TEXT("reply echoing another request's timestamp never selects legacy"),
		EvaluatePongLease(LegacyPong(2000, Address), 10, Pings, Inc, nullptr, Next, Ip), EPongLeaseDecision::NoChange);
	TestEqual(TEXT("reply to a previous incarnation's request never selects legacy"),
		EvaluatePongLease(LegacyPong(3000, Address), 12, Pings, Inc, nullptr, Next, Ip), EPongLeaseDecision::NoChange);
	TestEqual(TEXT("late tail-less reply cannot demote an accepted v2 endpoint"),
		EvaluatePongLease(LegacyPong(1000, Address), 10, Pings, Inc, &AcceptedV2, Next, Ip), EPongLeaseDecision::NoChange);
	TestEqual(TEXT("newer matched tail-less reply demotes v2 to legacy (firmware rollback)"),
		EvaluatePongLease(LegacyPong(2000, Address), 11, Pings, Inc, &AcceptedV2, Next, Ip), EPongLeaseDecision::Accept);
	TestEqual(TEXT("... to legacy"), Next.Class, EHapbeatStreamEndpointClass::Legacy);

	TestEqual(TEXT("HBS2 echoing another incarnation is discarded"),
		EvaluatePongLease(V2Pong(1000, Address, Inc + 1, 0x11, 3), 10, Pings, Inc, nullptr, Next, Ip), EPongLeaseDecision::Discard);
	TestEqual(TEXT("unsolicited HBS2 is discarded"),
		EvaluatePongLease(V2Pong(0, Address, Inc, 0x11, 3), 10, Pings, Inc, nullptr, Next, Ip), EPongLeaseDecision::Discard);
	TestEqual(TEXT("unmatched HBS2 is discarded"),
		EvaluatePongLease(V2Pong(1000, Address, Inc, 0x11, 3), 99, Pings, Inc, nullptr, Next, Ip), EPongLeaseDecision::Discard);
	FHapbeatStreamEndpointLeaseState NewerV2 = AcceptedV2;
	NewerV2.LastAcceptedTimestampUs = 2000;
	TestEqual(TEXT("late old-boot HBS2 cannot revert a newer lease"),
		EvaluatePongLease(V2Pong(1000, Address, Inc, 0x99, 1), 10, Pings, Inc, &NewerV2, Next, Ip), EPongLeaseDecision::Discard);

	TestEqual(TEXT("lease_valid=0 is still v2"),
		EvaluatePongLease(V2Pong(1000, Address, Inc, 0x11, 0, /*Flags=*/0x00), 10, Pings, Inc, nullptr, Next, Ip), EPongLeaseDecision::Accept);
	TestEqual(TEXT("... class v2, never legacy"), Next.Class, EHapbeatStreamEndpointClass::V2);
	TestFalse(TEXT("... and defers"), Next.IsStreamable(Inc));

	TestEqual(TEXT("superseded reply is accepted"),
		EvaluatePongLease(V2Pong(1000, Address, Inc, 0x11, 3, 0x03), 10, Pings, Inc, nullptr, Next, Ip), EPongLeaseDecision::Accept);
	TestFalse(TEXT("superseded v2 defers"), Next.IsStreamable(Inc));
	const FHapbeatStreamEndpointLeaseState Superseded = Next;
	TestEqual(TEXT("later discovery reply is accepted"),
		EvaluatePongLease(V2Pong(2000, Address, Inc, 0x11, 4), 11, Pings, Inc, &Superseded, Next, Ip), EPongLeaseDecision::Accept);
	TestTrue(TEXT("... but does not take the lease back automatically"), Next.bLeaseSuperseded);
	TestFalse(TEXT("... and keeps deferring"), Next.IsStreamable(Inc));
	TestEqual(TEXT("... and keeps the superseded lease"), Next.Lease.LeaseTicket, 3u);
	return true;
}

// ---------------------------------------------------------------------------
// Subsystem: per-endpoint classes, guard scope, class change, ignore rules
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatStreamLegacyFallbackSubsystemTest,
	"Hapbeat.Streaming.SessionV2.LegacyFallbackSubsystem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatStreamLegacyFallbackSubsystemTest::RunTest(const FString& Parameters)
{
	using namespace HapbeatStreamSessionV2Test;
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UHapbeatSubsystem* Sub = NewObject<UHapbeatSubsystem>(GameInstance);
	Sub->bSuppressStreamDiscoveryForAutomationTest = true;
	const uint64 Inc = 0x0123456789ABCDEFULL;
	Sub->ClientIncarnation = Inc;
	TSharedRef<FStreamPacketLog, ESPMode::ThreadSafe> Log = MakeShared<FStreamPacketLog, ESPMode::ThreadSafe>();
	Sub->StreamRunnableFactoryForAutomationTest =
		[Log](const FGuid& SourceId, TArray<uint8>&& Pcm16, const FString& Target,
			TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> Mirror, EHapbeatStreamWireFormat Format,
			const FHapbeatStreamSessionIdentity& Identity) -> FHapbeatStreamRunnable*
		{
			return new FLoggingStreamRunnable(SourceId, MoveTemp(Pcm16), Target, Mirror, Format, Identity, Log);
		};

	const FString AddrA = TEXT("player_1/pos_l_arm"); // pre-v2 device
	const FString AddrB = TEXT("player_1/pos_r_arm"); // v2 device
	const FString KeyA = TEXT("192.0.2.80:7700|player_1/pos_l_arm");
	const FString KeyB = TEXT("192.0.2.81:7700|player_1/pos_r_arm");
	const uint64 BootB = 0x00000000000B0071ULL;
	int64 NextTs = 5000;
	uint16 NextSeq = 100;
	// One pending lease request answered by both devices (not bound to an IP,
	// see Pending()): the entry must survive the first reply so the second can
	// correlate too.
	auto IssuePing = [&]() -> TPair<uint16, int64>
	{
		const TPair<uint16, int64> Issued(NextSeq++, NextTs);
		NextTs += 1000;
		Sub->PendingPings.Add(Issued.Key, Pending(Issued.Value, Inc));
		return Issued;
	};
	auto Deliver = [&](const TCHAR* Ip, uint16 Seq, const FHapbeatProtocol::FPongInfo& Pong)
	{
		Sub->HandlePongGameThread(Ip, 7700, Seq, static_cast<int64>(FPlatformTime::Seconds() * 1'000'000.0), Pong);
	};
	auto ClassOf = [&](const FString& Key)
	{
		const UHapbeatSubsystem::FStreamEndpoint* Endpoint = Sub->StreamEndpoints.Find(Key);
		return Endpoint != nullptr ? Endpoint->LeaseState.Class : EHapbeatStreamEndpointClass::Unknown;
	};

	// A looping source for every endpoint (empty target matches all).
	UHapbeatStreamPlayback* Playback = NewObject<UHapbeatStreamPlayback>(Sub);
	Playback->Init(1.0f, 1.0f);
	Playback->SetLoop(true);
	UHapbeatSubsystem::FStreamSource& Source = Sub->StreamSources.Add(Playback->Id);
	Source.CanonicalPcm16 = MakeSessionTestPcm(1000, 160);
	Source.Playback = Playback;
	Sub->ActivePlaybacks.Add(Playback);

	// --- Unknown endpoints defer until a matched reply classifies them.
	Deliver(TEXT("192.0.2.80"), 0, LegacyPong(0, "player_1/pos_l_arm"));
	TestTrue(TEXT("unsolicited PONG registers the endpoint"), Sub->StreamEndpoints.Contains(KeyA));
	TestEqual(TEXT("unsolicited PONG leaves it unknown"), ClassOf(KeyA), EHapbeatStreamEndpointClass::Unknown);
	TestEqual(TEXT("no session for an unknown endpoint"), Sub->StreamSessions.Num(), 0);
	TestEqual(TEXT("the source is deferred"), Playback->GetStatus(), EHapbeatStreamPlaybackStatus::Deferred);

	const TPair<uint16, int64> First = IssuePing();
	Deliver(TEXT("192.0.2.80"), First.Key, LegacyPong(First.Value, "player_1/pos_l_arm"));
	Deliver(TEXT("192.0.2.81"), First.Key, V2Pong(First.Value, "player_1/pos_r_arm", Inc, BootB, 3));
	TestEqual(TEXT("A is legacy"), ClassOf(KeyA), EHapbeatStreamEndpointClass::Legacy);
	TestEqual(TEXT("B is v2 from the same request"), ClassOf(KeyB), EHapbeatStreamEndpointClass::V2);
	TestTrue(TEXT("legacy and v2 endpoints stream concurrently"),
		Sub->StreamSessions.Contains(KeyA) && Sub->StreamSessions.Contains(KeyB));
	if (!Sub->StreamSessions.Contains(KeyA) || !Sub->StreamSessions.Contains(KeyB))
	{
		Sub->StopStream();
		return false;
	}
	TestEqual(TEXT("A session is legacy format"), Sub->StreamSessions[KeyA].Format, EHapbeatStreamWireFormat::LegacyV1);
	TestEqual(TEXT("B session is v2 format"), Sub->StreamSessions[KeyB].Format, EHapbeatStreamWireFormat::SessionV2);
	TestEqual(TEXT("B session starts at generation 1"), Sub->StreamSessions[KeyB].Identity.Generation, 1u);
	TestTrue(TEXT("A BEGIN sent"), WaitForCount(*Log, AddrA, FHapbeatProtocol::CmdStreamBegin, 1));
	TestTrue(TEXT("B BEGIN sent"), WaitForCount(*Log, AddrB, FHapbeatProtocol::CmdStreamBegin, 1));

	// --- END then restart: v2 immediately, legacy only after 300 ms, per endpoint.
	Sub->StopStreamSession(KeyA);
	Sub->StopStreamSession(KeyB);
	Sub->ReconcileStreamSources();
	TestTrue(TEXT("v2 endpoint restarts immediately after END"), Sub->StreamSessions.Contains(KeyB));
	TestFalse(TEXT("legacy endpoint waits for its 300 ms guard"), Sub->StreamSessions.Contains(KeyA));
	if (Sub->StreamSessions.Contains(KeyB))
	{
		TestEqual(TEXT("v2 restart uses the next generation"), Sub->StreamSessions[KeyB].Identity.Generation, 2u);
	}
	TestFalse(TEXT("a v2 END arms no guard"), Sub->StreamSessionEndedAt.Contains(KeyB));
	TestTrue(TEXT("a legacy END arms the guard"), Sub->StreamSessionEndedAt.Contains(KeyA));
	// Age the legacy END past 300 ms instead of sleeping.
	Sub->StreamSessionEndedAt[KeyA] -= 0.31;
	for (TPair<FString, double>& RouteEnd : Sub->StreamRouteEndedAt)
	{
		RouteEnd.Value -= 0.31;
	}
	Sub->ReconcileStreamSources();
	TestTrue(TEXT("legacy endpoint restarts once 300 ms have passed"), Sub->StreamSessions.Contains(KeyA));
	TestTrue(TEXT("A second BEGIN sent"), WaitForCount(*Log, AddrA, FHapbeatProtocol::CmdStreamBegin, 2));
	TestTrue(TEXT("B second BEGIN sent"), WaitForCount(*Log, AddrB, FHapbeatProtocol::CmdStreamBegin, 2));

	{
		bool bAllV1 = true;
		for (const FStreamPacketLog::FEntry& Entry : Log->Snapshot(AddrA))
		{
			bAllV1 &= Entry.Packet[2] == FHapbeatProtocol::Version;
			if (Entry.Packet[3] == FHapbeatProtocol::CmdStreamBegin)
			{
				TestEqual(TEXT("legacy BEGIN has no envelope"), static_cast<int32>(ReadLe(Entry.Packet, 6, 2)), 12 + AddrA.Len() + 1);
			}
			if (Entry.Packet[3] == FHapbeatProtocol::CmdStreamEnd)
			{
				TestEqual(TEXT("legacy END payload is empty"), static_cast<int32>(ReadLe(Entry.Packet, 6, 2)), 0);
			}
		}
		TestTrue(TEXT("legacy endpoint only ever receives header version 1"), bAllV1);
		bool bAllV2 = true;
		TArray<uint32> BeginGenerations;
		for (const FStreamPacketLog::FEntry& Entry : Log->Snapshot(AddrB))
		{
			bAllV2 &= Entry.Packet[2] == FHapbeatProtocol::StreamProtocolVersion
				&& EnvelopeOf(Entry.Packet).GetLease() == FHapbeatStreamLeaseIdentity(BootB, 3);
			// The same BEGIN is repeated (Sender requirements 2): count each generation once.
			if (Entry.Packet[3] == FHapbeatProtocol::CmdStreamBegin
				&& (BeginGenerations.Num() == 0 || BeginGenerations.Last() != EnvelopeOf(Entry.Packet).Generation))
			{
				BeginGenerations.Add(EnvelopeOf(Entry.Packet).Generation);
			}
		}
		TestTrue(TEXT("v2 endpoint only ever receives its lease envelope, header version 2"), bAllV2);
		TestTrue(TEXT("v2 BEGIN generations are 1 then 2"),
			BeginGenerations.Num() >= 2 && BeginGenerations[0] == 1u && BeginGenerations[1] == 2u);
	}

	// --- Ignore rules: nothing below may change B's class or restart its session.
	FHapbeatStreamRunnable* RunningB = Sub->StreamSessions.Contains(KeyB) ? Sub->StreamSessions[KeyB].Runnable : nullptr;
	const TPair<uint16, int64> Newer = IssuePing();
	Deliver(TEXT("192.0.2.81"), Newer.Key, V2Pong(Newer.Value, "player_1/pos_r_arm", Inc, BootB, 3));
	const int64 AcceptedTs = Sub->StreamEndpoints.FindChecked(KeyB).LeaseState.LastAcceptedTimestampUs;
	TestEqual(TEXT("repeated lease reply is accepted"), AcceptedTs, Newer.Value);
	Deliver(TEXT("192.0.2.81"), 0, LegacyPong(0, "player_1/pos_r_arm"));
	Deliver(TEXT("192.0.2.81"), First.Key, LegacyPong(First.Value, "player_1/pos_r_arm")); // late
	Deliver(TEXT("192.0.2.81"), 999, LegacyPong(Newer.Value, "player_1/pos_r_arm"));    // unmatched seq
	const TPair<uint16, int64> ForIgnored = IssuePing();
	AddExpectedMessage(TEXT("answered the lease request with a reply this SDK cannot read"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 2, /*IsRegex=*/false);
	Deliver(TEXT("192.0.2.81"), ForIgnored.Key, IgnoredPong(ForIgnored.Value, "player_1/pos_r_arm", Inc));
	TestEqual(TEXT("unsolicited / late / unmatched / ignored-tail replies keep B v2"), ClassOf(KeyB), EHapbeatStreamEndpointClass::V2);
	TestEqual(TEXT("... and do not advance its accepted request"),
		Sub->StreamEndpoints.FindChecked(KeyB).LeaseState.LastAcceptedTimestampUs, AcceptedTs);
	TestTrue(TEXT("... and leave its session running untouched"),
		Sub->StreamSessions.Contains(KeyB) && Sub->StreamSessions[KeyB].Runnable == RunningB);

	const FString KeyC = TEXT("192.0.2.82:7700|player_1/pos_chest");
	const TPair<uint16, int64> ForC = IssuePing();
	Deliver(TEXT("192.0.2.82"), ForC.Key, IgnoredPong(ForC.Value, "player_1/pos_chest", Inc));
	TestEqual(TEXT("an ignored tail leaves a new endpoint unknown"), ClassOf(KeyC), EHapbeatStreamEndpointClass::Unknown);
	TestFalse(TEXT("... and deferred"), Sub->StreamSessions.Contains(KeyC));

	const FString KeyE = TEXT("192.0.2.84:7700|player_1/pos_back");
	const TPair<uint16, int64> ForE = IssuePing();
	Deliver(TEXT("192.0.2.84"), ForE.Key, V2Pong(ForE.Value, "player_1/pos_back", Inc + 1, 0xE, 1));
	TestFalse(TEXT("HBS2 for another incarnation is discarded entirely"),
		Sub->StreamEndpoints.Contains(KeyE) || Sub->DevicePongTimes.Contains(TEXT("192.0.2.84")));

	// --- Class change v2 -> legacy: END in v2 format first, then legacy BEGIN.
	const int32 BPacketsBeforeRollback = Log->Snapshot(AddrB).Num();
	const TPair<uint16, int64> Rollback = IssuePing();
	Deliver(TEXT("192.0.2.81"), Rollback.Key, LegacyPong(Rollback.Value, "player_1/pos_r_arm"));
	TestEqual(TEXT("matched tail-less reply turns B legacy"), ClassOf(KeyB), EHapbeatStreamEndpointClass::Legacy);
	TestTrue(TEXT("B restarts at once in legacy format (the previous END was v2)"),
		Sub->StreamSessions.Contains(KeyB) && Sub->StreamSessions[KeyB].Format == EHapbeatStreamWireFormat::LegacyV1);
	TestTrue(TEXT("legacy BEGIN for B sent"), WaitForCount(*Log, AddrB, FHapbeatProtocol::CmdStreamBegin, 3));
	{
		const TArray<FStreamPacketLog::FEntry> Entries = Log->Snapshot(AddrB);
		int32 V2EndIndex = INDEX_NONE;
		int32 V1BeginIndex = INDEX_NONE;
		for (int32 Index = BPacketsBeforeRollback; Index < Entries.Num(); ++Index)
		{
			const TArray<uint8>& Packet = Entries[Index].Packet;
			if (V2EndIndex == INDEX_NONE && Packet[3] == FHapbeatProtocol::CmdStreamEnd
				&& Packet[2] == FHapbeatProtocol::StreamProtocolVersion)
			{
				V2EndIndex = Index;
			}
			if (V1BeginIndex == INDEX_NONE && Packet[3] == FHapbeatProtocol::CmdStreamBegin
				&& Packet[2] == FHapbeatProtocol::Version)
			{
				V1BeginIndex = Index;
			}
		}
		TestTrue(TEXT("class change: old v2 session ENDs (with its envelope) before the legacy BEGIN"),
			V2EndIndex != INDEX_NONE && V1BeginIndex != INDEX_NONE && V2EndIndex < V1BeginIndex);
		if (V2EndIndex != INDEX_NONE)
		{
			TestEqual(TEXT("the v2 END carries the retired session identity"),
				EnvelopeOf(Entries[V2EndIndex].Packet).Generation, 2u);
		}
	}

	// --- Class change legacy -> v2: legacy END first, v2 BEGIN immediately.
	const int32 BPacketsBeforeUpdate = Log->Snapshot(AddrB).Num();
	const TPair<uint16, int64> Update = IssuePing();
	Deliver(TEXT("192.0.2.81"), Update.Key, V2Pong(Update.Value, "player_1/pos_r_arm", Inc, BootB, 3));
	TestEqual(TEXT("matched HBS2 reply turns B back to v2"), ClassOf(KeyB), EHapbeatStreamEndpointClass::V2);
	TestTrue(TEXT("v2 BEGIN is not held by the legacy guard"),
		Sub->StreamSessions.Contains(KeyB) && Sub->StreamSessions[KeyB].Format == EHapbeatStreamWireFormat::SessionV2);
	if (Sub->StreamSessions.Contains(KeyB))
	{
		TestEqual(TEXT("generation keeps increasing under the same lease"), Sub->StreamSessions[KeyB].Identity.Generation, 3u);
	}
	TestTrue(TEXT("v2 BEGIN for B sent"), WaitForCount(*Log, AddrB, FHapbeatProtocol::CmdStreamBegin, 4));
	{
		const TArray<FStreamPacketLog::FEntry> Entries = Log->Snapshot(AddrB);
		int32 V1EndIndex = INDEX_NONE;
		int32 V2BeginIndex = INDEX_NONE;
		for (int32 Index = BPacketsBeforeUpdate; Index < Entries.Num(); ++Index)
		{
			const TArray<uint8>& Packet = Entries[Index].Packet;
			if (V1EndIndex == INDEX_NONE && Packet[3] == FHapbeatProtocol::CmdStreamEnd && Packet[2] == FHapbeatProtocol::Version)
			{
				V1EndIndex = Index;
			}
			if (V2BeginIndex == INDEX_NONE && Packet[3] == FHapbeatProtocol::CmdStreamBegin
				&& Packet[2] == FHapbeatProtocol::StreamProtocolVersion)
			{
				V2BeginIndex = Index;
			}
		}
		TestTrue(TEXT("class change: legacy END (empty payload) precedes the v2 BEGIN"),
			V1EndIndex != INDEX_NONE && V2BeginIndex != INDEX_NONE && V1EndIndex < V2BeginIndex);
	}

	// --- Device reboot: END under the old identity, BEGIN under the new boot.
	const uint64 RebootedB = 0x00000000000B0072ULL;
	const int32 BPacketsBeforeReboot = Log->Snapshot(AddrB).Num();
	const TPair<uint16, int64> AfterReboot = IssuePing();
	Deliver(TEXT("192.0.2.81"), AfterReboot.Key, V2Pong(AfterReboot.Value, "player_1/pos_r_arm", Inc, RebootedB, 1));
	TestTrue(TEXT("reboot starts a session under the new lease"),
		Sub->StreamSessions.Contains(KeyB)
		&& Sub->StreamSessions[KeyB].Identity == FHapbeatStreamSessionIdentity(FHapbeatStreamLeaseIdentity(RebootedB, 1), 1));
	TestTrue(TEXT("new-boot BEGIN sent"), WaitForCount(*Log, AddrB, FHapbeatProtocol::CmdStreamBegin, 5));
	{
		const TArray<FStreamPacketLog::FEntry> Entries = Log->Snapshot(AddrB);
		bool bOldEndBeforeNewBegin = false;
		bool bSawOldEnd = false;
		bool bOldBootDataAfterNewBegin = false;
		bool bSawNewBegin = false;
		for (int32 Index = BPacketsBeforeReboot; Index < Entries.Num(); ++Index)
		{
			const TArray<uint8>& Packet = Entries[Index].Packet;
			const FHapbeatStreamSessionIdentity Envelope = EnvelopeOf(Packet);
			if (Packet[3] == FHapbeatProtocol::CmdStreamEnd && Envelope.DeviceBootId == BootB)
			{
				bSawOldEnd = true;
			}
			if (Packet[3] == FHapbeatProtocol::CmdStreamBegin && Envelope.DeviceBootId == RebootedB)
			{
				bOldEndBeforeNewBegin = bSawOldEnd;
				bSawNewBegin = true;
			}
			if (bSawNewBegin && Envelope.DeviceBootId == BootB)
			{
				bOldBootDataAfterNewBegin = true;
			}
		}
		TestTrue(TEXT("reboot: retired identity ENDs before the new-boot BEGIN"), bOldEndBeforeNewBegin);
		TestFalse(TEXT("reboot: nothing is sent under the retired identity afterwards"), bOldBootDataAfterNewBegin);
	}

	// --- Superseded: stop and defer that endpoint, no automatic takeover.
	AddExpectedMessage(TEXT("was superseded by another writer"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 1, /*IsRegex=*/false);
	const TPair<uint16, int64> Takeover = IssuePing();
	Deliver(TEXT("192.0.2.81"), Takeover.Key, V2Pong(Takeover.Value, "player_1/pos_r_arm", Inc, RebootedB, 1, 0x03));
	TestFalse(TEXT("superseded lease stops the endpoint session"), Sub->StreamSessions.Contains(KeyB));
	TestEqual(TEXT("superseded stays v2 (never legacy)"), ClassOf(KeyB), EHapbeatStreamEndpointClass::V2);
	const TPair<uint16, int64> Periodic = IssuePing();
	Deliver(TEXT("192.0.2.81"), Periodic.Key, V2Pong(Periodic.Value, "player_1/pos_r_arm", Inc, RebootedB, 2));
	TestFalse(TEXT("periodic discovery does not take the stream back"), Sub->StreamSessions.Contains(KeyB));
	TestTrue(TEXT("the legacy endpoint is unaffected"), Sub->StreamSessions.Contains(KeyA));

	Sub->StopStream();
	TestEqual(TEXT("StopStream ends every session"), Sub->StreamSessions.Num(), 0);
	return true;
}

// ---------------------------------------------------------------------------
// Subsystem: reacquisition after supersede, foreground hook, identity-update
// discovery, v2 lease-refresh cadence
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatStreamReacquireOwnershipTest,
	"Hapbeat.Streaming.SessionV2.ReacquireOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatStreamReacquireOwnershipTest::RunTest(const FString& Parameters)
{
	using namespace HapbeatStreamSessionV2Test;
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UHapbeatSubsystem* Sub = NewObject<UHapbeatSubsystem>(GameInstance);
	Sub->bSuppressStreamDiscoveryForAutomationTest = true;
	const uint64 Inc = 0x0F0E0D0C0B0A0908ULL;
	Sub->ClientIncarnation = Inc;
	TSharedRef<FStreamPacketLog, ESPMode::ThreadSafe> Log = MakeShared<FStreamPacketLog, ESPMode::ThreadSafe>();
	Sub->StreamRunnableFactoryForAutomationTest =
		[Log](const FGuid& SourceId, TArray<uint8>&& Pcm16, const FString& Target,
			TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> Mirror, EHapbeatStreamWireFormat Format,
			const FHapbeatStreamSessionIdentity& Identity) -> FHapbeatStreamRunnable*
		{
			return new FLoggingStreamRunnable(SourceId, MoveTemp(Pcm16), Target, Mirror, Format, Identity, Log);
		};

	const FString AddrA = TEXT("player_1/pos_l_arm"); // pre-v2 device
	const FString AddrB = TEXT("player_1/pos_r_arm"); // v2 device
	const FString KeyA = TEXT("192.0.2.90:7700|player_1/pos_l_arm");
	const FString KeyB = TEXT("192.0.2.91:7700|player_1/pos_r_arm");
	const uint64 BootB = 0x00000000000B0091ULL;
	int64 NextTs = 9000;
	uint16 NextSeq = 300;
	auto IssuePing = [&]() -> TPair<uint16, int64>
	{
		const TPair<uint16, int64> Issued(NextSeq++, NextTs);
		NextTs += 1000;
		Sub->PendingPings.Add(Issued.Key, Pending(Issued.Value, Sub->ClientIncarnation));
		return Issued;
	};
	auto Deliver = [&](const TCHAR* Ip, uint16 Seq, const FHapbeatProtocol::FPongInfo& Pong)
	{
		Sub->HandlePongGameThread(Ip, 7700, Seq, static_cast<int64>(FPlatformTime::Seconds() * 1'000'000.0), Pong);
	};
	auto ClassOf = [&](const FString& Key)
	{
		const UHapbeatSubsystem::FStreamEndpoint* Endpoint = Sub->StreamEndpoints.Find(Key);
		return Endpoint != nullptr ? Endpoint->LeaseState.Class : EHapbeatStreamEndpointClass::Unknown;
	};

	UHapbeatStreamPlayback* Playback = NewObject<UHapbeatStreamPlayback>(Sub);
	Playback->Init(1.0f, 1.0f);
	Playback->SetLoop(true);
	UHapbeatSubsystem::FStreamSource& Source = Sub->StreamSources.Add(Playback->Id);
	Source.CanonicalPcm16 = MakeSessionTestPcm(1000, 160);
	Source.Playback = Playback;
	Sub->ActivePlaybacks.Add(Playback);

	const TPair<uint16, int64> First = IssuePing();
	Deliver(TEXT("192.0.2.90"), First.Key, LegacyPong(First.Value, "player_1/pos_l_arm"));
	Deliver(TEXT("192.0.2.91"), First.Key, V2Pong(First.Value, "player_1/pos_r_arm", Inc, BootB, 3));
	if (!TestTrue(TEXT("legacy + v2 sessions running"),
			Sub->StreamSessions.Contains(KeyA) && Sub->StreamSessions.Contains(KeyB)))
	{
		Sub->StopStream();
		return false;
	}
	TestTrue(TEXT("B BEGIN sent"), WaitForCount(*Log, AddrB, FHapbeatProtocol::CmdStreamBegin, 1));

	// --- Cadences: the discovery broadcast no longer refreshes leases (it is the
	// 8-byte PING), so it keeps PingInterval; leases are refreshed by unicast at
	// TTL/3 (Hapbeat.Streaming.Gen2.LeasePing covers the schedule itself).
	TestEqual(TEXT("discovery keeps PingInterval once a device answered"),
		UHapbeatSubsystem::ComputeKeepAliveIntervalSeconds(2, 30.0f), 30.0f);
	TestEqual(TEXT("nothing alive keeps the fast discovery cadence"),
		UHapbeatSubsystem::ComputeKeepAliveIntervalSeconds(0, 30.0f), UHapbeatSubsystem::DiscoveryTickSeconds);

	// --- Superseded: logged once, deferred, and periodic discovery keeps it so.
	AddExpectedMessage(TEXT("was superseded by another writer"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 1, /*IsRegex=*/false);
	const TPair<uint16, int64> Takeover = IssuePing();
	Deliver(TEXT("192.0.2.91"), Takeover.Key, V2Pong(Takeover.Value, "player_1/pos_r_arm", Inc, BootB, 3, 0x03));
	TestFalse(TEXT("superseded lease stops B"), Sub->StreamSessions.Contains(KeyB));
	const TPair<uint16, int64> Periodic = IssuePing();
	Deliver(TEXT("192.0.2.91"), Periodic.Key, V2Pong(Periodic.Value, "player_1/pos_r_arm", Inc, BootB, 3, 0x03));
	TestFalse(TEXT("B stays deferred under the same incarnation"), Sub->StreamSessions.Contains(KeyB));
	FHapbeatStreamRunnable* const RunningA = Sub->StreamSessions[KeyA].Runnable;
	const int32 APacketsBefore = Log->Snapshot(AddrA).Num();

	// --- The foreground hook is bound to the application lifecycle and reacquires.
	Sub->BindApplicationLifecycle();
	TestTrue(TEXT("foreground delegate bound"), FCoreDelegates::ApplicationHasEnteredForegroundDelegate.IsBoundToObject(Sub));
	TestTrue(TEXT("reactivation delegate bound"), FCoreDelegates::ApplicationHasReactivatedDelegate.IsBoundToObject(Sub));
	const int32 DiscoveryBefore = Sub->StreamDiscoveryRequestCount;
	Sub->HandleApplicationForeground();
	const uint64 NewInc = Sub->ClientIncarnation;
	TestTrue(TEXT("foreground resume issues a fresh nonzero incarnation"), NewInc != 0 && NewInc != Inc);
	TestEqual(TEXT("... sends one discovery PING"), Sub->StreamDiscoveryRequestCount, DiscoveryBefore + 1);
	{
		bool bOnlyNewIncarnation = true;
		for (const TPair<uint16, FHapbeatPendingPing>& Pair : Sub->PendingPings)
		{
			bOnlyNewIncarnation &= Pair.Value.ClientIncarnation == NewInc;
		}
		TestTrue(TEXT("... drops the old incarnation's outstanding requests"), bOnlyNewIncarnation);
		bool bAskedB = false;
		for (const UHapbeatSubsystem::FAutomationUnicastRecord& Record : Sub->LeasePingsForAutomationTest)
		{
			const FHapbeatPendingPing* Request = Sub->PendingPings.Find(Record.Seq);
			bAskedB |= Record.Key == KeyB && Request != nullptr && Request->ClientIncarnation == NewInc;
		}
		TestTrue(TEXT("... and asks the lease target for a lease under the new incarnation at once"), bAskedB);
	}
	Sub->HandleApplicationForeground();
	TestEqual(TEXT("a duplicate lifecycle event within 1 s does not reacquire again"), Sub->ClientIncarnation, NewInc);
	Sub->UnbindApplicationLifecycle();
	TestFalse(TEXT("lifecycle delegates unbound"),
		FCoreDelegates::ApplicationHasEnteredForegroundDelegate.IsBoundToObject(Sub)
		|| FCoreDelegates::ApplicationHasReactivatedDelegate.IsBoundToObject(Sub));

	TestEqual(TEXT("reacquire keeps the legacy class"), ClassOf(KeyA), EHapbeatStreamEndpointClass::Legacy);
	TestTrue(TEXT("... and leaves the legacy session running untouched"),
		Sub->StreamSessions.Contains(KeyA) && Sub->StreamSessions[KeyA].Runnable == RunningA);
	TestEqual(TEXT("... without sending it an END"),
		Log->Count(AddrA, FHapbeatProtocol::CmdStreamEnd), 0);
	TestEqual(TEXT("reacquire forgets the superseded v2 lease"), ClassOf(KeyB), EHapbeatStreamEndpointClass::Unknown);

	// A late reply to the old incarnation's request cannot restore anything.
	Sub->PendingPings.Add(Periodic.Key, Pending(Periodic.Value, Inc));
	Deliver(TEXT("192.0.2.91"), Periodic.Key, V2Pong(Periodic.Value, "player_1/pos_r_arm", Inc, BootB, 3));
	TestEqual(TEXT("an old-incarnation reply is discarded after reacquire"), ClassOf(KeyB), EHapbeatStreamEndpointClass::Unknown);
	Sub->PendingPings.Empty();

	// The fresh lease reply resumes B at once under the new ticket.
	const TPair<uint16, int64> Fresh = IssuePing();
	Deliver(TEXT("192.0.2.90"), Fresh.Key, LegacyPong(Fresh.Value, "player_1/pos_l_arm"));
	Deliver(TEXT("192.0.2.91"), Fresh.Key, V2Pong(Fresh.Value, "player_1/pos_r_arm", NewInc, BootB, 4));
	TestTrue(TEXT("superseded endpoint resumes after reacquisition"),
		Sub->StreamSessions.Contains(KeyB)
		&& Sub->StreamSessions[KeyB].Identity == FHapbeatStreamSessionIdentity(FHapbeatStreamLeaseIdentity(BootB, 4), 1));
	TestTrue(TEXT("B BEGIN under the new lease sent"), WaitForCount(*Log, AddrB, FHapbeatProtocol::CmdStreamBegin, 2));
	TestTrue(TEXT("legacy session still the same runner"),
		Sub->StreamSessions.Contains(KeyA) && Sub->StreamSessions[KeyA].Runnable == RunningA);

	// --- Reacquire while a v2 session is running: END under the relinquished
	// identity first, nothing more under it afterwards.
	const int32 BPacketsBefore = Log->Snapshot(AddrB).Num();
	Sub->ReacquireStreamOwnership();
	TestFalse(TEXT("reacquire ends the running v2 session"), Sub->StreamSessions.Contains(KeyB));
	const uint64 ThirdInc = Sub->ClientIncarnation;
	const TPair<uint16, int64> Renewed = IssuePing();
	Deliver(TEXT("192.0.2.91"), Renewed.Key, V2Pong(Renewed.Value, "player_1/pos_r_arm", ThirdInc, BootB, 5));
	TestTrue(TEXT("renewed-lease BEGIN sent"), WaitForCount(*Log, AddrB, FHapbeatProtocol::CmdStreamBegin, 3));
	{
		const TArray<FStreamPacketLog::FEntry> Entries = Log->Snapshot(AddrB);
		bool bSawOldEnd = false;
		bool bOldEndBeforeNewBegin = false;
		bool bOldAfterNewBegin = false;
		bool bSawNewBegin = false;
		for (int32 Index = BPacketsBefore; Index < Entries.Num(); ++Index)
		{
			const TArray<uint8>& Packet = Entries[Index].Packet;
			const uint32 Ticket = EnvelopeOf(Packet).LeaseTicket;
			if (Packet[3] == FHapbeatProtocol::CmdStreamEnd && Ticket == 4u)
			{
				bSawOldEnd = true;
			}
			if (Packet[3] == FHapbeatProtocol::CmdStreamBegin && Ticket == 5u)
			{
				bOldEndBeforeNewBegin = bSawOldEnd;
				bSawNewBegin = true;
			}
			if (bSawNewBegin && Ticket == 4u)
			{
				bOldAfterNewBegin = true;
			}
		}
		TestTrue(TEXT("reacquire: relinquished lease ENDs before the renewed BEGIN"), bOldEndBeforeNewBegin);
		TestFalse(TEXT("reacquire: nothing goes out under the relinquished lease afterwards"), bOldAfterNewBegin);
	}
	TestEqual(TEXT("the legacy endpoint never saw an END through all of it"),
		Log->Count(AddrA, FHapbeatProtocol::CmdStreamEnd), 0);
	TestTrue(TEXT("... and never a second BEGIN"), Log->Count(AddrA, FHapbeatProtocol::CmdStreamBegin) == 1
		&& Log->Snapshot(AddrA).Num() >= APacketsBefore);

	Sub->StopStream();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatStreamIdentityUpdateDiscoveryTest,
	"Hapbeat.Streaming.SessionV2.IdentityUpdateDiscovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatStreamIdentityUpdateDiscoveryTest::RunTest(const FString& Parameters)
{
	using namespace HapbeatStreamSessionV2Test;
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UHapbeatSubsystem* Sub = NewObject<UHapbeatSubsystem>(GameInstance);
	Sub->bSuppressStreamDiscoveryForAutomationTest = true;
	const uint64 Inc = 0x1111222233334444ULL;
	Sub->ClientIncarnation = Inc;
	TSharedRef<FStreamPacketLog, ESPMode::ThreadSafe> Log = MakeShared<FStreamPacketLog, ESPMode::ThreadSafe>();
	Sub->StreamRunnableFactoryForAutomationTest =
		[Log](const FGuid& SourceId, TArray<uint8>&& Pcm16, const FString& Target,
			TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> Mirror, EHapbeatStreamWireFormat Format,
			const FHapbeatStreamSessionIdentity& Identity) -> FHapbeatStreamRunnable*
		{
			return new FLoggingStreamRunnable(SourceId, MoveTemp(Pcm16), Target, Mirror, Format, Identity, Log);
		};
	auto Deliver = [&](const TCHAR* Ip, uint16 Seq, const FHapbeatProtocol::FPongInfo& Pong)
	{
		Sub->HandlePongGameThread(Ip, 7700, Seq, static_cast<int64>(FPlatformTime::Seconds() * 1'000'000.0), Pong);
	};

	const FString OldKey = TEXT("192.0.2.95:7700|player_1/pos_chest");
	const FString NewKey = TEXT("192.0.2.95:7700|player_2/pos_chest");
	const uint64 Boot = 0x00000000000B0095ULL;

	// One source on the device's OLD address, one on its NEW address.
	auto AddLoop = [&](const TCHAR* Target)
	{
		UHapbeatStreamPlayback* Playback = NewObject<UHapbeatStreamPlayback>(Sub);
		Playback->Init(1.0f, 1.0f);
		Playback->SetLoop(true);
		UHapbeatSubsystem::FStreamSource& Source = Sub->StreamSources.Add(Playback->Id);
		Source.CanonicalPcm16 = MakeSessionTestPcm(1000, 160);
		Source.Playback = Playback;
		Source.ResolvedTarget = Target;
		Sub->ActivePlaybacks.Add(Playback);
		return Playback;
	};
	UHapbeatStreamPlayback* OldAddressPlayback = AddLoop(TEXT("player_1"));
	UHapbeatStreamPlayback* NewAddressPlayback = AddLoop(TEXT("player_2"));

	// Classify the device (Stream Gen 2) under its old address: the old-address source streams.
	Sub->PendingPings.Add(400, Pending(20000, Inc, TEXT("192.0.2.95")));
	Deliver(TEXT("192.0.2.95"), 400, V2Pong(20000, "player_1/pos_chest", Inc, Boot, 3));
	TestTrue(TEXT("old key streams"), Sub->StreamSessions.Contains(OldKey));
	TestEqual(TEXT("new-address source waits"), NewAddressPlayback->GetStatus(), EHapbeatStreamPlaybackStatus::Deferred);
	TestTrue(TEXT("old-key BEGIN sent"), WaitForCount(*Log, TEXT("player_1/pos_chest"), FHapbeatProtocol::CmdStreamBegin, 1));
	FHapbeatStreamRunnable* const Runner = Sub->StreamSessions.Contains(OldKey) ? Sub->StreamSessions[OldKey].Runnable : nullptr;

	// Address change: the device's unsolicited identity PONG (seq 0, timestamp 0).
	// The same route moves to the new key at once, alive or not, with its class,
	// lease and session: no old key is left streaming the old address's source.
	Deliver(TEXT("192.0.2.95"), 0, LegacyPong(0, "player_2/pos_chest"));
	TestFalse(TEXT("the old key is gone at once"), Sub->StreamEndpoints.Contains(OldKey));
	TestTrue(TEXT("the new key exists"), Sub->StreamEndpoints.Contains(NewKey));
	TestEqual(TEXT("... with the class carried over (no new round trip)"),
		Sub->StreamEndpoints.FindChecked(NewKey).LeaseState.Class, EHapbeatStreamEndpointClass::V2);
	TestTrue(TEXT("the session moved with the device (same runner, same lease)"),
		Sub->StreamSessions.Contains(NewKey) && Sub->StreamSessions[NewKey].Runnable == Runner
		&& !Sub->StreamSessions.Contains(OldKey));
	TestEqual(TEXT("the old-address source no longer matches and waits"),
		OldAddressPlayback->GetStatus(), EHapbeatStreamPlaybackStatus::Deferred);
	TestEqual(TEXT("... with no endpoint"), Sub->StreamSources.FindChecked(OldAddressPlayback->Id).EndpointKeys.Num(), 0);
	TestEqual(TEXT("the new-address source joins the moved session"),
		NewAddressPlayback->GetStatus(), EHapbeatStreamPlaybackStatus::Active);
	TestTrue(TEXT("... on the new key"), Sub->StreamSources.FindChecked(NewAddressPlayback->Id).EndpointKeys.Contains(NewKey));

	// The repeated BEGIN now carries the new address (stream-session.md Sender requirements 2).
	{
		// The runner logs under the target it was built with; the periodic repeat
		// (every 50 blocks = 500 ms) is the first BEGIN after the move.
		auto HasNewTargetBegin = [&Log]()
		{
			for (const FStreamPacketLog::FEntry& Entry : Log->Snapshot(TEXT("player_1/pos_chest")))
			{
				if (Entry.Packet[3] != FHapbeatProtocol::CmdStreamBegin)
				{
					continue;
				}
				const int32 TargetOffset = FHapbeatProtocol::HeaderSize + FHapbeatProtocol::StreamSessionEnvelopeSize + 12;
				FString Target;
				for (int32 Index = TargetOffset; Index < Entry.Packet.Num() && Entry.Packet[Index] != 0; ++Index)
				{
					Target.AppendChar(static_cast<TCHAR>(Entry.Packet[Index]));
				}
				if (Target == TEXT("player_2/pos_chest"))
				{
					return true;
				}
			}
			return false;
		};
		const double Deadline = FPlatformTime::Seconds() + 2.0;
		while (!HasNewTargetBegin() && FPlatformTime::Seconds() < Deadline)
		{
			FPlatformProcess::Sleep(0.01f);
		}
		TestTrue(TEXT("a repeated BEGIN carries the new target player_2/pos_chest"), HasNewTargetBegin());
	}

	Sub->StopStream();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
