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
// Wire bytes: Gen 1 regression + Gen 2 contract fixture
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatStreamSessionV2WireTest,
	"Hapbeat.Streaming.SessionV2.WireFormat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatStreamSessionV2WireTest::RunTest(const FString& Parameters)
{
	using namespace HapbeatStreamSessionV2Test;
	const uint16 Seq = 0x1234;
	const uint8 Audio[4] = { 0x10, 0x00, 0xF0, 0xFF }; // PCM16 LE samples 16, -16 (contract fixture)

	// Stream Gen 1 builders must stay byte-identical to message-format.md
	// "Stream Gen 1" section: header version 1, no envelope, END with an empty payload.
	TestEqual(TEXT("Gen 1 BEGIN bytes"),
		FHapbeatProtocol::BuildStreamBegin(Seq, 16000, 2, FHapbeatProtocol::AudioFormatPcm16, 0, 1.0f, TEXT("*")),
		FromHex(TEXT("424801303412" "0e00" "803e" "02" "00" "00000000" "0000803f" "2a00")));
	TestEqual(TEXT("Gen 1 BEGIN without target omits the field"),
		FHapbeatProtocol::BuildStreamBegin(Seq, 16000, 2, FHapbeatProtocol::AudioFormatPcm16, 0, 1.0f),
		FromHex(TEXT("424801303412" "0c00" "803e" "02" "00" "00000000" "0000803f")));
	TestEqual(TEXT("Gen 1 DATA bytes"),
		FHapbeatProtocol::BuildStreamData(Seq, 0, Audio, 4),
		FromHex(TEXT("424801313412" "0800" "00000000" "1000f0ff")));
	TestEqual(TEXT("Gen 1 END bytes"),
		FHapbeatProtocol::BuildStreamEnd(Seq), FromHex(TEXT("4248013234120000")));

	// Gen 2: repos-core/hapbeat-contracts/fixtures/stream-session-v2.json
	// (boot 0102030405060708, ticket 7, generation 42, seq 4660).
	const FHapbeatStreamSessionIdentity Identity(FHapbeatStreamLeaseIdentity(0x0102030405060708ULL, 7), 42);
	TestEqual(TEXT("Gen 2 BEGIN matches the contract fixture"),
		FHapbeatProtocol::BuildStreamBeginV2(Seq, Identity, 16000, 2, FHapbeatProtocol::AudioFormatPcm16, 0, 1.0f, TEXT("*")),
		FromHex(TEXT("4248023034121e000807060504030201070000002a000000803e0200000000000000803f2a00")));
	TestEqual(TEXT("Gen 2 DATA matches the contract fixture"),
		FHapbeatProtocol::BuildStreamDataV2(Seq, Identity, 0, Audio, 4),
		FromHex(TEXT("42480231341218000807060504030201070000002a000000000000001000f0ff")));
	TestEqual(TEXT("Gen 2 END matches the contract fixture"),
		FHapbeatProtocol::BuildStreamEndV2(Seq, Identity),
		FromHex(TEXT("42480232341210000807060504030201070000002a000000")));

	// Extended PING stays header version 1 with a 16-byte payload; the Gen 1
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
		TestTrue(bV2 ? TEXT("Gen 2 session: every packet is header version 2") : TEXT("Gen 1 session: every packet is header version 1"), bAllVersion);
		TestTrue(TEXT("Gen 2 session: every packet carries the session envelope"), bAllEnvelope);
		const TArray<uint8>& Begin = Packets[0];
		const TArray<uint8>& End = Packets.Last();
		TestEqual(TEXT("first packet is BEGIN"), Begin[3], FHapbeatProtocol::CmdStreamBegin);
		TestEqual(TEXT("BEGIN payload length"), static_cast<int32>(ReadLe(Begin, 6, 2)),
			(bV2 ? FHapbeatProtocol::StreamSessionEnvelopeSize : 0) + 12 + 9 /* "player_1\0" */);
		TestEqual(TEXT("last packet is END"), End[3], FHapbeatProtocol::CmdStreamEnd);
		TestEqual(TEXT("END payload is the envelope (Gen 2) or empty (Gen 1)"), static_cast<int32>(ReadLe(End, 6, 2)),
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
	TestEqual(TEXT("... as Gen 2"), Next.Class, EHapbeatStreamEndpointClass::V2);
	TestTrue(TEXT("... with a usable lease"), Next.IsUsableV2Lease(Inc));
	TestEqual(TEXT("... recording the accepted request"), Next.LastAcceptedTimestampUs, 1000LL);
	const FHapbeatStreamEndpointLeaseState AcceptedV2 = Next;

	TestEqual(TEXT("matched reply without a tail is accepted"),
		EvaluatePongLease(LegacyPong(1000, Address), 10, Pings, Inc, nullptr, Next, Ip), EPongLeaseDecision::Accept);
	TestEqual(TEXT("... as Gen 1"), Next.Class, EHapbeatStreamEndpointClass::Legacy);
	TestTrue(TEXT("Gen 1 endpoints are streamable"), Next.IsStreamable(Inc));

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

	TestEqual(TEXT("unsolicited (timestamp 0) PONG never selects Gen 1"),
		EvaluatePongLease(LegacyPong(0, Address), 10, Pings, Inc, nullptr, Next, Ip), EPongLeaseDecision::NoChange);
	TestEqual(TEXT("reply to an unknown seq never selects Gen 1"),
		EvaluatePongLease(LegacyPong(1000, Address), 99, Pings, Inc, nullptr, Next, Ip), EPongLeaseDecision::NoChange);
	TestEqual(TEXT("reply echoing another request's timestamp never selects Gen 1"),
		EvaluatePongLease(LegacyPong(2000, Address), 10, Pings, Inc, nullptr, Next, Ip), EPongLeaseDecision::NoChange);
	TestEqual(TEXT("reply to a previous incarnation's request never selects Gen 1"),
		EvaluatePongLease(LegacyPong(3000, Address), 12, Pings, Inc, nullptr, Next, Ip), EPongLeaseDecision::NoChange);
	TestEqual(TEXT("late tail-less reply cannot demote an accepted Gen 2 endpoint"),
		EvaluatePongLease(LegacyPong(1000, Address), 10, Pings, Inc, &AcceptedV2, Next, Ip), EPongLeaseDecision::NoChange);
	TestEqual(TEXT("newer matched tail-less reply demotes Gen 2 to Gen 1 (firmware rollback)"),
		EvaluatePongLease(LegacyPong(2000, Address), 11, Pings, Inc, &AcceptedV2, Next, Ip), EPongLeaseDecision::Accept);
	TestEqual(TEXT("... to Gen 1"), Next.Class, EHapbeatStreamEndpointClass::Legacy);

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

	TestEqual(TEXT("lease_valid=0 is still Gen 2"),
		EvaluatePongLease(V2Pong(1000, Address, Inc, 0x11, 0, /*Flags=*/0x00), 10, Pings, Inc, nullptr, Next, Ip), EPongLeaseDecision::Accept);
	TestEqual(TEXT("... class Gen 2, never Gen 1"), Next.Class, EHapbeatStreamEndpointClass::V2);
	TestFalse(TEXT("... and defers"), Next.IsStreamable(Inc));

	TestEqual(TEXT("superseded reply is accepted"),
		EvaluatePongLease(V2Pong(1000, Address, Inc, 0x11, 3, 0x03), 10, Pings, Inc, nullptr, Next, Ip), EPongLeaseDecision::Accept);
	TestFalse(TEXT("superseded Gen 2 defers"), Next.IsStreamable(Inc));
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

	const FString AddrA = TEXT("player_1/pos_l_arm"); // Stream Gen 1 device
	const FString AddrB = TEXT("player_1/pos_r_arm"); // Gen 2 device
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
	TestEqual(TEXT("A is Gen 1"), ClassOf(KeyA), EHapbeatStreamEndpointClass::Legacy);
	TestEqual(TEXT("B is Gen 2 from the same request"), ClassOf(KeyB), EHapbeatStreamEndpointClass::V2);
	TestTrue(TEXT("Gen 1 and Gen 2 endpoints stream concurrently"),
		Sub->StreamSessions.Contains(KeyA) && Sub->StreamSessions.Contains(KeyB));
	if (!Sub->StreamSessions.Contains(KeyA) || !Sub->StreamSessions.Contains(KeyB))
	{
		Sub->StopStream();
		return false;
	}
	TestEqual(TEXT("A session is Gen 1 format"), Sub->StreamSessions[KeyA].Format, EHapbeatStreamWireFormat::LegacyV1);
	TestEqual(TEXT("B session is Gen 2 format"), Sub->StreamSessions[KeyB].Format, EHapbeatStreamWireFormat::SessionV2);
	TestEqual(TEXT("B session starts at generation 1"), Sub->StreamSessions[KeyB].Identity.Generation, 1u);
	TestTrue(TEXT("A BEGIN sent"), WaitForCount(*Log, AddrA, FHapbeatProtocol::CmdStreamBegin, 1));
	TestTrue(TEXT("B BEGIN sent"), WaitForCount(*Log, AddrB, FHapbeatProtocol::CmdStreamBegin, 1));

	// --- END then restart: Gen 2 immediately, Gen 1 only after 300 ms, per endpoint.
	Sub->StopStreamSession(KeyA);
	Sub->StopStreamSession(KeyB);
	Sub->ReconcileStreamSources();
	TestTrue(TEXT("Gen 2 endpoint restarts immediately after END"), Sub->StreamSessions.Contains(KeyB));
	TestFalse(TEXT("Gen 1 endpoint waits for its 300 ms guard"), Sub->StreamSessions.Contains(KeyA));
	if (Sub->StreamSessions.Contains(KeyB))
	{
		TestEqual(TEXT("Gen 2 restart uses the next generation"), Sub->StreamSessions[KeyB].Identity.Generation, 2u);
	}
	TestFalse(TEXT("a Gen 2 END arms no guard"), Sub->StreamSessionEndedAt.Contains(KeyB));
	TestTrue(TEXT("a Gen 1 END arms the guard"), Sub->StreamSessionEndedAt.Contains(KeyA));
	// Age the Gen 1 END past 300 ms instead of sleeping.
	Sub->StreamSessionEndedAt[KeyA] -= 0.31;
	for (TPair<FString, double>& RouteEnd : Sub->StreamRouteEndedAt)
	{
		RouteEnd.Value -= 0.31;
	}
	Sub->ReconcileStreamSources();
	TestTrue(TEXT("Gen 1 endpoint restarts once 300 ms have passed"), Sub->StreamSessions.Contains(KeyA));
	TestTrue(TEXT("A second BEGIN sent"), WaitForCount(*Log, AddrA, FHapbeatProtocol::CmdStreamBegin, 2));
	TestTrue(TEXT("B second BEGIN sent"), WaitForCount(*Log, AddrB, FHapbeatProtocol::CmdStreamBegin, 2));

	{
		bool bAllV1 = true;
		for (const FStreamPacketLog::FEntry& Entry : Log->Snapshot(AddrA))
		{
			bAllV1 &= Entry.Packet[2] == FHapbeatProtocol::Version;
			if (Entry.Packet[3] == FHapbeatProtocol::CmdStreamBegin)
			{
				TestEqual(TEXT("Gen 1 BEGIN has no envelope"), static_cast<int32>(ReadLe(Entry.Packet, 6, 2)), 12 + AddrA.Len() + 1);
			}
			if (Entry.Packet[3] == FHapbeatProtocol::CmdStreamEnd)
			{
				TestEqual(TEXT("Gen 1 END payload is empty"), static_cast<int32>(ReadLe(Entry.Packet, 6, 2)), 0);
			}
		}
		TestTrue(TEXT("Gen 1 endpoint only ever receives header version 1"), bAllV1);
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
		TestTrue(TEXT("Gen 2 endpoint only ever receives its lease envelope, header version 2"), bAllV2);
		TestTrue(TEXT("Gen 2 BEGIN generations are 1 then 2"),
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
	TestEqual(TEXT("unsolicited / late / unmatched / ignored-tail replies keep B Gen 2"), ClassOf(KeyB), EHapbeatStreamEndpointClass::V2);
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

	// --- Class change Gen 2 -> Gen 1: END in Gen 2 format first, then Gen 1 BEGIN.
	const int32 BPacketsBeforeRollback = Log->Snapshot(AddrB).Num();
	const TPair<uint16, int64> Rollback = IssuePing();
	Deliver(TEXT("192.0.2.81"), Rollback.Key, LegacyPong(Rollback.Value, "player_1/pos_r_arm"));
	TestEqual(TEXT("matched tail-less reply turns B Gen 1"), ClassOf(KeyB), EHapbeatStreamEndpointClass::Legacy);
	TestTrue(TEXT("B restarts at once in Gen 1 format (the previous END was Gen 2)"),
		Sub->StreamSessions.Contains(KeyB) && Sub->StreamSessions[KeyB].Format == EHapbeatStreamWireFormat::LegacyV1);
	TestTrue(TEXT("Gen 1 BEGIN for B sent"), WaitForCount(*Log, AddrB, FHapbeatProtocol::CmdStreamBegin, 3));
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
		TestTrue(TEXT("class change: old Gen 2 session ENDs (with its envelope) before the Gen 1 BEGIN"),
			V2EndIndex != INDEX_NONE && V1BeginIndex != INDEX_NONE && V2EndIndex < V1BeginIndex);
		if (V2EndIndex != INDEX_NONE)
		{
			TestEqual(TEXT("the Gen 2 END carries the retired session identity"),
				EnvelopeOf(Entries[V2EndIndex].Packet).Generation, 2u);
		}
	}

	// --- Class change Gen 1 -> Gen 2: Gen 1 END first, Gen 2 BEGIN immediately.
	const int32 BPacketsBeforeUpdate = Log->Snapshot(AddrB).Num();
	const TPair<uint16, int64> Update = IssuePing();
	Deliver(TEXT("192.0.2.81"), Update.Key, V2Pong(Update.Value, "player_1/pos_r_arm", Inc, BootB, 3));
	TestEqual(TEXT("matched HBS2 reply turns B back to Gen 2"), ClassOf(KeyB), EHapbeatStreamEndpointClass::V2);
	TestTrue(TEXT("Gen 2 BEGIN is not held by the Gen 1 guard"),
		Sub->StreamSessions.Contains(KeyB) && Sub->StreamSessions[KeyB].Format == EHapbeatStreamWireFormat::SessionV2);
	if (Sub->StreamSessions.Contains(KeyB))
	{
		TestEqual(TEXT("generation keeps increasing under the same lease"), Sub->StreamSessions[KeyB].Identity.Generation, 3u);
	}
	TestTrue(TEXT("Gen 2 BEGIN for B sent"), WaitForCount(*Log, AddrB, FHapbeatProtocol::CmdStreamBegin, 4));
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
		TestTrue(TEXT("class change: Gen 1 END (empty payload) precedes the Gen 2 BEGIN"),
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
	TestEqual(TEXT("superseded stays Gen 2 (never Gen 1)"), ClassOf(KeyB), EHapbeatStreamEndpointClass::V2);
	const TPair<uint16, int64> Periodic = IssuePing();
	Deliver(TEXT("192.0.2.81"), Periodic.Key, V2Pong(Periodic.Value, "player_1/pos_r_arm", Inc, RebootedB, 2));
	TestFalse(TEXT("periodic discovery does not take the stream back"), Sub->StreamSessions.Contains(KeyB));
	TestTrue(TEXT("the Gen 1 endpoint is unaffected"), Sub->StreamSessions.Contains(KeyA));

	Sub->StopStream();
	TestEqual(TEXT("StopStream ends every session"), Sub->StreamSessions.Num(), 0);
	return true;
}

// ---------------------------------------------------------------------------
// Subsystem: reacquisition after supersede, foreground hook, identity-update
// discovery, Gen 2 lease-refresh cadence
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

	const FString AddrA = TEXT("player_1/pos_l_arm"); // Stream Gen 1 device
	const FString AddrB = TEXT("player_1/pos_r_arm"); // Gen 2 device
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
	if (!TestTrue(TEXT("Gen 1 + Gen 2 sessions running"),
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

	TestEqual(TEXT("reacquire keeps the Gen 1 class"), ClassOf(KeyA), EHapbeatStreamEndpointClass::Legacy);
	TestTrue(TEXT("... and leaves the Gen 1 session running untouched"),
		Sub->StreamSessions.Contains(KeyA) && Sub->StreamSessions[KeyA].Runnable == RunningA);
	TestEqual(TEXT("... without sending it an END"),
		Log->Count(AddrA, FHapbeatProtocol::CmdStreamEnd), 0);
	TestEqual(TEXT("reacquire forgets the superseded Gen 2 lease"), ClassOf(KeyB), EHapbeatStreamEndpointClass::Unknown);

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
	TestTrue(TEXT("Gen 1 session still the same runner"),
		Sub->StreamSessions.Contains(KeyA) && Sub->StreamSessions[KeyA].Runnable == RunningA);

	// --- Reacquire while a Gen 2 session is running: END under the relinquished
	// identity first, nothing more under it afterwards.
	const int32 BPacketsBefore = Log->Snapshot(AddrB).Num();
	Sub->ReacquireStreamOwnership();
	TestFalse(TEXT("reacquire ends the running Gen 2 session"), Sub->StreamSessions.Contains(KeyB));
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
	TestEqual(TEXT("the Gen 1 endpoint never saw an END through all of it"),
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

// ===========================================================================
// Hapbeat.Streaming.Gen2.*: the 1.x additions (DEC-096; contracts 46f246e,
// specs/stream-session.md, specs/sdk-multi-stream.md, specs/message-format.md).
// Fixture hex strings are copied from repos-core/hapbeat-contracts/fixtures at
// contracts 46f246e; each copy names its <file>.json and <key>.<name>.
// Unreal has no live (push) stream source -- StreamClip / PlayEntry only queue
// whole clips -- so the "idle live source sends silence" conformance item does
// not apply here.
// ===========================================================================

namespace HapbeatStreamSessionV2Test
{
	/** Stereo PCM16 whose left and right sample at frame N is N (a position probe). */
	TArray<uint8> MakeRampPcm(int32 Frames)
	{
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(Frames * 4);
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			const uint16 Value = static_cast<uint16>(static_cast<int16>(Frame));
			for (int32 Channel = 0; Channel < 2; ++Channel)
			{
				Bytes[(Frame * 2 + Channel) * 2] = static_cast<uint8>(Value & 0xFF);
				Bytes[(Frame * 2 + Channel) * 2 + 1] = static_cast<uint8>((Value >> 8) & 0xFF);
			}
		}
		return Bytes;
	}

	/** Sample (Frame, Channel) of a STREAM_DATA packet of either format. */
	int16 DataSample(const TArray<uint8>& Packet, int32 Frame, int32 Channel)
	{
		const int32 Envelope = Packet[2] == FHapbeatProtocol::StreamProtocolVersion ? FHapbeatProtocol::StreamSessionEnvelopeSize : 0;
		const int32 Offset = FHapbeatProtocol::HeaderSize + Envelope + 4 + (Frame * 2 + Channel) * 2;
		return static_cast<int16>(static_cast<uint16>(Packet[Offset] | (Packet[Offset + 1] << 8)));
	}

	/** An ERROR whose detail is the envelope of Identity (cut to DetailLength bytes). */
	FHapbeatProtocol::FErrorInfo StreamError(uint16 Code, const FHapbeatStreamSessionIdentity& Identity,
		int32 DetailLength = FHapbeatProtocol::StreamSessionEnvelopeSize)
	{
		FHapbeatProtocol::FErrorInfo Error;
		Error.ErrorCode = Code;
		Error.Message = TEXT("test");
		Error.bHasReference = true;
		Error.RefCommand = FHapbeatProtocol::CmdStreamData;
		AppendLe(Error.Detail, Identity.DeviceBootId, 8);
		AppendLe(Error.Detail, Identity.LeaseTicket, 4);
		AppendLe(Error.Detail, Identity.Generation, 4);
		Error.Detail.SetNum(DetailLength);
		return Error;
	}

	/** Packets a bare FHapbeatStreamer produced, in order. */
	struct FStreamerCapture
	{
		TArray<TArray<uint8>> Packets;

		TFunction<void(const TArray<uint8>&)> Sink()
		{
			return [this](const TArray<uint8>& Packet) { Packets.Add(Packet); };
		}
		int32 Count(uint8 Cmd, int32 From = 0) const
		{
			int32 Result = 0;
			for (int32 Index = From; Index < Packets.Num(); ++Index)
			{
				Result += Packets[Index][3] == Cmd ? 1 : 0;
			}
			return Result;
		}
	};

}

// ---------------------------------------------------------------------------
// PONG classification: every pong_classification case, expected_1x
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatGen2PongClassificationFixtureTest,
	"Hapbeat.Streaming.Gen2.PongClassificationFixture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatGen2PongClassificationFixtureTest::RunTest(const FString& Parameters)
{
	using namespace HapbeatStreamSessionV2Test;
	struct FCase
	{
		const TCHAR* Name;
		const TCHAR* Hex;
		EHapbeatPongLeaseTail Tail;
		bool bLeaseValid;
		bool bSuperseded;
		uint32 Ticket;
		uint32 HighWater;
		bool bReissued;
	};
	// stream-session-cases.json pong_classification.cases[*].hex / expected_1x
	// (contracts 46f246e). Each is a matched direct reply to pong_classification.ping_hex.
	const FCase Cases[] = {
		{ TEXT("no_tail"),
			TEXT("4248011101013d0000401e18240a060088531e18240a0600486170626561742d4100706c617965725f312f706f735f63686573742f67726f75705f3100312e302e300008c8"),
			EHapbeatPongLeaseTail::Absent, false, false, 0, 0, false },
		{ TEXT("tail_32"),
			TEXT("4248011101015d0000401e18240a060088531e18240a0600486170626561742d4100706c617965725f312f706f735f63686573742f67726f75705f3100312e302e300008c84842533201010000887766554433221108070605040302010700000007000000"),
			EHapbeatPongLeaseTail::Valid, true, false, 7, 7, false },
		{ TEXT("tail_superseded"),
			TEXT("4248011101015d0000401e18240a060088531e18240a0600486170626561742d4100706c617965725f312f706f735f63686573742f67726f75705f3100312e302e300008c84842533201030000887766554433221108070605040302010500000007000000"),
			EHapbeatPongLeaseTail::Valid, true, true, 5, 7, false },
		{ TEXT("tail_40"),
			TEXT("424801110101650000401e18240a060088531e18240a0600486170626561742d4100706c617965725f312f706f735f63686573742f67726f75705f3100312e302e300008c848425332010100008877665544332211080706050403020107000000070000000001020304050607"),
			EHapbeatPongLeaseTail::Valid, true, false, 7, 7, false },
		{ TEXT("tail_reissued"),
			TEXT("4248011101015d0000401e18240a060088531e18240a0600486170626561742d4100706c617965725f312f706f735f63686573742f67726f75705f3100312e302e300008c84842533201050000887766554433221108070605040302010900000007000000"),
			EHapbeatPongLeaseTail::Valid, true, false, 9, 7, true },
		{ TEXT("tail_unknown_flag"),
			TEXT("4248011101015d0000401e18240a060088531e18240a0600486170626561742d4100706c617965725f312f706f735f63686573742f67726f75705f3100312e302e300008c84842533201090000887766554433221108070605040302010700000007000000"),
			EHapbeatPongLeaseTail::Valid, true, false, 7, 7, false },
		{ TEXT("tail_reserved_nonzero"),
			TEXT("4248011101015d0000401e18240a060088531e18240a0600486170626561742d4100706c617965725f312f706f735f63686573742f67726f75705f3100312e302e300008c84842533201010201887766554433221108070605040302010700000007000000"),
			EHapbeatPongLeaseTail::Valid, true, false, 7, 7, false },
		{ TEXT("tail_ext_version_2"),
			TEXT("4248011101015d0000401e18240a060088531e18240a0600486170626561742d4100706c617965725f312f706f735f63686573742f67726f75705f3100312e302e300008c84842533202010000887766554433221108070605040302010700000007000000"),
			EHapbeatPongLeaseTail::Ignored, false, false, 0, 0, false },
		{ TEXT("tail_short"),
			TEXT("424801110101550000401e18240a060088531e18240a0600486170626561742d4100706c617965725f312f706f735f63686573742f67726f75705f3100312e302e300008c8484253320101000088776655443322110807060504030201"),
			EHapbeatPongLeaseTail::Ignored, false, false, 0, 0, false },
		{ TEXT("other_bytes"),
			TEXT("4248011101015d0000401e18240a060088531e18240a0600486170626561742d4100706c617965725f312f706f735f63686573742f67726f75705f3100312e302e300008c85854524100000000000000000000000000000000000000000000000000000000"),
			EHapbeatPongLeaseTail::Ignored, false, false, 0, 0, false },
		{ TEXT("truncated_marker"),
			TEXT("424801110101400000401e18240a060088531e18240a0600486170626561742d4100706c617965725f312f706f735f63686573742f67726f75705f3100312e302e300008c8484253"),
			EHapbeatPongLeaseTail::Ignored, false, false, 0, 0, false },
		{ TEXT("non_streaming_node"),
			TEXT("4248011101015d0000401e18240a060088531e18240a0600486170626561742d4100706c617965725f312f706f735f63686573742f67726f75705f3100312e302e300008c84842533201000000887766554433221108070605040302010000000000000000"),
			EHapbeatPongLeaseTail::Valid, false, false, 0, 0, false },
	};
	TestEqual(TEXT("every fixture case is covered"), static_cast<int32>(UE_ARRAY_COUNT(Cases)), 12);

	for (const FCase& Case : Cases)
	{
		const TArray<uint8> Datagram = FromHex(Case.Hex);
		FHapbeatProtocol::FParsedPacket Parsed;
		if (!TestTrue(FString::Printf(TEXT("%s: datagram parses"), Case.Name),
				FHapbeatProtocol::ParsePacket(Datagram.GetData(), Datagram.Num(), Parsed)))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s: whole payload present"), Case.Name), Parsed.PayloadAvail, static_cast<int32>(Parsed.PayloadLength));
		TestEqual(FString::Printf(TEXT("%s: seq echoes the PING"), Case.Name), Parsed.Seq, static_cast<uint16>(257));
		FHapbeatProtocol::FPongInfo Pong;
		if (!TestTrue(FString::Printf(TEXT("%s: PONG parses"), Case.Name),
				FHapbeatProtocol::ParsePong(Parsed.Payload, Parsed.PayloadAvail, Pong)))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s: timestamp echoed"), Case.Name), Pong.Timestamp, static_cast<int64>(1700000000000000LL));
		TestEqual(FString::Printf(TEXT("%s: address"), Case.Name), Pong.Address, FString(TEXT("player_1/pos_chest/group_1")));
		TestTrue(FString::Printf(TEXT("%s: class"), Case.Name), Pong.LeaseTail == Case.Tail);
		if (Case.Tail == EHapbeatPongLeaseTail::Valid)
		{
			TestEqual(FString::Printf(TEXT("%s: lease_valid"), Case.Name), Pong.bLeaseValid, Case.bLeaseValid);
			TestEqual(FString::Printf(TEXT("%s: superseded"), Case.Name), Pong.bLeaseSuperseded, Case.bSuperseded);
			TestEqual(FString::Printf(TEXT("%s: ticket"), Case.Name), Pong.Lease.LeaseTicket, Case.Ticket);
			TestEqual(FString::Printf(TEXT("%s: high_water_ticket"), Case.Name), Pong.HighWaterTicket, Case.HighWater);
			TestEqual(FString::Printf(TEXT("%s: lease_reissued"), Case.Name), Pong.bLeaseReissued, Case.bReissued);
			TestEqual(FString::Printf(TEXT("%s: echoed incarnation"), Case.Name), Pong.EchoedClientIncarnation, 0x1122334455667788ULL);
			TestEqual(FString::Printf(TEXT("%s: boot id"), Case.Name), Pong.Lease.DeviceBootId, 0x0102030405060708ULL);
		}
	}

	// The PING the cases answer: pong_classification.ping_hex.
	TestEqual(TEXT("ping_hex is this SDK's extended PING"),
		FHapbeatProtocol::BuildPing(257, 1700000000000000LL, 1234605616436508552ULL),
		FromHex(TEXT("424801100101100000401e18240a06008877665544332211")));
	return true;
}

// ---------------------------------------------------------------------------
// CAPS_REQ / CAPS
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatGen2CapsTest,
	"Hapbeat.Streaming.Gen2.Caps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatGen2CapsTest::RunTest(const FString& Parameters)
{
	using namespace HapbeatStreamSessionV2Test;
	// wire-extension-cases.json caps_req.hex (contracts 46f246e).
	TestEqual(TEXT("CAPS_REQ matches the fixture"), FHapbeatProtocol::BuildCapsRequest(0x0202),
		FromHex(TEXT("424801120202010001")));

	auto ParseCapsHex = [this](const TCHAR* Hex, FHapbeatProtocol::FCapsInfo& Out)
	{
		const TArray<uint8> Datagram = FromHex(Hex);
		FHapbeatProtocol::FParsedPacket Parsed;
		return TestTrue(TEXT("CAPS datagram parses"), FHapbeatProtocol::ParsePacket(Datagram.GetData(), Datagram.Num(), Parsed))
			&& TestEqual(TEXT("command is CAPS"), Parsed.Cmd, FHapbeatProtocol::CmdCaps)
			&& TestTrue(TEXT("CAPS payload parses"), FHapbeatProtocol::ParseCaps(Parsed.Payload, Parsed.PayloadAvail, Out));
	};
	// wire-extension-cases.json caps.band_1x.
	FHapbeatProtocol::FCapsInfo Band;
	if (ParseCapsHex(TEXT("424801130202240001000101010205312e302e3003010204010705040f20050006010207013f080403000000"), Band))
	{
		TestEqual(TEXT("band_1x caps_version"), Band.CapsVersion, 1);
		TestEqual(TEXT("band_1x generation"), Band.Generation, 1);
		TestEqual(TEXT("band_1x firmware_version"), Band.FirmwareVersion, FString(TEXT("1.0.0")));
		TestEqual(TEXT("band_1x stream_gens = [2]"), Band.StreamGens, 0x02);
		TestEqual(TEXT("band_1x stream_formats = pcm16, adpcm, opus"), Band.StreamFormats, 0x07);
		TestEqual(TEXT("band_1x lease_ttl_s"), Band.LeaseTtlSeconds, 15);
		TestEqual(TEXT("band_1x lease_slots"), Band.LeaseSlots, 32);
		TestEqual(TEXT("band_1x inactivity_timeout_s"), Band.InactivityTimeoutSeconds, 5);
		TestEqual(TEXT("band_1x haptic_channels"), Band.HapticChannels, 2);
		TestEqual(TEXT("band_1x address_max_len"), Band.AddressMaxLen, 63);
		TestEqual(TEXT("band_1x features = play_pan, error_reply"), Band.Features, static_cast<int64>(0x03));
	}
	// wire-extension-cases.json caps.future_entries.
	FHapbeatProtocol::FCapsInfo Future;
	if (ParseCapsHex(TEXT("4248011302021c0001000903010203010101800365787005060f20050009090600010107"), Future))
	{
		TestEqual(TEXT("future_entries caps_version"), Future.CapsVersion, 1);
		TestEqual(TEXT("future_entries generation (first wins)"), Future.Generation, 1);
		TestEqual(TEXT("future_entries lease_ttl_s (longer value read from its start)"), Future.LeaseTtlSeconds, 15);
		TestEqual(TEXT("future_entries lease_slots"), Future.LeaseSlots, 32);
		TestEqual(TEXT("future_entries inactivity_timeout_s"), Future.InactivityTimeoutSeconds, 5);
		TestEqual(TEXT("future_entries empty haptic_channels is ignored"), Future.HapticChannels, -1);
		TestTrue(TEXT("future_entries has nothing else"), Future.FirmwareVersion.IsEmpty() && Future.StreamGens == -1
			&& Future.StreamFormats == -1 && Future.AddressMaxLen == -1 && Future.Features == -1);
	}

	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UHapbeatSubsystem* Sub = NewObject<UHapbeatSubsystem>(GameInstance);
	Sub->bSuppressStreamDiscoveryForAutomationTest = true;
	const double Now = FPlatformTime::Seconds();

	// --- No reply to three requests 500 ms apart: the device reports none.
	const FString Silent = TEXT("192.0.2.130:7700");
	Sub->MaybeRequestCaps(TEXT("192.0.2.130"), 7700, TEXT("0.4.2"), 0);
	TestEqual(TEXT("first CAPS_REQ at once"), Sub->CapsRequestsForAutomationTest.Num(), 1);
	Sub->TickCapsRequests(Now + 0.1);
	TestEqual(TEXT("no retry before 500 ms"), Sub->CapsRequestsForAutomationTest.Num(), 1);
	Sub->TickCapsRequests(Now + 10.0);
	Sub->TickCapsRequests(Now + 20.0);
	TestEqual(TEXT("three requests in all"), Sub->CapsRequestsForAutomationTest.Num(), 3);
	Sub->TickCapsRequests(Now + 30.0);
	TestEqual(TEXT("no fourth request"), Sub->CapsRequestsForAutomationTest.Num(), 3);
	TestTrue(TEXT("unanswered -> reports none"),
		Sub->CapsByRoute.FindChecked(Silent).Status == UHapbeatSubsystem::FCapsState::EStatus::None);
	TestFalse(TEXT("a device without CAPS keeps the pan"), Sub->IsSingleMotorRoute(TEXT("192.0.2.130"), 7700));
	Sub->MaybeRequestCaps(TEXT("192.0.2.130"), 7700, TEXT("0.4.2"), 0);
	TestEqual(TEXT("same firmware and boot is not asked again"), Sub->CapsRequestsForAutomationTest.Num(), 3);
	Sub->MaybeRequestCaps(TEXT("192.0.2.130"), 7700, TEXT("1.0.0"), 0);
	TestEqual(TEXT("a new firmware_version is asked again"), Sub->CapsRequestsForAutomationTest.Num(), 4);

	// --- A CAPS must answer one of our requests.
	const FString Mono = TEXT("192.0.2.131:7700");
	Sub->MaybeRequestCaps(TEXT("192.0.2.131"), 7700, TEXT("1.0.0"), 0x31);
	const uint16 MonoSeq = Sub->CapsRequestsForAutomationTest.Last().Seq;
	FHapbeatProtocol::FCapsInfo MonoCaps = Band;
	MonoCaps.HapticChannels = 1;
	Sub->HandleCapsGameThread(TEXT("192.0.2.131"), 7700, static_cast<uint16>(MonoSeq + 1000), MonoCaps);
	TestTrue(TEXT("a CAPS answering no request is ignored"),
		Sub->CapsByRoute.FindChecked(Mono).Status == UHapbeatSubsystem::FCapsState::EStatus::Requesting);
	Sub->HandleCapsGameThread(TEXT("192.0.2.131"), 7700, MonoSeq, MonoCaps);
	TestTrue(TEXT("a CAPS answering our request is kept"),
		Sub->CapsByRoute.FindChecked(Mono).Status == UHapbeatSubsystem::FCapsState::EStatus::Received);
	TestTrue(TEXT("haptic_channels 1 -> mixed without pan"), Sub->IsSingleMotorRoute(TEXT("192.0.2.131"), 7700));
	Sub->MaybeRequestCaps(TEXT("192.0.2.131"), 7700, TEXT("1.0.0"), 0x32);
	TestTrue(TEXT("a new boot asks again"),
		Sub->CapsByRoute.FindChecked(Mono).Status == UHapbeatSubsystem::FCapsState::EStatus::Requesting);

	// --- A device of a later major: one warning per subsystem.
	AddExpectedMessage(TEXT("newer than this Hapbeat SDK"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 1, /*IsRegex=*/false);
	FHapbeatProtocol::FCapsInfo Major2 = Band;
	Major2.Generation = 2;
	for (const TCHAR* Ip : { TEXT("192.0.2.132"), TEXT("192.0.2.133") })
	{
		Sub->MaybeRequestCaps(Ip, 7700, TEXT("2.0.0"), 0);
		Sub->HandleCapsGameThread(Ip, 7700, Sub->CapsRequestsForAutomationTest.Last().Seq, Major2);
	}
	TestTrue(TEXT("the newer-major warning is latched"), Sub->bWarnedNewerDeviceMajor);
	return true;
}

// ---------------------------------------------------------------------------
// ERROR: fixtures, acceptance, per-code actions
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatGen2ErrorTest,
	"Hapbeat.Streaming.Gen2.Error",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatGen2ErrorTest::RunTest(const FString& Parameters)
{
	using namespace HapbeatStreamSessionV2Test;
	auto ParseErrorHex = [this](const TCHAR* Hex, FHapbeatProtocol::FErrorInfo& Out)
	{
		const TArray<uint8> Datagram = FromHex(Hex);
		FHapbeatProtocol::FParsedPacket Parsed;
		return TestTrue(TEXT("ERROR datagram parses"), FHapbeatProtocol::ParsePacket(Datagram.GetData(), Datagram.Num(), Parsed))
			&& TestEqual(TEXT("command is ERROR"), Parsed.Cmd, FHapbeatProtocol::CmdError)
			&& TestTrue(TEXT("ERROR payload parses"), FHapbeatProtocol::ParseErrorExtended(Parsed.Payload, Parsed.PayloadAvail, Out));
	};
	// wire-extension-cases.json error.stream_timed_out (contracts 46f246e).
	FHapbeatProtocol::FErrorInfo TimedOut;
	if (ParseErrorHex(TEXT("424801ff03032700140073747265616d2074696d6564206f7574003134121008070605040302010700000029000000"), TimedOut))
	{
		TestEqual(TEXT("stream_timed_out code"), TimedOut.ErrorCode, FHapbeatProtocol::ErrorStreamTimedOut);
		TestEqual(TEXT("stream_timed_out message"), TimedOut.Message, FString(TEXT("stream timed out")));
		TestTrue(TEXT("stream_timed_out has the reference fields"), TimedOut.bHasReference);
		TestEqual(TEXT("stream_timed_out ref_command"), TimedOut.RefCommand, static_cast<uint8>(49));
		TestEqual(TEXT("stream_timed_out ref_seq"), TimedOut.RefSeq, static_cast<uint16>(4660));
		TestEqual(TEXT("stream_timed_out detail"), TimedOut.Detail, FromHex(TEXT("08070605040302010700000029000000")));
	}
	// wire-extension-cases.json error.code_and_message_only.
	FHapbeatProtocol::FErrorInfo GenUnsupported;
	if (ParseErrorHex(TEXT("424801ff03031800100073747265616d2067656e203220726571756972656400"), GenUnsupported))
	{
		TestEqual(TEXT("code_and_message_only code"), GenUnsupported.ErrorCode, FHapbeatProtocol::ErrorStreamGenUnsupported);
		TestEqual(TEXT("code_and_message_only message"), GenUnsupported.Message, FString(TEXT("stream gen 2 required")));
		TestFalse(TEXT("code_and_message_only has no reference fields"), GenUnsupported.bHasReference);
		TestEqual(TEXT("code_and_message_only has no detail"), GenUnsupported.Detail.Num(), 0);
	}

	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UHapbeatSubsystem* Sub = NewObject<UHapbeatSubsystem>(GameInstance);
	Sub->bSuppressStreamDiscoveryForAutomationTest = true;
	const uint64 Inc = 0x2222333344445555ULL;
	Sub->ClientIncarnation = Inc;
	TSharedRef<FStreamPacketLog, ESPMode::ThreadSafe> Log = MakeShared<FStreamPacketLog, ESPMode::ThreadSafe>();
	Sub->StreamRunnableFactoryForAutomationTest =
		[Log](const FGuid& SourceId, TArray<uint8>&& Pcm16, const FString& Target,
			TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> Mirror, EHapbeatStreamWireFormat Format,
			const FHapbeatStreamSessionIdentity& Identity) -> FHapbeatStreamRunnable*
		{
			return new FLoggingStreamRunnable(SourceId, MoveTemp(Pcm16), Target, Mirror, Format, Identity, Log);
		};
	const FString AddrA = TEXT("player_1/pos_l_arm");
	const FString AddrB = TEXT("player_1/pos_r_arm");
	const FString KeyA = TEXT("192.0.2.140:7700|player_1/pos_l_arm");
	const FString KeyB = TEXT("192.0.2.141:7700|player_1/pos_r_arm");
	auto AddLoop = [&](const FString& Target)
	{
		UHapbeatStreamPlayback* Playback = NewObject<UHapbeatStreamPlayback>(Sub);
		Playback->Init(1.0f, 1.0f);
		Playback->SetLoop(true);
		UHapbeatSubsystem::FStreamSource& Source = Sub->StreamSources.Add(Playback->Id);
		Source.CanonicalPcm16 = MakeSessionTestPcm(1000, 1600);
		Source.Playback = Playback;
		Source.ResolvedTarget = Target;
		Sub->ActivePlaybacks.Add(Playback);
		return Playback;
	};
	AddLoop(AddrA);
	UHapbeatStreamPlayback* PlaybackB = AddLoop(AddrB);
	Sub->PendingPings.Add(500, Pending(30000, Inc));
	Sub->HandlePongGameThread(TEXT("192.0.2.140"), 7700, 500, 0, V2Pong(30000, "player_1/pos_l_arm", Inc, 0xA140, 2));
	Sub->HandlePongGameThread(TEXT("192.0.2.141"), 7700, 500, 0, V2Pong(30000, "player_1/pos_r_arm", Inc, 0xB141, 3));
	if (!TestTrue(TEXT("both Stream Gen 2 sessions run"), Sub->StreamSessions.Contains(KeyA) && Sub->StreamSessions.Contains(KeyB)))
	{
		Sub->StopStream();
		return false;
	}
	FHapbeatStreamRunnable* const RunnerA = Sub->StreamSessions[KeyA].Runnable;
	FHapbeatStreamRunnable* const RunnerB = Sub->StreamSessions[KeyB].Runnable;
	const FHapbeatStreamSessionIdentity IdentityA = RunnerA->GetCurrentIdentity();
	const FHapbeatStreamSessionIdentity IdentityB = RunnerB->GetCurrentIdentity();
	TestTrue(TEXT("B has sent its first BEGINs"), WaitForCount(*Log, AddrB, FHapbeatProtocol::CmdStreamBegin, 3));

	// --- Acceptance: none of these may be acted on (or even logged).
	const int32 LoggedBefore = Sub->LoggedErrorKeys.Num();
	Sub->HandleErrorGameThread(TEXT("192.0.2.199"), 7700, StreamError(FHapbeatProtocol::ErrorStreamNotStarted, IdentityB));
	Sub->HandleErrorGameThread(TEXT("192.0.2.141"), 7701, StreamError(FHapbeatProtocol::ErrorStreamNotStarted, IdentityB));
	FHapbeatStreamSessionIdentity Other = IdentityB;
	Other.Generation += 100;
	Sub->HandleErrorGameThread(TEXT("192.0.2.141"), 7700, StreamError(FHapbeatProtocol::ErrorStreamNotStarted, Other));
	Sub->HandleErrorGameThread(TEXT("192.0.2.141"), 7700, StreamError(FHapbeatProtocol::ErrorStreamNotStarted, IdentityB, 15));
	// An envelope of A's session reported by B's device names no session there.
	Sub->HandleErrorGameThread(TEXT("192.0.2.141"), 7700, StreamError(FHapbeatProtocol::ErrorStreamNotStarted, IdentityA));
	TestEqual(TEXT("unknown IP / other port / other envelope / short detail are ignored"),
		Sub->LoggedErrorKeys.Num(), LoggedBefore);

	// --- STREAM_NOT_STARTED: the identical BEGIN goes out again before the next DATA.
	const int32 BeforeResend = Log->Snapshot(AddrB).Num();
	Sub->HandleErrorGameThread(TEXT("192.0.2.141"), 7700, StreamError(FHapbeatProtocol::ErrorStreamNotStarted, IdentityB));
	TestEqual(TEXT("an accepted ERROR is logged once"), Sub->LoggedErrorKeys.Num(), LoggedBefore + 1);
	// Again a few blocks later: had the first request landed on a block that
	// repeats BEGIN anyway, this one cannot (blocks 0-2 and every 50th only).
	FPlatformProcess::Sleep(0.03f);
	Sub->HandleErrorGameThread(TEXT("192.0.2.141"), 7700, StreamError(FHapbeatProtocol::ErrorStreamNotStarted, IdentityB));
	TestEqual(TEXT("... and not again for the same code and device"), Sub->LoggedErrorKeys.Num(), LoggedBefore + 1);
	{
		// The resent BEGIN is the one before a DATA block the schedule (0-2, every 50th) would not repeat it for.
		auto HasOffScheduleBegin = [&]()
		{
			const TArray<FStreamPacketLog::FEntry> Entries = Log->Snapshot(AddrB);
			int32 DataInGeneration = 0;
			for (int32 Index = 0; Index < Entries.Num(); ++Index)
			{
				const TArray<uint8>& Packet = Entries[Index].Packet;
				if (Packet[3] == FHapbeatProtocol::CmdStreamData)
				{
					++DataInGeneration;
				}
				else if (Packet[3] == FHapbeatProtocol::CmdStreamBegin && Index >= BeforeResend
					&& EnvelopeOf(Packet) == IdentityB && !FHapbeatStreamer::ShouldRepeatBegin(DataInGeneration))
				{
					return true;
				}
			}
			return false;
		};
		const double Deadline = FPlatformTime::Seconds() + 2.0;
		while (!HasOffScheduleBegin() && FPlatformTime::Seconds() < Deadline)
		{
			FPlatformProcess::Sleep(0.005f);
		}
		TestTrue(TEXT("STREAM_NOT_STARTED: one more identical BEGIN"), HasOffScheduleBegin());
	}

	// --- STREAM_TIMED_OUT: END of this generation, BEGIN of the next, same lease.
	Sub->HandleErrorGameThread(TEXT("192.0.2.141"), 7700, StreamError(FHapbeatProtocol::ErrorStreamTimedOut, IdentityB));
	{
		const double Deadline = FPlatformTime::Seconds() + 2.0;
		while (RunnerB->GetCurrentIdentity() == IdentityB && FPlatformTime::Seconds() < Deadline)
		{
			FPlatformProcess::Sleep(0.005f);
		}
		const FHapbeatStreamSessionIdentity Next = RunnerB->GetCurrentIdentity();
		TestTrue(TEXT("STREAM_TIMED_OUT: a new generation of the same lease"),
			Next.GetLease() == IdentityB.GetLease() && Next.Generation > IdentityB.Generation);
		TestTrue(TEXT("... the old generation ENDs"), WaitForCount(*Log, AddrB, FHapbeatProtocol::CmdStreamEnd, 1));
		const TArray<FStreamPacketLog::FEntry> Entries = Log->Snapshot(AddrB);
		int32 OldEnd = INDEX_NONE;
		int32 NewBegin = INDEX_NONE;
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			const FHapbeatStreamSessionIdentity Envelope = EnvelopeOf(Entries[Index].Packet);
			if (OldEnd == INDEX_NONE && Entries[Index].Packet[3] == FHapbeatProtocol::CmdStreamEnd && Envelope == IdentityB)
			{
				OldEnd = Index;
			}
			if (NewBegin == INDEX_NONE && Entries[Index].Packet[3] == FHapbeatProtocol::CmdStreamBegin && Envelope == Next)
			{
				NewBegin = Index;
			}
		}
		TestTrue(TEXT("... END of the old generation before the BEGIN of the new one"),
			OldEnd != INDEX_NONE && NewBegin != INDEX_NONE && OldEnd < NewBegin);
	}

	// --- STREAM_SUPERSEDED: B defers as Superseded; A is untouched.
	AddExpectedMessage(TEXT("was superseded by another writer"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 1, /*IsRegex=*/false);
	Sub->HandleErrorGameThread(TEXT("192.0.2.141"), 7700,
		StreamError(FHapbeatProtocol::ErrorStreamSuperseded, RunnerB->GetCurrentIdentity()));
	TestFalse(TEXT("STREAM_SUPERSEDED ends B's session"), Sub->StreamSessions.Contains(KeyB));
	TestEqual(TEXT("... B's Playback waits"), PlaybackB->GetStatus(), EHapbeatStreamPlaybackStatus::Deferred);
	TestEqual(TEXT("... as Superseded"), PlaybackB->GetDeferredReason(), EHapbeatStreamDeferredReason::Superseded);
	TestTrue(TEXT("A's session is the same runner"), Sub->StreamSessions.Contains(KeyA) && Sub->StreamSessions[KeyA].Runnable == RunnerA);
	TestTrue(TEXT("... still on its first generation"), RunnerA->GetCurrentIdentity() == IdentityA);
	TestEqual(TEXT("... and never got an END"), Log->Count(AddrA, FHapbeatProtocol::CmdStreamEnd), 0);

	Sub->StopStream();
	return true;
}

// ---------------------------------------------------------------------------
// BEGIN repeat (stream-session.md Sender requirements 2)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatGen2BeginRepeatTest,
	"Hapbeat.Streaming.Gen2.BeginRepeat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatGen2BeginRepeatTest::RunTest(const FString& Parameters)
{
	using namespace HapbeatStreamSessionV2Test;
	const FHapbeatStreamSessionIdentity Identity(FHapbeatStreamLeaseIdentity(0xBE61, 4), 9);
	for (const EHapbeatStreamWireFormat Format : { EHapbeatStreamWireFormat::SessionV2, EHapbeatStreamWireFormat::LegacyV1 })
	{
		const bool bGen2 = Format == EHapbeatStreamWireFormat::SessionV2;
		FStreamerCapture Capture;
		auto Mirror = MakeShared<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>();
		Mirror->bLoop.store(true);
		FHapbeatStreamer Streamer(16000, 2, TEXT("player_1"), Mirror, []() { return static_cast<uint16>(3); },
			Capture.Sink(), 0.05f, Format, bGen2 ? Identity : FHapbeatStreamSessionIdentity());
		Streamer.AddSource(FGuid::NewGuid(), MakeSessionTestPcm(1000, 1600), Mirror);
		Streamer.Start(0.0);
		for (int32 Step = 0; Step <= 120; ++Step)
		{
			Streamer.Tick(Step * 0.01);
		}

		// Which DATA blocks a BEGIN was sent right before.
		TArray<int32> BeginBeforeBlock;
		int32 Blocks = 0;
		bool bIdentical = true;
		for (const TArray<uint8>& Packet : Capture.Packets)
		{
			if (Packet[3] == FHapbeatProtocol::CmdStreamBegin)
			{
				BeginBeforeBlock.Add(Blocks);
				// Same tuple every time: only the header's seq could differ.
				bIdentical &= Packet.Num() == Capture.Packets[0].Num()
					&& FMemory::Memcmp(Packet.GetData() + 6, Capture.Packets[0].GetData() + 6, Packet.Num() - 6) == 0;
			}
			else if (Packet[3] == FHapbeatProtocol::CmdStreamData)
			{
				++Blocks;
			}
		}
		TestTrue(TEXT("over a second of blocks was sent"), Blocks > 100);
		if (bGen2)
		{
			TestEqual(TEXT("Stream Gen 2: BEGIN before blocks 0, 1, 2, 50, 100"),
				BeginBeforeBlock, TArray<int32>({ 0, 1, 2, 50, 100 }));
			TestTrue(TEXT("... each one the identical BEGIN"), bIdentical);
		}
		else
		{
			TestEqual(TEXT("Stream Gen 1: one BEGIN only (that firmware restarts on every BEGIN)"),
				BeginBeforeBlock, TArray<int32>({ 0 }));
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// Own send gap of 3 s (stream-session.md Sender requirements 3)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatGen2OwnGapTest,
	"Hapbeat.Streaming.Gen2.OwnGap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatGen2OwnGapTest::RunTest(const FString& Parameters)
{
	using namespace HapbeatStreamSessionV2Test;
	const FHapbeatStreamLeaseIdentity Lease(0x6A9, 2);
	for (const double Gap : { 2.9, 3.2 })
	{
		const bool bRestart = Gap >= FHapbeatStreamer::OwnGapSeconds;
		FStreamerCapture Capture;
		uint32 NextGeneration = 1;
		auto Mirror = MakeShared<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>();
		Mirror->bLoop.store(true);
		FHapbeatStreamer Streamer(16000, 2, TEXT("player_1"), Mirror, []() { return static_cast<uint16>(3); },
			Capture.Sink(), 0.05f, EHapbeatStreamWireFormat::SessionV2, FHapbeatStreamSessionIdentity(Lease, 1),
			[&NextGeneration]() { return ++NextGeneration; });
		Streamer.AddSource(FGuid::NewGuid(), MakeRampPcm(16000), Mirror);
		Streamer.Start(0.0);
		for (int32 Step = 0; Step <= 20; ++Step)
		{
			Streamer.Tick(Step * 0.01);
		}
		// The last frame sent before the gap.
		int32 LastData = INDEX_NONE;
		for (int32 Index = 0; Index < Capture.Packets.Num(); ++Index)
		{
			LastData = Capture.Packets[Index][3] == FHapbeatProtocol::CmdStreamData ? Index : LastData;
		}
		const int32 FramesPerBlock = (Capture.Packets[LastData].Num() - FHapbeatProtocol::HeaderSize - 16 - 4) / 4;
		const int16 LastFrame = DataSample(Capture.Packets[LastData], FramesPerBlock - 1, 0);
		const int32 Before = Capture.Packets.Num();

		Streamer.Tick(0.2 + Gap);
		const int32 Sent = Capture.Packets.Num() - Before;
		if (!TestTrue(TEXT("something was sent after the gap"), Sent > 0))
		{
			continue;
		}
		if (!bRestart)
		{
			TestEqual(TEXT("under 3 s: no END"), Capture.Count(FHapbeatProtocol::CmdStreamEnd, Before), 0);
			TestEqual(TEXT("under 3 s: same generation"), Streamer.GetIdentity().Generation, 1u);
			continue;
		}
		const TArray<uint8>& First = Capture.Packets[Before];
		const TArray<uint8>& Second = Capture.Packets[Before + 1];
		const TArray<uint8>& Third = Capture.Packets[Before + 2];
		TestEqual(TEXT("3 s: END first"), First[3], FHapbeatProtocol::CmdStreamEnd);
		TestEqual(TEXT("... of the old generation"), EnvelopeOf(First).Generation, 1u);
		TestEqual(TEXT("then BEGIN"), Second[3], FHapbeatProtocol::CmdStreamBegin);
		TestTrue(TEXT("... of the next generation, same lease"), EnvelopeOf(Second) == FHapbeatStreamSessionIdentity(Lease, 2));
		TestEqual(TEXT("then DATA"), Third[3], FHapbeatProtocol::CmdStreamData);
		TestEqual(TEXT("... from wire offset 0"), static_cast<uint32>(ReadLe(Third, FHapbeatProtocol::HeaderSize + 16, 4)), 0u);
		TestEqual(TEXT("... continuing at the cursor (no skip, no rewind)"),
			static_cast<int32>(DataSample(Third, 0, 0)), static_cast<int32>(LastFrame) + 1);
		TestTrue(TEXT("no catch-up: only the send-ahead is sent"),
			Capture.Count(FHapbeatProtocol::CmdStreamData, Before) <= 6);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Retired identity: no BEGIN / DATA under it, END still goes out
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatGen2RetiredIdentityTest,
	"Hapbeat.Streaming.Gen2.RetiredIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatGen2RetiredIdentityTest::RunTest(const FString& Parameters)
{
	using namespace HapbeatStreamSessionV2Test;
	TSharedRef<FStreamPacketLog, ESPMode::ThreadSafe> Log = MakeShared<FStreamPacketLog, ESPMode::ThreadSafe>();
	auto Mirror = MakeShared<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>();
	Mirror->bLoop.store(true);
	const FString Target = TEXT("player_1/pos_chest");
	FLoggingStreamRunnable* Runner = new FLoggingStreamRunnable(FGuid::NewGuid(), MakeSessionTestPcm(1000, 1600), Target,
		Mirror, EHapbeatStreamWireFormat::SessionV2, FHapbeatStreamSessionIdentity(FHapbeatStreamLeaseIdentity(0x7E7, 1), 1), Log);
	FRunnableThread* Thread = FRunnableThread::Create(Runner, TEXT("HapbeatRetiredTest"), 0, TPri_Normal);
	TestTrue(TEXT("DATA flows"), WaitForCount(*Log, Target, FHapbeatProtocol::CmdStreamData, 5));
	Runner->Retire();
	TestTrue(TEXT("retired flag reads back"), Runner->IsRetired());
	FPlatformProcess::Sleep(0.03f); // a packet already past the check may still land
	const int32 AfterRetire = Log->Count(Target, FHapbeatProtocol::CmdStreamData) + Log->Count(Target, FHapbeatProtocol::CmdStreamBegin);
	FPlatformProcess::Sleep(0.2f);
	TestEqual(TEXT("no BEGIN / DATA under a retired identity"),
		Log->Count(Target, FHapbeatProtocol::CmdStreamData) + Log->Count(Target, FHapbeatProtocol::CmdStreamBegin), AfterRetire);
	Thread->Kill(true);
	delete Thread;
	TestEqual(TEXT("END still goes out"), Log->Count(Target, FHapbeatProtocol::CmdStreamEnd), 1);
	delete Runner;
	return true;
}


// ---------------------------------------------------------------------------
// ERROR STREAM_STOPPED (sdk-multi-stream.md §6.13)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatGen2StreamStoppedTest,
	"Hapbeat.Streaming.Gen2.StreamStopped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatGen2StreamStoppedTest::RunTest(const FString& Parameters)
{
	using namespace HapbeatStreamSessionV2Test;
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UHapbeatSubsystem* Sub = NewObject<UHapbeatSubsystem>(GameInstance);
	Sub->bSuppressStreamDiscoveryForAutomationTest = true;
	const uint64 Inc = 0x3333444455556666ULL;
	Sub->ClientIncarnation = Inc;
	TSharedRef<FStreamPacketLog, ESPMode::ThreadSafe> Log = MakeShared<FStreamPacketLog, ESPMode::ThreadSafe>();
	Sub->StreamRunnableFactoryForAutomationTest =
		[Log](const FGuid& SourceId, TArray<uint8>&& Pcm16, const FString& Target,
			TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> Mirror, EHapbeatStreamWireFormat Format,
			const FHapbeatStreamSessionIdentity& Identity) -> FHapbeatStreamRunnable*
		{
			return new FLoggingStreamRunnable(SourceId, MoveTemp(Pcm16), Target, Mirror, Format, Identity, Log);
		};
	auto AddPlayback = [&](const FString& Target, bool bLoop)
	{
		UHapbeatStreamPlayback* Playback = NewObject<UHapbeatStreamPlayback>(Sub);
		Playback->Init(1.0f, 1.0f);
		Playback->SetLoop(bLoop);
		UHapbeatSubsystem::FStreamSource& Source = Sub->StreamSources.Add(Playback->Id);
		Source.CanonicalPcm16 = MakeSessionTestPcm(1000, 32000); // 2 s: nothing ends by itself here
		Source.Playback = Playback;
		Source.ResolvedTarget = Target;
		Sub->ActivePlaybacks.Add(Playback);
		Sub->ReconcileStreamSources();
		return Playback;
	};
	auto WaitFinished = [&](const FString& Key)
	{
		const FHapbeatStreamRunnable* Runner = Sub->StreamSessions.Contains(Key) ? Sub->StreamSessions[Key].Runnable : nullptr;
		const double Deadline = FPlatformTime::Seconds() + 2.0;
		while (Runner != nullptr && !Runner->IsFinished() && FPlatformTime::Seconds() < Deadline)
		{
			FPlatformProcess::Sleep(0.005f);
		}
		return Runner == nullptr || Runner->IsFinished();
	};
	const FString AddrA = TEXT("player_1/pos_l_arm");
	const FString AddrB = TEXT("player_1/pos_r_arm");
	const FString KeyA = TEXT("192.0.2.150:7700|player_1/pos_l_arm");
	const FString KeyB = TEXT("192.0.2.151:7700|player_1/pos_r_arm");

	// One loop on both devices.
	UHapbeatStreamPlayback* Both = AddPlayback(TEXT(""), true);
	Sub->PendingPings.Add(600, Pending(40000, Inc));
	Sub->HandlePongGameThread(TEXT("192.0.2.150"), 7700, 600, 0, V2Pong(40000, "player_1/pos_l_arm", Inc, 0xA150, 1));
	Sub->HandlePongGameThread(TEXT("192.0.2.151"), 7700, 600, 0, V2Pong(40000, "player_1/pos_r_arm", Inc, 0xB151, 1));
	if (!TestTrue(TEXT("sessions on both devices"), Sub->StreamSessions.Contains(KeyA) && Sub->StreamSessions.Contains(KeyB)))
	{
		Sub->StopStream();
		return false;
	}
	TestTrue(TEXT("A streams"), WaitForCount(*Log, AddrA, FHapbeatProtocol::CmdStreamData, 3));

	// --- STOP_ALL on A: the Playback leaves A for good and keeps playing on B.
	Sub->HandleErrorGameThread(TEXT("192.0.2.150"), 7700,
		StreamError(FHapbeatProtocol::ErrorStreamStopped, Sub->StreamSessions[KeyA].Runnable->GetCurrentIdentity()));
	const UHapbeatSubsystem::FStreamSource& BothSource = Sub->StreamSources.FindChecked(Both->Id);
	TestTrue(TEXT("A is recorded as stopped for the Playback"), BothSource.StoppedEndpointKeys.Contains(KeyA));
	TestFalse(TEXT("... which leaves A"), BothSource.EndpointKeys.Contains(KeyA));
	TestTrue(TEXT("... and stays on B"), BothSource.EndpointKeys.Contains(KeyB));
	TestEqual(TEXT("... so it stays Active"), Both->GetStatus(), EHapbeatStreamPlaybackStatus::Active);
	TestTrue(TEXT("A's emptied session closes"), WaitFinished(KeyA));
	const int32 ADataAfter = Log->Count(AddrA, FHapbeatProtocol::CmdStreamData);
	const int32 BDataAfter = Log->Count(AddrB, FHapbeatProtocol::CmdStreamData);
	Sub->TickStream(0.0f);
	FPlatformProcess::Sleep(0.15f);
	Sub->ReconcileStreamSources();
	TestEqual(TEXT("no more DATA to A"), Log->Count(AddrA, FHapbeatProtocol::CmdStreamData), ADataAfter);
	TestTrue(TEXT("B keeps streaming"), Log->Count(AddrB, FHapbeatProtocol::CmdStreamData) > BDataAfter);
	TestFalse(TEXT("the Playback never rejoins A"), Sub->StreamSources.FindChecked(Both->Id).EndpointKeys.Contains(KeyA));

	// --- STOP_ALL on B as well: nothing left to play on, so both Playbacks stop
	// (a one-shot counts as played, a loop is stopped).
	UHapbeatStreamPlayback* OneShotB = AddPlayback(AddrB, false);
	TestTrue(TEXT("the one-shot joins B"), Sub->StreamSources.FindChecked(OneShotB->Id).EndpointKeys.Contains(KeyB));
	Sub->HandleErrorGameThread(TEXT("192.0.2.151"), 7700,
		StreamError(FHapbeatProtocol::ErrorStreamStopped, Sub->StreamSessions[KeyB].Runnable->GetCurrentIdentity()));
	TestEqual(TEXT("the one-shot on its only device stops"), OneShotB->GetStatus(), EHapbeatStreamPlaybackStatus::Stopped);
	TestEqual(TEXT("the loop with no device left stops"), Both->GetStatus(), EHapbeatStreamPlaybackStatus::Stopped);

	// --- A new Playback is not affected by the old STOP_ALL.
	TestTrue(TEXT("B's emptied session closes"), WaitFinished(KeyB));
	Sub->TickStream(0.0f);
	UHapbeatStreamPlayback* Fresh = AddPlayback(TEXT(""), true);
	TestEqual(TEXT("a new Playback plays"), Fresh->GetStatus(), EHapbeatStreamPlaybackStatus::Active);
	TestTrue(TEXT("... on both devices"), Sub->StreamSources.FindChecked(Fresh->Id).EndpointKeys.Contains(KeyA)
		&& Sub->StreamSources.FindChecked(Fresh->Id).EndpointKeys.Contains(KeyB));

	Sub->StopStream();
	return true;
}

// ---------------------------------------------------------------------------
// deferredReason and the endpoint class API (sdk-multi-stream.md §3)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatGen2DeferredReasonTest,
	"Hapbeat.Streaming.Gen2.DeferredReason",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatGen2DeferredReasonTest::RunTest(const FString& Parameters)
{
	using namespace HapbeatStreamSessionV2Test;
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UHapbeatSubsystem* Sub = NewObject<UHapbeatSubsystem>(GameInstance);
	Sub->bSuppressStreamDiscoveryForAutomationTest = true;
	const uint64 Inc = 0x4444555566667777ULL;
	Sub->ClientIncarnation = Inc;
	TSharedRef<FStreamPacketLog, ESPMode::ThreadSafe> Log = MakeShared<FStreamPacketLog, ESPMode::ThreadSafe>();
	Sub->StreamRunnableFactoryForAutomationTest =
		[Log](const FGuid& SourceId, TArray<uint8>&& Pcm16, const FString& Target,
			TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> Mirror, EHapbeatStreamWireFormat Format,
			const FHapbeatStreamSessionIdentity& Identity) -> FHapbeatStreamRunnable*
		{
			return new FLoggingStreamRunnable(SourceId, MoveTemp(Pcm16), Target, Mirror, Format, Identity, Log);
		};
	auto AddLoop = [&](const TCHAR* Target)
	{
		UHapbeatStreamPlayback* Playback = NewObject<UHapbeatStreamPlayback>(Sub);
		Playback->Init(1.0f, 1.0f);
		Playback->SetLoop(true);
		UHapbeatSubsystem::FStreamSource& Source = Sub->StreamSources.Add(Playback->Id);
		Source.CanonicalPcm16 = MakeSessionTestPcm(1000, 1600);
		Source.Playback = Playback;
		Source.ResolvedTarget = Target;
		Sub->ActivePlaybacks.Add(Playback);
		return Playback;
	};
	UHapbeatStreamPlayback* OnUnknown = AddLoop(TEXT("player_1/pos_unknown"));
	UHapbeatStreamPlayback* OnSuperseded = AddLoop(TEXT("player_1/pos_superseded"));
	UHapbeatStreamPlayback* OnNoLease = AddLoop(TEXT("player_1/pos_nolease"));
	UHapbeatStreamPlayback* OnNothing = AddLoop(TEXT("player_9"));
	UHapbeatStreamPlayback* OnAll = AddLoop(TEXT("player_1"));
	UHapbeatStreamPlayback* OnGen1Guard = AddLoop(TEXT("player_2"));

	AddExpectedMessage(TEXT("was superseded by another writer"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 1, /*IsRegex=*/false);
	Sub->PendingPings.Add(700, Pending(50000, Inc));
	// Never answered a lease request.
	Sub->HandlePongGameThread(TEXT("192.0.2.160"), 7700, 0, 0, LegacyPong(0, "player_1/pos_unknown"));
	// Another writer holds it.
	Sub->HandlePongGameThread(TEXT("192.0.2.161"), 7700, 700, 0, V2Pong(50000, "player_1/pos_superseded", Inc, 0x161, 2, 0x03));
	// lease_valid = 0 (no lease slot).
	Sub->HandlePongGameThread(TEXT("192.0.2.162"), 7700, 700, 0, V2Pong(50000, "player_1/pos_nolease", Inc, 0x162, 0, 0x00));
	// A Stream Gen 1 device that just got an END: its 300 ms guard is running.
	Sub->HandlePongGameThread(TEXT("192.0.2.163"), 7700, 700, 0, LegacyPong(50000, "player_2/pos_chest"));
	const FString KeyGen1 = TEXT("192.0.2.163:7700|player_2/pos_chest");
	Sub->StopStreamSession(KeyGen1);
	Sub->ReconcileStreamSources();

	TestEqual(TEXT("unknown class -> UnknownClass"), OnUnknown->GetDeferredReason(), EHapbeatStreamDeferredReason::UnknownClass);
	TestEqual(TEXT("superseded -> Superseded"), OnSuperseded->GetDeferredReason(), EHapbeatStreamDeferredReason::Superseded);
	TestEqual(TEXT("lease_valid 0 -> LeaseUnavailable"), OnNoLease->GetDeferredReason(), EHapbeatStreamDeferredReason::LeaseUnavailable);
	TestEqual(TEXT("no matching device -> NoResolvedEndpoint"), OnNothing->GetDeferredReason(), EHapbeatStreamDeferredReason::NoResolvedEndpoint);
	TestEqual(TEXT("several reasons: Superseded first"), OnAll->GetDeferredReason(), EHapbeatStreamDeferredReason::Superseded);
	for (UHapbeatStreamPlayback* Playback : { OnUnknown, OnSuperseded, OnNoLease, OnNothing, OnAll })
	{
		TestEqual(TEXT("each of them is Deferred"), Playback->GetStatus(), EHapbeatStreamPlaybackStatus::Deferred);
	}
	TestFalse(TEXT("the Stream Gen 1 guard holds the session back"), Sub->StreamSessions.Contains(KeyGen1));
	TestEqual(TEXT("... but the Playback stays Active while it waits"), OnGen1Guard->GetStatus(), EHapbeatStreamPlaybackStatus::Active);

	// The endpoint class API.
	TMap<FString, FHapbeatStreamEndpointInfo> ByIp;
	for (const FHapbeatStreamEndpointInfo& Info : Sub->GetStreamEndpoints())
	{
		ByIp.Add(Info.Ip, Info);
	}
	TestEqual(TEXT("four endpoints are reported"), ByIp.Num(), 4);
	if (ByIp.Num() == 4)
	{
		TestEqual(TEXT("unknown: class"), ByIp[TEXT("192.0.2.160")].StreamClass, EHapbeatStreamClass::Unknown);
		TestEqual(TEXT("unknown: lease"), ByIp[TEXT("192.0.2.160")].LeaseState, EHapbeatStreamLeaseState::None);
		TestEqual(TEXT("superseded: class"), ByIp[TEXT("192.0.2.161")].StreamClass, EHapbeatStreamClass::Gen2);
		TestEqual(TEXT("superseded: lease"), ByIp[TEXT("192.0.2.161")].LeaseState, EHapbeatStreamLeaseState::Superseded);
		TestEqual(TEXT("no lease: lease"), ByIp[TEXT("192.0.2.162")].LeaseState, EHapbeatStreamLeaseState::Unavailable);
		TestEqual(TEXT("Stream Gen 1: class"), ByIp[TEXT("192.0.2.163")].StreamClass, EHapbeatStreamClass::Gen1);
		TestEqual(TEXT("address is reported"), ByIp[TEXT("192.0.2.163")].Address, FString(TEXT("player_2/pos_chest")));
	}

	// A lease that becomes valid makes the Playback Active and reports Valid.
	Sub->PendingPings.Add(701, Pending(50001, Inc));
	Sub->HandlePongGameThread(TEXT("192.0.2.162"), 7700, 701, 0, V2Pong(50001, "player_1/pos_nolease", Inc, 0x162, 4, 0x01));
	TestEqual(TEXT("a valid lease plays"), OnNoLease->GetStatus(), EHapbeatStreamPlaybackStatus::Active);
	TestEqual(TEXT("... and one playing device makes a multi-device Playback Active"),
		OnAll->GetStatus(), EHapbeatStreamPlaybackStatus::Active);
	for (const FHapbeatStreamEndpointInfo& Info : Sub->GetStreamEndpoints())
	{
		if (Info.Ip == TEXT("192.0.2.162"))
		{
			TestEqual(TEXT("... reported as Valid"), Info.LeaseState, EHapbeatStreamLeaseState::Valid);
		}
	}
	Sub->StopStream();
	return true;
}

// ---------------------------------------------------------------------------
// Superseded hold: lifted only by a re-issued lease ([1.x])
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatGen2SupersededReissueTest,
	"Hapbeat.Streaming.Gen2.SupersededReissue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatGen2SupersededReissueTest::RunTest(const FString& Parameters)
{
	using namespace HapbeatStreamSessionV2Test;
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UHapbeatSubsystem* Sub = NewObject<UHapbeatSubsystem>(GameInstance);
	Sub->bSuppressStreamDiscoveryForAutomationTest = true;
	const uint64 Inc = 0x5555666677778888ULL;
	Sub->ClientIncarnation = Inc;
	TSharedRef<FStreamPacketLog, ESPMode::ThreadSafe> Log = MakeShared<FStreamPacketLog, ESPMode::ThreadSafe>();
	Sub->StreamRunnableFactoryForAutomationTest =
		[Log](const FGuid& SourceId, TArray<uint8>&& Pcm16, const FString& Target,
			TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> Mirror, EHapbeatStreamWireFormat Format,
			const FHapbeatStreamSessionIdentity& Identity) -> FHapbeatStreamRunnable*
		{
			return new FLoggingStreamRunnable(SourceId, MoveTemp(Pcm16), Target, Mirror, Format, Identity, Log);
		};
	UHapbeatStreamPlayback* Playback = NewObject<UHapbeatStreamPlayback>(Sub);
	Playback->Init(1.0f, 1.0f);
	Playback->SetLoop(true);
	UHapbeatSubsystem::FStreamSource& Source = Sub->StreamSources.Add(Playback->Id);
	Source.CanonicalPcm16 = MakeSessionTestPcm(1000, 1600);
	Source.Playback = Playback;
	Sub->ActivePlaybacks.Add(Playback);

	const FString Key = TEXT("192.0.2.170:7700|player_1/pos_chest");
	const uint64 Boot = 0x170;
	int64 NextTs = 60000;
	uint16 NextSeq = 800;
	auto Reply = [&](uint32 Ticket, uint8 Flags)
	{
		Sub->PendingPings.Add(NextSeq, Pending(NextTs, Inc, TEXT("192.0.2.170")));
		Sub->HandlePongGameThread(TEXT("192.0.2.170"), 7700, NextSeq, 0, V2Pong(NextTs, "player_1/pos_chest", Inc, Boot, Ticket, Flags));
		++NextSeq;
		NextTs += 1000;
	};
	Reply(3, 0x01);
	TestTrue(TEXT("streams under ticket 3"), Sub->StreamSessions.Contains(Key));

	AddExpectedMessage(TEXT("was superseded by another writer"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 1, /*IsRegex=*/false);
	Reply(3, 0x03);
	TestFalse(TEXT("superseded: deferred"), Sub->StreamSessions.Contains(Key));
	TestEqual(TEXT("... as Superseded"), Playback->GetDeferredReason(), EHapbeatStreamDeferredReason::Superseded);

	// The SDK's own entry expired and the device issued a new ticket WITHOUT
	// lease_reissued: the other writer may still be there, so the hold stays.
	Reply(4, 0x01);
	TestFalse(TEXT("a new ticket without lease_reissued keeps the hold"), Sub->StreamSessions.Contains(Key));
	TestTrue(TEXT("... still superseded"), Sub->StreamEndpoints.FindChecked(Key).LeaseState.bLeaseSuperseded);

	// lease_valid + !superseded + lease_reissued: the other writer left.
	Reply(5, 0x05);
	TestFalse(TEXT("lease_reissued lifts the hold"), Sub->StreamEndpoints.FindChecked(Key).LeaseState.bLeaseSuperseded);
	TestTrue(TEXT("... and the stream resumes under the re-issued ticket"),
		Sub->StreamSessions.Contains(Key) && Sub->StreamSessions[Key].Identity.LeaseTicket == 5u);
	TestEqual(TEXT("... Active again"), Playback->GetStatus(), EHapbeatStreamPlaybackStatus::Active);
	Sub->StopStream();
	return true;
}

// ---------------------------------------------------------------------------
// Ticket change under the same incarnation: the next session continues at the cursor
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatGen2CursorCarryTest,
	"Hapbeat.Streaming.Gen2.CursorCarry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatGen2CursorCarryTest::RunTest(const FString& Parameters)
{
	using namespace HapbeatStreamSessionV2Test;
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UHapbeatSubsystem* Sub = NewObject<UHapbeatSubsystem>(GameInstance);
	Sub->bSuppressStreamDiscoveryForAutomationTest = true;
	const uint64 Inc = 0x6666777788889999ULL;
	Sub->ClientIncarnation = Inc;
	TSharedRef<FStreamPacketLog, ESPMode::ThreadSafe> Log = MakeShared<FStreamPacketLog, ESPMode::ThreadSafe>();
	Sub->StreamRunnableFactoryForAutomationTest =
		[Log](const FGuid& SourceId, TArray<uint8>&& Pcm16, const FString& Target,
			TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> Mirror, EHapbeatStreamWireFormat Format,
			const FHapbeatStreamSessionIdentity& Identity) -> FHapbeatStreamRunnable*
		{
			return new FLoggingStreamRunnable(SourceId, MoveTemp(Pcm16), Target, Mirror, Format, Identity, Log);
		};
	// A one-shot whose sample value is its frame index.
	UHapbeatStreamPlayback* Playback = NewObject<UHapbeatStreamPlayback>(Sub);
	Playback->Init(1.0f, 1.0f);
	Playback->SetLoop(false);
	UHapbeatSubsystem::FStreamSource& Source = Sub->StreamSources.Add(Playback->Id);
	Source.CanonicalPcm16 = MakeRampPcm(16000);
	Source.Playback = Playback;
	Sub->ActivePlaybacks.Add(Playback);

	const FString Addr = TEXT("player_1/pos_chest");
	const FString Key = TEXT("192.0.2.180:7700|player_1/pos_chest");
	const uint64 Boot = 0x180;
	Sub->PendingPings.Add(900, Pending(70000, Inc, TEXT("192.0.2.180")));
	Sub->HandlePongGameThread(TEXT("192.0.2.180"), 7700, 900, 0, V2Pong(70000, "player_1/pos_chest", Inc, Boot, 3));
	if (!TestTrue(TEXT("streams under ticket 3"), Sub->StreamSessions.Contains(Key)))
	{
		Sub->StopStream();
		return false;
	}
	TestTrue(TEXT("some blocks went out"), WaitForCount(*Log, Addr, FHapbeatProtocol::CmdStreamData, 10));

	// Same incarnation, new ticket (the device's table dropped and re-issued the entry).
	Sub->PendingPings.Add(901, Pending(71000, Inc, TEXT("192.0.2.180")));
	Sub->HandlePongGameThread(TEXT("192.0.2.180"), 7700, 901, 0, V2Pong(71000, "player_1/pos_chest", Inc, Boot, 4));
	TestTrue(TEXT("a new session under ticket 4"),
		Sub->StreamSessions.Contains(Key) && Sub->StreamSessions[Key].Identity.LeaseTicket == 4u);
	TestTrue(TEXT("its BEGIN goes out"), WaitForCount(*Log, Addr, FHapbeatProtocol::CmdStreamEnd, 1));
	{
		const double Deadline = FPlatformTime::Seconds() + 2.0;
		auto NewTicketData = [&]()
		{
			for (const FStreamPacketLog::FEntry& Entry : Log->Snapshot(Addr))
			{
				if (Entry.Packet[3] == FHapbeatProtocol::CmdStreamData && EnvelopeOf(Entry.Packet).LeaseTicket == 4u)
				{
					return true;
				}
			}
			return false;
		};
		while (!NewTicketData() && FPlatformTime::Seconds() < Deadline)
		{
			FPlatformProcess::Sleep(0.005f);
		}
	}
	const TArray<FStreamPacketLog::FEntry> Entries = Log->Snapshot(Addr);
	int32 OldEnds = 0;
	int32 LastOldFrame = -1;
	int32 FirstNewFrame = -1;
	bool bEndBeforeNewBegin = false;
	bool bSawEnd = false;
	for (const FStreamPacketLog::FEntry& Entry : Entries)
	{
		const TArray<uint8>& Packet = Entry.Packet;
		const uint32 Ticket = EnvelopeOf(Packet).LeaseTicket;
		if (Packet[3] == FHapbeatProtocol::CmdStreamEnd && Ticket == 3u)
		{
			++OldEnds;
			bSawEnd = true;
		}
		if (Packet[3] == FHapbeatProtocol::CmdStreamBegin && Ticket == 4u && !bEndBeforeNewBegin)
		{
			bEndBeforeNewBegin = bSawEnd;
		}
		if (Packet[3] == FHapbeatProtocol::CmdStreamData)
		{
			const int32 Frames = (Packet.Num() - FHapbeatProtocol::HeaderSize - 16 - 4) / 4;
			if (Ticket == 3u)
			{
				LastOldFrame = DataSample(Packet, Frames - 1, 0);
			}
			else if (Ticket == 4u && FirstNewFrame < 0)
			{
				FirstNewFrame = DataSample(Packet, 0, 0);
			}
		}
	}
	TestEqual(TEXT("the old identity ENDs once"), OldEnds, 1);
	TestTrue(TEXT("... before the new BEGIN"), bEndBeforeNewBegin);
	TestTrue(TEXT("the new session does not start over at frame 0"), FirstNewFrame > 0);
	// A block the worker mixed between Retire and its join is dropped, not resent:
	// the cursor may be a block or two past the last frame on the wire.
	TestTrue(TEXT("... it continues right after the last frame sent"),
		FirstNewFrame >= LastOldFrame + 1 && FirstNewFrame <= LastOldFrame + 1 + 2 * 160);
	Sub->StopStream();
	return true;
}

// ---------------------------------------------------------------------------
// Unicast extended PINGs to lease targets, and their cadence
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatGen2LeasePingTest,
	"Hapbeat.Streaming.Gen2.LeasePing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatGen2LeasePingTest::RunTest(const FString& Parameters)
{
	using namespace HapbeatStreamSessionV2Test;
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UHapbeatSubsystem* Sub = NewObject<UHapbeatSubsystem>(GameInstance);
	Sub->bSuppressStreamDiscoveryForAutomationTest = true;
	const uint64 Inc = 0x7777888899990000ULL;
	Sub->ClientIncarnation = Inc;
	TSharedRef<FStreamPacketLog, ESPMode::ThreadSafe> Log = MakeShared<FStreamPacketLog, ESPMode::ThreadSafe>();
	Sub->StreamRunnableFactoryForAutomationTest =
		[Log](const FGuid& SourceId, TArray<uint8>&& Pcm16, const FString& Target,
			TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> Mirror, EHapbeatStreamWireFormat Format,
			const FHapbeatStreamSessionIdentity& Identity) -> FHapbeatStreamRunnable*
		{
			return new FLoggingStreamRunnable(SourceId, MoveTemp(Pcm16), Target, Mirror, Format, Identity, Log);
		};
	auto PingsTo = [&](const FString& Key)
	{
		int32 Count = 0;
		for (const UHapbeatSubsystem::FAutomationUnicastRecord& Record : Sub->LeasePingsForAutomationTest)
		{
			Count += Record.Key == Key ? 1 : 0;
		}
		return Count;
	};

	TestEqual(TEXT("answered endpoints are refreshed every TTL/3"),
		UHapbeatSubsystem::ComputeLeasePingIntervalSeconds(EHapbeatStreamEndpointClass::V2), 5.0);
	TestEqual(TEXT("... Stream Gen 1 too"),
		UHapbeatSubsystem::ComputeLeasePingIntervalSeconds(EHapbeatStreamEndpointClass::Legacy), 5.0);
	TestTrue(TEXT("an unanswered endpoint is asked again within 1 s"),
		UHapbeatSubsystem::ComputeLeasePingIntervalSeconds(EHapbeatStreamEndpointClass::Unknown) <= 1.0);
	TestEqual(TEXT("TTL/3"), UHapbeatSubsystem::LeaseRefreshSeconds, UHapbeatSubsystem::LeaseTtlSeconds / 3.0);

	UHapbeatStreamPlayback* Playback = NewObject<UHapbeatStreamPlayback>(Sub);
	Playback->Init(1.0f, 1.0f);
	Playback->SetLoop(true);
	UHapbeatSubsystem::FStreamSource& Source = Sub->StreamSources.Add(Playback->Id);
	Source.CanonicalPcm16 = MakeSessionTestPcm(1000, 1600);
	Source.Playback = Playback;
	Source.ResolvedTarget = TEXT("player_1");
	Sub->ActivePlaybacks.Add(Playback);

	const FString KeyT = TEXT("192.0.2.190:7700|player_1/pos_chest");
	const FString KeyX = TEXT("192.0.2.191:7700|player_2/pos_chest");
	// Discovery (an unsolicited PONG) finds both devices.
	Sub->HandlePongGameThread(TEXT("192.0.2.190"), 7700, 0, 0, LegacyPong(0, "player_1/pos_chest"));
	Sub->HandlePongGameThread(TEXT("192.0.2.191"), 7700, 0, 0, LegacyPong(0, "player_2/pos_chest"));
	TestEqual(TEXT("a new lease target is asked at once"), PingsTo(KeyT), 1);
	TestEqual(TEXT("a device nothing streams to is never asked"), PingsTo(KeyX), 0);
	if (PingsTo(KeyT) != 1)
	{
		Sub->StopStream();
		return false;
	}
	const UHapbeatSubsystem::FAutomationUnicastRecord First = Sub->LeasePingsForAutomationTest.Last();
	const FHapbeatPendingPing* Request = Sub->PendingPings.Find(First.Seq);
	TestTrue(TEXT("the request is an extended (lease) PING"), Request != nullptr && Request->bLeaseRequest);
	TestTrue(TEXT("... unicast to that device only"), Request != nullptr && Request->TargetIp == TEXT("192.0.2.190"));
	TestTrue(TEXT("... under the current incarnation"), Request != nullptr && Request->ClientIncarnation == Inc);
	{
		const double Due = Sub->StreamEndpoints.FindChecked(KeyT).NextLeasePingSeconds - FPlatformTime::Seconds();
		TestTrue(TEXT("unanswered: asked again within 1 s"), Due > 0.5 && Due <= 1.0);
	}

	// The matched reply classifies it and sets the TTL/3 schedule.
	Sub->HandlePongGameThread(TEXT("192.0.2.190"), 7700, First.Seq, 0, V2Pong(First.TimestampUs, "player_1/pos_chest", Inc, 0x190, 2));
	TestTrue(TEXT("the reply classifies the target"), Sub->StreamSessions.Contains(KeyT));
	{
		const double Due = Sub->StreamEndpoints.FindChecked(KeyT).NextLeasePingSeconds - FPlatformTime::Seconds();
		TestTrue(TEXT("answered: next request in TTL/3 (5 s)"), Due > 4.5 && Due <= 5.0);
	}
	const double Now = FPlatformTime::Seconds();
	Sub->PumpLeasePings(Now);
	TestEqual(TEXT("nothing more before it is due"), PingsTo(KeyT), 1);
	Sub->StreamEndpoints.FindChecked(KeyT).NextLeasePingSeconds = Now - 0.01;
	Sub->PumpLeasePings(Now);
	TestEqual(TEXT("one request when it is due"), PingsTo(KeyT), 2);
	TestEqual(TEXT("still none to the other device"), PingsTo(KeyX), 0);

	// A session that ended keeps the device a target for one TTL.
	Playback->Stop();
	Sub->StopStreamSession(KeyT);
	UHapbeatSubsystem::FStreamEndpoint& Target = Sub->StreamEndpoints.FindChecked(KeyT);
	Target.LastSessionEndedSeconds = Now - 10.0;
	TestTrue(TEXT("10 s after its last session: still a target"), Sub->IsLeaseTarget(KeyT, Now));
	Target.LastSessionEndedSeconds = Now - (UHapbeatSubsystem::LeaseTtlSeconds + 1.0);
	TestFalse(TEXT("over one TTL after: not a target"), Sub->IsLeaseTarget(KeyT, Now));
	Target.NextLeasePingSeconds = 0.0;
	Sub->PumpLeasePings(Now);
	TestEqual(TEXT("... and not asked any more"), PingsTo(KeyT), 2);
	Sub->StopStream();
	return true;
}

// ---------------------------------------------------------------------------
// A matched reply whose tail is ignored: one warning per endpoint
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatGen2IgnoredTailWarningTest,
	"Hapbeat.Streaming.Gen2.IgnoredTailWarning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatGen2IgnoredTailWarningTest::RunTest(const FString& Parameters)
{
	using namespace HapbeatStreamSessionV2Test;
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UHapbeatSubsystem* Sub = NewObject<UHapbeatSubsystem>(GameInstance);
	Sub->bSuppressStreamDiscoveryForAutomationTest = true;
	const uint64 Inc = 0x8888999900001111ULL;
	Sub->ClientIncarnation = Inc;
	const FString Key = TEXT("192.0.2.200:7700|player_1/pos_chest");
	AddExpectedMessage(TEXT("answered the lease request with a reply this SDK cannot read"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 1, /*IsRegex=*/false);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const uint16 Seq = static_cast<uint16>(1000 + Index);
		const int64 Ts = 80000 + Index;
		Sub->PendingPings.Add(Seq, Pending(Ts, Inc, TEXT("192.0.2.200")));
		Sub->HandlePongGameThread(TEXT("192.0.2.200"), 7700, Seq, 0, IgnoredPong(Ts, "player_1/pos_chest", Inc));
	}
	TestTrue(TEXT("the endpoint is known"), Sub->StreamEndpoints.Contains(Key));
	TestEqual(TEXT("... and stays unknown"), Sub->StreamEndpoints.FindChecked(Key).LeaseState.Class, EHapbeatStreamEndpointClass::Unknown);
	// An unmatched reply with the same tail is not worth a warning.
	Sub->HandlePongGameThread(TEXT("192.0.2.201"), 7700, 0, 0, IgnoredPong(0, "player_1/pos_back", Inc));
	return true;
}

// ---------------------------------------------------------------------------
// Re-splice (sdk-multi-stream.md §6.14, §7 case 11)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatGen2RespliceTest,
	"Hapbeat.Streaming.Gen2.Resplice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatGen2RespliceTest::RunTest(const FString& Parameters)
{
	using namespace HapbeatStreamSessionV2Test;
	const FHapbeatStreamLeaseIdentity Lease(0x5911, 6);
	struct FRun
	{
		FStreamerCapture Capture;
		int32 BeforeAdd = 0;
	};
	// Starts a session (source A: frame index ramp), sends ahead, then adds B
	// (constant 1000) requested at RequestedAt.
	auto Run = [&](EHapbeatStreamWireFormat Format, double AddAt, FRun& Out)
	{
		uint32 NextGeneration = 1;
		auto MirrorA = MakeShared<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>();
		MirrorA->bLoop.store(true);
		auto MirrorB = MakeShared<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>();
		const bool bGen2 = Format == EHapbeatStreamWireFormat::SessionV2;
		FHapbeatStreamer Streamer(16000, 2, TEXT("player_1"), MirrorA, []() { return static_cast<uint16>(3); },
			Out.Capture.Sink(), 0.05f, Format, bGen2 ? FHapbeatStreamSessionIdentity(Lease, 1) : FHapbeatStreamSessionIdentity(),
			[&NextGeneration]() { return ++NextGeneration; });
		Streamer.AddSource(FGuid::NewGuid(), MakeRampPcm(16000), MirrorA);
		Streamer.Start(0.0);
		Streamer.Tick(0.0);
		Out.BeforeAdd = Out.Capture.Packets.Num();
		Streamer.AddSource(FGuid::NewGuid(), MakeSessionTestPcm(1000, 1600), MirrorB, 0.0, AddAt, AddAt);
		Streamer.Tick(AddAt);
		Streamer.SendEnd();
	};

	// Gen 2, 49 ms of audio queued after the new source's start: re-splice.
	{
		FRun Splice;
		Run(EHapbeatStreamWireFormat::SessionV2, 0.001, Splice);
		const TArray<TArray<uint8>>& Packets = Splice.Capture.Packets;
		if (TestTrue(TEXT("re-splice sends END, BEGIN and DATA"), Packets.Num() >= Splice.BeforeAdd + 3))
		{
			const TArray<uint8>& End = Packets[Splice.BeforeAdd];
			const TArray<uint8>& Begin = Packets[Splice.BeforeAdd + 1];
			const TArray<uint8>& Data = Packets[Splice.BeforeAdd + 2];
			TestEqual(TEXT("END of the current generation first"), End[3], FHapbeatProtocol::CmdStreamEnd);
			TestEqual(TEXT("... generation 1"), EnvelopeOf(End).Generation, 1u);
			TestEqual(TEXT("then BEGIN"), Begin[3], FHapbeatProtocol::CmdStreamBegin);
			TestTrue(TEXT("... of a new generation, same lease"), EnvelopeOf(Begin) == FHapbeatStreamSessionIdentity(Lease, 2));
			TestEqual(TEXT("then the rebuilt block"), Data[3], FHapbeatProtocol::CmdStreamData);
			TestEqual(TEXT("... from wire offset 0"), static_cast<uint32>(ReadLe(Data, FHapbeatProtocol::HeaderSize + 16, 4)), 0u);
			// A is rewound to the first resent block (frame 0); B enters at its
			// request time, 1 ms = 16 frames later.
			TestEqual(TEXT("A rewound to the start of the resent blocks"), static_cast<int32>(DataSample(Data, 0, 0)), 0);
			TestEqual(TEXT("B not yet started before its request time"), static_cast<int32>(DataSample(Data, 15, 0)), 15);
			TestEqual(TEXT("B mixed in from its first frame at its request time"), static_cast<int32>(DataSample(Data, 16, 0)), 16 + 1000);
		}
	}
	// Gen 2, under 40 ms queued: no re-splice, B joins the next unsent block.
	{
		FRun Late;
		Run(EHapbeatStreamWireFormat::SessionV2, 0.02, Late);
		TestEqual(TEXT("under 40 ms queued: no END before the session's own"),
			Late.Capture.Count(FHapbeatProtocol::CmdStreamEnd, Late.BeforeAdd), 1);
	}
	// Gen 1: never (its 300 ms END->BEGIN guard).
	{
		FRun Gen1;
		Run(EHapbeatStreamWireFormat::LegacyV1, 0.001, Gen1);
		TestEqual(TEXT("Stream Gen 1: no re-splice"), Gen1.Capture.Count(FHapbeatProtocol::CmdStreamBegin, Gen1.BeforeAdd), 0);
		TestEqual(TEXT("... only the session's own END"), Gen1.Capture.Count(FHapbeatProtocol::CmdStreamEnd, Gen1.BeforeAdd), 1);
	}
	return true;
}

// ---------------------------------------------------------------------------
// One-motor receiver: mixed without pan (sdk-multi-stream.md §6.15, §7 case 12)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatGen2SingleMotorPanTest,
	"Hapbeat.Streaming.Gen2.SingleMotorPan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatGen2SingleMotorPanTest::RunTest(const FString& Parameters)
{
	using namespace HapbeatStreamSessionV2Test;
	for (const bool bIgnorePan : { false, true })
	{
		FStreamerCapture Capture;
		auto Mirror = MakeShared<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>();
		Mirror->Pan.store(1.0f); // fully right
		FHapbeatStreamer Streamer(16000, 2, TEXT("player_1"), Mirror, []() { return static_cast<uint16>(3); },
			Capture.Sink(), 0.05f, EHapbeatStreamWireFormat::SessionV2,
			FHapbeatStreamSessionIdentity(FHapbeatStreamLeaseIdentity(0x15, 1), 1));
		Streamer.SetIgnorePan(bIgnorePan);
		Streamer.AddSource(FGuid::NewGuid(), MakeSessionTestPcm(1000, 1600), Mirror);
		Streamer.Start(0.0);
		Streamer.Tick(0.0);
		const TArray<uint8>* Data = Capture.Packets.FindByPredicate([](const TArray<uint8>& Packet)
		{
			return Packet[3] == FHapbeatProtocol::CmdStreamData;
		});
		if (!TestNotNull(TEXT("a DATA block"), Data))
		{
			continue;
		}
		if (bIgnorePan)
		{
			TestEqual(TEXT("one motor: left keeps the signal"), static_cast<int32>(DataSample(*Data, 10, 0)), 1000);
			TestEqual(TEXT("one motor: right keeps the signal"), static_cast<int32>(DataSample(*Data, 10, 1)), 1000);
		}
		else
		{
			TestEqual(TEXT("two motors: pan 1 silences left"), static_cast<int32>(DataSample(*Data, 10, 0)), 0);
			TestEqual(TEXT("two motors: pan 1 keeps right"), static_cast<int32>(DataSample(*Data, 10, 1)), 1000);
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
